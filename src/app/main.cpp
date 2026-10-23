// 主程序：启动时探测本机硬件（自动检测），探测成功的采集器注入 hub 并默认全开，
// 随后进入 TUI 主循环（DESIGN §1 / §3.1 / §9）。
#include <memory>
#include <string>

#include "collectors/cpu/CpuCollector.h"
#include "collectors/disk/DiskCollector.h"
#include "collectors/gpu/GpuCollector.h"
#include "collectors/mem/MemCollector.h"
#include "collectors/net/NetCollector.h"
#include "collectors/npu/NpuCollector.h"
#include "core/monitor_hub.h"
#include "tui/app_ui.h"

int main(int argc, char** argv) {
    using namespace hwmon;

    MonitorHub hub;

    // 自动检测：probe() 成功的域才注入；未注入的域 = 该机器没有（UI 整节隐藏）
    if (auto c = std::make_unique<CpuCollector>(); c->probe()) hub.setCpu(std::move(c));
    if (auto c = std::make_unique<GpuCollector>(); c->probe()) hub.setGpu(std::move(c));
    if (auto c = std::make_unique<NpuCollector>(); c->probe()) hub.setNpu(std::move(c));
    if (auto c = std::make_unique<MemCollector>(); c->probe()) hub.setMem(std::move(c));
    if (auto c = std::make_unique<DiskCollector>(); c->probe()) hub.setDisk(std::move(c));
    if (auto c = std::make_unique<NetCollector>(); c->probe()) hub.setNet(std::move(c));

    hub.start();
    const bool altScreen = !(argc > 1 && std::string(argv[1]) == "--no-alt");
    tui::runAppUi(hub, tui::AppUiOptions{altScreen});
    hub.stop();  // 顺序铁律：先停 UI 刷新线程，再停采集线程（03-tui §9）
    return 0;
}
