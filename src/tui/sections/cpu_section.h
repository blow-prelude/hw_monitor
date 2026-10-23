// CPU 节渲染。权威规格：docs/design/03-tui.md §5.1。
#ifndef HWMON_TUI_SECTIONS_CPU_SECTION_H_
#define HWMON_TUI_SECTIONS_CPU_SECTION_H_

#include "core/snapshot.h"
#include "ftxui/dom/elements.hpp"

namespace hwmon {
namespace tui {

// 首行：架构 · 型号 · N 逻辑核 ……（右端）总占用条；其后为核条网格
// （列数 = clamp(W/28, 1, 4)，随宽度折行）。全部核 invalid → 一行“（频率信息不可用）”。
ftxui::Element cpuSection(const CpuView& v, int termWidth);

}  // namespace tui
}  // namespace hwmon

#endif  // HWMON_TUI_SECTIONS_CPU_SECTION_H_
