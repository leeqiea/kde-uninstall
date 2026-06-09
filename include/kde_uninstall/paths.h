#pragma once

#include <filesystem>
#include <optional>

namespace kde_uninstall {

std::filesystem::path homeDir();
std::filesystem::path dataHome();
std::filesystem::path localApplicationsDir();
std::filesystem::path kickerActionsDir();
std::filesystem::path localBinDir();
std::filesystem::path canonicalIfPossible(const std::filesystem::path &path);
std::optional<std::filesystem::path> selfPath();
std::filesystem::path installSelf();

} // namespace kde_uninstall
