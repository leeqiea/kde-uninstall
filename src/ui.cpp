#include "kde_uninstall/ui.h"

#include "kde_uninstall/command.h"
#include "kde_uninstall/paths.h"

#include <iostream>
#include <unistd.h>

namespace kde_uninstall {

void notify(const std::string &title, const std::string &message) {
    if (commandExists("notify-send")) {
        runNoCapture({"notify-send", title, message});
    }
}

void showError(const std::string &message) {
    if (commandExists("kdialog")) {
        runNoCapture({"kdialog", "--title", "KDE Uninstall", "--error", message});
    } else {
        notify("KDE Uninstall", message);
    }
    std::cerr << message << '\n';
}

bool confirmRemoval(const RemovalPlan &plan) {
    std::string commandText;
    for (const auto &arg : plan.command) {
        if (!commandText.empty()) {
            commandText += ' ';
        }
        commandText += arg;
    }

    std::string message = "确定卸载 “" + plan.displayName + "” 吗？\n\n来源: " + plan.manager +
                          "\n目标: " + plan.packageName + "\n将执行: " + commandText;

    if (commandExists("kdialog")) {
        return runNoCapture({"kdialog", "--title", "卸载应用", "--warningyesno", message}) == 0;
    }
    if (commandExists("zenity")) {
        return runNoCapture({"zenity", "--question", "--title=卸载应用", "--text=" + message}) == 0;
    }
    if (::isatty(STDIN_FILENO)) {
        std::cout << message << "\n输入 yes 继续: ";
        std::string answer;
        std::getline(std::cin, answer);
        return answer == "yes" || answer == "YES";
    }
    showError("找不到 kdialog/zenity，无法在图形界面确认卸载。请先安装 kdialog，或从终端运行。 ");
    return false;
}

void refreshKdeCache() {
    if (commandExists("kbuildsycoca6")) {
        runCapture({"kbuildsycoca6", "--noincremental"});
    } else if (commandExists("kbuildsycoca5")) {
        runCapture({"kbuildsycoca5", "--noincremental"});
    }
    if (commandExists("update-desktop-database")) {
        runCapture({"update-desktop-database", localApplicationsDir().string()});
    }
}

} // namespace kde_uninstall
