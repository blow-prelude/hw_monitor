// TUI 演示入口：假采集器 + MonitorHub + app_ui 全链路（无真实系统读取）。
// 真实硬件的组装（探测、注入）属 src/app/main，由采集层分支/集成阶段完成。
// 用法：hw_tui_demo [--no-alt]
#include <chrono>
#include <memory>
#include <string>

#include "core/monitor_hub.h"
#include "tui/app_ui.h"

#include "fake_collectors.h"  // NOLINT(build/include)

int main(int argc, char** argv) {
  using namespace hwmon;  // NOLINT(build/namespaces)

  MonitorHub hub;
  hub.setCpu(std::make_unique<testing::FakeCpuCollector>());
  hub.setGpu(std::make_unique<testing::FakeGpuCollector>());
  hub.setNpu(std::make_unique<testing::FakeNpuCollector>(/*supported=*/true));
  hub.setMem(std::make_unique<testing::FakeMemCollector>());
  hub.setDisk(std::make_unique<testing::FakeDiskCollector>());
  hub.setNet(std::make_unique<testing::FakeNetCollector>());

  hub.start(std::chrono::seconds{1});
  const bool altScreen = !(argc > 1 && std::string(argv[1]) == "--no-alt");
  tui::runAppUi(hub, tui::AppUiOptions{altScreen});
  hub.stop();  // 顺序铁律：runAppUi 已停刷新线程，再停 hub（03-tui.md §9）
  return 0;
}
