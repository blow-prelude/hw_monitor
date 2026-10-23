#include "tui/app_ui.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/terminal.hpp"
#include "tui/sections/cpu_section.h"
#include "tui/sections/gpu_section.h"
#include "tui/sections/mem_disk_section.h"
#include "tui/sections/net_section.h"
#include "tui/sections/npu_section.h"

namespace hwmon {
namespace tui {

using namespace ftxui;  // NOLINT(build/namespaces) — FTXUI 惯用用法

// ===== 帧组装（纯函数，03-tui.md §4 组件树）=====

Element renderFrame(const UiSnapshot& s, bool paused, int termWidth) {
  using namespace ftxui;  // NOLINT(build/namespaces)

  Elements rows;

  // header："hw_monitor · 1s 刷新"，右端 hint（暂停状态）
  rows.push_back(hbox({
      text("hw_monitor") | bold,
      text(" · 1s 刷新"),
      filler(),
      paused ? text("已暂停") | bold | color(Color::Yellow) : text(""),
  }));

  // 终端过窄：顶部提示一行，其余照常尽力渲染（03-tui.md §10）
  if (termWidth < 60)
    rows.push_back(text("请加宽终端（当前 " + std::to_string(termWidth) +
                        " 列，建议 ≥60）") |
                   color(Color::Red));

  if (s.cpu)
    rows.push_back(cpuSection(*s.cpu, termWidth));

  // hbox{ gpu_section | filler, npu_section }：各 opt，缺谁跳谁
  const bool hasGpu = s.gpu.has_value();
  const bool hasNpu = s.npu.has_value();
  if (hasGpu && hasNpu) {
    rows.push_back(hbox({gpuSection(*s.gpu, termWidth), filler(),
                         npuSection(*s.npu, termWidth)}));
  } else if (hasGpu) {
    rows.push_back(hbox({gpuSection(*s.gpu, termWidth), filler()}));
  } else if (hasNpu) {
    rows.push_back(npuSection(*s.npu, termWidth));
  }

  if (s.mem || s.disk) {
    const MemView emptyMem{};
    const DiskView emptyDisk{};
    rows.push_back(memDiskSection(s.mem ? *s.mem : emptyMem,
                                  s.disk ? *s.disk : emptyDisk, termWidth));
  }

  if (s.net)
    rows.push_back(netSection(*s.net, termWidth));

  rows.push_back(filler());
  rows.push_back(text("q 退出 · 空格 暂停刷新") | dim);
  return vbox(std::move(rows));
}

// ===== 运行时（线程模型见 03-tui.md §9）=====

namespace {

using ftxui::Event;

constexpr std::chrono::milliseconds kFrameInterval{200};  // 刷新 200ms 一帧

class AppUi {
 public:
  AppUi(MonitorHub& hub, const AppUiOptions& opt)
      : hub_(hub),
        screen_(opt.alternateScreen ? ftxui::ScreenInteractive::Fullscreen()
                                    : ftxui::ScreenInteractive::TerminalOutput()) {}

  void run() {
    refresher_ = std::thread([this] { refreshLoop(); });
    auto renderer = ftxui::Renderer([this] {
      pullViews();  // UI 线程内拉取；暂停时不更新 → 显示冻结，hub 照常跑
      return renderFrame(snapshot_, paused_.load(), ftxui::Terminal::Size().dimx);
    });
    auto root = ftxui::CatchEvent(std::move(renderer), [this](const Event& e) {
      return handleKey(e);
    });
    screen_.Loop(root);
    quit_.store(true);
    refresher_.join();
  }

 private:
  void pullViews() {
    if (paused_.load())
      return;
    UiSnapshot s;
    s.cpu = hub_.cpu();
    s.gpu = hub_.gpu();
    s.npu = hub_.npu();
    s.mem = hub_.mem();
    s.disk = hub_.disk();
    s.net = hub_.net();
    snapshot_ = std::move(s);
  }

  // 刷新线程：只向 UI 线程投递 Event::Custom 触发重绘，不触碰任何视图状态
  void refreshLoop() {
    while (!quit_.load()) {
      std::this_thread::sleep_for(kFrameInterval);
      if (!quit_.load())
        screen_.PostEvent(Event::Custom);
    }
  }

  bool handleKey(const Event& e) {
    if (e == Event::Character('q') || e == Event::Character('Q') ||
        e == Event::Special("\x03")) {  // Ctrl+C
      screen_.Exit();
      return true;
    }
    if (e == Event::Character(' ')) {
      paused_.store(!paused_.load());
      return true;
    }
    return false;
  }

  MonitorHub& hub_;
  ftxui::ScreenInteractive screen_;
  std::thread refresher_;
  std::atomic<bool> paused_{false};
  std::atomic<bool> quit_{false};
  UiSnapshot snapshot_;  // 仅 UI 线程访问
};

}  // namespace

void runAppUi(MonitorHub& hub, const AppUiOptions& opts) {
  AppUi ui(hub, opts);
  ui.run();
  // 返回后调用方执行 hub.stop()：此时刷新线程已 join，先停刷新线程、再停 hub
}

}  // namespace tui
}  // namespace hwmon
