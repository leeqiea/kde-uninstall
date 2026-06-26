#include "kde_uninstall/app.h"

#include "kde_uninstall/integration.h"
<<<<<<< HEAD
=======
#include "kde_uninstall/package_manager.h"
>>>>>>> 74cff8e (Initial commit)
#include "kde_uninstall/ui.h"

#include <exception>
#include <iostream>
#include <string>

namespace kde_uninstall {
namespace {

void printStats(const PatchStats &stats, const std::string &mode) {
    std::cout << mode << " complete\n";
    std::cout << "patched: " << stats.patched << '\n';
    std::cout << "created: " << stats.created << '\n';
    std::cout << "skipped: " << stats.skipped << '\n';
    std::cout << "removed stale: " << stats.removedStale << '\n';
    if (!stats.errors.empty()) {
        std::cout << "errors:\n";
        for (const auto &error : stats.errors) {
            std::cout << "  " << error << '\n';
        }
    }
}

void printHelp() {
    std::cout << "KDE application launcher uninstall action\n\n"
              << "Usage:\n"
              << "  kde-uninstall                 Install or refresh launcher uninstall action\n"
              << "  kde-uninstall --install       Install or refresh launcher uninstall action\n"
              << "  kde-uninstall --restore       Remove launcher action added by this tool\n"
              << "  kde-uninstall --uninstall-desktop <file.desktop>\n"
<<<<<<< HEAD
              << "                                Internal entry used by the launcher menu\n\n"
=======
              << "                                Internal entry used by the launcher menu\n"
              << "  kde-uninstall --remove-paths <home> <data-home> <kind> <path>...\n"
              << "                                Internal guarded removal entry\n\n"
>>>>>>> 74cff8e (Initial commit)
              << "The installer writes only to ~/.local/bin and ~/.local/share/plasma/kickeractions.\n";
}

} // namespace

int runApp(int argc, char **argv) {
    try {
        if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--install") ||
            (argc == 2 && std::string(argv[1]) == "--refresh")) {
            printStats(installIntegration(), "install");
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--restore") {
            printStats(restoreIntegration(), "restore");
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--uninstall-desktop") {
            return uninstallDesktop(argv[2]);
        }
<<<<<<< HEAD
=======
        if (argc >= 6 && std::string(argv[1]) == "--remove-paths") {
            return removePlannedPaths(argc, argv);
        }
>>>>>>> 74cff8e (Initial commit)
        if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
            printHelp();
            return 0;
        }
        printHelp();
        return 64;
    } catch (const std::exception &error) {
        showError(error.what());
        return 1;
    }
}

} // namespace kde_uninstall
