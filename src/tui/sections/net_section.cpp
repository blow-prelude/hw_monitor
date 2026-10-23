#include "tui/sections/net_section.h"

#include <algorithm>
#include <string>

#include "tui/format.h"
#include "tui/widgets/line_chart.h"

namespace hwmon {
namespace tui {

namespace {
using namespace ftxui;  // NOLINT(build/namespaces)

constexpr int kChartHeight = 5;   // Canvas 内容高（边框另占 2 行）
constexpr int kMaxCharted = 2;    // 折线图最多画前 2 个接口（03-tui.md §5.5）
constexpr int kMinChartWidth = 24;
constexpr int kMaxChartWidth = 118;

Element ifaceHeader(const NetIfaceView& f, bool first) {
  return hbox({
      text(first ? "网络 " : "     "),
      text(f.latest.name + " (" + f.latest.state + ")"),
      text("   RX ") | color(Color::Green),
      text(bytesPerSec(f.rxBps)),  // 首轮/无效 -1 → “—”
      text("   TX ") | color(Color::Cyan),
      text(bytesPerSec(f.txBps)),
  });
}

}  // namespace

Element netSection(const NetView& v, int termWidth) {
  Elements rows;
  const int chartWidth = std::max(kMinChartWidth, std::min(termWidth - 4, kMaxChartWidth));
  bool first = true;
  for (size_t i = 0; i < v.ifaces.size(); ++i) {
    const NetIfaceView& f = v.ifaces[i];
    rows.push_back(ifaceHeader(f, first));  // 接口 down：文字行照常，速率 0
    first = false;
    if (static_cast<int>(i) < kMaxCharted)
      rows.push_back(hbox({
          text("  "),
          throughputChart(f.rxHist, f.txHist, chartWidth, kChartHeight),
      }));
  }
  return vbox(std::move(rows));
}

}  // namespace tui
}  // namespace hwmon
