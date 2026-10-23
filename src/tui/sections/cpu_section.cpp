#include "tui/sections/cpu_section.h"

#include <algorithm>
#include <string>
#include <vector>

#include "tui/format.h"
#include "tui/widgets/progress_bar.h"

namespace hwmon {
namespace tui {

namespace {

using namespace ftxui;  // NOLINT(build/namespaces) — FTXUI 惯用用法

constexpr int kCellWidth = 26;      // 每个核条单元格占约 26 列（03-tui.md §5.1）
constexpr int kCoreBarWidth = 10;   // 核条宽（字符数）
constexpr int kTotalBarWidth = 18;  // 总占用条宽

Element coreCell(int index, const Gauge& g) {
  return hbox({
             text("C" + std::to_string(index) + " "),
             gaugeText(g, kCoreBarWidth, BarColor::Frequency,
                       g.valid ? freqMHz(g.value) : ""),
         }) |
         size(WIDTH, EQUAL, kCellWidth);
}

}  // namespace

Element cpuSection(const CpuView& v, int termWidth) {
  Elements rows;

  // 首行：CPU  架构 · 型号 · N 逻辑核 …… 总占用 [条] 42%
  rows.push_back(hbox({
      text("CPU ") | bold,
      text(v.info.arch + " · " + v.info.model + " · " +
           std::to_string(v.info.nLogicalCores) + " 逻辑核"),
      filler(),
      text("总占用 "),
      gaugeText(v.latest.totalUsage, kTotalBarWidth, BarColor::Frequency,
                percent(v.latest.totalUsage.value)),
  }));

  // 核条网格：valid=false → C3 ▕----------▏ —（gaugeBar 内置降级）
  const std::vector<Gauge>& cores = v.latest.coreFreq;
  bool anyValid = false;
  for (const Gauge& g : cores)
    anyValid = anyValid || g.valid;
  if (cores.empty() || !anyValid) {
    // 无 cpufreq：整块核条网格替换为一行说明，总占用照常（03-tui.md §5.1）
    rows.push_back(text("（频率信息不可用）") | dim);
  } else {
    const int cols = std::clamp(termWidth / 28, 1, 4);
    Elements row;
    for (size_t i = 0; i < cores.size(); ++i) {
      row.push_back(coreCell(static_cast<int>(i), cores[i]));
      if (row.size() == static_cast<size_t>(cols)) {
        rows.push_back(hbox(std::move(row)));
        row.clear();
      }
    }
    if (!row.empty())
      rows.push_back(hbox(std::move(row)));
  }

  return vbox(std::move(rows));
}

}  // namespace tui
}  // namespace hwmon
