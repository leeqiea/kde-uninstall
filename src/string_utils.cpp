#include "kde_uninstall/string_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace kde_uninstall {

std::string trim(std::string s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

bool startsWith(const std::string &value, const std::string &prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool endsWith(const std::string &value, const std::string &suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> split(const std::string &value, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, delimiter)) {
        parts.push_back(item);
    }
    return parts;
}

std::string firstLine(const std::string &value) {
    auto pos = value.find('\n');
    return trim(pos == std::string::npos ? value : value.substr(0, pos));
}

std::string basenameOf(const std::string &path) {
    return fs::path(path).filename().string();
}

std::string currentLocaleName() {
    const char *vars[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
    for (const char *var : vars) {
        const char *value = std::getenv(var);
        if (value && *value) {
            std::string locale(value);
            auto dot = locale.find('.');
            if (dot != std::string::npos) {
                locale = locale.substr(0, dot);
            }
            auto at = locale.find('@');
            if (at != std::string::npos) {
                locale = locale.substr(0, at);
            }
            if (!locale.empty() && locale != "C" && locale != "POSIX") {
                return locale;
            }
        }
    }
    return {};
}

} // namespace kde_uninstall
