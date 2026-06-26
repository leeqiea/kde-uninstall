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

<<<<<<< HEAD
=======
struct RemovalTarget {
    std::string kind;
    std::string path;
    std::string label;
};

>>>>>>> 74cff8e (Initial commit)
struct RemovalPlan {
    std::string displayName;
    std::string manager;
    std::string packageName;
    std::vector<std::string> command;
    std::string sourcePath;
<<<<<<< HEAD
=======
    std::vector<RemovalTarget> removalTargets;
    std::string note;
>>>>>>> 74cff8e (Initial commit)
};

} // namespace kde_uninstall
