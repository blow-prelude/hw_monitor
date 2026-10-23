// 单位格式化（纯函数）。权威规格：docs/design/03-tui.md §8。
// 约定：1024 进制；单位符号 K/M/G/T（不用 KiB/MiB 后缀，屏幕宽度优先）。
#ifndef HWMON_TUI_FORMAT_H_
#define HWMON_TUI_FORMAT_H_

#include <string>

namespace hwmon {
namespace tui {

// 字节/秒 → "1.2MB/s"；v < 0（无效）→ "—"
std::string bytesPerSec(double v);

// KiB → "21.3G"；< 1MiB 显示 K（如 "512K"）；v < 0 → "—"
std::string bytesKiB(double v);

// MHz → "4.21GHz"（≥1GHz，两位小数）/ "600MHz"；v < 0 → "—"
std::string freqMHz(double v);

// 42.0 → "42%"（整数）；v < 0 → "—"
std::string percent(double v);

}  // namespace tui
}  // namespace hwmon

#endif  // HWMON_TUI_FORMAT_H_
