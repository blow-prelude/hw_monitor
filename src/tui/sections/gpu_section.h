// GPU 节渲染（每卡两行）。权威规格：docs/design/03-tui.md §5.2。
#ifndef HWMON_TUI_SECTIONS_GPU_SECTION_H_
#define HWMON_TUI_SECTIONS_GPU_SECTION_H_

#include "core/snapshot.h"
#include "ftxui/dom/elements.hpp"

namespace hwmon {
namespace tui {

// 每卡：首行型号；次行 使用率/频率/显存/温度。
// AMD 卡频率段 invalid → "—"；Intel 卡整卡仅型号 + “（数据暂不支持）”；
// 无卡（latest 为空）→ 渲染空元素（调用方 nullopt 时整节消失）。
ftxui::Element gpuSection(const GpuView& v, int termWidth);

}  // namespace tui
}  // namespace hwmon

#endif  // HWMON_TUI_SECTIONS_GPU_SECTION_H_
