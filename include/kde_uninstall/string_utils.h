#pragma once

#include <string>
#include <vector>

namespace kde_uninstall {

std::string trim(std::string s);
bool startsWith(const std::string &value, const std::string &prefix);
bool endsWith(const std::string &value, const std::string &suffix);
std::vector<std::string> split(const std::string &value, char delimiter);
std::string firstLine(const std::string &value);
std::string basenameOf(const std::string &path);
std::string currentLocaleName();

} // namespace kde_uninstall
