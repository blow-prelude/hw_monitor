#include "tui/sections/npu_section.h"

#include <cmath>
#include <string>

#include "tui/format.h"
#include "tui/widgets/progress_bar.h"

namespace hwmon {
namespace tui {

namespace {
using namespace ftxui;  // NOLINT(build/namespaces)

constexpr int kBarWidth = 10;  // 频率 / 负载条宽
}  // namespace

Element npuSection(const NpuView& v, int termWidth) {
  (void)termWidth;
  if (!v.info.supported || !v.latest.supported)
    return hbox({
        text("NPU ") | bold,
        text("未识别（本机设备树未报告受支持的 NPU）") | dim,
    });

  const std::string freqText =
      v.latest.freq.valid
          ? std::to_string(static_cast<long long>(std::lround(v.latest.freq.value))) + "/" +
                std::to_string(static_cast<long long>(std::lround(v.latest.freq.max))) + "MHz"
          : "";
  return vbox({
      hbox({
          text("NPU ") | bold,
          text(v.info.model),
      }),
      hbox({
          text("  频率 "),
          gaugeText(v.latest.freq, kBarWidth, BarColor::Frequency, freqText),
          text("   负载 "),
          gaugeText(v.latest.load, kBarWidth, BarColor::Frequency,
                    percent(v.latest.load.value)),
      }),
  });
}

}  // namespace tui
}  // namespace hwmon
