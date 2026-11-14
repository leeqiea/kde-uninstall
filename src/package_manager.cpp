#include "kde_uninstall/package_manager.h"

#include "kde_uninstall/command.h"
#include "kde_uninstall/constants.h"
#include "kde_uninstall/desktop_file.h"
#include "kde_uninstall/paths.h"
#include "kde_uninstall/string_utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace kde_uninstall {

using Env = std::map<std::string, std::string>;

// ---------------------------------------------------------------------------
// Exec= parsing. Parse only; never run a launcher command to discover its
// installation.
// ---------------------------------------------------------------------------

std::vector<std::string> parseDesktopExec(const std::string &exec) {
    std::vector<std::string> tokens;
    std::string current;
    char quote = 0;
    bool escaped = false;
    bool started = false;
    for (char c : exec) {
        if (escaped) {
            current.push_back(c);
            escaped = false;
        } else if (c == '\\' && quote != '\'') {
            escaped = true;
            started = true;
        } else if ((c == '"' || c == '\'') && (!quote || quote == c)) {
            quote = quote ? 0 : c;
            started = true;
        } else if (std::isspace(static_cast<unsigned char>(c)) && !quote) {
            if (started) {
                tokens.push_back(current);
                current.clear();
                started = false;
            }
        } else {
            current.push_back(c);
            started = true;
        }
    }
    if (quote || escaped) {
        return {};
    }
    if (started) {
        tokens.push_back(current);
    }
    return tokens;
}

bool looksLikeEnvAssignment(const std::string &token) {
    auto eq = token.find('=');
    if (eq == std::string::npos || eq == 0 ||
        !(std::isalpha(static_cast<unsigned char>(token[0])) || token[0] == '_')) {
        return false;
    }
    for (std::size_t i = 1; i < eq; ++i) {
        unsigned char c = token[i];
        if (!(std::isalnum(c) || c == '_')) {
            return false;
        }
    }
    return true;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

// Wrappers that only change how the real program runs.
bool isTransparentWrapper(const std::string &name) {
    static const std::set<std::string> names = {
        "gamemoderun", "prime-run", "mangohud", "nohup", "firejail", "optirun", "primusrun", "pvkrun", "strangle"};
    return names.count(lowerAscii(name)) != 0;
}

// Returns the tokens starting at the real program, after env/shell/wrappers.
std::optional<std::vector<std::string>> commandFromTokens(const std::vector<std::string> &tokens,
                                                          Env &env,
                                                          int depth = 0) {
    if (depth > 4) {
        return std::nullopt;
    }
    auto take = [&env](const std::string &token) {
        auto eq = token.find('=');
        env[token.substr(0, eq)] = token.substr(eq + 1);
    };
    std::size_t i = 0;
    while (i < tokens.size() && looksLikeEnvAssignment(tokens[i])) {
        take(tokens[i++]);
    }
    if (i == tokens.size() || tokens[i].empty() || tokens[i][0] == '%') {
        return std::nullopt;
    }
    const std::string name = basenameOf(tokens[i]);
    if (name == "env") {
        ++i;
        while (i < tokens.size()) {
            const auto &arg = tokens[i];
            if (arg == "--") {
                ++i;
                break;
            }
            if (looksLikeEnvAssignment(arg)) {
                take(arg);
                ++i;
            } else if (arg == "-i" || arg == "--ignore-environment") {
                ++i;
            } else if (arg == "-u" || arg == "--unset") {
                if (i + 1 >= tokens.size()) {
                    return std::nullopt;
                }
                i += 2;
            } else if (startsWith(arg, "--unset=")) {
                ++i;
            } else if (!arg.empty() && arg[0] == '-') {
                return std::nullopt;
            } else {
                break;
            }
        }
    } else if (name == "sh" || name == "bash" || name == "dash" || name == "zsh") {
        ++i;
        if (i < tokens.size() && (tokens[i] == "-c" || tokens[i] == "-lc")) {
            if (++i == tokens.size() || tokens[i].find_first_of(";&|<>`$\n\r") != std::string::npos) {
                return std::nullopt;
            }
            auto command = parseDesktopExec(tokens[i]);
            if (!command.empty() && command.front() == "exec") {
                command.erase(command.begin());
            }
            return commandFromTokens(command, env, depth + 1);
        }
        if (i < tokens.size() && tokens[i] == "--") {
            ++i;
        }
        if (i == tokens.size() || tokens[i].empty() || tokens[i][0] == '-') {
            return std::nullopt;
        }
    } else if (isTransparentWrapper(name)) {
        ++i;
        while (i < tokens.size() && !tokens[i].empty() && tokens[i][0] == '-') {
            ++i;
        }
    } else {
        return std::vector<std::string>(tokens.begin() + static_cast<std::ptrdiff_t>(i), tokens.end());
    }
    return commandFromTokens(std::vector<std::string>(tokens.begin() + static_cast<std::ptrdiff_t>(i), tokens.end()),
                             env, depth + 1);
}

std::optional<std::string> executableFromDesktopExec(const std::string &exec) {
    Env env;
    auto command = commandFromTokens(parseDesktopExec(exec), env);
    if (!command) {
        return std::nullopt;
    }
    return command->front();
}

// ---------------------------------------------------------------------------
// Native package ownership
// ---------------------------------------------------------------------------

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
    std::error_code ec;
    if (path.empty() || !fs::exists(path, ec)) {
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

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

// Keeps ASCII alphanumerics and every non-ASCII byte so CJK names still match.
std::string identityToken(const std::string &value) {
    std::string result;
    for (unsigned char c : lowerAscii(value)) {
        if (std::isalnum(c) || c >= 0x80) {
            result.push_back(static_cast<char>(c));
        }
    }
    return result;
}

// "Obsidian-1.5.3-x86_64" -> "Obsidian", "foo_v2" -> "foo".
std::string stripVersion(std::string value) {
    std::string lowered = lowerAscii(value);
    for (const char *suffix : {"-x86_64", "_x86_64", ".x86_64", "-amd64", "_amd64", "-x64", "-aarch64",
                               "-arm64", "-linux", "_linux"}) {
        auto pos = lowered.find(suffix);
        if (pos != std::string::npos && pos > 0) {
            value.resize(pos);
            lowered.resize(pos);
        }
    }
    for (std::size_t i = 1; i < value.size(); ++i) {
        char c = value[i];
        if (c != '-' && c != '_' && c != '.' && c != ' ') {
            continue;
        }
        std::size_t j = i + 1;
        if (j < value.size() && (value[j] == 'v' || value[j] == 'V')) {
            ++j;
        }
        if (j < value.size() && std::isdigit(static_cast<unsigned char>(value[j]))) {
            value.resize(i);
            break;
        }
    }
    return value;
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
    return std::distance(p.begin(), p.end()) > std::distance(r.begin(), r.end());
}

bool isFileOrSymlink(const fs::path &path) {
    std::error_code ec;
    auto status = fs::symlink_status(path, ec);
    if (ec) {
        return false;
    }
    return fs::is_regular_file(status) || fs::is_symlink(status);
}

bool hasContainedParent(const fs::path &path, const fs::path &root) {
    return lexicallyInside(path, root) &&
           lexicallyUnderOrEqual(canonicalIfPossible(path.parent_path()), canonicalIfPossible(root));
}

fs::path canonicalNormal(const fs::path &path) {
    return canonicalIfPossible(path).lexically_normal();
}

std::vector<fs::path> canonicalAll(std::vector<fs::path> paths) {
    std::vector<fs::path> result;
    for (const auto &path : paths) {
        auto canonical = canonicalNormal(path);
        if (std::find(result.begin(), result.end(), canonical) == result.end()) {
            result.push_back(canonical);
        }
    }
    return result;
}

bool isPublicDirName(const std::string &name) {
    static const std::set<std::string> publicNames = {
        "bin", "sbin", "share", "lib", "lib32", "lib64", "libx32", "libexec", "etc", "include", "var",
        "tmp", "cache", "doc", "docs", "man", "src", "local", "opt"};
    return publicNames.count(lowerAscii(name)) != 0;
}

// Directories that usually hold several applications; the app is one level below.
bool isNamedContainer(const std::string &name) {
    static const std::set<std::string> names = {
        "google", "jetbrains", "microsoft", "apps", "applications", "common", "games", "programs",
        "program files", "program files (x86)", "steamapps", "software", "tools", "opt", "packages"};
    return names.count(lowerAscii(name)) != 0;
}

std::map<std::string, fs::path> xdgUserDirMap(const fs::path &home) {
    std::map<std::string, fs::path> dirs = {
        {"DESKTOP", home / "Desktop"},     {"DOWNLOAD", home / "Downloads"}, {"DOCUMENTS", home / "Documents"},
        {"MUSIC", home / "Music"},         {"PICTURES", home / "Pictures"},  {"VIDEOS", home / "Videos"},
        {"TEMPLATES", home / "Templates"}, {"PUBLICSHARE", home / "Public"}};
    std::error_code ec;
    fs::path config = home / ".config" / "user-dirs.dirs";
    if (!fs::exists(config, ec)) {
        return dirs;
    }
    try {
        for (const auto &raw : readLines(config)) {
            std::string line = trim(raw);
            if (!startsWith(line, "XDG_") || line.find("_DIR=") == std::string::npos) {
                continue;
            }
            auto eq = line.find('=');
            std::string key = line.substr(4, line.find("_DIR=") - 4);
            std::string value = trim(line.substr(eq + 1));
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            if (startsWith(value, "$HOME/")) {
                dirs[key] = home / value.substr(6);
            } else if (fs::path(value).is_absolute() && lexicallyInside(value, home)) {
                dirs[key] = fs::path(value);
            }
        }
    } catch (const std::exception &) {
    }
    return dirs;
}

// Places where users unpack or keep applications; an app is a child of one.
std::vector<fs::path> appContainers(const fs::path &home, const fs::path &ownerDataHome) {
    std::vector<fs::path> containers = {
        "/opt", "/usr/local", home, ownerDataHome, home / ".local/share", home / ".local/opt",
        home / ".local/apps", home / "Applications", home / "applications", home / "apps", home / "Apps",
        home / "opt", home / ".opt", home / "programs", home / "Programs", home / "software",
        home / "Software", home / "Games", home / "games", home / "tools", home / "Tools"};
    for (const auto &entry : xdgUserDirMap(home)) {
        containers.push_back(entry.second);
    }
    return canonicalAll(containers);
}

// Paths that must never be removed, nor any of their ancestors.
std::vector<fs::path> protectedPaths(const fs::path &home, const fs::path &ownerDataHome) {
    std::vector<fs::path> paths = {
        "/", "/opt", "/usr", "/usr/local", "/home", "/root", "/var", "/etc", "/srv", "/mnt", "/media",
        home, home / ".local", home / ".local/bin", home / ".local/lib", home / ".local/state",
        home / ".local/share", home / ".config", home / ".cache", home / ".ssh", home / ".gnupg",
        home / ".pki", home / ".var", home / "bin", ownerDataHome};
    for (const char *name : {"applications", "icons", "pixmaps", "Trash", "flatpak", "fonts", "mime",
                             "keyrings", "plasma", "kwalletd", "pipx", "Steam", "lutris", "bottles",
                             "containers", "virtualenvs", "pnpm", "umu", "heroic"}) {
        paths.push_back(ownerDataHome / name);
        paths.push_back(home / ".local/share" / name);
    }
    auto containers = appContainers(home, ownerDataHome);
    paths.insert(paths.end(), containers.begin(), containers.end());
    return canonicalAll(paths);
}

bool isProtectedPath(const fs::path &path, const fs::path &home, const fs::path &ownerDataHome) {
    for (const auto &protectedPath : protectedPaths(home, ownerDataHome)) {
        if (lexicallyUnderOrEqual(protectedPath, path)) {
            return true;
        }
    }
    return false;
}

// Deepest container holding path. "Program Files" and "steamapps/common"
// directories act as containers wherever they appear (Wine prefixes, Steam).
std::optional<fs::path> containerFor(const fs::path &path, const fs::path &home, const fs::path &ownerDataHome) {
    const auto canonicalHome = canonicalNormal(home);
    if (lexicallyInside(path, canonicalHome) || lexicallyInside(path, "/opt") ||
        lexicallyInside(path, "/usr/local")) {
        fs::path best;
        fs::path acc;
        for (const auto &part : path.parent_path()) {
            acc /= part;
            const auto name = lowerAscii(part.string());
            if (name == "program files" || name == "program files (x86)" ||
                (name == "common" && lowerAscii(acc.parent_path().filename().string()) == "steamapps")) {
                best = acc;
            }
        }
        if (!best.empty()) {
            return best;
        }
    }

    std::optional<fs::path> best;
    std::ptrdiff_t depth = -1;
    for (const auto &container : appContainers(home, ownerDataHome)) {
        if (lexicallyInside(path, container)) {
            auto d = std::distance(container.begin(), container.end());
            if (d > depth) {
                best = container;
                depth = d;
            }
        }
    }
    return best;
}

bool isPathEntry(const fs::path &dir) {
    const char *pathEnv = std::getenv("PATH");
    if (!pathEnv) {
        return false;
    }
    for (const auto &entry : split(pathEnv, ':')) {
        if (!entry.empty() && canonicalNormal(entry) == dir) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Safety checks. These run again inside the privileged remover, so they may
// only depend on the owner home/data paths passed to it.
// ---------------------------------------------------------------------------

bool isSafeAppDir(const fs::path &root, const fs::path &ownerHome, const fs::path &ownerDataHome) {
    const fs::path normalized = root.lexically_normal();
    if (!normalized.is_absolute()) {
        return false;
    }
    std::error_code ec;
    auto status = fs::symlink_status(normalized, ec);
    if (ec || !fs::is_directory(status) || fs::is_symlink(status) || canonicalNormal(normalized) != normalized) {
        return false;
    }
    auto container = containerFor(normalized / "x", ownerHome, ownerDataHome);
    if (!container || !lexicallyInside(normalized, *container)) {
        return false;
    }
    for (const auto &part : normalized.lexically_relative(*container)) {
        const auto name = part.string();
        if (name.empty() || name.front() == '.' || isPublicDirName(name)) {
            return false;
        }
    }
    if (isNamedContainer(normalized.filename().string()) ||
        isProtectedPath(normalized, ownerHome, ownerDataHome)) {
        return false;
    }
    return !nativeOwnerForPath(normalized).has_value();
}

bool isAllowedLooseLocation(const fs::path &path, const fs::path &ownerHome, const fs::path &ownerDataHome) {
    const auto home = canonicalNormal(ownerHome);
    if (lexicallyInside(path, home) || lexicallyInside(path, canonicalNormal(ownerDataHome))) {
        for (const auto &secret : {home / ".ssh", home / ".gnupg", home / ".pki"}) {
            if (lexicallyUnderOrEqual(path, secret)) {
                return false;
            }
        }
        return true;
    }
    return lexicallyInside(path, "/opt") || lexicallyInside(path, "/usr/local");
}

// A single program file (or a link to one) that no package owns.
bool isSafeLooseFile(const fs::path &path,
                     const fs::path &ownerHome,
                     const fs::path &ownerDataHome,
                     bool requireExecutable) {
    const fs::path normalized = path.lexically_normal();
    if (!normalized.is_absolute() || !isFileOrSymlink(normalized) ||
        canonicalNormal(normalized.parent_path()) != normalized.parent_path() ||
        !isAllowedLooseLocation(normalized, ownerHome, ownerDataHome)) {
        return false;
    }
    std::error_code ec;
    auto status = fs::status(normalized, ec);
    if (!ec && fs::exists(status)) {
        if (!fs::is_regular_file(status)) {
            return false;
        }
        const auto exec = fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;
        if (requireExecutable && (status.permissions() & exec) == fs::perms::none) {
            return false;
        }
    }
    return !nativeOwnerForPath(normalized).has_value();
}

bool isSafeAppImagePath(const fs::path &path, const fs::path &ownerHome, const fs::path &ownerDataHome) {
    return extensionEquals(path, ".appimage") && isSafeLooseFile(path, ownerHome, ownerDataHome, false);
}

// Vendor directories in ~/.config etc. that keep one subdirectory per product.
bool isVendorDataName(const std::string &name) {
    static const std::set<std::string> names = {"jetbrains", "google", "microsoft", "adobe", "unity3d", "epic"};
    return names.count(lowerAscii(name)) != 0;
}

bool isProtectedDataName(std::string name) {
    name = lowerAscii(name);
    if (!name.empty() && name.front() == '.') {
        name.erase(name.begin());
    }
    static const std::set<std::string> names = {
        "applications", "icons", "pixmaps", "mime", "fonts", "themes", "color-schemes", "menus",
        "desktop-directories", "autostart", "trash", "flatpak", "systemd", "dbus-1", "dconf", "pulse",
        "pipewire", "wireplumber", "fontconfig", "keyrings", "kwalletd", "baloo", "akonadi", "sddm",
        "session", "sessions", "thumbnails", "mesa_shader_cache", "nvidia", "vulkan", "recently-used.xbel",
        "user-places.xbel", "user-dirs.dirs", "user-dirs.locale", "mimeapps.list", "containers", "nix",
        "steam", "lutris", "bottles", "wine", "pipx", "virtualenvs", "python", "pip", "npm", "nvm", "cargo",
        "rustup", "go", "java", "gradle", "m2", "ssh", "gnupg", "pki", "local", "config", "cache", "var",
        "mozilla", "evolution", "gvfs-metadata", "tracker", "tracker3", "konsole", "dolphin", "okular",
        "spectacle", "kate", "konqueror", "kdeconnect", "bash_history", "bashrc", "profile", "zshrc",
        // Vendor directories hold data for several products.
        "jetbrains", "google", "microsoft", "adobe", "valve", "oracle", "nvidia", "intel", "amd", "apple",
        "canonical", "mozilla", "unity3d", "epic", "tencent", "alibaba", "bytedance", "baidu", "kingsoft"};
    if (names.count(name) || isNamedContainer(name)) {
        return true;
    }
    for (const char *prefix : {"kde", "plasma", "kwin", "kscreen", "kactivity", "kservices", "kxmlgui", "knewstuff",
                               "kf5", "kf6", "gtk", "gnome", "xdg", "qt5ct", "qt6ct", "xsession", "xauthority"}) {
        if (startsWith(name, prefix)) {
            return true;
        }
    }
    return false;
}

std::vector<fs::path> userDataRoots(const fs::path &ownerHome, const fs::path &ownerDataHome) {
    return canonicalAll({ownerHome / ".config", ownerHome / ".cache", ownerHome / ".local/state", ownerDataHome,
                         ownerHome / ".local/share"});
}

// A direct child of ~/.config, ~/.cache, ~/.local/state, the data home, or a
// hidden entry directly in home.
bool isSafeDataPath(const fs::path &path, const fs::path &ownerHome, const fs::path &ownerDataHome) {
    const fs::path normalized = path.lexically_normal();
    if (!normalized.is_absolute()) {
        return false;
    }
    std::error_code ec;
    auto status = fs::symlink_status(normalized, ec);
    if (ec || !(fs::is_directory(status) || fs::is_regular_file(status) || fs::is_symlink(status))) {
        return false;
    }
    const auto parent = normalized.parent_path();
    const auto name = normalized.filename().string();
    if (name.empty() || name == "." || name == ".." || canonicalNormal(parent) != parent) {
        return false;
    }
    bool allowed = false;
    const auto roots = userDataRoots(ownerHome, ownerDataHome);
    auto isRoot = [&roots](const fs::path &dir) { return std::find(roots.begin(), roots.end(), dir) != roots.end(); };
    if (parent == canonicalNormal(ownerHome)) {
        allowed = name.front() == '.';
    } else if (isRoot(parent)) {
        allowed = name.front() != '.';
    } else if (isVendorDataName(parent.filename().string()) && isRoot(parent.parent_path())) {
        // e.g. ~/.config/JetBrains/CLion2026.3
        allowed = name.front() != '.';
    }
    return allowed && !isProtectedDataName(name) && !isProtectedPath(normalized, ownerHome, ownerDataHome);
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

    if (hasContainedParent(normalized, ownerDataHome / "applications") ||
        hasContainedParent(normalized, ownerHome / ".local" / "share" / "applications") ||
        hasContainedParent(normalized, ownerHome / ".config" / "autostart") ||
        hasContainedParent(normalized, xdgUserDirMap(ownerHome)["DESKTOP"])) {
        return true;
    }

    if (!allowSystemDesktop) {
        return false;
    }

    if (hasContainedParent(normalized, fs::path("/usr/share/applications")) ||
        hasContainedParent(normalized, fs::path("/usr/local/share/applications"))) {
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

    if (hasContainedParent(normalized, ownerDataHome / "icons") ||
        hasContainedParent(normalized, ownerDataHome / "pixmaps") ||
        hasContainedParent(normalized, ownerHome / ".local" / "share" / "icons") ||
        hasContainedParent(normalized, ownerHome / ".local" / "share" / "pixmaps") ||
        hasContainedParent(normalized, ownerHome / "Applications")) {
        return true;
    }

    if (lexicallyInside(normalized, fs::path("/opt"))) {
        fs::path rel = normalized.lexically_relative(fs::path("/opt"));
        auto first = rel.begin();
        return first != rel.end() && !isPublicDirName(first->string()) &&
               canonicalIfPossible(normalized.parent_path()) == normalized.parent_path() &&
               (!fs::exists(normalized) || !nativeOwnerForPath(normalized).has_value());
    }

    if (isSystemIconPath(normalized)) {
        return canonicalIfPossible(normalized.parent_path()) == normalized.parent_path() &&
               (!fs::exists(normalized) || !nativeOwnerForPath(normalized).has_value());
    }

    return false;
}

std::vector<fs::path> commandLinkRoots(const fs::path &ownerHome) {
    return {ownerHome / ".local/bin", ownerHome / "bin", "/usr/local/bin",
            "/usr/local/sbin", "/usr/bin", "/opt/bin"};
}

bool isSafeCommandLink(const fs::path &path, const fs::path &ownerHome) {
    const auto normalized = path.lexically_normal();
    const auto roots = commandLinkRoots(ownerHome);
    if (std::find(roots.begin(), roots.end(), normalized.parent_path()) == roots.end() ||
        canonicalIfPossible(normalized.parent_path()) != normalized.parent_path()) {
        return false;
    }
    std::error_code ec;
    return fs::is_symlink(fs::symlink_status(normalized, ec)) &&
           !nativeOwnerForPath(normalized).has_value();
}

bool pointsToApplication(const fs::path &path, const fs::path &application, bool directory) {
    const auto resolved = canonicalIfPossible(path);
    return directory ? lexicallyInside(resolved, application) : resolved == application;
}

bool isApplicationKind(const std::string &kind) {
    return kind == "app-dir" || kind == "opt-dir" || kind == "appimage" || kind == "file";
}

bool isDirectoryKind(const std::string &kind) {
    return kind == "app-dir" || kind == "opt-dir";
}

// ---------------------------------------------------------------------------
// Launcher analysis
// ---------------------------------------------------------------------------

bool isWineName(const std::string &name) {
    const auto n = lowerAscii(name);
    return n == "wine" || n == "wine64" || n == "wine-stable" || n == "wine-staging" || n == "wine-devel";
}

// Runtimes that run an application file; the runtime package is never the app.
bool isInterpreterName(const std::string &name) {
    static const std::set<std::string> names = {
        "java", "javaw", "node", "nodejs", "mono", "dotnet", "perl", "ruby", "php", "lua", "luajit", "love",
        "gjs", "qmlscene", "qml", "appimage-run", "steam-run", "tclsh", "wish", "godot", "godot4", "box64"};
    const auto n = lowerAscii(name);
    return names.count(n) || isWineName(n) || startsWith(n, "python") || startsWith(n, "pypy") ||
           startsWith(n, "electron") || startsWith(n, "java-");
}

// Programs that open something else; they never identify the application.
bool isLauncherToolName(const std::string &name) {
    static const std::set<std::string> names = {
        "flatpak", "snap", "steam", "xdg-open", "gtk-launch", "gtk4-launch", "gio", "kioclient", "kioclient5",
        "kioclient6", "kstart", "kstart5", "kstart6", "exo-open", "kde-open", "kde-open5", "kde-open6",
        "lutris", "heroic", "bottles", "bottles-cli", "pkexec", "sudo", "kdesu", "kdesudo", "systemd-run",
        "firefoxpwa", "nice", "ionice", "taskset", "bwrap", "distrobox", "toolbox", "konsole", "xterm",
        "gnome-terminal", "kitty", "alacritty"};
    return names.count(lowerAscii(name)) != 0;
}

struct LaunchInfo {
    std::vector<std::string> command; // starts at the real program
    Env env;
    std::optional<fs::path> program;  // resolved through PATH, not canonical
    std::optional<fs::path> payload;  // file run by an interpreter
    bool sharedProgram = false;       // interpreter or launcher tool
    bool webApp = false;
};

std::optional<fs::path> resolveCommandPath(const std::string &command,
                                           const std::vector<std::string> &lines,
                                           bool searchPath) {
    fs::path path(command);
    if (path.is_absolute()) {
        return path.lexically_normal();
    }
    std::optional<fs::path> workingDir;
    if (auto value = getKey(lines, "Desktop Entry", "Path"); value && fs::path(trim(*value)).is_absolute()) {
        workingDir = fs::path(trim(*value));
    }
    if (command.find('/') != std::string::npos || !searchPath) {
        if (workingDir) {
            return (*workingDir / path).lexically_normal();
        }
        return std::nullopt;
    }
    if (auto resolved = findExecutable(command)) {
        return fs::path(*resolved).lexically_normal();
    }
    return std::nullopt;
}

std::optional<std::string> interpreterPayloadArg(const std::vector<std::string> &command) {
    for (std::size_t i = 1; i < command.size(); ++i) {
        const auto &arg = command[i];
        if (arg.empty() || arg[0] == '%') {
            continue;
        }
        if (arg == "-jar") {
            return i + 1 < command.size() ? std::optional<std::string>(command[i + 1]) : std::nullopt;
        }
        if (arg == "-cp" || arg == "-classpath" || arg == "--class-path") {
            ++i;
            continue;
        }
        if (arg == "-m" || arg == "-c" || arg == "-e") {
            return std::nullopt;
        }
        if (arg[0] == '-') {
            continue;
        }
        return arg;
    }
    return std::nullopt;
}

std::optional<LaunchInfo> launchInfo(const std::vector<std::string> &lines) {
    auto exec = getKey(lines, "Desktop Entry", "Exec");
    if (!exec) {
        return std::nullopt;
    }
    LaunchInfo info;
    auto command = commandFromTokens(parseDesktopExec(*exec), info.env);
    if (!command) {
        return std::nullopt;
    }
    info.command = *command;
    for (const auto &token : info.command) {
        if (startsWith(token, "--app-id=") || startsWith(token, "--app=")) {
            info.webApp = true;
        }
    }
    const auto name = basenameOf(info.command.front());
    info.program = resolveCommandPath(info.command.front(), lines, true);
    if (isInterpreterName(name)) {
        info.sharedProgram = true;
        if (auto arg = interpreterPayloadArg(info.command)) {
            std::error_code ec;
            if (auto path = resolveCommandPath(*arg, lines, false); path && fs::is_regular_file(*path, ec)) {
                info.payload = path;
            }
        }
    } else if (isLauncherToolName(name) || info.webApp) {
        info.sharedProgram = true;
        info.webApp = info.webApp || lowerAscii(name) == "firefoxpwa";
    }
    return info;
}

// The file that is the application itself, when there is one.
std::optional<fs::path> applicationFile(const LaunchInfo &info) {
    if (info.payload) {
        return info.payload;
    }
    if (!info.sharedProgram) {
        return info.program;
    }
    return std::nullopt;
}

std::optional<fs::path> applicationFileForDesktop(const std::vector<std::string> &lines) {
    auto info = launchInfo(lines);
    return info ? applicationFile(*info) : std::nullopt;
}

// Kept for callers that only need the resolved program path.
std::optional<fs::path> desktopExecPath(const std::vector<std::string> &lines) {
    auto info = launchInfo(lines);
    return info ? info->program : std::nullopt;
}

std::optional<fs::path> inferAppRoot(const fs::path &file, const fs::path &ownerHome, const fs::path &ownerDataHome) {
    const fs::path normalized = canonicalNormal(file);
    std::error_code ec;
    if (!normalized.is_absolute() || !fs::is_regular_file(normalized, ec)) {
        return std::nullopt;
    }
    auto container = containerFor(normalized, ownerHome, ownerDataHome);
    if (!container) {
        return std::nullopt;
    }
    std::vector<fs::path> parts;
    for (const auto &part : normalized.lexically_relative(*container)) {
        parts.push_back(part);
    }
    if (parts.size() < 2) {
        return std::nullopt;
    }
    // A bin/libexec layout identifies the app directory directly.
    fs::path root = *container;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        const auto name = lowerAscii(parts[i].string());
        if (i > 0 && (name == "bin" || name == "sbin" || name == "libexec")) {
            return root;
        }
        root /= parts[i];
    }
    // Inside a multi-app directory, the app is the child of the deepest one.
    std::optional<std::size_t> named;
    for (std::size_t i = 0; i + 2 < parts.size(); ++i) {
        if (isNamedContainer(parts[i].string())) {
            named = i;
        }
    }
    if (!named) {
        if (isNamedContainer(parts.front().string())) {
            return std::nullopt;
        }
        return *container / parts.front();
    }
    root = *container;
    for (std::size_t i = 0; i <= *named + 1; ++i) {
        root /= parts[i];
    }
    return root;
}

// ---------------------------------------------------------------------------
// Target collection
// ---------------------------------------------------------------------------

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

struct PlanContext {
    std::vector<std::string> lines;
    fs::path desktopPath;
    fs::path sourcePath;
    std::string displayName;
    std::optional<LaunchInfo> launch;
    fs::path home;
    fs::path dataHome;
};

std::vector<std::string> appIdentities(const PlanContext &ctx,
                                       const std::optional<fs::path> &application,
                                       const std::optional<fs::path> &installRoot) {
    std::vector<std::string> identities;
    auto add = [&identities](const std::string &value) {
        for (const auto &candidate : {value, stripVersion(value)}) {
            std::string id = identityToken(candidate);
            if (!id.empty() && std::find(identities.begin(), identities.end(), id) == identities.end()) {
                identities.push_back(id);
            }
        }
    };
    for (const auto &desktop : {ctx.desktopPath, ctx.sourcePath}) {
        const auto stem = desktop.stem().string();
        add(stem);
        // Reverse-DNS ids such as org.example.App also match "App".
        if (std::count(stem.begin(), stem.end(), '.') >= 2) {
            add(stem.substr(stem.rfind('.') + 1));
        }
    }
    add(ctx.displayName);
    if (auto name = getKey(ctx.lines, "Desktop Entry", "Name")) {
        add(trim(*name));
    }
    if (auto wmClass = getKey(ctx.lines, "Desktop Entry", "StartupWMClass")) {
        add(trim(*wmClass));
    }
    if (ctx.launch && ctx.launch->program && !ctx.launch->sharedProgram) {
        add(ctx.launch->program->stem().string());
    }
    if (application) {
        add(application->stem().string());
        add(canonicalIfPossible(*application).stem().string());
    }
    if (installRoot) {
        add(installRoot->filename().string());
    }
    return identities;
}

std::vector<std::string> dataIdentities(const std::vector<std::string> &identities) {
    static const std::set<std::string> generic = {
        "start", "run", "launch", "launcher", "apprun", "main", "application", "wrapper", "exec", "java",
        "python", "python3", "electron", "node", "nodejs", "mono", "wine", "wine64", "default", "appimage",
        "program", "setup", "client", "desktop", "linux", "chrome", "chromium", "firefox", "steam", "user",
        "data", "config", "cache", "share", "local", "app", "bin", "lib", "cli", "gui", "tmp", "log", "etc",
        "usr", "opt", "dev", "git", "ssh", "gpg", "pki", "var", "nix", "npm", "pip", "web", "kde", "qt5", "qt6",
        "gtk", "new", "old", "out", "src", "doc", "run"};
    std::vector<std::string> result;
    for (const auto &id : identities) {
        if (id.size() >= 3 && !generic.count(id)) {
            result.push_back(id);
        }
    }
    return result;
}

void addDesktopTargets(std::vector<RemovalTarget> &targets, const PlanContext &ctx) {
    for (const auto &path : {ctx.desktopPath, ctx.sourcePath}) {
        if (fs::exists(path) && isSafeDesktopPath(path, ctx.home, ctx.dataHome, true)) {
            addRemovalTarget(targets, "desktop", path, "启动器");
        }
    }
}

void addCommandLinkTargets(std::vector<RemovalTarget> &targets,
                           const fs::path &application,
                           bool directory,
                           const fs::path &ownerHome) {
    for (const auto &root : commandLinkRoots(ownerHome)) {
        std::error_code ec;
        for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             it != end && !ec; it.increment(ec)) {
            const auto path = it->path();
            if (pointsToApplication(path, application, directory) && isSafeCommandLink(path, ownerHome)) {
                addRemovalTarget(targets, "command-link", path, "命令链接");
            }
        }
    }
}

void addRelatedDesktopTargets(std::vector<RemovalTarget> &targets,
                              const fs::path &application,
                              bool directory,
                              const fs::path &ownerHome,
                              const fs::path &ownerDataHome) {
    // Match by destination, never by a similar display name.
    std::vector<fs::path> visited;
    for (const auto &root : {ownerDataHome / "applications", ownerHome / ".local/share/applications",
                             fs::path("/usr/share/applications"), fs::path("/usr/local/share/applications"),
                             ownerHome / ".config/autostart", xdgUserDirMap(ownerHome)["DESKTOP"]}) {
        if (std::find(visited.begin(), visited.end(), root) != visited.end()) {
            continue;
        }
        visited.push_back(root);
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            const auto path = it->path();
            if (!extensionEquals(path, ".desktop") || !isFileOrSymlink(path)) {
                continue;
            }
            try {
                auto file = applicationFileForDesktop(readLines(path));
                if (((directory && pointsToApplication(path, application, true)) ||
                     (file && pointsToApplication(*file, application, directory))) &&
                    isSafeDesktopPath(path, ownerHome, ownerDataHome, true)) {
                    addRemovalTarget(targets, "desktop", path, "关联启动器");
                }
            } catch (const std::exception &) {
                // An unreadable unrelated launcher must not prevent removal.
            }
        }
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

    // Only hicolor and pixmaps hold per-application icons; other themes are
    // icon packs whose files merely share the application's icon name.
    std::vector<fs::path> roots = {
        ownerDataHome / "icons/hicolor",
        ownerDataHome / "pixmaps",
        ownerHome / ".local" / "share" / "icons/hicolor",
        ownerHome / ".local" / "share" / "pixmaps",
        "/usr/share/icons/hicolor",
        "/usr/share/pixmaps",
        "/usr/local/share/icons/hicolor",
        "/usr/local/share/pixmaps"};
    // Icon names may carry an extension (Icon=foo.png).
    std::string wanted = identityToken(hasIconExtension(iconPath) ? iconPath.stem().string() : iconName);

    std::vector<std::string> seenRoots;
    for (const auto &root : roots) {
        fs::path normalizedRoot = root.lexically_normal();
        std::string rootKey = normalizedRoot.string();
        std::error_code ec;
        if (std::find(seenRoots.begin(), seenRoots.end(), rootKey) != seenRoots.end() ||
            !fs::exists(normalizedRoot, ec)) {
            continue;
        }
        seenRoots.push_back(rootKey);

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
                    const PlanContext &ctx,
                    const std::optional<fs::path> &coveredRoot,
                    const std::vector<std::string> &identities) {
    auto icon = getKey(ctx.lines, "Desktop Entry", "Icon");
    if (!icon || trim(*icon).empty()) {
        return;
    }

    std::string value = trim(*icon);
    fs::path iconPath(value);
    if (!iconPath.is_absolute()) {
        addNamedIconTargets(targets, value, ctx.home, ctx.dataHome, identities);
        return;
    }

    iconPath = iconPath.lexically_normal();
    if (coveredRoot && lexicallyUnderOrEqual(canonicalIfPossible(iconPath), *coveredRoot)) {
        return;
    }
    if (!fs::exists(iconPath) || !isSafeIconPath(iconPath, ctx.home, ctx.dataHome) ||
        !iconLooksAppSpecific(iconPath, identities)) {
        return;
    }

    addRemovalTarget(targets, "icon", iconPath, "图标");
}

// User configuration, cache and data whose name exactly matches the app.
void addDataTargets(std::vector<RemovalTarget> &targets,
                    const std::vector<std::string> &identities,
                    const fs::path &ownerHome,
                    const fs::path &ownerDataHome) {
    const auto wanted = dataIdentities(identities);
    if (wanted.empty()) {
        return;
    }
    auto roots = userDataRoots(ownerHome, ownerDataHome);
    for (const auto &root : userDataRoots(ownerHome, ownerDataHome)) {
        std::error_code ec;
        for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             it != end && !ec; it.increment(ec)) {
            if (isVendorDataName(it->path().filename().string())) {
                roots.push_back(canonicalNormal(it->path()));
            }
        }
    }
    roots.push_back(canonicalNormal(ownerHome));
    for (const auto &root : roots) {
        std::error_code ec;
        for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             it != end && !ec; it.increment(ec)) {
            const auto path = it->path();
            const auto name = path.filename().string();
            std::vector<std::string> names = {name, stripVersion(name)};
            std::error_code fileEc;
            if (fs::is_regular_file(fs::symlink_status(path, fileEc))) {
                auto stem = path.stem().string();
                names.push_back(stem);
                if (endsWith(stem, "rc")) {
                    names.push_back(stem.substr(0, stem.size() - 2));
                }
            }
            bool match = std::any_of(names.begin(), names.end(), [&wanted](const std::string &candidate) {
                auto id = identityToken(candidate);
                // "CLion2026.3" -> "clion"
                auto unversioned = id.substr(0, id.find_last_not_of("0123456789") + 1);
                return std::find(wanted.begin(), wanted.end(), id) != wanted.end() ||
                       (unversioned.size() >= 3 &&
                        std::find(wanted.begin(), wanted.end(), unversioned) != wanted.end());
            });
            if (match && isSafeDataPath(path, ownerHome, ownerDataHome)) {
                addRemovalTarget(targets, "data-dir", path, "用户数据");
            }
        }
    }
}

bool targetNeedsRoot(const RemovalTarget &target, const fs::path &ownerHome, const fs::path &ownerDataHome) {
    fs::path path(target.path);
    for (const auto &root : {ownerHome, ownerDataHome, canonicalNormal(ownerHome), canonicalNormal(ownerDataHome)}) {
        if (lexicallyUnderOrEqual(path, root)) {
            return false;
        }
    }
    return true;
}

std::optional<std::vector<std::string>> removalCommandForTargets(const std::vector<RemovalTarget> &targets) {
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

void dropUnassociatedCommandLinks(std::vector<RemovalTarget> &targets) {
    std::vector<RemovalTarget> kept;
    for (const auto &target : targets) {
        if (target.kind == "command-link" &&
            std::none_of(targets.begin(), targets.end(), [&target](const RemovalTarget &application) {
                return isApplicationKind(application.kind) &&
                       pointsToApplication(target.path, application.path, isDirectoryKind(application.kind));
            })) {
            continue;
        }
        kept.push_back(target);
    }
    targets = std::move(kept);
}

std::string manualRemovalNote() {
    return "已同时匹配名称相同的配置、缓存和数据目录；不需要删除的项目请取消勾选。";
}

// Collects every removable piece of a manually installed application.
std::optional<RemovalPlan> manualPlan(const PlanContext &ctx,
                                      const std::string &manager,
                                      const std::optional<fs::path> &application,
                                      const std::string &kind,
                                      const std::string &label,
                                      const std::string &note) {
    const bool directory = isDirectoryKind(kind);
    std::vector<RemovalTarget> targets;
    if (application) {
        addRemovalTarget(targets, kind, *application, label);
    }
    addDesktopTargets(targets, ctx);
    if (application) {
        addRelatedDesktopTargets(targets, *application, directory, ctx.home, ctx.dataHome);
        addCommandLinkTargets(targets, *application, directory, ctx.home);
    }
    std::optional<fs::path> covered = directory ? application : std::nullopt;
    auto file = ctx.launch ? applicationFile(*ctx.launch) : std::nullopt;
    auto identities = appIdentities(ctx, file, covered);
    addIconTargets(targets, ctx, covered, identities);
    addDataTargets(targets, identities, ctx.home, ctx.dataHome);

    if (targets.empty()) {
        return std::nullopt;
    }
    auto command = removalCommandForTargets(targets);
    if (!command) {
        return std::nullopt;
    }
    std::string packageName = application ? application->string() : ctx.desktopPath.string();
    return RemovalPlan{ctx.displayName, manager, packageName, *command, ctx.sourcePath.string(),
                       std::move(targets), note};
}

// ---------------------------------------------------------------------------
// Plans
// ---------------------------------------------------------------------------

std::optional<RemovalPlan> flatpakPlan(const PlanContext &ctx, const std::string &appId) {
    std::vector<std::string> command = {"flatpak", "uninstall", "--noninteractive", "--delete-data"};
    const auto source = ctx.sourcePath.string();
    std::error_code ec;
    if (source.find("/.local/share/flatpak/") != std::string::npos ||
        fs::exists(ctx.dataHome / "flatpak/app" / appId, ec)) {
        command.emplace_back("--user");
    } else if (source.find("/var/lib/flatpak/") != std::string::npos ||
               fs::exists(fs::path("/var/lib/flatpak/app") / appId, ec)) {
        command.emplace_back("--system");
    }
    command.push_back(appId);
    return RemovalPlan{ctx.displayName, "flatpak", appId, command, source, {}, {}};
}

std::optional<RemovalPlan> planForFlatpak(const PlanContext &ctx) {
    if (auto flatpak = getKey(ctx.lines, "Desktop Entry", "X-Flatpak"); flatpak && !trim(*flatpak).empty()) {
        return flatpakPlan(ctx, trim(*flatpak));
    }
    const auto source = ctx.sourcePath.string();
    if (source.find("/flatpak/exports/share/applications/") != std::string::npos && commandExists("flatpak")) {
        return flatpakPlan(ctx, ctx.sourcePath.stem().string());
    }
    if (ctx.launch && basenameOf(ctx.launch->command.front()) == "flatpak") {
        const auto &command = ctx.launch->command;
        auto run = std::find(command.begin(), command.end(), "run");
        for (auto it = run == command.end() ? run : run + 1; it != command.end(); ++it) {
            if (it->empty() || (*it)[0] == '-' || (*it)[0] == '@' || (*it)[0] == '%') {
                continue;
            }
            if (it->find('.') != std::string::npos) {
                return flatpakPlan(ctx, *it);
            }
            break;
        }
    }
    return std::nullopt;
}

std::optional<RemovalPlan> snapPlan(const PlanContext &ctx, std::string snapName) {
    auto dot = snapName.find('.');
    if (dot != std::string::npos) {
        snapName.resize(dot);
    }
    if (snapName.empty()) {
        return std::nullopt;
    }
    auto command = withPkexec({"snap", "remove", snapName});
    if (command.empty()) {
        return std::nullopt;
    }
    return RemovalPlan{ctx.displayName, "snap", snapName, command, ctx.sourcePath.string(), {}, {}};
}

std::optional<RemovalPlan> planForSnap(const PlanContext &ctx) {
    if (auto snap = getKey(ctx.lines, "Desktop Entry", "X-SnapInstanceName"); snap && !trim(*snap).empty()) {
        if (auto plan = snapPlan(ctx, trim(*snap))) {
            return plan;
        }
    }
    if (ctx.sourcePath.string().find("/snapd/desktop/applications/") != std::string::npos && commandExists("snap")) {
        std::string file = ctx.sourcePath.stem().string();
        auto underscore = file.find('_');
        if (auto plan = snapPlan(ctx, underscore == std::string::npos ? file : file.substr(0, underscore))) {
            return plan;
        }
    }
    if (ctx.launch) {
        const auto &command = ctx.launch->command;
        if (startsWith(command.front(), "/snap/bin/")) {
            return snapPlan(ctx, fs::path(command.front()).filename().string());
        }
        if (basenameOf(command.front()) == "snap" && command.size() >= 3 && command[1] == "run") {
            for (std::size_t i = 2; i < command.size(); ++i) {
                if (!command[i].empty() && command[i][0] != '-') {
                    return snapPlan(ctx, command[i]);
                }
            }
        }
    }
    return std::nullopt;
}

std::optional<RemovalPlan> planForSteam(const PlanContext &ctx) {
    auto exec = getKey(ctx.lines, "Desktop Entry", "Exec");
    if (!exec) {
        return std::nullopt;
    }
    auto tokens = parseDesktopExec(*exec);
    for (std::size_t k = 0; k < tokens.size(); ++k) {
        std::string id;
        for (const char *prefix : {"steam://rungameid/", "steam://run/", "steam://launch/"}) {
            if (startsWith(tokens[k], prefix)) {
                id = tokens[k].substr(std::string(prefix).size());
            }
        }
        auto end = std::find_if(id.begin(), id.end(), [](unsigned char c) { return !std::isdigit(c); });
        id.erase(end, id.end());
        if (id.empty()) {
            continue;
        }
        std::vector<std::string> command;
        for (std::size_t i = 0; i < k; ++i) {
            if (!tokens[i].empty() && tokens[i][0] != '%') {
                command.push_back(tokens[i]);
            }
        }
        if (command.empty()) {
            command.push_back("xdg-open");
        } else if (looksLikeEnvAssignment(command.front())) {
            command.insert(command.begin(), "env");
        }
        command.push_back("steam://uninstall/" + id);
        return RemovalPlan{ctx.displayName, "steam", id, command, ctx.sourcePath.string(), {},
                           "Steam 会弹出自己的卸载确认窗口。"};
    }
    return std::nullopt;
}

std::optional<RemovalPlan> planForNativePackage(const PlanContext &ctx) {
    std::vector<fs::path> candidates = {ctx.sourcePath, ctx.desktopPath, canonicalIfPossible(ctx.desktopPath)};

    // A local copy of a packaged launcher belongs to that package.
    if (isUnderPath(ctx.desktopPath, localApplicationsDir())) {
        for (const auto &root : applicationRoots()) {
            if (canonicalIfPossible(root) != canonicalIfPossible(localApplicationsDir())) {
                candidates.push_back(root / ctx.desktopPath.filename());
            }
        }
    }
    if (auto tryExec = getKey(ctx.lines, "Desktop Entry", "TryExec"); tryExec && !trim(*tryExec).empty()) {
        const auto name = basenameOf(trim(*tryExec));
        if (!isInterpreterName(name) && !isLauncherToolName(name)) {
            if (auto path = resolveCommandPath(trim(*tryExec), ctx.lines, true)) {
                candidates.push_back(*path);
            }
        }
    }
    if (ctx.launch) {
        if (auto file = applicationFile(*ctx.launch)) {
            candidates.push_back(*file);
            candidates.push_back(canonicalIfPossible(*file));
        }
    }

    std::optional<std::pair<std::string, std::string>> owner;
    std::vector<fs::path> checked;
    for (const auto &candidate : candidates) {
        if (std::find(checked.begin(), checked.end(), candidate) != checked.end()) {
            continue;
        }
        checked.push_back(candidate);
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
    return RemovalPlan{ctx.displayName, manager, package, removeCommand, ctx.sourcePath.string(), {}, {}};
}

std::optional<RemovalPlan> planForWine(const PlanContext &ctx) {
    if (!ctx.launch || ctx.launch->payload || !isWineName(basenameOf(ctx.launch->command.front()))) {
        return std::nullopt;
    }
    std::vector<std::string> command = {"env"};
    std::string prefix = "~/.wine";
    if (auto it = ctx.launch->env.find("WINEPREFIX"); it != ctx.launch->env.end()) {
        prefix = it->second;
        command.push_back("WINEPREFIX=" + prefix);
    }
    command.push_back(ctx.launch->command.front());
    command.emplace_back("uninstaller");
    return RemovalPlan{ctx.displayName, "wine", prefix, command, ctx.sourcePath.string(), {},
                       "将打开 Wine 卸载程序，请在其中选择要卸载的程序。"};
}

std::optional<RemovalPlan> planForPipx(const PlanContext &ctx, const fs::path &file) {
    const auto canonical = canonicalNormal(file);
    std::vector<fs::path> venvRoots = {ctx.dataHome / "pipx/venvs", ctx.home / ".local/share/pipx/venvs",
                                       ctx.home / ".local/pipx/venvs"};
    if (const char *pipxHome = std::getenv("PIPX_HOME"); pipxHome && *pipxHome) {
        venvRoots.push_back(fs::path(pipxHome) / "venvs");
    }
    for (const auto &root : canonicalAll(venvRoots)) {
        if (lexicallyInside(canonical, root) && commandExists("pipx")) {
            std::string name = canonical.lexically_relative(root).begin()->string();
            return RemovalPlan{ctx.displayName, "pipx", name, {"pipx", "uninstall", name},
                               ctx.sourcePath.string(), {}, {}};
        }
    }
    return std::nullopt;
}

std::optional<RemovalPlan> planForAppFile(const PlanContext &ctx, const fs::path &file) {
    const auto canonical = canonicalNormal(file);

    if (isSafeAppImagePath(canonical, ctx.home, ctx.dataHome)) {
        return manualPlan(ctx, "AppImage", canonical, "appimage", "AppImage", manualRemovalNote());
    }

    if (auto root = inferAppRoot(canonical, ctx.home, ctx.dataHome);
        root && !isPathEntry(*root) && isSafeAppDir(*root, ctx.home, ctx.dataHome)) {
        return manualPlan(ctx, "manual", *root, "app-dir", "应用目录（含全部内容）", manualRemovalNote());
    }

    if (isSafeLooseFile(canonical, ctx.home, ctx.dataHome, !(ctx.launch && ctx.launch->payload))) {
        return manualPlan(ctx, "manual", canonical, "file", "程序文件", manualRemovalNote());
    }
    return std::nullopt;
}

std::optional<RemovalPlan> planLauncherOnly(const PlanContext &ctx, const std::string &manager, const std::string &note) {
    return manualPlan(ctx, manager, std::nullopt, "", "", note);
}

std::optional<RemovalPlan> removalPlanForDesktop(const fs::path &desktopPath) {
    PlanContext ctx;
    ctx.lines = readLines(desktopPath);
    ctx.desktopPath = desktopPath;
    ctx.displayName = localizedName(ctx.lines);
    ctx.home = homeDir();
    ctx.dataHome = dataHome();
    ctx.sourcePath = desktopPath;
    if (auto source = getKey(ctx.lines, "Desktop Entry", kSourceKey); source && !trim(*source).empty()) {
        ctx.sourcePath = trim(*source);
    }
    ctx.launch = launchInfo(ctx.lines);

    if (auto plan = planForFlatpak(ctx)) {
        return plan;
    }
    if (auto plan = planForSnap(ctx)) {
        return plan;
    }
    if (auto plan = planForSteam(ctx)) {
        return plan;
    }
    if (auto plan = planForNativePackage(ctx)) {
        return plan;
    }
    if (ctx.launch && ctx.launch->webApp) {
        return planLauncherOnly(ctx, "网页应用", "这是浏览器创建的网页应用，将删除它的启动器和图标。");
    }
    if (auto plan = planForWine(ctx)) {
        return plan;
    }
    if (auto file = ctx.launch ? applicationFile(*ctx.launch) : std::nullopt) {
        if (auto plan = planForPipx(ctx, *file)) {
            return plan;
        }
        if (auto plan = planForAppFile(ctx, *file)) {
            return plan;
        }
    }

    std::string reason;
    if (!ctx.launch) {
        reason = "无法解析 Exec= 中的程序";
    } else if (auto file = applicationFile(*ctx.launch); !file || !fs::exists(*file)) {
        reason = "启动器指向的程序已不存在";
    } else {
        reason = "程序位于系统目录或无法确认归属（" + file->string() + "），不会删除";
    }
    return planLauncherOnly(ctx, "启动器残留",
                            reason + "；将删除启动器以及名称匹配的图标和用户数据，不需要的项目请取消勾选。");
}

// ---------------------------------------------------------------------------
// Privileged removal
// ---------------------------------------------------------------------------

bool isSafeRemovalTarget(const std::string &kind,
                         const fs::path &path,
                         const fs::path &ownerHome,
                         const fs::path &ownerDataHome,
                         std::string &error) {
    bool safe = false;
    if (kind == "appimage") {
        safe = isSafeAppImagePath(path, ownerHome, ownerDataHome);
    } else if (kind == "desktop") {
        safe = isSafeDesktopPath(path, ownerHome, ownerDataHome, true);
    } else if (kind == "icon") {
        safe = isSafeIconPath(path, ownerHome, ownerDataHome);
    } else if (kind == "app-dir" || kind == "opt-dir") {
        safe = isSafeAppDir(path, ownerHome, ownerDataHome);
    } else if (kind == "file") {
        safe = isSafeLooseFile(path, ownerHome, ownerDataHome, false);
    } else if (kind == "data-dir") {
        safe = isSafeDataPath(path, ownerHome, ownerDataHome);
    } else if (kind == "command-link") {
        safe = isSafeCommandLink(path, ownerHome);
    } else {
        error = "unknown target kind: " + kind;
        return false;
    }
    if (!safe) {
        error = "refusing unsafe " + kind + " path: " + path.string();
    }
    return safe;
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

    // Validate every link while all destinations still exist. Removing one link
    // may break another link in the same chain.
    for (const auto &target : targets) {
        if (target.kind != "command-link") {
            continue;
        }
        bool associated = std::any_of(targets.begin(), targets.end(), [&target](const Target &application) {
            return isApplicationKind(application.kind) &&
                   pointsToApplication(target.path, application.path, isDirectoryKind(application.kind));
        });
        if (!associated) {
            std::cerr << "refusing unrelated command link: " << target.path.string() << '\n';
            return 65;
        }
    }

    for (const auto &target : targets) {
        std::error_code ec;
        auto status = fs::symlink_status(target.path, ec);
        if (ec || !fs::exists(status)) {
            continue;
        }

        if (isDirectoryKind(target.kind)) {
            if (!fs::is_directory(status) || fs::is_symlink(status)) {
                std::cerr << "refusing non-directory app target: " << target.path.string() << '\n';
                return 65;
            }
            fs::remove_all(target.path, ec);
        } else if (target.kind == "data-dir") {
            // remove_all removes a symlink itself, never its destination.
            fs::remove_all(target.path, ec);
        } else {
            if (fs::is_directory(status)) {
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
}

} // namespace kde_uninstall
