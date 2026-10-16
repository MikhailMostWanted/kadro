#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace kadro {
enum class Kind { Unknown, Photo, Video };
Kind kindForExtension(std::string_view extension);
bool naturalLess(std::string_view a, std::string_view b);
// Cross-platform restrictions: names created on a Mac remain usable on Windows.
std::string nameError(std::string_view name);
int nextIndex(int current, int count, int delta, bool wrap = true);
std::pair<int, int> boundedSize(int width, int height, int edge, std::int64_t maxPixels);
}
