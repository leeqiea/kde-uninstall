#include "kde_uninstall/paths.h"

#include "kde_uninstall/constants.h"
#include "kde_uninstall/string_utils.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <unistd.h>

namespace fs = std::filesystem;

namespace kde_uninstall {

fs::path homeDir() {
    const char *home = std::getenv("HOME");
    if (!home || !*home) {
        throw std::runtime_error("HOME is not set");
    }
    return fs::path(home);
}

fs::path dataHome() {
    const char *xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg && fs::path(xdg).is_absolute()) {
        return fs::path(xdg);
    }
    return homeDir() / ".local" / "share";
}

fs::path localApplicationsDir() {
    return dataHome() / "applications";
}

fs::path kickerActionsDir() {
    return dataHome() / "plasma" / "kickeractions";
}

fs::path localBinDir() {
    return homeDir() / ".local" / "bin";
}
fs::path canonicalIfPossible(const fs::path &path) {
    std::error_code ec;
    auto canonical = fs::weakly_canonical(path, ec);
    if (!ec) {
        return canonical;
    }
    auto absolute = fs::absolute(path, ec);
    return ec ? path : absolute;
}

bool isUnderPath(const fs::path &path, const fs::path &root) {
    fs::path p = canonicalIfPossible(path);
    fs::path r = canonicalIfPossible(root);
    auto pit = p.begin();
    auto rit = r.begin();
    for (; rit != r.end(); ++rit, ++pit) {
        if (pit == p.end() || *pit != *rit) {
            return false;
        }
    }
    return true;
}

std::string desktopIdFor(const fs::path &root, const fs::path &desktopPath) {
    std::error_code ec;
    auto relative = fs::relative(desktopPath, root, ec);
    std::string id = ec ? desktopPath.filename().string() : relative.generic_string();
    std::replace(id.begin(), id.end(), '/', '-');
    return id;
}

void addRoot(std::vector<fs::path> &roots, const fs::path &root) {
    if (root.empty()) {
        return;
    }
    fs::path normalized = canonicalIfPossible(root);
    std::string key = normalized.string();
    for (const auto &existing : roots) {
        if (canonicalIfPossible(existing).string() == key) {
            return;
        }
    }
    roots.push_back(root);
}

std::vector<fs::path> applicationRoots() {
    std::vector<fs::path> roots;
    addRoot(roots, localApplicationsDir());

    const char *xdgDataDirs = std::getenv("XDG_DATA_DIRS");
    std::string dirs = (xdgDataDirs && *xdgDataDirs) ? xdgDataDirs : "/usr/local/share:/usr/share";
    for (const auto &dir : split(dirs, ':')) {
        if (!dir.empty()) {
            addRoot(roots, fs::path(dir) / "applications");
        }
    }

    addRoot(roots, dataHome() / "flatpak" / "exports" / "share" / "applications");
    addRoot(roots, fs::path("/var/lib/flatpak/exports/share/applications"));
    addRoot(roots, fs::path("/var/lib/snapd/desktop/applications"));
    return roots;
}

std::optional<fs::path> selfPath() {
    std::array<char, 4096> buffer{};
    ssize_t length = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length <= 0) {
        return std::nullopt;
    }
    buffer[static_cast<std::size_t>(length)] = '\0';
    return fs::path(buffer.data());
}

fs::path installSelf() {
    auto self = selfPath();
    if (!self) {
        throw std::runtime_error("cannot resolve /proc/self/exe");
    }

    fs::path target = localBinDir() / kProgramName;
    fs::create_directories(target.parent_path());

    if (canonicalIfPossible(*self) != canonicalIfPossible(target)) {
        fs::copy_file(*self, target, fs::copy_options::overwrite_existing);
    }
    fs::permissions(target,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                        fs::perms::group_read | fs::perms::group_exec |
                        fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace);
    return target;
}

} // namespace kde_uninstall
