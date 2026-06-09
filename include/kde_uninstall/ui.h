#pragma once

#include "kde_uninstall/types.h"

#include <string>

namespace kde_uninstall {

void notify(const std::string &title, const std::string &message);
void showError(const std::string &message);
bool confirmRemoval(const RemovalPlan &plan);
void refreshKdeCache();

} // namespace kde_uninstall
