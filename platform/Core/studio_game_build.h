#pragma once
#include <string>
#include <vector>
namespace studio {
// Executes the selected checkout's build.py or supported cave helper without a command shell.
class GameBuild {
  public:
    ~GameBuild();
    GameBuild() = default;
    GameBuild(const GameBuild &) = delete;
    GameBuild &operator=(const GameBuild &) = delete;
    bool start(const std::string &root, const std::string &log_path, std::string &error,
               const std::string &script_relative = "build.py");
    bool start_reviewed(const std::string &root, const std::string &log_path,
                        const std::string &adapter_folder, const std::string &job,
                        const std::string &output, std::string &error);
    void poll();
    bool running() const { return running_; }
    bool started() const { return started_; }
    int exit_code() const { return exit_code_; }
    const std::string &log_path() const { return log_; }

  private:
    bool launch(const std::string &root, const std::string &log_path, const std::string &script,
                const std::vector<std::string> &arguments, std::string &error);
    void *process_ = nullptr;
    int pid_ = -1, exit_code_ = -1;
    bool running_ = false, started_ = false;
    std::string log_;
};
} // namespace studio
