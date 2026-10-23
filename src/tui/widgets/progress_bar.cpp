#include "tui/widgets/progress_bar.h"

#include <algorithm>

namespace hwmon {
namespace tui {

using ftxui::Element;

Element gaugeBar(const Gauge& g, int width, BarColor policy) {
  using namespace ftxui;  // NOLINT(build/namespaces) — FTXUI 惯用用法
  if (!g.valid || g.max <= 0)
    return hbox({
               text(std::string(static_cast<size_t>(std::max(0, width)), '-')),
               text(" —"),
           }) |
           dim;

  const double ratio = std::clamp(g.value / g.max, 0.0, 1.0);
  Color col = Color::Cyan;
  if (policy == BarColor::Capacity)
    col = ratio < 0.70 ? Color::Green : (ratio < 0.90 ? Color::Yellow : Color::Red);
  return gauge(static_cast<float>(ratio))  // NOLINT(readability-identifier-naming)
         | size(WIDTH, EQUAL, width) | color(col);
}

Element gaugeText(const Gauge& g, int width, BarColor policy, const std::string& label) {
  Element bar = gaugeBar(g, width, policy);
  if (!g.valid)
    return bar;
  return ftxui::hbox({std::move(bar), ftxui::text(" " + label)});
}

}  // namespace tui
}  // namespace hwmon
