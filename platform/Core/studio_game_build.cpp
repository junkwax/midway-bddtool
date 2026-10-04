#include "Core/studio_game_build.h"
#include <filesystem>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#endif
namespace studio {
GameBuild::~GameBuild() {
#ifdef _WIN32
    if (process_)
        CloseHandle((HANDLE)process_);
#endif
}
bool GameBuild::start(const std::string &root_path, const std::string &log_path, std::string &error,
                      const std::string &script_relative) {
    if (running_) {
        error = "A game build is already running.";
        return false;
    }
    try {
        namespace fs = std::filesystem;
        auto root = fs::canonical(fs::u8path(root_path));
        if (script_relative != "build.py" && script_relative != "tools/bddtool_mk3cave_build.py") {
            error = "Unsupported game build adapter.";
            return false;
        }
        auto script = fs::canonical(root / fs::u8path(script_relative));
        auto relative = script.lexically_relative(root);
        if (relative.empty() || *relative.begin() == "..") {
            error = "Game build adapter escapes the selected checkout.";
            return false;
        }
        if (!fs::is_regular_file(script)) {
            error = "The selected checkout has no build.py.";
            return false;
        }
        return launch(root.u8string(), log_path, script.u8string(), {}, error);
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
bool GameBuild::start_reviewed(const std::string &root, const std::string &log_path,
                               const std::string &adapter_folder, const std::string &job,
                               const std::string &output, std::string &error) {
    try {
        namespace fs = std::filesystem;
        auto script = fs::canonical(fs::u8path(adapter_folder) / "reviewed_stage_build.py");
        auto plan = fs::canonical(fs::u8path(job));
        if (!fs::is_regular_file(script) || !fs::is_regular_file(plan))
            throw std::runtime_error("Missing reviewed build adapter or job.");
        return launch(root, log_path, script.u8string(),
                      {"run", plan.u8string(), fs::absolute(fs::u8path(output)).u8string(),
                       "--candidate", fs::canonical(fs::u8path(root)).u8string()}, error);
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
bool GameBuild::launch(const std::string &root_path, const std::string &log_path,
                       const std::string &script_path, const std::vector<std::string> &arguments,
                       std::string &error) {
    if (running_) { error = "A game build is already running."; return false; }
    try {
        namespace fs = std::filesystem;
        auto root = fs::canonical(fs::u8path(root_path));
        auto script = fs::u8path(script_path);
        auto log = fs::absolute(fs::u8path(log_path));
#ifdef _WIN32
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE) {
            error = "Cannot open game build log.";
            return false;
        }
        HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        startup.hStdOutput = output;
        startup.hStdError = output;
        startup.hStdInput = input;
        PROCESS_INFORMATION process{};
        auto quote = [](const std::wstring &value) {
            std::wstring result = L"\"";
            size_t slashes = 0;
            for (wchar_t c : value) {
                if (c == L'\\') { ++slashes; continue; }
                result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
                if (c == L'"') result += L'\\';
                result += c;
                slashes = 0;
            }
            result.append(slashes * 2, L'\\');
            return result + L'"';
        };
        std::wstring command = L"python.exe -B -u " + quote(script.wstring());
        for (const auto &arg : arguments)
            command += L" " + quote(fs::u8path(arg).wstring());
        BOOL ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                 nullptr, root.c_str(), &startup, &process);
        DWORD code = GetLastError();
        CloseHandle(output);
        if (input != INVALID_HANDLE_VALUE)
            CloseHandle(input);
        if (!ok) {
            error = "Could not start python.exe (Windows error " + std::to_string(code) +
                    "). Install Python or add it to PATH.";
            return false;
        }
        CloseHandle(process.hThread);
        if (process_)
            CloseHandle((HANDLE)process_);
        process_ = process.hProcess;
#else
        int output = ::open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (output < 0) {
            error = "Cannot open game build log: " + std::string(std::strerror(errno));
            return false;
        }
        std::vector<std::string> values = {"python3", "-B", "-u", script.u8string()};
        values.insert(values.end(), arguments.begin(), arguments.end());
        std::vector<char *> argv;
        for (auto &value : values) argv.push_back(value.data());
        argv.push_back(nullptr);
        auto child = fork();
        if (child == 0) {
            dup2(output, STDOUT_FILENO);
            dup2(output, STDERR_FILENO);
            close(output);
            int input = ::open("/dev/null", O_RDONLY);
            if (input >= 0) {
                dup2(input, STDIN_FILENO);
                close(input);
            }
            if (chdir(root.c_str()) != 0)
                _exit(126);
            execvp("python3", argv.data());
            _exit(127);
        }
        close(output);
        if (child < 0) {
            error = "Could not start game build.";
            return false;
        }
        pid_ = (int)child;
#endif
        log_ = log.u8string();
        exit_code_ = -1;
        started_ = running_ = true;
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
void GameBuild::poll() {
    if (!running_)
        return;
#ifdef _WIN32
    DWORD code = 0;
    if (WaitForSingleObject((HANDLE)process_, 0) != WAIT_OBJECT_0)
        return;
    if (!GetExitCodeProcess((HANDLE)process_, &code))
        code = 1;
    exit_code_ = (int)code;
    CloseHandle((HANDLE)process_);
    process_ = nullptr;
#else
    int status = 0;
    auto result = waitpid(pid_, &status, WNOHANG);
    if (result == 0)
        return;
    if (result < 0 && errno == EINTR)
        return;
    exit_code_ = result < 0 ? 1 : WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    pid_ = -1;
#endif
    running_ = false;
}
} // namespace studio
