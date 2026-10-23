// 统一进度条：Gauge → Element。权威规格：docs/design/03-tui.md §6。
#ifndef HWMON_TUI_WIDGETS_PROGRESS_BAR_H_
#define HWMON_TUI_WIDGETS_PROGRESS_BAR_H_

#include <string>

#include "core/snapshot.h"
#include "ftxui/dom/elements.hpp"

namespace hwmon {
namespace tui {

// DESIGN.md §6.2 两套配色
enum class BarColor {
  Capacity,   // 容量类：绿 <70% · 黄 70–90% · 红 ≥90%
  Frequency,  // 频率/速率/使用率类：恒青色
};

// valid=false → 全横线空条 + "—"（dim 色）；否则按策略着色的 gauge(ratio)，
// ratio = value/max 钳制 [0,1]，width 为条宽字符数。
ftxui::Element gaugeBar(const Gauge& g, int width, BarColor policy);

// “条 + 右侧文本”组合：valid → 条 + " " + text；invalid → gaugeBar 自带 "—"，
// text 忽略（调用方可无条件传格式化结果）。
ftxui::Element gaugeText(const Gauge& g, int width, BarColor policy, const std::string& text);

}  // namespace tui
}  // namespace hwmon

#endif  // HWMON_TUI_WIDGETS_PROGRESS_BAR_H_
