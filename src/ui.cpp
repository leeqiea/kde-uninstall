#include "kde_uninstall/ui.h"

#include "kde_uninstall/command.h"
#include "kde_uninstall/paths.h"
#include "kde_uninstall/string_utils.h"

#include <string>
#include <vector>

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

namespace {

std::string targetText(const RemovalTarget &target) {
    return target.label.empty() ? target.path : target.label + ": " + target.path;
}

// Lets the user untick removal targets. Returns false when cancelled.
bool selectTargets(RemovalPlan &plan, const std::string &header) {
    std::vector<std::string> selected;
    if (commandExists("kdialog")) {
        std::vector<std::string> args = {"kdialog", "--title", "卸载应用", "--separate-output", "--checklist", header};
        for (std::size_t i = 0; i < plan.removalTargets.size(); ++i) {
            args.push_back(std::to_string(i));
            args.push_back(targetText(plan.removalTargets[i]));
            args.emplace_back("on");
        }
        auto result = runCapture(args, 65536, false);
        if (result.exitCode != 0) {
            return false;
        }
        selected = split(result.output, '\n');
    } else if (commandExists("zenity")) {
        std::vector<std::string> args = {"zenity", "--list", "--checklist", "--title=卸载应用", "--text=" + header,
                                         "--column=删除", "--column=ID", "--column=项目", "--hide-column=2",
                                         "--print-column=2", "--separator=\n", "--width=800", "--height=500"};
        for (std::size_t i = 0; i < plan.removalTargets.size(); ++i) {
            args.emplace_back("TRUE");
            args.push_back(std::to_string(i));
            args.push_back(targetText(plan.removalTargets[i]));
        }
        auto result = runCapture(args, 65536, false);
        if (result.exitCode != 0) {
            return false;
        }
        selected = split(result.output, '\n');
    } else if (::isatty(STDIN_FILENO)) {
        std::cout << header << '\n';
        for (const auto &target : plan.removalTargets) {
            std::cout << "- " << targetText(target) << '\n';
        }
        std::cout << "输入 yes 继续: ";
        std::string answer;
        std::getline(std::cin, answer);
        return answer == "yes" || answer == "YES";
    } else {
        showError("找不到 kdialog/zenity，无法在图形界面确认卸载。请先安装 kdialog，或从终端运行。");
        return false;
    }

    std::vector<RemovalTarget> kept;
    for (auto item : selected) {
        item = trim(item);
        if (item.size() >= 2 && item.front() == '"' && item.back() == '"') {
            item = item.substr(1, item.size() - 2);
        }
        if (item.empty() || item.find_first_not_of("0123456789") != std::string::npos) {
            continue;
        }
        auto index = std::stoul(item);
        if (index < plan.removalTargets.size()) {
            kept.push_back(plan.removalTargets[index]);
        }
    }
    if (kept.empty()) {
        return false;
    }
    plan.removalTargets = std::move(kept);
    return true;
}

} // namespace

bool confirmRemoval(RemovalPlan &plan) {
    std::string message = "确定卸载 “" + plan.displayName + "” 吗？\n\n来源: " + plan.manager +
                          "\n目标: " + plan.packageName;

    if (!plan.removalTargets.empty()) {
        message += "\n\n勾选要删除的项目：";
        if (!plan.note.empty()) {
            message += "\n" + plan.note;
        }
        return selectTargets(plan, message);
    }

    std::string commandText;
    for (const auto &arg : plan.command) {
        if (!commandText.empty()) {
            commandText += ' ';
        }
        commandText += arg;
    }
    message += "\n将执行: " + commandText;
    if (!plan.note.empty()) {
        message += "\n\n" + plan.note;
    }

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
