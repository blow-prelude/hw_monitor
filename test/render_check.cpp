// 无头渲染自检：Element → ftxui::Screen（无需真实终端），校验各节内容与
// 留白/降级路径（03-tui.md §10）。无外部测试框架依赖。
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "tui/app_ui.h"
#include "tui/format.h"
#include "tui/sections/cpu_section.h"
#include "tui/sections/gpu_section.h"
#include "tui/sections/mem_disk_section.h"
#include "tui/sections/net_section.h"
#include "tui/sections/npu_section.h"
#include "tui/widgets/line_chart.h"
#include "tui/widgets/progress_bar.h"

#include "fake_collectors.h"  // NOLINT(build/include)

using namespace hwmon;   // NOLINT(build/namespaces)
using namespace hwmon::tui;  // NOLINT(build/namespaces)

namespace {

int g_failures = 0;

std::string stripAnsi(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
      size_t j = i + 2;
      while (j < s.size() && (s[j] == ';' || (s[j] >= '0' && s[j] <= '9')))
        ++j;
      if (j < s.size() && s[j] == 'm') {
        i = j + 1;
        continue;
      }
    }
    out += s[i++];
  }
  return out;
}

std::string renderToText(const ftxui::Element& el, int w, int h) {
  ftxui::Screen screen(w, h);
  ftxui::Render(screen, el);
  return stripAnsi(screen.ToString());
}

void check(bool ok, const std::string& what, const std::string& ctx = "") {
  if (ok) {
    std::printf("[ ok ] %s\n", what.c_str());
  } else {
    ++g_failures;
    std::printf("[FAIL] %s\n", what.c_str());
    if (!ctx.empty())
      std::printf("---- 渲染内容 ----\n%s\n-----------------\n", ctx.c_str());
  }
}

void expectContains(const std::string& text, const std::string& needle,
                    const std::string& what) {
  check(text.find(needle) != std::string::npos, what + " ⊃ 「" + needle + "」", text);
}

void expectNotContains(const std::string& text, const std::string& needle,
                       const std::string& what) {
  check(text.find(needle) == std::string::npos, what + " ∌ 「" + needle + "」", text);
}

size_t countOf(const std::string& text, const std::string& needle) {
  size_t n = 0, pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) {
    ++n;
    pos += needle.size();
  }
  return n;
}

// —— 视图构造 ——
CpuView fullCpuView() {
  CpuView v;
  v.info.arch = "x86_64";
  v.info.model = "AMD Ryzen 9 7950X";
  v.info.nLogicalCores = 8;
  v.latest.totalUsage = testing::gaugeOf(42.0, 100.0);
  for (int i = 0; i < 8; ++i)
    v.latest.coreFreq.push_back(i == 3 ? testing::badGauge()
                                       : testing::gaugeOf(4210.0, 4500.0));
  return v;
}

CpuView noCpufreqView() {
  CpuView v = fullCpuView();
  for (Gauge& g : v.latest.coreFreq)
    g = testing::badGauge();
  return v;
}

GpuView gpuView() {
  GpuView v;
  v.info.cards = {{"Fake NVIDIA GeForce RTX 4060", "NVIDIA"}};
  GpuSample s;
  s.name = "Fake NVIDIA GeForce RTX 4060";
  s.util = testing::gaugeOf(62.0, 100.0);
  s.smClock = testing::gaugeOf(1890.0, 2595.0);
  s.memUsed = testing::gaugeOf(6.0 * 1024 * 1024, 8.0 * 1024 * 1024);
  s.temperature = 64;
  v.latest.push_back(s);
  return v;
}

GpuView intelGpuView() {
  GpuView v;
  v.info.cards = {{"Fake Intel UHD 770", "Intel"}};
  GpuSample s;
  s.name = "Fake Intel UHD 770";
  v.latest.push_back(s);
  return v;
}

NpuView rkNpuView() {
  NpuView v;
  v.info.supported = true;
  v.info.model = "Rockchip RK3588 NPU";
  v.latest.supported = true;
  v.latest.freq = testing::gaugeOf(600.0, 1000.0);
  v.latest.load = testing::gaugeOf(38.0, 100.0);
  return v;
}

MemView memView() {
  MemView v;
  const double total = 31.2 * 1024 * 1024;
  v.latest.used = testing::gaugeOf(total * 0.68, total);
  const double swap = 8.0 * 1024 * 1024;
  v.latest.swap = testing::gaugeOf(swap * 0.15, swap);
  return v;
}

DiskView diskView() {
  DiskView v;
  DiskBlock nvme;
  nvme.dev = "nvme0n1";
  nvme.sizeKiB = static_cast<uint64_t>(931.5 * 1024 * 1024);
  nvme.usage = testing::gaugeOf(412.0 * 1024 * 1024, nvme.sizeKiB);
  v.blocks.push_back(nvme);
  v.readBps.push_back(85.2e6);
  v.writeBps.push_back(12.4e6);

  DiskBlock sda;
  sda.dev = "sda";
  sda.sizeKiB = static_cast<uint64_t>(238.5 * 1024 * 1024);
  v.blocks.push_back(sda);  // usage invalid：只显示总量
  v.readBps.push_back(-1);
  v.writeBps.push_back(-1);

  DiskMount root;
  root.dev = "nvme0n1p2";
  root.mount = "/";
  root.fs = "ext4";
  root.usage = testing::gaugeOf(612.0 * 1024 * 1024, 931.0 * 1024 * 1024);
  v.mounts.push_back(root);

  DiskMount shm;
  shm.dev = "";  // 归属不明 → 其他挂载
  shm.mount = "/dev/shm";
  shm.fs = "tmpfs";
  shm.usage = testing::gaugeOf(1.0 * 1024 * 1024, 16.0 * 1024 * 1024);
  v.mounts.push_back(shm);
  return v;
}

NetView netView() {
  NetView v;
  NetIfaceView eth;
  eth.latest.name = "eth0";
  eth.latest.state = "up";
  eth.rxBps = 12.3e6;
  eth.txBps = 2.1e6;
  eth.rxHist = testing::seriesOf({-1, 1e6, 2e6, -1, 3e6, 12e6});
  eth.txHist = testing::seriesOf({-1, 5e5, 6e5, 7e5, -1, 2e6});
  v.ifaces.push_back(eth);
  return v;
}

NetView allInvalidNetView() {
  NetView v;
  NetIfaceView eth;
  eth.latest.name = "eth0";
  eth.latest.state = "up";
  eth.rxBps = -1;
  eth.txBps = -1;
  eth.rxHist = testing::seriesOf({-1, -1, -1});
  eth.txHist = testing::seriesOf({-1, -1, -1});
  v.ifaces.push_back(eth);
  return v;
}

}  // namespace

int main() {
  const std::string kDash = "—";

  // —— format（03-tui.md §8）——
  check(bytesPerSec(-1) == kDash, "bytesPerSec(-1) → —");
  check(bytesPerSec(512) == "512B/s", "bytesPerSec(512) → 512B/s");
  check(bytesPerSec(1234567) == "1.2MB/s", "bytesPerSec(1234567) → 1.2MB/s");
  check(bytesPerSec(1024.0 * 1024) == "1.0MB/s", "bytesPerSec(1MiB) → 1.0MB/s");
  check(bytesKiB(512) == "512K", "bytesKiB(512) → 512K");
  check(bytesKiB(22334603.8) == "21.3G", "bytesKiB(21.3GiB) → 21.3G");
  check(freqMHz(-1) == kDash, "freqMHz(-1) → —");
  check(freqMHz(4210) == "4.21GHz", "freqMHz(4210) → 4.21GHz");
  check(freqMHz(600) == "600MHz", "freqMHz(600) → 600MHz");
  check(percent(-1) == kDash, "percent(-1) → —");
  check(percent(42.0) == "42%", "percent(42) → 42%");
  check(percent(41.6) == "42%", "percent(41.6) → 42%");
  check(percent(103.0) == "100%", "percent(103) 钳制 → 100%");

  // —— gaugeBar（03-tui.md §6）——
  const std::string validBar = renderToText(gaugeBar(testing::gaugeOf(0.5, 1.0), 10,
                                                     BarColor::Frequency), 40, 1);
  check(validBar.find("█") != std::string::npos && validBar.find(kDash) == std::string::npos,
        "有效 gauge 画填充块", validBar);
  const std::string badBar = renderToText(gaugeBar(testing::badGauge(), 10, BarColor::Capacity),
                                          40, 1);
  check(badBar.find("---") != std::string::npos && badBar.find(kDash) != std::string::npos,
        "invalid gauge → 全横线空条 + —", badBar);

  // —— throughputChart（03-tui.md §7/§10）——
  const std::string chart = renderToText(
      throughputChart(testing::seriesOf({-1, 1e6, 2e6, -1, 3e6}),
                      testing::seriesOf({-1, 5e5, 6e5, -1, 7e5}), 60, 5), 70, 9);
  expectContains(chart, "(120s)", "折线图");
  expectContains(chart, "4.8MB/s", "折线图峰值标注");  // nice(3e6) = 5e6 → 4.8MB/s
  expectNotContains(chart, "等待数据…", "折线图");
  const std::string emptyChart = renderToText(
      throughputChart(testing::seriesOf({-1, -1}), testing::seriesOf({-1, -1}), 60, 5), 70, 9);
  expectContains(emptyChart, "等待数据…", "空折线图");

  // —— CPU 节（03-tui.md §5.1）——
  const std::string cpu = renderToText(cpuSection(fullCpuView(), 100), 110, 30);
  expectContains(cpu, "CPU", "CPU 节");
  expectContains(cpu, "x86_64", "CPU 节");
  expectContains(cpu, "8 逻辑核", "CPU 节");
  expectContains(cpu, "总占用", "CPU 节");
  expectContains(cpu, "42%", "CPU 节");
  expectContains(cpu, "C0", "CPU 节");
  expectContains(cpu, "4.21GHz", "CPU 节");
  expectContains(cpu, kDash, "CPU 节（C3 留白）");
  const std::string cpuNoFreq = renderToText(cpuSection(noCpufreqView(), 100), 110, 30);
  expectContains(cpuNoFreq, "（频率信息不可用）", "CPU 节");
  expectContains(cpuNoFreq, "总占用", "CPU 节");
  expectNotContains(cpuNoFreq, "C0", "CPU 节");

  // —— GPU 节（03-tui.md §5.2）——
  const std::string gpu = renderToText(gpuSection(gpuView(), 100), 110, 10);
  expectContains(gpu, "RTX 4060", "GPU 节");
  expectContains(gpu, "使用率", "GPU 节");
  expectContains(gpu, "62%", "GPU 节");
  expectContains(gpu, "1890/2595MHz", "GPU 节");
  expectContains(gpu, "6.0G/8.0G", "GPU 节");
  expectContains(gpu, "64°C", "GPU 节");
  const std::string intel = renderToText(gpuSection(intelGpuView(), 100), 110, 10);
  expectContains(intel, "（数据暂不支持）", "Intel GPU 节");
  expectNotContains(intel, "使用率", "Intel GPU 节");
  check(gpuSection(GpuView{}, 100).get(), "空 GPU → 空元素不崩溃");

  // —— NPU 节（03-tui.md §5.3）——
  const std::string npu = renderToText(npuSection(rkNpuView(), 100), 110, 10);
  expectContains(npu, "RK3588", "NPU 节");
  expectContains(npu, "600/1000MHz", "NPU 节");
  expectContains(npu, "负载", "NPU 节");
  NpuView unknown = rkNpuView();
  unknown.info.supported = false;
  unknown.info.model = "未识别";
  unknown.latest.supported = false;
  const std::string npuUnknown = renderToText(npuSection(unknown, 100), 110, 10);
  expectContains(npuUnknown, "未识别（本机设备树未报告受支持的 NPU）", "NPU 节");

  // —— 内存 + 硬盘节（03-tui.md §5.4）——
  const std::string memdisk = renderToText(memDiskSection(memView(), diskView(), 100), 110, 20);
  expectContains(memdisk, "内存", "内存硬盘节");
  expectContains(memdisk, "21.2G", "内存硬盘节");
  expectContains(memdisk, "SWAP", "内存硬盘节");
  expectContains(memdisk, "nvme0n1", "内存硬盘节");
  expectContains(memdisk, "81.3MB/s", "内存硬盘节（R 速率）");  // 85.2e6 B/s，1024 进制
  expectContains(memdisk, "11.8MB/s", "内存硬盘节（W 速率）");
  expectContains(memdisk, "R — · W —", "内存硬盘节（sda 速率留白）");
  expectContains(memdisk, "/ (ext4)", "内存硬盘节（挂载点归属）");
  expectContains(memdisk, "其他挂载", "内存硬盘节（归属不明组）");
  check(countOf(memdisk, "其他挂载") == 1, "其他挂载组只出现一次");

  // —— 网络节（03-tui.md §5.5/§10）——
  const std::string net = renderToText(netSection(netView(), 100), 110, 20);
  expectContains(net, "eth0 (up)", "网络节");
  expectContains(net, "11.7MB/s", "网络节");  // 12.3e6 B/s，1024 进制
  expectContains(net, "(120s)", "网络节");
  const std::string netInvalid = renderToText(netSection(allInvalidNetView(), 100), 110, 20);
  expectContains(netInvalid, "等待数据…", "网络节（全 -1）");
  expectContains(netInvalid, "RX —", "网络节（首轮速率留白）");
  // >3 接口：只画前 2 个折线图
  NetView manyIfaces;
  for (int i = 0; i < 4; ++i) {
    NetIfaceView f;
    f.latest.name = "eth" + std::to_string(i);
    f.latest.state = "up";
    f.rxBps = 1e6;
    f.txBps = 1e6;
    f.rxHist = testing::seriesOf({1e6, 2e6, 3e6});
    f.txHist = testing::seriesOf({1e6, 2e6, 3e6});
    manyIfaces.ifaces.push_back(f);
  }
  const std::string netMany = renderToText(netSection(manyIfaces, 100), 110, 40);
  check(countOf(netMany, "(120s)") == 2, "4 接口只画 2 张折线图");

  // —— 整帧（03-tui.md §4/§10）——
  UiSnapshot full;
  full.cpu = fullCpuView();
  full.gpu = gpuView();
  full.npu = rkNpuView();
  full.mem = memView();
  full.disk = diskView();
  full.net = netView();
  const std::string frame = renderToText(renderFrame(full, false, 100), 110, 40);
  expectContains(frame, "hw_monitor", "整帧");
  expectContains(frame, "q 退出 · 空格 暂停刷新", "整帧");
  expectNotContains(frame, "已暂停", "整帧");
  expectContains(frame, "RTX 4060", "整帧");
  const std::string framePaused = renderToText(renderFrame(full, true, 100), 110, 40);
  expectContains(framePaused, "已暂停", "整帧（暂停态）");

  // nullopt 域整节消失，无残留空行错乱
  UiSnapshot bare;  // 全 nullopt
  const std::string frameBare = renderToText(renderFrame(bare, false, 100), 110, 40);
  expectContains(frameBare, "hw_monitor", "整帧（全 nullopt）");
  expectNotContains(frameBare, "总占用", "整帧（全 nullopt）");
  expectNotContains(frameBare, "内存", "整帧（全 nullopt）");

  // 窄终端提示（<60 列）
  const std::string frameNarrow = renderToText(renderFrame(full, false, 40), 60, 40);
  expectContains(frameNarrow, "请加宽终端", "窄终端提示");
  expectContains(frameNarrow, "hw_monitor", "窄终端仍渲染");

  std::printf("\n%s：%d 项失败\n", g_failures == 0 ? "渲染自检通过" : "渲染自检失败",
              g_failures);
  return g_failures == 0 ? 0 : 1;
}
