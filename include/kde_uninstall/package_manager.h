#pragma once

#include "kde_uninstall/types.h"

#include <filesystem>
#include <optional>

namespace kde_uninstall {

std::optional<RemovalPlan> removalPlanForDesktop(const std::filesystem::path &desktopPath);
<<<<<<< HEAD
=======
int removePlannedPaths(int argc, char **argv);
>>>>>>> 74cff8e (Initial commit)

} // namespace kde_uninstall
