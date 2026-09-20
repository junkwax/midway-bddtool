#pragma once
#include <string>
namespace studio {
// Executes only the selected checkout's build.py, without a command shell.
class GameBuild {
  public:
    ~GameBuild();
    GameBuild() = default;
    GameBuild(const GameBuild &) = delete;
    GameBuild &operator=(const GameBuild &) = delete;
    bool start(const std::string &root, const std::string &log_path, std::string &error);
    void poll();
    bool running() const { return running_; }
    bool started() const { return started_; }
    int exit_code() const { return exit_code_; }
    const std::string &log_path() const { return log_; }

  private:
    void *process_ = nullptr;
    int pid_ = -1, exit_code_ = -1;
    bool running_ = false, started_ = false;
    std::string log_;
};
} // namespace studio
