#pragma once

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace kde_uninstall {

std::vector<std::string> readLines(const std::filesystem::path &path);
void writeLinesAtomic(const std::filesystem::path &path, const std::vector<std::string> &lines);
std::optional<std::string> getKey(const std::vector<std::string> &lines,
                                  const std::string &group,
                                  const std::string &key);
bool desktopBool(const std::vector<std::string> &lines, const std::string &key);
std::string localizedName(const std::vector<std::string> &lines);
bool isDesktopApplication(const std::vector<std::string> &lines);
std::vector<std::string> patchDesktopFile(const std::vector<std::string> &original,
                                          const std::filesystem::path &desktopPath,
                                          const std::filesystem::path &sourcePath,
                                          const std::filesystem::path &binaryPath,
                                          bool generated);
std::vector<std::string> kickerActionProviderFile(const std::filesystem::path &binaryPath);
std::vector<std::string> unpatchDesktopFile(const std::vector<std::string> &original);
bool sameLines(const std::vector<std::string> &left, const std::vector<std::string> &right);
bool generatedCopyPointsToMissingSource(const std::vector<std::string> &lines);

} // namespace kde_uninstall
