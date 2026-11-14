#pragma once

#include "kde_uninstall/types.h"

#include <string>

namespace kde_uninstall {

void notify(const std::string &title, const std::string &message);
void showError(const std::string &message);
// For plans with removal targets, lets the user deselect items and keeps only
// the selected ones in plan.removalTargets.
bool confirmRemoval(RemovalPlan &plan);
void refreshKdeCache();

} // namespace kde_uninstall
