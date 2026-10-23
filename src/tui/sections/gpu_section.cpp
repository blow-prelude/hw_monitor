#include "tui/sections/gpu_section.h"

#include <cmath>
#include <string>

#include "tui/format.h"
#include "tui/widgets/progress_bar.h"

namespace hwmon {
namespace tui {

namespace {
using namespace ftxui;  // NOLINT(build/namespaces)

constexpr int kBarWidth = 10;  // 使用率 / 频率 / 显存条宽

std::string curOverMaxMHz(const Gauge& g) {
  return std::to_string(static_cast<long long>(std::lround(g.value))) + "/" +
         std::to_string(static_cast<long long>(std::lround(g.max))) + "MHz";
}

}  // namespace

Element gpuSection(const GpuView& v, int termWidth) {
  (void)termWidth;  // 单行信息不随宽度分栏，超宽由外层自然截断
  Elements rows;
  for (size_t i = 0; i < v.latest.size(); ++i) {
    const GpuSample& s = v.latest[i];
    const std::string vendor =
        i < v.info.cards.size() ? v.info.cards[i].vendor : std::string();
    const bool intel =
        vendor == "Intel" || s.name.find("Intel") != std::string::npos;

    rows.push_back(hbox({
        text("GPU ") | bold,
        text(s.name),
    }));
    if (intel) {
      rows.push_back(hbox({
          text("  "),
          text("（数据暂不支持）") | dim,
      }));
      continue;
    }

    // AMD：smClock invalid → 频率段显示 “—”（gaugeBar 内置降级）
    rows.push_back(hbox({
        text("  使用率 "),
        gaugeText(s.util, kBarWidth, BarColor::Frequency, percent(s.util.value)),
        text("   频率 "),
        gaugeText(s.smClock, kBarWidth, BarColor::Frequency,
                  s.smClock.valid ? curOverMaxMHz(s.smClock) : ""),
        text("   显存 "),
        gaugeText(s.memUsed, kBarWidth, BarColor::Capacity,
                  s.memUsed.valid ? bytesKiB(s.memUsed.value) + "/" + bytesKiB(s.memUsed.max)
                                  : ""),
        text("   温度 " + (s.temperature >= 0 ? std::to_string(s.temperature) + "°C" : "—")),
    }));
  }
  return vbox(std::move(rows));
}

}  // namespace tui
}  // namespace hwmon
