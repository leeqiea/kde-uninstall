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

struct RemovalTarget {
    std::string kind;
    std::string path;
    std::string label;
};

struct RemovalPlan {
    std::string displayName;
    std::string manager;
    std::string packageName;
    std::vector<std::string> command;
    std::string sourcePath;
    std::vector<RemovalTarget> removalTargets;
    std::string note;
};

} // namespace kde_uninstall
