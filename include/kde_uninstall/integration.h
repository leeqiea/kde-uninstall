#pragma once

#include "kde_uninstall/types.h"

#include <filesystem>

namespace kde_uninstall {

int uninstallDesktop(const std::filesystem::path &desktopPath);
PatchStats installIntegration();
PatchStats restoreIntegration();

} // namespace kde_uninstall
