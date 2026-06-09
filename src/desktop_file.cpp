#include "kde_uninstall/desktop_file.h"

#include "kde_uninstall/constants.h"
#include "kde_uninstall/string_utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace kde_uninstall {

std::optional<std::string> groupName(const std::string &line) {
    std::string t = trim(line);
    if (t.size() >= 3 && t.front() == '[' && t.back() == ']') {
        return t.substr(1, t.size() - 2);
    }
    return std::nullopt;
}

std::optional<std::pair<std::string, std::string>> keyValue(const std::string &line) {
    std::string t = trim(line);
    if (t.empty() || t.front() == '#' || t.front() == '[') {
        return std::nullopt;
    }
    auto pos = line.find('=');
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    return std::make_pair(trim(line.substr(0, pos)), line.substr(pos + 1));
}

std::vector<std::string> readLines(const fs::path &path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot read " + path.string());
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

void writeLinesAtomic(const fs::path &path, const std::vector<std::string> &lines) {
    fs::create_directories(path.parent_path());
    fs::path temp = path;
    temp += ".tmp";

    {
        std::ofstream output(temp, std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot write " + temp.string());
        }
        for (const auto &line : lines) {
            output << line << '\n';
        }
    }

    fs::rename(temp, path);
    fs::permissions(path,
                    fs::perms::owner_read | fs::perms::owner_write |
                        fs::perms::group_read | fs::perms::others_read,
                    fs::perm_options::replace);
}

std::optional<std::string> getKey(const std::vector<std::string> &lines,
                                  const std::string &group,
                                  const std::string &key) {
    std::string currentGroup;
    for (const auto &line : lines) {
        if (auto groupValue = groupName(line)) {
            currentGroup = *groupValue;
            continue;
        }
        if (currentGroup != group) {
            continue;
        }
        auto kv = keyValue(line);
        if (kv && kv->first == key) {
            return kv->second;
        }
    }
    return std::nullopt;
}

bool desktopBool(const std::vector<std::string> &lines, const std::string &key) {
    auto value = getKey(lines, "Desktop Entry", key);
    if (!value) {
        return false;
    }
    std::string t = trim(*value);
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
    return t == "true" || t == "1" || t == "yes";
}

std::string localizedName(const std::vector<std::string> &lines) {
    std::vector<std::string> candidates;
    std::string locale = currentLocaleName();
    if (!locale.empty()) {
        candidates.push_back("Name[" + locale + "]");
        auto underscore = locale.find('_');
        if (underscore != std::string::npos) {
            candidates.push_back("Name[" + locale.substr(0, underscore) + "]");
        }
    }
    candidates.push_back("Name[zh_CN]");
    candidates.push_back("Name[zh_TW]");
    candidates.push_back("Name");

    for (const auto &key : candidates) {
        auto value = getKey(lines, "Desktop Entry", key);
        if (value && !trim(*value).empty()) {
            return trim(*value);
        }
    }
    return "this application";
}

bool isDesktopApplication(const std::vector<std::string> &lines) {
    auto type = getKey(lines, "Desktop Entry", "Type");
    if (!type || trim(*type) != "Application") {
        return false;
    }
    if (desktopBool(lines, "Hidden") || desktopBool(lines, "NoDisplay")) {
        return false;
    }
    return true;
}

std::vector<std::string> removeGroup(const std::vector<std::string> &lines, const std::string &targetGroup) {
    std::vector<std::string> result;
    bool skipping = false;
    for (const auto &line : lines) {
        if (auto groupValue = groupName(line)) {
            skipping = (*groupValue == targetGroup);
            if (skipping) {
                continue;
            }
        }
        if (!skipping) {
            result.push_back(line);
        }
    }
    while (!result.empty() && trim(result.back()).empty()) {
        result.pop_back();
    }
    return result;
}

std::vector<std::string> actionListWithoutManagedAction(const std::string &value) {
    std::vector<std::string> actions;
    for (auto action : split(value, ';')) {
        action = trim(action);
        if (!action.empty() && action != kActionId) {
            actions.push_back(action);
        }
    }
    return actions;
}

std::string serializeActions(const std::vector<std::string> &actions) {
    std::string result;
    for (const auto &action : actions) {
        result += action;
        result += ';';
    }
    return result;
}

void ensureActionInDesktopEntry(std::vector<std::string> &lines) {
    bool inDesktopEntry = false;
    bool foundDesktopEntry = false;
    bool foundActions = false;
    std::size_t insertAt = lines.size();

    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (auto groupValue = groupName(lines[i])) {
            if (inDesktopEntry && insertAt == lines.size()) {
                insertAt = i;
            }
            inDesktopEntry = (*groupValue == "Desktop Entry");
            if (inDesktopEntry) {
                foundDesktopEntry = true;
                insertAt = i + 1;
            }
            continue;
        }
        if (!inDesktopEntry) {
            continue;
        }
        auto kv = keyValue(lines[i]);
        if (kv && kv->first == "Actions") {
            auto actions = actionListWithoutManagedAction(kv->second);
            actions.push_back(kActionId);
            lines[i] = "Actions=" + serializeActions(actions);
            foundActions = true;
        }
    }

    if (!foundDesktopEntry) {
        lines.insert(lines.begin(), "[Desktop Entry]");
        insertAt = 1;
    }
    if (!foundActions) {
        lines.insert(lines.begin() + static_cast<long>(insertAt), std::string("Actions=") + kActionId + ";");
    }
}

void removeManagedActionFromDesktopEntry(std::vector<std::string> &lines) {
    bool inDesktopEntry = false;
    std::vector<std::string> result;

    for (const auto &line : lines) {
        if (auto groupValue = groupName(line)) {
            inDesktopEntry = (*groupValue == "Desktop Entry");
            result.push_back(line);
            continue;
        }
        if (!inDesktopEntry) {
            result.push_back(line);
            continue;
        }
        auto kv = keyValue(line);
        if (!kv || kv->first != "Actions") {
            result.push_back(line);
            continue;
        }
        auto actions = actionListWithoutManagedAction(kv->second);
        if (!actions.empty()) {
            result.push_back("Actions=" + serializeActions(actions));
        }
    }

    lines = std::move(result);
}

void removeDesktopEntryKeys(std::vector<std::string> &lines, const std::set<std::string> &keys) {
    bool inDesktopEntry = false;
    std::vector<std::string> result;

    for (const auto &line : lines) {
        if (auto groupValue = groupName(line)) {
            inDesktopEntry = (*groupValue == "Desktop Entry");
            result.push_back(line);
            continue;
        }
        auto kv = keyValue(line);
        if (inDesktopEntry && kv && keys.count(kv->first)) {
            continue;
        }
        result.push_back(line);
    }

    lines = std::move(result);
}

void insertDesktopEntryKeys(std::vector<std::string> &lines,
                            const std::vector<std::pair<std::string, std::string>> &keys) {
    std::size_t insertAt = lines.size();
    bool foundDesktopEntry = false;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (auto groupValue = groupName(lines[i])) {
            if (*groupValue == "Desktop Entry") {
                foundDesktopEntry = true;
                insertAt = i + 1;
            } else if (foundDesktopEntry) {
                break;
            }
        }
    }

    if (!foundDesktopEntry) {
        lines.insert(lines.begin(), "[Desktop Entry]");
        insertAt = 1;
    }

    std::vector<std::string> serialized;
    serialized.reserve(keys.size());
    for (const auto &[key, value] : keys) {
        serialized.push_back(key + "=" + value);
    }
    lines.insert(lines.begin() + static_cast<long>(insertAt), serialized.begin(), serialized.end());
}

std::string quoteDesktopExecArg(const std::string &arg) {
    bool needsQuote = arg.empty();
    for (unsigned char c : arg) {
        if (!(std::isalnum(c) || c == '/' || c == '_' || c == '-' || c == '.' || c == ':' || c == '+')) {
            needsQuote = true;
            break;
        }
    }
    if (!needsQuote) {
        return arg;
    }

    std::string quoted = "\"";
    for (char c : arg) {
        if (c == '\\' || c == '"' || c == '`' || c == '$') {
            quoted.push_back('\\');
        }
        quoted.push_back(c);
    }
    quoted.push_back('"');
    return quoted;
}

std::vector<std::string> patchDesktopFile(const std::vector<std::string> &original,
                                          const fs::path &desktopPath,
                                          const fs::path &sourcePath,
                                          const fs::path &binaryPath,
                                          bool generated) {
    auto lines = removeGroup(original, kActionGroup);
    removeDesktopEntryKeys(lines, {kManagedKey, kGeneratedKey, kSourceKey});
    ensureActionInDesktopEntry(lines);
    insertDesktopEntryKeys(lines,
                           {{kManagedKey, "true"},
                            {kGeneratedKey, generated ? "true" : "false"},
                            {kSourceKey, sourcePath.string()}});

    if (!lines.empty() && !trim(lines.back()).empty()) {
        lines.emplace_back();
    }
    lines.push_back(std::string("[") + kActionGroup + "]");
    lines.push_back("Name=Uninstall");
    lines.push_back("Name[zh_CN]=卸载");
    lines.push_back("Name[zh_TW]=解除安裝");
    lines.push_back("Icon=edit-delete");
    lines.push_back("Exec=" + quoteDesktopExecArg(binaryPath.string()) + " --uninstall-desktop " +
                    quoteDesktopExecArg(desktopPath.string()));
    return lines;
}

std::vector<std::string> kickerActionProviderFile(const fs::path &binaryPath) {
    return {"[Desktop Entry]",
            "Type=Service",
            "Name=KDE Uninstall",
            std::string("Actions=") + kActionId + ";",
            "",
            std::string("[") + kActionGroup + "]",
            "Name=Uninstall",
            "Name[zh_CN]=卸载",
            "Name[zh_TW]=解除安裝",
            "Icon=edit-delete",
            "Exec=" + quoteDesktopExecArg(binaryPath.string()) + " --uninstall-desktop %f"};
}

std::vector<std::string> unpatchDesktopFile(const std::vector<std::string> &original) {
    auto lines = removeGroup(original, kActionGroup);
    removeManagedActionFromDesktopEntry(lines);
    removeDesktopEntryKeys(lines, {kManagedKey, kGeneratedKey, kSourceKey});
    while (!lines.empty() && trim(lines.back()).empty()) {
        lines.pop_back();
    }
    return lines;
}

bool sameLines(const std::vector<std::string> &left, const std::vector<std::string> &right) {
    return left == right;
}
bool generatedCopyPointsToMissingSource(const std::vector<std::string> &lines) {
    if (!desktopBool(lines, kGeneratedKey)) {
        return false;
    }
    auto source = getKey(lines, "Desktop Entry", kSourceKey);
    return source && !trim(*source).empty() && !fs::exists(trim(*source));
}

} // namespace kde_uninstall
