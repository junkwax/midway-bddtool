#include "utils/compat.h"
#include "UI/dialogs/native_file_dialogs.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <shobjidl.h>
#include <vector>
#else
#include <fstream>
#include <sys/wait.h>
#endif

namespace {
namespace fs = std::filesystem;
enum class DialogKind { Open, Save, Folder };

// Keep each operation's history independent, even if two operations use the same filters.
std::uint64_t dialog_key(DialogKind kind, const char *title)
{
    std::uint64_t hash = 14695981039346656037ull;
    hash = (hash ^ static_cast<unsigned>(kind)) * 1099511628211ull;
    for (const char *c = title; *c; ++c)
        hash = (hash ^ static_cast<unsigned char>(*c)) * 1099511628211ull;
    return hash;
}

bool copy_result(const std::string &path, char *out, int outsz)
{
    if (path.empty() || path.size() >= static_cast<size_t>(outsz)) return false;
    std::memcpy(out, path.c_str(), path.size() + 1);
    return true;
}

void append_default_ext(std::string &path, const char *ext)
{
    if (!ext || !*ext) return;
    const auto slash = path.find_last_of("/\\");
    if (path.find('.', slash == std::string::npos ? 0 : slash + 1) == std::string::npos) {
        path += '.';
        path += ext;
    }
}

#ifdef _WIN32
std::wstring to_wide(const char *text)
{
    if (!text || !*text) return {};
    const int length = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (length <= 1) return {};
    std::wstring wide(length, L'\0');
    if (!MultiByteToWideChar(CP_ACP, 0, text, -1, wide.data(), length)) return {};
    wide.pop_back();
    return wide;
}

std::string to_narrow(const wchar_t *text)
{
    const int length = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string narrow(length, '\0');
    BOOL substituted = FALSE;
    // CP_UTF8 is allowed as the system ANSI code page but does not accept this flag or output parameter.
    const bool utf8 = GetACP() == CP_UTF8;
    if (!WideCharToMultiByte(CP_ACP, utf8 ? 0 : WC_NO_BEST_FIT_CHARS, text, -1, narrow.data(), length,
                             nullptr, utf8 ? nullptr : &substituted) || substituted)
        return {};
    narrow.pop_back();
    return narrow;
}

void set_file_types(IFileDialog &dialog, const char *filter)
{
    if (!filter) return;
    std::vector<std::wstring> parts;
    for (const char *part = filter; *part; part += std::strlen(part) + 1)
        parts.push_back(to_wide(part));
    std::vector<COMDLG_FILTERSPEC> types;
    for (size_t i = 1; i < parts.size(); i += 2)
        types.push_back({parts[i - 1].c_str(), parts[i].c_str()});
    if (!types.empty()) dialog.SetFileTypes(static_cast<UINT>(types.size()), types.data());
}

std::string show_windows_dialog(DialogKind kind, const char *title, const char *filter,
                                const char *extension, const std::string &suggested)
{
    IFileDialog *dialog = nullptr;
    const auto &class_id = kind == DialogKind::Save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
    if (FAILED(CoCreateInstance(class_id, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return {};

    // A stable client GUID lets the Windows shell persist the folder for this dialog across launches.
    const auto key = dialog_key(kind, title);
    GUID client = {0x7e955064, 0x7a36, 0x4e5c, {}};
    for (unsigned i = 0; i < sizeof client.Data4; ++i)
        client.Data4[i] = static_cast<unsigned char>(key >> (8 * i));
    dialog->SetClientGuid(client);
    dialog->SetTitle(to_wide(title).c_str());

    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_PATHMUSTEXIST;
    if (kind == DialogKind::Folder) options |= FOS_PICKFOLDERS;
    if (kind == DialogKind::Open) options |= FOS_FILEMUSTEXIST;
    if (kind == DialogKind::Save) options |= FOS_OVERWRITEPROMPT;
    dialog->SetOptions(options);
    if (kind != DialogKind::Folder) set_file_types(*dialog, filter);

    if (kind == DialogKind::Save) {
        if (extension && *extension) dialog->SetDefaultExtension(to_wide(extension).c_str());
        const fs::path name = to_wide(suggested.c_str());
        if (name.has_parent_path()) {
            IShellItem *folder = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(name.parent_path().c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                // The stage's current folder is useful until this dialog has its own history.
                dialog->SetDefaultFolder(folder);
                folder->Release();
            }
        }
        if (name.has_filename()) dialog->SetFileName(name.filename().c_str());
    }

    std::string path;
    if (SUCCEEDED(dialog->Show(GetActiveWindow()))) {
        IShellItem *item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            wchar_t *filename = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &filename))) {
                path = to_narrow(filename);
                CoTaskMemFree(filename);
            }
            item->Release();
        }
    }
    dialog->Release();
    return path;
}
#else
std::string shell_arg(const std::string &value)
{
    std::string quoted = "'";
    for (char c : value)
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    return quoted + "'";
}

fs::path history_file(DialogKind kind, const char *title)
{
    char *pref = SDL_GetPrefPath("midway-bddtool", "file-dialogs");
    if (!pref) return {};
    fs::path file = fs::path(pref) / std::to_string(dialog_key(kind, title));
    SDL_free(pref);
    return file;
}

fs::path last_folder(const fs::path &history)
{
    if (history.empty()) return {};
    std::ifstream input(history);
    std::string path;
    std::getline(input, path);
    std::error_code ec;
    return fs::is_directory(path, ec) ? fs::path(path) : fs::path{};
}

int run_picker(const std::string &command, std::string &path)
{
    FILE *pipe = popen((command + " 2>/dev/null").c_str(), "r");
    if (!pipe) return 127;
    path.clear();
    char buffer[512];
    while (std::fgets(buffer, sizeof buffer, pipe)) path += buffer;
    const int status = pclose(pipe);
    if (status == -1 || !WIFEXITED(status)) return 1;
    while (!path.empty() && (path.back() == '\r' || path.back() == '\n')) path.pop_back();
    return WEXITSTATUS(status);
}

std::string show_desktop_dialog(DialogKind kind, const char *title, const char *filter,
                                const std::string &suggested, const fs::path &history)
{
    fs::path folder = last_folder(history);
    const fs::path name = suggested;
    if (folder.empty()) folder = name.parent_path();
    fs::path start = folder / name.filename();
    if (name.empty() && !folder.empty()) start /= "";
    std::string zenity = "zenity --file-selection --title=" + shell_arg(title);
    if (kind == DialogKind::Save) zenity += " --save --confirm-overwrite";
    if (kind == DialogKind::Folder) zenity += " --directory";
    if (!start.empty()) zenity += " --filename=" + shell_arg(start.string());

    std::string kde_filters;
    if (filter && kind != DialogKind::Folder) {
        for (const char *part = filter; *part;) {
            const std::string label = part;
            part += label.size() + 1;
            if (!*part) break;
            std::string pattern = part;
            part += pattern.size() + 1;
            if (pattern == "*.*") pattern = "*";
            for (char &c : pattern) if (c == ';') c = ' ';
            zenity += " --file-filter=" + shell_arg(label + " | " + pattern);
            if (!kde_filters.empty()) kde_filters += '\n';
            kde_filters += pattern + '|' + label;
        }
    }
    std::string path;
    int status = run_picker(zenity, path);
    if (status == 127) {
        std::string kde = kind == DialogKind::Save ? "kdialog --getsavefilename"
                       : kind == DialogKind::Folder ? "kdialog --getexistingdirectory"
                       : "kdialog --getopenfilename";
        kde += " " + shell_arg(start.empty() ? "." : start.string());
        if (!kde_filters.empty()) kde += " " + shell_arg(kde_filters);
        status = run_picker(kde + " --title " + shell_arg(title), path);
    }
    return status == 0 ? path : std::string{};
}
#endif

bool browse(DialogKind kind, const char *title, const char *filter, const char *extension,
            char *out, int outsz)
{
    if (!out || outsz <= 0) return false;
    const std::string suggested = kind == DialogKind::Save ? out : "";
    out[0] = '\0';
    if (!title) title = kind == DialogKind::Save ? "Save" : kind == DialogKind::Folder ? "Select folder" : "Open";
#ifdef _WIN32
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(init)) return false;
    std::string path = show_windows_dialog(kind, title, filter, extension, suggested);
    CoUninitialize();
#else
    const auto history = history_file(kind, title);
    std::string path = show_desktop_dialog(kind, title, filter, suggested, history);
#endif
    if (path.empty()) return false;
    if (kind == DialogKind::Save) append_default_ext(path, extension);
    if (!copy_result(path, out, outsz)) return false;
#ifndef _WIN32
    if (!history.empty()) {
        std::error_code ec;
        const auto chosen = fs::absolute(path, ec);
        const auto folder = kind == DialogKind::Folder ? chosen : chosen.parent_path();
        if (!ec && fs::is_directory(folder, ec))
            std::ofstream(history) << folder.string() << '\n';
    }
#endif
    return true;
}
} // namespace

bool file_dialog_open(const char *title, const char *filter, char *out, int outsz)
{
    return browse(DialogKind::Open, title, filter, nullptr, out, outsz);
}

bool file_dialog_save_ext(const char *title, const char *filter, const char *default_ext,
                          char *out, int outsz)
{
    return browse(DialogKind::Save, title, filter, default_ext, out, outsz);
}

bool file_dialog_save(const char *title, const char *filter, char *out, int outsz)
{
    if (out && outsz > 0) out[0] = '\0';
    return file_dialog_save_ext(title, filter, nullptr, out, outsz);
}

bool folder_dialog_open(const char *title, char *out, int outsz)
{
    return browse(DialogKind::Folder, title, nullptr, nullptr, out, outsz);
}
