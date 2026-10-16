#include "core/media.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
using namespace kadro;
static int checks = 0;
static void check(bool ok, const char* label) { ++checks; if (!ok) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); } }
int main() {
  check(kindForExtension(".JPEG") == Kind::Photo, "JPEG uppercase");
  check(kindForExtension("heic") == Kind::Photo, "HEIC classified");
  check(kindForExtension("MOV") == Kind::Video, "MOV uppercase");
  check(kindForExtension("pdf") == Kind::Unknown, "non media excluded");
  check(kindForExtension("") == Kind::Unknown, "empty suffix");
  std::vector<std::string> names = {"IMG10.jpg", "IMG02.jpg", "IMG2.jpg", "IMG1.jpg"};
  std::sort(names.begin(), names.end(), naturalLess);
  check(names == std::vector<std::string>({"IMG1.jpg","IMG2.jpg","IMG02.jpg","IMG10.jpg"}), "natural numeric ordering");
  check(naturalLess("x99999999999999999999", "x100000000000000000000"), "no numeric overflow");
  std::vector<std::string> corpus = {"", "A", "a", "a0", "a00", "a1", "a01", "a2", "a10", "9", "10", "Фото1", "Фото2"};
  for (auto& a : corpus) for (auto& b : corpus) {
    check(!(naturalLess(a,b) && naturalLess(b,a)), "asymmetry");
    for (auto& c : corpus) if (naturalLess(a,b) && naturalLess(b,c)) check(naturalLess(a,c), "transitivity");
  }
  for (auto name : {"", ".", "..", "CON", "con.jpg", "LPT9.png", "a/b.jpg", "a\\b", "a:b", "x.", "x ", "CONIN$.png"})
    check(!nameError(name).empty(), "unsafe name");
  check(!nameError(std::string("x\0.png", 6)).empty(), "embedded NUL");
  for (auto name : {"Моя фотография.jpg", "image (1).png", "composition.webp", "COM10.jpg", "nulled.jpg"}) check(nameError(name).empty(), "valid name");
  check(nextIndex(0,0,1) == -1, "empty navigation");
  check(nextIndex(0,3,-1) == 2, "wrap previous");
  check(nextIndex(2,3,1) == 0, "wrap next");
  check(nextIndex(2,3,1,false) == 2, "clamp next");
  check(nextIndex(-1,3,1) == 0, "unselected next");
  check(nextIndex(-1,3,-1) == 2, "unselected previous");
  check(nextIndex(1,3,std::numeric_limits<int>::max()) >= 0, "navigation overflow");
  check(boundedSize(6000,4000,1500,24000000) == std::pair(1500,1000), "preview aspect ratio");
  check(boundedSize(0,100,500,24000000) == std::pair(0,0), "invalid dimensions");
  auto [w,h] = boundedSize(100000,100000,0,24000000);
  check(std::int64_t(w)*h <= 24000000, "pixel budget");
  check(boundedSize(1,2000000000,2048,24000000).first == 1, "extreme panorama");
  check(boundedSize(1,2000000000,0,1) == std::pair(1,1), "skinny pixel budget");
  std::cout << checks << " checks passed\n";
}
