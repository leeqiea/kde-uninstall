#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kde_uninstall {

std::filesystem::path homeDir();
std::filesystem::path dataHome();
std::filesystem::path localApplicationsDir();
std::filesystem::path kickerActionsDir();
std::filesystem::path localBinDir();
std::filesystem::path canonicalIfPossible(const std::filesystem::path &path);
bool isUnderPath(const std::filesystem::path &path, const std::filesystem::path &root);
std::string desktopIdFor(const std::filesystem::path &root, const std::filesystem::path &desktopPath);
std::vector<std::filesystem::path> applicationRoots();
std::optional<std::filesystem::path> selfPath();
std::filesystem::path installSelf();

} // namespace kde_uninstall
