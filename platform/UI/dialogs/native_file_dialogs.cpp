#include "utils/compat.h"
#include "UI/dialogs/native_file_dialogs.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <commdlg.h>
#include <shlobj.h>
#else
#include <cstdio>
#endif

bool file_dialog_open(const char *title, const char *filter,
                      char *out, int outsz)
{
#ifdef _WIN32
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    out[0] = '\0';
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = out;
    ofn.nMaxFile    = (DWORD)outsz;
    ofn.lpstrTitle  = title;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) ? true : false;
#else
    (void)filter;
    char cmd[512];
    snprintf(cmd, sizeof cmd, "zenity --file-selection --title='%s' 2>/dev/null", title ? title : "Open");
    FILE *p = popen(cmd, "r");
    if (!p) {
        snprintf(cmd, sizeof cmd, "kdialog --getopenfilename . 2>/dev/null");
        p = popen(cmd, "r");
    }
    if (p) {
        if (fgets(out, outsz, p)) {
            out[strcspn(out, "\r\n")] = '\0';
            int ok = out[0] != '\0';
            pclose(p);
            return ok;
        }
        pclose(p);
    }
    out[0] = '\0';
    return false;
#endif
}

/* Append ".EXT" when the file name the user typed carries no extension of its
   own. Windows does this via lpstrDefExt, but the zenity/kdialog fallbacks
   don't, so both paths funnel through here. */
static void append_default_ext(char *path, int outsz, const char *default_ext)
{
    if (!path || !path[0] || !default_ext || !default_ext[0]) return;

    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '\\' || *p == '/') base = p + 1;
    if (strrchr(base, '.')) return;

    size_t len = strlen(path);
    if (len + 1 + strlen(default_ext) + 1 > (size_t)outsz) return;
    snprintf(path + len, (size_t)outsz - len, ".%s", default_ext);
}

bool file_dialog_save_ext(const char *title, const char *filter,
                          const char *default_ext, char *out, int outsz)
{
#ifdef _WIN32
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = out;          /* pre-filled by the caller = suggested name */
    ofn.nMaxFile    = (DWORD)outsz;
    ofn.lpstrTitle  = title;
    ofn.lpstrDefExt = (default_ext && default_ext[0]) ? default_ext : NULL;
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameA(&ofn)) {
        out[0] = '\0';
        return false;
    }
    append_default_ext(out, outsz, default_ext);
    return true;
#else
    (void)filter;
    char cmd[640];
    char suggested[512];
    snprintf(suggested, sizeof suggested, "%s", out);
    if (suggested[0])
        snprintf(cmd, sizeof cmd,
                 "zenity --file-selection --save --confirm-overwrite --filename='%s' --title='%s' 2>/dev/null",
                 suggested, title ? title : "Save");
    else
        snprintf(cmd, sizeof cmd,
                 "zenity --file-selection --save --confirm-overwrite --title='%s' 2>/dev/null",
                 title ? title : "Save");
    FILE *p = popen(cmd, "r");
    if (!p) {
        snprintf(cmd, sizeof cmd, "kdialog --getsavefilename . 2>/dev/null");
        p = popen(cmd, "r");
    }
    if (p) {
        if (fgets(out, outsz, p)) {
            out[strcspn(out, "\r\n")] = '\0';
            int ok = out[0] != '\0';
            pclose(p);
            if (ok) append_default_ext(out, outsz, default_ext);
            return ok;
        }
        pclose(p);
    }
    out[0] = '\0';
    return false;
#endif
}

bool file_dialog_save(const char *title, const char *filter,
                      char *out, int outsz)
{
    if (outsz > 0) out[0] = '\0';
    return file_dialog_save_ext(title, filter, NULL, out, outsz);
}

bool folder_dialog_open(const char *title, char *out, int outsz)
{
#ifdef _WIN32
    char sel[1024] = {0};
    BROWSEINFOA bi = {0};
    bi.lpszTitle = title ? title : "Select folder";
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        bool ok = SHGetPathFromIDListA(pidl, sel) ? true : false;
        CoTaskMemFree(pidl);
        if (ok) {
            snprintf(out, (size_t)outsz, "%s", sel);
            return true;
        }
    }
    out[0] = '\0';
    return false;
#else
    char cmd[512];
    snprintf(cmd, sizeof cmd, "zenity --file-selection --directory --title='%s' 2>/dev/null",
             title ? title : "Select folder");
    FILE *p = popen(cmd, "r");
    if (!p) {
        snprintf(cmd, sizeof cmd, "kdialog --getexistingdirectory . 2>/dev/null");
        p = popen(cmd, "r");
    }
    if (p) {
        if (fgets(out, outsz, p)) {
            out[strcspn(out, "\r\n")] = '\0';
            int ok = out[0] != '\0';
            pclose(p);
            return ok;
        }
        pclose(p);
    }
    out[0] = '\0';
    return false;
#endif
}
