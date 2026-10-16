#include "core/media.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace kadro {
namespace {
char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c + ('a' - 'A')) : c; }
bool digit(char c) { return c >= '0' && c <= '9'; }
std::string lowered(std::string_view s) {
  std::string r(s); std::transform(r.begin(), r.end(), r.begin(), lower); return r;
}
}
Kind kindForExtension(std::string_view extension) {
  auto s = lowered(extension);
  if (!s.empty() && s.front() == '.') s.erase(0, 1);
  static const std::vector<std::string_view> photos = {"jpg","jpeg","jpe","jfif","png","bmp","gif","webp","tif","tiff","heic","heif","avif","ico"};
  static const std::vector<std::string_view> videos = {"mp4","m4v","mov","mkv","webm","avi","wmv","mpg","mpeg","mts","m2ts","3gp"};
  if (std::find(photos.begin(), photos.end(), s) != photos.end()) return Kind::Photo;
  if (std::find(videos.begin(), videos.end(), s) != videos.end()) return Kind::Video;
  return Kind::Unknown;
}
bool naturalLess(std::string_view a, std::string_view b) {
  std::size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (digit(a[i]) && digit(b[j])) {
      auto ae = i, be = j;
      while (ae < a.size() && digit(a[ae])) ++ae;
      while (be < b.size() && digit(b[be])) ++be;
      auto az = i, bz = j;
      while (az < ae && a[az] == '0') ++az;
      while (bz < be && b[bz] == '0') ++bz;
      if (ae - az != be - bz) return ae - az < be - bz;
      int cmp = a.substr(az, ae - az).compare(b.substr(bz, be - bz));
      if (cmp) return cmp < 0;
      // Same numeric value: fewer leading zeros first, without integer overflow.
      if (ae - i != be - j) return ae - i < be - j;
      i = ae; j = be;
    } else {
      auto ac = static_cast<unsigned char>(lower(a[i]));
      auto bc = static_cast<unsigned char>(lower(b[j]));
      if (ac != bc) return ac < bc;
      ++i; ++j;
    }
  }
  if (i != a.size() || j != b.size()) return i == a.size();
  return a < b; // deterministic case-sensitive tie-break
}
std::string nameError(std::string_view name) {
  if (name.empty() || name == "." || name == "..") return "Введите имя файла.";
  if (name.size() > 240) return "Слишком длинное имя: максимум 240 байт UTF-8.";
  for (unsigned char c : name)
    if (c < 32 || std::string_view("<>:\"/\\|?*").find(char(c)) != std::string_view::npos)
      return "В имени есть недопустимые символы.";
  if (name.back() == '.' || name.back() == ' ') return "Имя не должно заканчиваться точкой или пробелом.";
  auto stem = lowered(name.substr(0, name.find('.')));
  while (!stem.empty() && stem.back() == ' ') stem.pop_back();
  if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" || stem == "conin$" || stem == "conout$")
    return "Это имя зарезервировано системой.";
  if (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9')
    return "Это имя зарезервировано системой.";
  return {};
}
int nextIndex(int current, int count, int delta, bool wrap) {
  if (count <= 0) return -1;
  if (current < 0 || current >= count) return delta < 0 ? count - 1 : 0;
  auto n = std::int64_t(current) + delta;
  if (!wrap) return int(std::clamp(n, std::int64_t(0), std::int64_t(count - 1)));
  return int((n % count + count) % count);
}
std::pair<int, int> boundedSize(int w, int h, int edge, std::int64_t maxPixels) {
  if (w <= 0 || h <= 0 || maxPixels <= 0) return {0, 0};
  double scale = std::min(1.0, std::sqrt(double(maxPixels) / (double(w) * h)));
  if (edge > 0) scale = std::min(scale, double(edge) / std::max(w, h));
  int rw=std::max(1,int(std::floor(w*scale))), rh=std::max(1,int(std::floor(h*scale)));
  if (std::int64_t(rw)*rh > maxPixels) {
    if (rw>=rh) rw=int(maxPixels/rh); else rh=int(maxPixels/rw);
  }
  return {rw,rh};
}
}
