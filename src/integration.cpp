#include "kde_uninstall/integration.h"

#include "kde_uninstall/command.h"
#include "kde_uninstall/constants.h"
#include "kde_uninstall/desktop_file.h"
#include "kde_uninstall/package_manager.h"
#include "kde_uninstall/paths.h"
#include "kde_uninstall/string_utils.h"
#include "kde_uninstall/ui.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace kde_uninstall {

void cleanupAfterSuccessfulRemoval(const fs::path &desktopPath) {
    if (!fs::exists(desktopPath)) {
        return;
    }

    auto lines = readLines(desktopPath);
    if (desktopBool(lines, kGeneratedKey)) {
        std::error_code ec;
        fs::remove(desktopPath, ec);
        return;
    }

    auto unpatched = unpatchDesktopFile(lines);
    if (!sameLines(lines, unpatched)) {
        writeLinesAtomic(desktopPath, unpatched);
    }
}

fs::path kickerActionProviderPath() {
    return kickerActionsDir() / kKickerActionProviderFile;
}

void cleanupLegacyDesktopActions(PatchStats &stats) {
    fs::path localApps = localApplicationsDir();
    if (!fs::exists(localApps)) {
        return;
    }

    std::error_code ec;
    for (fs::recursive_directory_iterator it(localApps, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec) || it->path().extension() != ".desktop") {
            continue;
        }
        try {
            auto lines = readLines(it->path());
            if (!desktopBool(lines, kManagedKey)) {
                continue;
            }
            if (desktopBool(lines, kGeneratedKey)) {
                fs::remove(it->path());
                ++stats.removedStale;
                continue;
            }
            auto unpatched = unpatchDesktopFile(lines);
            if (!sameLines(lines, unpatched)) {
                writeLinesAtomic(it->path(), unpatched);
                ++stats.patched;
            }
        } catch (const std::exception &error) {
            stats.errors.push_back(error.what());
        }
    }
}

void installKickerActionProvider(const fs::path &binaryPath, PatchStats &stats) {
    fs::create_directories(kickerActionsDir());

    fs::path target = kickerActionProviderPath();
    auto provider = kickerActionProviderFile(binaryPath);
    bool existed = fs::exists(target);
    if (!existed || !sameLines(readLines(target), provider)) {
        writeLinesAtomic(target, provider);
        ++stats.patched;
        if (!existed) {
            ++stats.created;
        }
    }
}

void removeKickerActionProvider(PatchStats &stats) {
    fs::path target = kickerActionProviderPath();
    if (!fs::exists(target)) {
        return;
    }

    std::error_code ec;
    fs::remove(target, ec);
    if (ec) {
        stats.errors.push_back("cannot remove " + target.string() + ": " + ec.message());
    } else {
        ++stats.removedStale;
    }
}

int uninstallDesktop(const fs::path &desktopPath) {
    if (!fs::exists(desktopPath)) {
        showError("找不到启动器文件: " + desktopPath.string());
        return 2;
    }

    std::optional<RemovalPlan> plan;
    try {
        plan = removalPlanForDesktop(desktopPath);
    } catch (const std::exception &error) {
        showError(std::string("读取启动器失败: ") + error.what());
        return 2;
    }

    if (!plan) {
        showError("无法识别这个应用的安装来源，不能安全卸载。\n启动器: " + desktopPath.string());
        return 3;
    }
    if (!commandExists(plan->command.front())) {
        showError("找不到卸载命令: " + plan->command.front());
        return 3;
    }
    if (!confirmRemoval(*plan)) {
        return 0;
    }

    notify("KDE Uninstall", "正在卸载 " + plan->displayName);
    auto result = runCapture(plan->command, 131072);
    if (result.exitCode != 0) {
        std::string detail = trim(result.output);
        if (detail.size() > 1600) {
            detail = detail.substr(detail.size() - 1600);
        }
        showError("卸载失败，退出码: " + std::to_string(result.exitCode) +
                  (detail.empty() ? std::string() : "\n\n" + detail));
        return result.exitCode;
    }

    try {
        cleanupAfterSuccessfulRemoval(desktopPath);
    } catch (const std::exception &error) {
        showError(std::string("应用已卸载，但清理菜单项失败: ") + error.what());
        return 1;
    }
    refreshKdeCache();
    notify("KDE Uninstall", "已卸载 " + plan->displayName);
    return 0;
}
bool patchOneDesktop(const fs::path &root,
                     const fs::path &sourceDesktop,
                     const fs::path &binaryPath,
                     PatchStats &stats) {
    fs::path localApps = localApplicationsDir();
    std::string desktopId = desktopIdFor(root, sourceDesktop);
    fs::path targetDesktop = isUnderPath(sourceDesktop, localApps) ? sourceDesktop : localApps / desktopId;
    bool targetIsGenerated = !isUnderPath(sourceDesktop, localApps);

    std::vector<std::string> sourceLines;
    try {
        sourceLines = readLines(sourceDesktop);
    } catch (const std::exception &error) {
        stats.errors.push_back(error.what());
        return false;
    }

    if (generatedCopyPointsToMissingSource(sourceLines)) {
        std::error_code ec;
        fs::remove(sourceDesktop, ec);
        if (!ec) {
            ++stats.removedStale;
        }
        return false;
    }

    fs::path realSource = sourceDesktop;
    if (isUnderPath(sourceDesktop, localApps) && desktopBool(sourceLines, kGeneratedKey)) {
        if (auto source = getKey(sourceLines, "Desktop Entry", kSourceKey); source && fs::exists(trim(*source))) {
            realSource = trim(*source);
            targetIsGenerated = true;
            try {
                sourceLines = readLines(realSource);
            } catch (const std::exception &error) {
                stats.errors.push_back(error.what());
                return false;
            }
        }
    }

    if (!isDesktopApplication(sourceLines)) {
        ++stats.skipped;
        return false;
    }

    if (targetDesktop != sourceDesktop && fs::exists(targetDesktop)) {
        auto targetLines = readLines(targetDesktop);
        if (!desktopBool(targetLines, kGeneratedKey)) {
            ++stats.skipped;
            return false;
        }
    }

    auto patched = patchDesktopFile(sourceLines, targetDesktop, realSource, binaryPath, targetIsGenerated);
    bool existed = fs::exists(targetDesktop);
    if (!existed || !sameLines(readLines(targetDesktop), patched)) {
        if (existed && !desktopBool(readLines(targetDesktop), kGeneratedKey)) {
            fs::path backup = targetDesktop;
            backup += ".kde-uninstall.bak";
            if (!fs::exists(backup)) {
                std::error_code ec;
                fs::copy_file(targetDesktop, backup, fs::copy_options::none, ec);
            }
        }
        writeLinesAtomic(targetDesktop, patched);
        ++stats.patched;
        if (!existed) {
            ++stats.created;
        }
    }
    return true;
}

PatchStats installIntegration() {
    PatchStats stats;
    fs::path binaryPath = installSelf();
    cleanupLegacyDesktopActions(stats);
    installKickerActionProvider(binaryPath, stats);

    refreshKdeCache();
    return stats;
}

PatchStats restoreIntegration() {
    PatchStats stats;
    removeKickerActionProvider(stats);
    cleanupLegacyDesktopActions(stats);
    refreshKdeCache();
    return stats;
}

} // namespace kde_uninstall
