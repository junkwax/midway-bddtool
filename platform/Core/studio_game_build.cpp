#include "Core/studio_game_build.h"
#include <filesystem>
#include <cerrno>
#include <cstring>
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
bool GameBuild::start(const std::string &root_path, const std::string &log_path,
                      std::string &error) {
    if (running_) {
        error = "A game build is already running.";
        return false;
    }
    try {
        namespace fs = std::filesystem;
        auto root = fs::canonical(fs::u8path(root_path));
        auto script = root / "build.py";
        auto log = fs::absolute(fs::u8path(log_path));
        if (!fs::is_regular_file(script)) {
            error = "The selected checkout has no build.py.";
            return false;
        }
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
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = output;
        startup.hStdError = output;
        startup.hStdInput = input;
        PROCESS_INFORMATION process{};
        std::wstring command = L"python.exe -u \"" + script.wstring() + L"\"";
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
            execlp("python3", "python3", "-u", script.c_str(), (char *)nullptr);
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
