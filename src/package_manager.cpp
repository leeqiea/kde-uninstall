#include "kde_uninstall/package_manager.h"

#include "kde_uninstall/command.h"
#include "kde_uninstall/constants.h"
#include "kde_uninstall/desktop_file.h"
#include "kde_uninstall/paths.h"
#include "kde_uninstall/string_utils.h"

<<<<<<< HEAD
#include <cctype>
#include <filesystem>
#include <string>
#include <unistd.h>
=======
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <utility>
>>>>>>> 74cff8e (Initial commit)
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

<<<<<<< HEAD
=======
std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string identityToken(const std::string &value) {
    std::string result;
    for (unsigned char c : lowerAscii(value)) {
        if (std::isalnum(c)) {
            result.push_back(static_cast<char>(c));
        }
    }
    return result;
}

bool extensionEquals(const fs::path &path, const std::string &extension) {
    return lowerAscii(path.extension().string()) == lowerAscii(extension);
}

bool hasAnyExtension(const fs::path &path, const std::vector<std::string> &extensions) {
    for (const auto &extension : extensions) {
        if (extensionEquals(path, extension)) {
            return true;
        }
    }
    return false;
}

bool lexicallyUnderOrEqual(const fs::path &path, const fs::path &root) {
    fs::path p = path.lexically_normal();
    fs::path r = root.lexically_normal();
    if (!p.is_absolute() || !r.is_absolute()) {
        return false;
    }

    auto pit = p.begin();
    auto rit = r.begin();
    for (; rit != r.end(); ++rit, ++pit) {
        if (pit == p.end() || *pit != *rit) {
            return false;
        }
    }
    return true;
}

bool lexicallyInside(const fs::path &path, const fs::path &root) {
    fs::path p = path.lexically_normal();
    fs::path r = root.lexically_normal();
    if (!lexicallyUnderOrEqual(p, r)) {
        return false;
    }

    auto pit = p.begin();
    auto rit = r.begin();
    for (; rit != r.end() && pit != p.end(); ++rit, ++pit) {
    }
    return pit != p.end();
}

bool isFileOrSymlink(const fs::path &path) {
    std::error_code ec;
    auto status = fs::symlink_status(path, ec);
    if (ec) {
        return false;
    }
    return fs::is_regular_file(status) || fs::is_symlink(status);
}

bool isPublicOptName(const std::string &name) {
    std::string lowered = lowerAscii(name);
    const std::vector<std::string> publicNames = {
        "bin",   "sbin", "share", "lib", "lib64", "libexec", "etc",
        "include", "var",  "tmp",   "cache", "doc", "docs",    "man",
        "src",   "local", "opt"};
    return std::find(publicNames.begin(), publicNames.end(), lowered) != publicNames.end();
}

bool isAppLikeOptDirectoryName(const std::string &name) {
    if (name.empty() || name == "." || name == ".." || name.front() == '.' || isPublicOptName(name)) {
        return false;
    }

    bool hasAlpha = false;
    for (unsigned char c : name) {
        if (std::isalpha(c)) {
            hasAlpha = true;
        }
        if (!(std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '+')) {
            return false;
        }
    }
    return hasAlpha;
}

bool hasOptAppMarker(const fs::path &root) {
    const std::vector<std::string> markerFiles = {
        "AppRun", ".DirIcon", "product-info.json", "package.json"};
    for (const auto &marker : markerFiles) {
        std::error_code ec;
        if (fs::exists(root / marker, ec)) {
            return true;
        }
    }

    std::error_code ec;
    if (fs::is_directory(root / "bin", ec)) {
        return true;
    }

    std::string rootId = identityToken(root.filename().string());
    int scanned = 0;
    for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        if (++scanned > 256) {
            break;
        }

        const fs::path entryPath = it->path();
        if (extensionEquals(entryPath, ".desktop")) {
            return true;
        }

        auto status = it->symlink_status(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        if (!fs::is_regular_file(status) && !fs::is_symlink(status)) {
            continue;
        }

        std::string entryId = identityToken(entryPath.stem().string());
        if (!rootId.empty() && entryId == rootId && ::access(entryPath.c_str(), X_OK) == 0) {
            return true;
        }
    }

    return false;
}

bool isSafeOptAppRoot(const fs::path &root) {
    fs::path normalized = root.lexically_normal();
    if (!normalized.is_absolute() || normalized == fs::path("/opt") ||
        !lexicallyInside(normalized, fs::path("/opt"))) {
        return false;
    }
    if (!isAppLikeOptDirectoryName(normalized.filename().string())) {
        return false;
    }

    std::error_code ec;
    auto status = fs::symlink_status(normalized, ec);
    if (ec || !fs::is_directory(status) || fs::is_symlink(status)) {
        return false;
    }

    fs::path canonical = canonicalIfPossible(normalized);
    if (!lexicallyInside(canonical, canonicalIfPossible(fs::path("/opt"))) ||
        lexicallyUnderOrEqual(canonical, fs::path("/usr")) ||
        lexicallyUnderOrEqual(canonical, fs::path("/home"))) {
        return false;
    }

    if (!hasOptAppMarker(normalized)) {
        return false;
    }

    return !nativeOwnerForPath(normalized).has_value();
}

std::optional<fs::path> desktopExecPath(const std::vector<std::string> &lines) {
    auto exec = getKey(lines, "Desktop Entry", "Exec");
    if (!exec) {
        return std::nullopt;
    }
    auto command = executableFromDesktopExec(*exec);
    if (!command) {
        return std::nullopt;
    }

    fs::path path(*command);
    if (path.is_absolute()) {
        return path.lexically_normal();
    }
    if (command->find('/') != std::string::npos) {
        return std::nullopt;
    }
    if (auto resolved = findExecutable(*command)) {
        return fs::path(*resolved).lexically_normal();
    }
    return std::nullopt;
}

bool isSafeAppImagePath(const fs::path &path, const fs::path &ownerHome) {
    fs::path normalized = path.lexically_normal();
    if (!normalized.is_absolute() || !extensionEquals(normalized, ".appimage") || !isFileOrSymlink(normalized)) {
        return false;
    }

    if (lexicallyInside(normalized, ownerHome / "Applications")) {
        return true;
    }

    if (!lexicallyInside(normalized, fs::path("/opt"))) {
        return false;
    }

    fs::path rel = normalized.lexically_relative(fs::path("/opt"));
    auto first = rel.begin();
    if (first == rel.end() || isPublicOptName(first->string())) {
        return false;
    }

    fs::path canonical = canonicalIfPossible(normalized);
    return lexicallyInside(canonical, canonicalIfPossible(fs::path("/opt"))) &&
           !lexicallyUnderOrEqual(canonical, fs::path("/usr")) &&
           !lexicallyUnderOrEqual(canonical, fs::path("/home"));
}

std::optional<fs::path> inferOptAppRootFromExecutable(const fs::path &executable) {
    fs::path normalized = executable.lexically_normal();
    if (!normalized.is_absolute() || !lexicallyInside(normalized, fs::path("/opt")) ||
        !fs::exists(normalized) || extensionEquals(normalized, ".appimage")) {
        return std::nullopt;
    }

    fs::path relative = normalized.lexically_relative(fs::path("/opt"));
    std::vector<fs::path> parts;
    for (const auto &part : relative) {
        parts.push_back(part);
    }
    if (parts.size() < 2) {
        return std::nullopt;
    }

    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        if (lowerAscii(parts[i].string()) != "bin" || i == 0) {
            continue;
        }
        fs::path root = "/opt";
        for (std::size_t j = 0; j < i; ++j) {
            root /= parts[j];
        }
        return root.lexically_normal();
    }

    fs::path parent = normalized.parent_path();
    if (parent == fs::path("/opt")) {
        return std::nullopt;
    }
    return parent.lexically_normal();
}

bool isSafeDesktopPath(const fs::path &path,
                       const fs::path &ownerHome,
                       const fs::path &ownerDataHome,
                       bool allowSystemDesktop) {
    fs::path normalized = path.lexically_normal();
    if (!normalized.is_absolute() || !extensionEquals(normalized, ".desktop")) {
        return false;
    }
    if (fs::exists(normalized) && !isFileOrSymlink(normalized)) {
        return false;
    }

    if (lexicallyInside(normalized, ownerDataHome / "applications") ||
        lexicallyInside(normalized, ownerHome / ".local" / "share" / "applications")) {
        return true;
    }

    if (!allowSystemDesktop) {
        return false;
    }

    if (lexicallyInside(normalized, fs::path("/usr/share/applications")) ||
        lexicallyInside(normalized, fs::path("/usr/local/share/applications"))) {
        return !fs::exists(normalized) || !nativeOwnerForPath(normalized).has_value();
    }

    return false;
}

bool hasIconExtension(const fs::path &path) {
    return hasAnyExtension(path, {".png", ".svg", ".svgz", ".xpm", ".ico", ".webp"});
}

bool isSystemIconPath(const fs::path &path) {
    return lexicallyInside(path, fs::path("/usr/share/icons")) ||
           lexicallyInside(path, fs::path("/usr/share/pixmaps")) ||
           lexicallyInside(path, fs::path("/usr/local/share/icons")) ||
           lexicallyInside(path, fs::path("/usr/local/share/pixmaps"));
}

bool isSafeIconPath(const fs::path &path, const fs::path &ownerHome, const fs::path &ownerDataHome) {
    fs::path normalized = path.lexically_normal();
    if (!normalized.is_absolute() || !hasIconExtension(normalized)) {
        return false;
    }
    if (fs::exists(normalized) && !isFileOrSymlink(normalized)) {
        return false;
    }

    if (lexicallyInside(normalized, ownerDataHome / "icons") ||
        lexicallyInside(normalized, ownerDataHome / "pixmaps") ||
        lexicallyInside(normalized, ownerHome / ".local" / "share" / "icons") ||
        lexicallyInside(normalized, ownerHome / ".local" / "share" / "pixmaps") ||
        lexicallyInside(normalized, ownerHome / "Applications")) {
        return true;
    }

    if (lexicallyInside(normalized, fs::path("/opt"))) {
        fs::path rel = normalized.lexically_relative(fs::path("/opt"));
        auto first = rel.begin();
        return first != rel.end() && !isPublicOptName(first->string());
    }

    if (isSystemIconPath(normalized)) {
        return !fs::exists(normalized) || !nativeOwnerForPath(normalized).has_value();
    }

    return false;
}

bool iconLooksAppSpecific(const fs::path &path, const std::vector<std::string> &identities) {
    std::string iconId = identityToken(path.stem().string());
    if (iconId.size() < 3) {
        return false;
    }
    for (const auto &id : identities) {
        if (id.size() < 3) {
            continue;
        }
        if (iconId == id) {
            return true;
        }
        if (iconId.size() >= 4 && id.size() >= 4 &&
            (iconId.find(id) != std::string::npos || id.find(iconId) != std::string::npos)) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> appIdentities(const fs::path &desktopPath,
                                       const fs::path &executable,
                                       const std::optional<fs::path> &installRoot,
                                       const std::string &displayName) {
    std::vector<std::string> identities;
    auto add = [&identities](const std::string &value) {
        std::string id = identityToken(value);
        if (id.empty()) {
            return;
        }
        if (std::find(identities.begin(), identities.end(), id) == identities.end()) {
            identities.push_back(id);
        }
    };

    add(desktopPath.stem().string());
    add(executable.stem().string());
    add(displayName);
    if (installRoot) {
        add(installRoot->filename().string());
    }
    return identities;
}

void addRemovalTarget(std::vector<RemovalTarget> &targets,
                      const std::string &kind,
                      const fs::path &path,
                      const std::string &label) {
    std::string normalized = path.lexically_normal().string();
    for (const auto &target : targets) {
        if (target.path == normalized) {
            return;
        }
    }
    targets.push_back(RemovalTarget{kind, normalized, label});
}

void addDesktopTargets(std::vector<RemovalTarget> &targets,
                       const fs::path &desktopPath,
                       const fs::path &sourcePath,
                       const fs::path &ownerHome,
                       const fs::path &ownerDataHome,
                       bool allowSystemDesktop) {
    if (fs::exists(desktopPath) &&
        isSafeDesktopPath(desktopPath, ownerHome, ownerDataHome, allowSystemDesktop)) {
        addRemovalTarget(targets, "desktop", desktopPath, "启动器");
    }
    if (sourcePath != desktopPath && fs::exists(sourcePath) &&
        isSafeDesktopPath(sourcePath, ownerHome, ownerDataHome, allowSystemDesktop)) {
        addRemovalTarget(targets, "desktop", sourcePath, "启动器");
    }
}

void addNamedIconTargets(std::vector<RemovalTarget> &targets,
                         const std::string &iconName,
                         const fs::path &ownerHome,
                         const fs::path &ownerDataHome,
                         const std::vector<std::string> &identities) {
    fs::path iconPath(iconName);
    if (iconName.empty() || iconPath.is_absolute() || iconName.find('/') != std::string::npos) {
        return;
    }
    if (!iconLooksAppSpecific(iconPath, identities)) {
        return;
    }

    std::vector<fs::path> roots = {
        ownerDataHome / "icons",
        ownerDataHome / "pixmaps",
        ownerHome / ".local" / "share" / "icons",
        ownerHome / ".local" / "share" / "pixmaps"};
    std::string wanted = identityToken(iconPath.stem().string());

    std::vector<std::string> seenRoots;
    for (const auto &root : roots) {
        fs::path normalizedRoot = root.lexically_normal();
        std::string rootKey = normalizedRoot.string();
        if (std::find(seenRoots.begin(), seenRoots.end(), rootKey) != seenRoots.end() ||
            !fs::exists(normalizedRoot)) {
            continue;
        }
        seenRoots.push_back(rootKey);

        std::error_code ec;
        for (fs::recursive_directory_iterator it(normalizedRoot,
                                                 fs::directory_options::skip_permission_denied,
                                                 ec),
             end;
             it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            const fs::path candidate = it->path();
            if (!hasIconExtension(candidate) || identityToken(candidate.stem().string()) != wanted ||
                !isSafeIconPath(candidate, ownerHome, ownerDataHome)) {
                continue;
            }
            addRemovalTarget(targets, "icon", candidate, "图标");
        }
    }
}

void addIconTargets(std::vector<RemovalTarget> &targets,
                    const std::vector<std::string> &lines,
                    const fs::path &ownerHome,
                    const fs::path &ownerDataHome,
                    const std::optional<fs::path> &coveredRoot,
                    const std::vector<std::string> &identities) {
    auto icon = getKey(lines, "Desktop Entry", "Icon");
    if (!icon || trim(*icon).empty()) {
        return;
    }

    std::string value = trim(*icon);
    fs::path iconPath(value);
    if (!iconPath.is_absolute()) {
        addNamedIconTargets(targets, value, ownerHome, ownerDataHome, identities);
        return;
    }

    iconPath = iconPath.lexically_normal();
    if (coveredRoot && lexicallyUnderOrEqual(iconPath, *coveredRoot)) {
        return;
    }
    if (!fs::exists(iconPath) || !isSafeIconPath(iconPath, ownerHome, ownerDataHome) ||
        !iconLooksAppSpecific(iconPath, identities)) {
        return;
    }

    addRemovalTarget(targets, "icon", iconPath, "图标");
}

bool targetNeedsRoot(const RemovalTarget &target, const fs::path &ownerHome, const fs::path &ownerDataHome) {
    fs::path path(target.path);
    if (target.kind == "opt-dir") {
        return true;
    }
    return !(lexicallyUnderOrEqual(path, ownerHome) || lexicallyUnderOrEqual(path, ownerDataHome));
}

std::optional<std::vector<std::string>> internalRemovalCommand(const std::vector<RemovalTarget> &targets) {
    if (targets.empty()) {
        return std::nullopt;
    }

    std::optional<fs::path> binary = selfPath();
    if (!binary) {
        if (auto found = findExecutable(kProgramName)) {
            binary = fs::path(*found);
        }
    }
    if (!binary) {
        return std::nullopt;
    }

    fs::path ownerHome = homeDir();
    fs::path ownerDataHome = dataHome();
    std::vector<std::string> command = {
        binary->string(), "--remove-paths", ownerHome.string(), ownerDataHome.string()};
    bool needsRoot = false;
    for (const auto &target : targets) {
        needsRoot = needsRoot || targetNeedsRoot(target, ownerHome, ownerDataHome);
        command.push_back(target.kind);
        command.push_back(target.path);
    }

    if (needsRoot) {
        command = withPkexec(command);
    }
    if (command.empty()) {
        return std::nullopt;
    }
    return command;
}

std::string manualRemovalNote() {
    return "不会默认删除 ~/.config、~/.local/share、~/.cache 中的用户数据。";
}

>>>>>>> 74cff8e (Initial commit)
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
<<<<<<< HEAD
    return RemovalPlan{displayName, manager, package, removeCommand, sourcePath.string()};
=======
    return RemovalPlan{displayName, manager, package, removeCommand, sourcePath.string(), {}, {}};
}

std::optional<RemovalPlan> planForAppImage(const std::vector<std::string> &lines,
                                           const fs::path &desktopPath,
                                           const fs::path &sourcePath,
                                           const std::string &displayName) {
    auto executable = desktopExecPath(lines);
    if (!executable) {
        return std::nullopt;
    }

    fs::path ownerHome = homeDir();
    fs::path ownerDataHome = dataHome();
    if (!isSafeAppImagePath(*executable, ownerHome)) {
        return std::nullopt;
    }

    std::vector<RemovalTarget> targets;
    addRemovalTarget(targets, "appimage", *executable, "AppImage");
    addDesktopTargets(targets, desktopPath, sourcePath, ownerHome, ownerDataHome, true);

    auto identities = appIdentities(desktopPath, *executable, std::nullopt, displayName);
    addIconTargets(targets, lines, ownerHome, ownerDataHome, std::nullopt, identities);

    auto command = internalRemovalCommand(targets);
    if (!command) {
        return std::nullopt;
    }

    RemovalPlan plan{
        displayName, "AppImage", executable->string(), *command, sourcePath.string(), std::move(targets),
        manualRemovalNote()};
    return plan;
}

std::optional<RemovalPlan> planForOptApplication(const std::vector<std::string> &lines,
                                                 const fs::path &desktopPath,
                                                 const fs::path &sourcePath,
                                                 const std::string &displayName) {
    auto executable = desktopExecPath(lines);
    if (!executable) {
        return std::nullopt;
    }

    auto root = inferOptAppRootFromExecutable(*executable);
    if (!root || !isSafeOptAppRoot(*root)) {
        return std::nullopt;
    }

    fs::path ownerHome = homeDir();
    fs::path ownerDataHome = dataHome();
    std::vector<RemovalTarget> targets;
    addDesktopTargets(targets, desktopPath, sourcePath, ownerHome, ownerDataHome, true);

    std::optional<fs::path> installRoot = *root;
    auto identities = appIdentities(desktopPath, *executable, installRoot, displayName);
    addIconTargets(targets, lines, ownerHome, ownerDataHome, installRoot, identities);
    addRemovalTarget(targets, "opt-dir", *root, "应用目录");

    auto command = internalRemovalCommand(targets);
    if (!command) {
        return std::nullopt;
    }

    RemovalPlan plan{
        displayName, "manual /opt", root->string(), *command, sourcePath.string(), std::move(targets),
        manualRemovalNote()};
    return plan;
>>>>>>> 74cff8e (Initial commit)
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
<<<<<<< HEAD
        return RemovalPlan{displayName, "flatpak", appId, command, sourcePath.string()};
=======
        return RemovalPlan{displayName, "flatpak", appId, command, sourcePath.string(), {}, {}};
>>>>>>> 74cff8e (Initial commit)
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
<<<<<<< HEAD
        return RemovalPlan{displayName, "flatpak", appId, command, sourcePath.string()};
=======
        return RemovalPlan{displayName, "flatpak", appId, command, sourcePath.string(), {}, {}};
>>>>>>> 74cff8e (Initial commit)
    }

    if (auto snap = getKey(lines, "Desktop Entry", "X-SnapInstanceName"); snap && !trim(*snap).empty()) {
        std::string snapName = trim(*snap);
        auto command = withPkexec({"snap", "remove", snapName});
        if (!command.empty()) {
<<<<<<< HEAD
            return RemovalPlan{displayName, "snap", snapName, command, sourcePath.string()};
=======
            return RemovalPlan{displayName, "snap", snapName, command, sourcePath.string(), {}, {}};
>>>>>>> 74cff8e (Initial commit)
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
<<<<<<< HEAD
            return RemovalPlan{displayName, "snap", snapName, command, sourcePath.string()};
        }
    }

    return planForNativePackage(lines, desktopPath, sourcePath, displayName);
=======
            return RemovalPlan{displayName, "snap", snapName, command, sourcePath.string(), {}, {}};
        }
    }

    if (auto native = planForNativePackage(lines, desktopPath, sourcePath, displayName)) {
        return native;
    }
    if (auto appImage = planForAppImage(lines, desktopPath, sourcePath, displayName)) {
        return appImage;
    }
    if (auto optApplication = planForOptApplication(lines, desktopPath, sourcePath, displayName)) {
        return optApplication;
    }

    return std::nullopt;
}

bool isSafeRemovalTarget(const std::string &kind,
                         const fs::path &path,
                         const fs::path &ownerHome,
                         const fs::path &ownerDataHome,
                         std::string &error) {
    if (kind == "appimage") {
        if (isSafeAppImagePath(path, ownerHome)) {
            return true;
        }
    } else if (kind == "desktop") {
        if (isSafeDesktopPath(path, ownerHome, ownerDataHome, true)) {
            return true;
        }
    } else if (kind == "icon") {
        if (isSafeIconPath(path, ownerHome, ownerDataHome)) {
            return true;
        }
    } else if (kind == "opt-dir") {
        if (isSafeOptAppRoot(path)) {
            return true;
        }
    } else {
        error = "unknown target kind: " + kind;
        return false;
    }

    error = "refusing unsafe " + kind + " path: " + path.string();
    return false;
}

int removePlannedPaths(int argc, char **argv) {
    if (argc < 6 || ((argc - 4) % 2) != 0) {
        std::cerr << "invalid --remove-paths arguments\n";
        return 64;
    }

    fs::path ownerHome = fs::path(argv[2]).lexically_normal();
    fs::path ownerDataHome = fs::path(argv[3]).lexically_normal();
    if (!ownerHome.is_absolute() || !ownerDataHome.is_absolute()) {
        std::cerr << "owner paths must be absolute\n";
        return 64;
    }

    struct Target {
        std::string kind;
        fs::path path;
    };

    std::vector<Target> targets;
    for (int i = 4; i < argc; i += 2) {
        Target target{argv[i], fs::path(argv[i + 1]).lexically_normal()};
        if (!target.path.is_absolute()) {
            std::cerr << "target path must be absolute: " << target.path.string() << '\n';
            return 64;
        }

        std::string error;
        if (!isSafeRemovalTarget(target.kind, target.path, ownerHome, ownerDataHome, error)) {
            std::cerr << error << '\n';
            return 65;
        }
        targets.push_back(std::move(target));
    }

    for (const auto &target : targets) {
        std::error_code ec;
        if (!fs::exists(target.path, ec) && !fs::is_symlink(fs::symlink_status(target.path, ec))) {
            continue;
        }

        if (target.kind == "opt-dir") {
            auto status = fs::symlink_status(target.path, ec);
            if (ec || !fs::is_directory(status) || fs::is_symlink(status)) {
                std::cerr << "refusing non-directory opt target: " << target.path.string() << '\n';
                return 65;
            }
            fs::remove_all(target.path, ec);
        } else {
            auto status = fs::symlink_status(target.path, ec);
            if (ec || fs::is_directory(status)) {
                std::cerr << "refusing directory file target: " << target.path.string() << '\n';
                return 65;
            }
            fs::remove(target.path, ec);
        }

        if (ec) {
            std::cerr << "cannot remove " << target.path.string() << ": " << ec.message() << '\n';
            return 1;
        }
    }

    return 0;
>>>>>>> 74cff8e (Initial commit)
}

} // namespace kde_uninstall
