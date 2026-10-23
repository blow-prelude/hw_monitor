// TUI 组件树组装、按键处理、刷新触发。权威规格：docs/design/03-tui.md §4/§9。
#ifndef HWMON_TUI_APP_UI_H_
#define HWMON_TUI_APP_UI_H_

#include <optional>

#include "core/monitor_hub.h"
#include "ftxui/dom/elements.hpp"

namespace hwmon {
namespace tui {

// 一帧所需的全部域视图（nullopt = 该域不存在/不可用 → 整节隐藏，03-tui.md §10）
struct UiSnapshot {
  std::optional<CpuView> cpu;
  std::optional<GpuView> gpu;
  std::optional<NpuView> npu;
  std::optional<MemView> mem;
  std::optional<DiskView> disk;
  std::optional<NetView> net;
};

// 组件树组装（纯函数：输入 = 快照 + UI 状态 + 终端宽度）。
// 结构：header / cpu / hbox{gpu | filler, npu} / mem+disk / net / footer。
ftxui::Element renderFrame(const UiSnapshot& s, bool paused, int termWidth);

struct AppUiOptions {
  bool alternateScreen = true;  // false → TerminalOutput()（--no-alt，备选屏异常时用）
};

// 阻塞运行直到用户退出（q/Q/Ctrl+C）。返回前已 join 刷新线程；
// 调用方在其后执行 hub.stop()（顺序铁律：先停刷新线程，再停 hub，03-tui.md §9）。
void runAppUi(MonitorHub& hub, const AppUiOptions& opts = {});

}  // namespace tui
}  // namespace hwmon

#endif  // HWMON_TUI_APP_UI_H_
