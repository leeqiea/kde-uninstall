#include "kde_uninstall/command.h"

#include "kde_uninstall/string_utils.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace kde_uninstall {

bool commandExists(const std::string &command) {
    if (command.find('/') != std::string::npos) {
        return ::access(command.c_str(), X_OK) == 0;
    }
    const char *pathEnv = std::getenv("PATH");
    std::string path = pathEnv && *pathEnv ? pathEnv : "/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin";
    for (const auto &dir : split(path, ':')) {
        if (!dir.empty() && ::access((fs::path(dir) / command).c_str(), X_OK) == 0) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> findExecutable(const std::string &command) {
    if (command.empty()) {
        return std::nullopt;
    }
    if (command.find('/') != std::string::npos) {
        return ::access(command.c_str(), X_OK) == 0 ? std::optional<std::string>(command) : std::nullopt;
    }
    const char *pathEnv = std::getenv("PATH");
    std::string path = pathEnv && *pathEnv ? pathEnv : "/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin";
    for (const auto &dir : split(path, ':')) {
        fs::path candidate = fs::path(dir) / command;
        if (::access(candidate.c_str(), X_OK) == 0) {
            return candidate.string();
        }
    }
    return std::nullopt;
}

CommandResult runCapture(const std::vector<std::string> &args, std::size_t maxOutput) {
    if (args.empty()) {
        return {127, "empty command"};
    }

    int pipeFd[2];
    if (::pipe(pipeFd) != 0) {
        return {127, std::string("pipe failed: ") + std::strerror(errno)};
    }

    pid_t pid = ::fork();
    if (pid == 0) {
        ::close(pipeFd[0]);
        ::dup2(pipeFd[1], STDOUT_FILENO);
        ::dup2(pipeFd[1], STDERR_FILENO);
        ::close(pipeFd[1]);

        std::vector<char *> argv;
        argv.reserve(args.size() + 1);
        for (const auto &arg : args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);
        ::execvp(argv[0], argv.data());
        std::string message = "exec failed: " + args[0] + ": " + std::strerror(errno) + "\n";
        ::write(STDERR_FILENO, message.data(), message.size());
        _exit(127);
    }

    if (pid < 0) {
        ::close(pipeFd[0]);
        ::close(pipeFd[1]);
        return {127, std::string("fork failed: ") + std::strerror(errno)};
    }

    ::close(pipeFd[1]);
    std::string output;
    std::array<char, 4096> buffer{};
    ssize_t count = 0;
    while ((count = ::read(pipeFd[0], buffer.data(), buffer.size())) > 0) {
        if (output.size() < maxOutput) {
            std::size_t remaining = maxOutput - output.size();
            output.append(buffer.data(), std::min<std::size_t>(remaining, static_cast<std::size_t>(count)));
        }
    }
    ::close(pipeFd[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (WIFEXITED(status)) {
        return {WEXITSTATUS(status), output};
    }
    if (WIFSIGNALED(status)) {
        return {128 + WTERMSIG(status), output};
    }
    return {127, output};
}

int runNoCapture(const std::vector<std::string> &args) {
    return runCapture(args, 4096).exitCode;
}

} // namespace kde_uninstall
