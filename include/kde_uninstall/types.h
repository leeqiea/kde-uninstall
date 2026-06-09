#pragma once

#include <string>
#include <vector>

namespace kde_uninstall {

struct CommandResult {
    int exitCode = 127;
    std::string output;
};

struct PatchStats {
    int patched = 0;
    int created = 0;
    int skipped = 0;
    int removedStale = 0;
    std::vector<std::string> errors;
};

struct RemovalPlan {
    std::string displayName;
    std::string manager;
    std::string packageName;
    std::vector<std::string> command;
    std::string sourcePath;
};

} // namespace kde_uninstall
