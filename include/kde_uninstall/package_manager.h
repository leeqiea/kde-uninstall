#pragma once

#include "kde_uninstall/types.h"

#include <filesystem>
#include <optional>

namespace kde_uninstall {

std::optional<RemovalPlan> removalPlanForDesktop(const std::filesystem::path &desktopPath);

} // namespace kde_uninstall
