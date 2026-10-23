#include "tui/format.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace hwmon {
namespace tui {

namespace {

// 1024 进制选单位：v ≥ 1024 则除以 1024 并进一档，1 位小数（首档整数）。
// units 由调用方给出档位符号序列（首档对应输入的原始单位）。
std::string scaled(double v, const char* const units[], int nUnits) {
  int i = 0;
  while (v >= 1024.0 && i < nUnits - 1) {
    v /= 1024.0;
    ++i;
  }
  char buf[48];
  if (i == 0)
    std::snprintf(buf, sizeof(buf), "%.0f%s", v, units[0]);
  else
    std::snprintf(buf, sizeof(buf), "%.1f%s", v, units[i]);
  return buf;
}

}  // namespace

std::string bytesPerSec(double v) {
  if (v < 0)
    return "—";
  static const char* kUnits[] = {"B", "KB", "MB", "GB", "TB"};  // 输入为字节
  return scaled(v, kUnits, 5) + "/s";
}

std::string bytesKiB(double v) {
  if (v < 0)
    return "—";
  static const char* kUnits[] = {"K", "M", "G", "T"};  // 输入为 KiB
  return scaled(v, kUnits, 4);
}

std::string freqMHz(double v) {
  if (v < 0)
    return "—";
  char buf[48];
  if (v >= 1000.0)
    std::snprintf(buf, sizeof(buf), "%.2fGHz", v / 1000.0);
  else
    std::snprintf(buf, sizeof(buf), "%.0fMHz", v);
  return buf;
}

std::string percent(double v) {
  if (v < 0)
    return "—";
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%.0f%%", std::max(0.0, std::min(100.0, v)));
  return buf;
}

}  // namespace tui
}  // namespace hwmon
