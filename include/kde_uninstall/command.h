#pragma once

#include "kde_uninstall/types.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace kde_uninstall {

bool commandExists(const std::string &command);
std::optional<std::string> findExecutable(const std::string &command);
CommandResult runCapture(const std::vector<std::string> &args, std::size_t maxOutput = 65536);
int runNoCapture(const std::vector<std::string> &args);

} // namespace kde_uninstall
