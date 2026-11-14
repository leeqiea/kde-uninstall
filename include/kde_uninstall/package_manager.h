#pragma once

#include "kde_uninstall/types.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kde_uninstall {

std::optional<RemovalPlan> removalPlanForDesktop(const std::filesystem::path &desktopPath);
int removePlannedPaths(int argc, char **argv);

// Drops command links whose application target is no longer selected.
void dropUnassociatedCommandLinks(std::vector<RemovalTarget> &targets);
std::optional<std::vector<std::string>> removalCommandForTargets(const std::vector<RemovalTarget> &targets);

} // namespace kde_uninstall
