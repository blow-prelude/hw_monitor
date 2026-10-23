// 网络节渲染（每接口一小节）。权威规格：docs/design/03-tui.md §5.5。
#ifndef HWMON_TUI_SECTIONS_NET_SECTION_H_
#define HWMON_TUI_SECTIONS_NET_SECTION_H_

#include "core/snapshot.h"
#include "ftxui/dom/elements.hpp"

namespace hwmon {
namespace tui {

// 每接口：文字行（名称/状态/RX/TX 当前值）+ 折线图（RX 绿 / TX 青）。
// 接口 >3 个时默认只画前 2 个的折线图，其余只保留文字行。
ftxui::Element netSection(const NetView& v, int termWidth);

}  // namespace tui
}  // namespace hwmon

#endif  // HWMON_TUI_SECTIONS_NET_SECTION_H_
