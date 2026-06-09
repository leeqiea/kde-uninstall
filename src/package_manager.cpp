#include "kde_uninstall/package_manager.h"

#include "kde_uninstall/command.h"
#include "kde_uninstall/constants.h"
#include "kde_uninstall/desktop_file.h"
#include "kde_uninstall/paths.h"
#include "kde_uninstall/string_utils.h"

#include <cctype>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace kde_uninstall {

std::vector<std::string> parseDesktopExec(const std::string &exec) {
    std::vector<std::string> tokens;
    std::string current;
    bool quoted = false;
    bool escaped = false;

    for (char c : exec) {
        if (escaped) {
            current.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            quoted = !quoted;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) && !quoted) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

bool looksLikeEnvAssignment(const std::string &token) {
    auto eq = token.find('=');
    if (eq == std::string::npos || eq == 0 || token.find('/') != std::string::npos) {
        return false;
    }
    for (std::size_t i = 0; i < eq; ++i) {
        unsigned char c = token[i];
        if (!(std::isalnum(c) || c == '_')) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> executableFromDesktopExec(const std::string &exec) {
    auto tokens = parseDesktopExec(exec);
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        std::string token = tokens[i];
        if (token.empty() || token[0] == '%') {
            continue;
        }
        if (basenameOf(token) == "env") {
            while (++i < tokens.size() && looksLikeEnvAssignment(tokens[i])) {
            }
            if (i < tokens.size()) {
                token = tokens[i];
            } else {
                return std::nullopt;
            }
        }
        if (looksLikeEnvAssignment(token)) {
            continue;
        }
        return token;
    }
    return std::nullopt;
}

std::optional<std::string> firstPackageFromDpkgOutput(const std::string &output) {
    std::string line = firstLine(output);
    auto colon = line.find(':');
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    std::string packages = line.substr(0, colon);
    auto comma = packages.find(',');
    if (comma != std::string::npos) {
        packages = packages.substr(0, comma);
    }
    packages = trim(packages);
    return packages.empty() ? std::nullopt : std::optional<std::string>(packages);
}

std::optional<std::string> firstPackageFromApkOutput(const std::string &output) {
    std::string line = firstLine(output);
    auto colon = line.find(':');
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    std::string pkg = trim(line.substr(0, colon));
    return pkg.empty() ? std::nullopt : std::optional<std::string>(pkg);
}

std::optional<std::string> firstPackageFromEopkgOutput(const std::string &output) {
    for (const auto &rawLine : split(output, '\n')) {
        std::string line = trim(rawLine);
        if (startsWith(line, "Package ")) {
            auto parts = split(line, ' ');
            if (parts.size() >= 2) {
                return parts[1];
            }
        }
    }
    return std::nullopt;
}

std::optional<std::pair<std::string, std::string>> nativeOwnerForPath(const fs::path &path) {
    if (path.empty() || !fs::exists(path)) {
        return std::nullopt;
    }
    std::string p = path.string();

    if (commandExists("pacman")) {
        auto result = runCapture({"pacman", "-Qqo", p});
        if (result.exitCode == 0 && !firstLine(result.output).empty()) {
            return std::make_pair("pacman", firstLine(result.output));
        }
    }
    if (commandExists("dpkg-query")) {
        auto result = runCapture({"dpkg-query", "-S", p});
        if (result.exitCode == 0) {
            if (auto pkg = firstPackageFromDpkgOutput(result.output)) {
                return std::make_pair("apt", *pkg);
            }
        }
    }
    if (commandExists("rpm")) {
        auto result = runCapture({"rpm", "-qf", "--queryformat", "%{NAME}\n", p});
        if (result.exitCode == 0 && !firstLine(result.output).empty()) {
            return std::make_pair("rpm", firstLine(result.output));
        }
    }
    if (commandExists("xbps-query")) {
        auto result = runCapture({"xbps-query", "-Ro", p});
        if (result.exitCode == 0 && !firstLine(result.output).empty()) {
            return std::make_pair("xbps", firstLine(result.output));
        }
    }
    if (commandExists("apk")) {
        auto result = runCapture({"apk", "info", "--who-owns", p});
        if (result.exitCode == 0) {
            if (auto pkg = firstPackageFromApkOutput(result.output)) {
                return std::make_pair("apk", *pkg);
            }
        }
    }
    if (commandExists("eopkg")) {
        auto result = runCapture({"eopkg", "search-file", p});
        if (result.exitCode == 0) {
            if (auto pkg = firstPackageFromEopkgOutput(result.output)) {
                return std::make_pair("eopkg", *pkg);
            }
        }
    }

    return std::nullopt;
}

std::vector<std::string> withPkexec(std::vector<std::string> command) {
    if (::geteuid() == 0) {
        return command;
    }
    if (!commandExists("pkexec")) {
        return {};
    }
    command.insert(command.begin(), "pkexec");
    return command;
}

std::optional<RemovalPlan> planForNativePackage(const std::vector<std::string> &lines,
                                                const fs::path &desktopPath,
                                                const fs::path &sourcePath,
                                                const std::string &displayName) {
    std::vector<fs::path> candidates;
    candidates.push_back(sourcePath);
    if (desktopPath != sourcePath) {
        candidates.push_back(desktopPath);
    }

    if (auto exec = getKey(lines, "Desktop Entry", "Exec")) {
        if (auto command = executableFromDesktopExec(*exec)) {
            if (auto executable = findExecutable(*command)) {
                candidates.emplace_back(*executable);
                candidates.emplace_back(canonicalIfPossible(*executable));
            }
        }
    }

    std::optional<std::pair<std::string, std::string>> owner;
    for (const auto &candidate : candidates) {
        owner = nativeOwnerForPath(candidate);
        if (owner) {
            break;
        }
    }
    if (!owner) {
        return std::nullopt;
    }

    std::vector<std::string> removeCommand;
    std::string manager = owner->first;
    std::string package = owner->second;

    if (manager == "pacman") {
        removeCommand = withPkexec({"pacman", "-Rns", "--noconfirm", package});
    } else if (manager == "apt") {
        removeCommand = withPkexec({"apt-get", "remove", "--purge", "-y", package});
    } else if (manager == "rpm") {
        if (commandExists("dnf")) {
            manager = "dnf";
            removeCommand = withPkexec({"dnf", "remove", "-y", package});
        } else if (commandExists("zypper")) {
            manager = "zypper";
            removeCommand = withPkexec({"zypper", "--non-interactive", "remove", package});
        } else {
            removeCommand = withPkexec({"rpm", "-e", package});
        }
    } else if (manager == "xbps") {
        removeCommand = withPkexec({"xbps-remove", "-Ry", package});
    } else if (manager == "apk") {
        removeCommand = withPkexec({"apk", "del", package});
    } else if (manager == "eopkg") {
        removeCommand = withPkexec({"eopkg", "remove", "-y", package});
    }

    if (removeCommand.empty()) {
        return std::nullopt;
    }
    return RemovalPlan{displayName, manager, package, removeCommand, sourcePath.string()};
}

std::optional<RemovalPlan> removalPlanForDesktop(const fs::path &desktopPath) {
    auto lines = readLines(desktopPath);
    std::string displayName = localizedName(lines);

    fs::path sourcePath = desktopPath;
    if (auto source = getKey(lines, "Desktop Entry", kSourceKey); source && !trim(*source).empty()) {
        sourcePath = trim(*source);
    }

    if (auto flatpak = getKey(lines, "Desktop Entry", "X-Flatpak"); flatpak && !trim(*flatpak).empty()) {
        std::string appId = trim(*flatpak);
        std::vector<std::string> command = {"flatpak", "uninstall", "--noninteractive", "--delete-data"};
        if (sourcePath.string().find("/.local/share/flatpak/") != std::string::npos) {
            command.emplace_back("--user");
        } else if (sourcePath.string().find("/var/lib/flatpak/") != std::string::npos) {
            command.emplace_back("--system");
        }
        command.push_back(appId);
        return RemovalPlan{displayName, "flatpak", appId, command, sourcePath.string()};
    }

    if (sourcePath.string().find("/flatpak/exports/share/applications/") != std::string::npos &&
        commandExists("flatpak")) {
        std::string appId = sourcePath.filename().string();
        if (endsWith(appId, ".desktop")) {
            appId.resize(appId.size() - std::string(".desktop").size());
        }
        std::vector<std::string> command = {"flatpak", "uninstall", "--noninteractive", "--delete-data"};
        if (sourcePath.string().find("/.local/share/flatpak/") != std::string::npos) {
            command.emplace_back("--user");
        } else if (sourcePath.string().find("/var/lib/flatpak/") != std::string::npos) {
            command.emplace_back("--system");
        }
        command.push_back(appId);
        return RemovalPlan{displayName, "flatpak", appId, command, sourcePath.string()};
    }

    if (auto snap = getKey(lines, "Desktop Entry", "X-SnapInstanceName"); snap && !trim(*snap).empty()) {
        std::string snapName = trim(*snap);
        auto command = withPkexec({"snap", "remove", snapName});
        if (!command.empty()) {
            return RemovalPlan{displayName, "snap", snapName, command, sourcePath.string()};
        }
    }

    if (sourcePath.string().find("/snapd/desktop/applications/") != std::string::npos && commandExists("snap")) {
        std::string file = sourcePath.filename().string();
        if (endsWith(file, ".desktop")) {
            file.resize(file.size() - std::string(".desktop").size());
        }
        auto underscore = file.find('_');
        std::string snapName = underscore == std::string::npos ? file : file.substr(0, underscore);
        auto command = withPkexec({"snap", "remove", snapName});
        if (!command.empty()) {
            return RemovalPlan{displayName, "snap", snapName, command, sourcePath.string()};
        }
    }

    return planForNativePackage(lines, desktopPath, sourcePath, displayName);
}

} // namespace kde_uninstall
