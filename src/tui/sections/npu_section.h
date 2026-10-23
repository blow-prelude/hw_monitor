// NPU 节渲染。权威规格：docs/design/03-tui.md §5.3。
#ifndef HWMON_TUI_SECTIONS_NPU_SECTION_H_
#define HWMON_TUI_SECTIONS_NPU_SECTION_H_

#include "core/snapshot.h"
#include "ftxui/dom/elements.hpp"

namespace hwmon {
namespace tui {

// supported=true → 型号行 + 频率/负载条；
// supported=false（域存在但未识别）→ 单行“未识别（本机设备树未报告受支持的 NPU）”。
// 域不存在（视图为 nullopt）→ 调用方整节消失。
ftxui::Element npuSection(const NpuView& v, int termWidth);

}  // namespace tui
}  // namespace hwmon

#endif  // HWMON_TUI_SECTIONS_NPU_SECTION_H_
