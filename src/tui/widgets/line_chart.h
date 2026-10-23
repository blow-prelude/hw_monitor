// 吞吐量折线图：Series → Canvas Element。权威规格：docs/design/03-tui.md §7。
#ifndef HWMON_TUI_WIDGETS_LINE_CHART_H_
#define HWMON_TUI_WIDGETS_LINE_CHART_H_

#include "core/snapshot.h"
#include "ftxui/dom/elements.hpp"

namespace hwmon {
namespace tui {

// rx/tx 时间序（-1 = 无效断点）；返回带边框 Canvas Element，
// 左上标 nice 化峰值（如 "1.0GB/s"），右上标 "(120s)"；Canvas 内只画线与峰值，
// 图例与当前值由调用方放在图上方文字行（§5.5）。
ftxui::Element throughputChart(const Series& rx, const Series& tx, int widthCells,
                               int heightCells);

}  // namespace tui
}  // namespace hwmon

#endif  // HWMON_TUI_WIDGETS_LINE_CHART_H_
