// 测试/演示用假采集器：实现 core 六域接口，产出合成数据。
// 只在 tests/ 内使用，不进产品目录。
#ifndef HWMON_TESTS_FAKE_COLLECTORS_H_
#define HWMON_TESTS_FAKE_COLLECTORS_H_

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/collector.h"

namespace hwmon {
namespace testing {

inline Gauge gaugeOf(double value, double max) { return Gauge{value, max, true}; }

inline Gauge badGauge() { return Gauge{}; }

inline Series seriesOf(std::initializer_list<double> pts) { return Series{pts}; }

// —— CPU：8 核，其中 C3 恒 invalid（留白路径）——
class FakeCpuCollector : public ICpuCollector {
 public:
  const char* domain() const override { return "cpu"; }
  bool probe() override { return true; }
  CpuInfo info() const override {
    CpuInfo info;
    info.arch = "x86_64";
    info.model = "Fake AMD Ryzen 9 7950X";
    info.nLogicalCores = 8;
    info.maxFreqMHz.assign(8, 4500.0);
    return info;
  }
  CpuSample sample() override {
    ++rounds_;
    CpuSample s;
    s.totalUsage = gaugeOf(50.0 + 40.0 * std::sin(rounds_ * 0.5), 100.0);
    for (int i = 0; i < 8; ++i) {
      if (i == 3) {
        s.coreFreq.push_back(badGauge());  // 留白铁律路径
        continue;
      }
      s.coreFreq.push_back(gaugeOf(2000.0 + 1500.0 * (1 + std::sin(rounds_ * 0.7 + i)), 4500.0));
    }
    return s;
  }

 private:
  int rounds_ = 0;
};

// 每轮必抛异常（验证 hub 失败降级）
class ThrowingCpuCollector : public ICpuCollector {
 public:
  const char* domain() const override { return "cpu"; }
  bool probe() override { return true; }
  CpuInfo info() const override { return CpuInfo{}; }
  CpuSample sample() override { throw std::runtime_error("fake cpu failure"); }
};

// —— GPU：一张 NVIDIA 卡 ——
class FakeGpuCollector : public IGpuCollector {
 public:
  const char* domain() const override { return "gpu"; }
  bool probe() override { return true; }
  GpuInfo info() const override {
    return GpuInfo{{{"Fake NVIDIA GeForce RTX 4060", "NVIDIA"}}};
  }
  std::vector<GpuSample> sample() override {
    ++rounds_;
    GpuSample s;
    s.name = "Fake NVIDIA GeForce RTX 4060";
    s.util = gaugeOf(60.0 + 30.0 * std::sin(rounds_ * 0.4), 100.0);
    s.smClock = gaugeOf(1890.0, 2595.0);
    s.memUsed = gaugeOf(6.0 * 1024 * 1024, 8.0 * 1024 * 1024);  // KiB
    s.temperature = 64;
    return {s};
  }

 private:
  int rounds_ = 0;
};

// —— NPU：RK3588（或未识别变体）——
class FakeNpuCollector : public INpuCollector {
 public:
  explicit FakeNpuCollector(bool supported = true) : supported_(supported) {}
  const char* domain() const override { return "npu"; }
  bool probe() override { return true; }
  NpuInfo info() const override {
    NpuInfo info;
    info.supported = supported_;
    info.model = supported_ ? "Rockchip RK3588 NPU" : "未识别";
    return info;
  }
  NpuSample sample() override {
    ++rounds_;
    NpuSample s;
    s.supported = supported_;
    if (supported_) {
      s.freq = gaugeOf(600.0, 1000.0);
      s.load = gaugeOf(38.0 + 20.0 * std::sin(rounds_ * 0.3), 100.0);
    }
    return s;
  }

 private:
  bool supported_;
  int rounds_ = 0;
};

// —— 内存 ——
class FakeMemCollector : public IMemCollector {
 public:
  const char* domain() const override { return "mem"; }
  bool probe() override { return true; }
  MemSample sample() override {
    MemSample s;
    const double totalKiB = 31.2 * 1024 * 1024;
    s.used = gaugeOf(totalKiB * 0.68, totalKiB);
    const double swapKiB = 8.0 * 1024 * 1024;
    s.swap = gaugeOf(swapKiB * 0.15, swapKiB);
    return s;
  }
};

// —— 硬盘：nvme0n1（有挂载 + 速率）+ sda（无挂载 → usage invalid）——
class FakeDiskCollector : public IDiskCollector {
 public:
  const char* domain() const override { return "disk"; }
  bool probe() override { return true; }
  DiskSample sample() override {
    ++rounds_;
    DiskSample s;
    DiskBlock nvme;
    nvme.dev = "nvme0n1";
    nvme.sizeKiB = static_cast<uint64_t>(931.5 * 1024 * 1024);
    nvme.usage = gaugeOf(412.0 * 1024 * 1024, nvme.sizeKiB);
    nvme.rSectors = 1000000 + rounds_ * 166000;  // ≈85MB/s @1s
    nvme.wSectors = 2000000 + rounds_ * 24000;   // ≈12MB/s @1s
    s.blocks.push_back(nvme);

    DiskBlock sda;
    sda.dev = "sda";
    sda.sizeKiB = static_cast<uint64_t>(238.5 * 1024 * 1024);
    sda.rSectors = 500000 + rounds_ * 1000;
    sda.wSectors = 500000 + rounds_ * 1000;
    s.blocks.push_back(sda);  // usage invalid：只显示总量

    DiskMount root;
    root.dev = "nvme0n1p2";
    root.mount = "/";
    root.fs = "ext4";
    root.usage = gaugeOf(612.0 * 1024 * 1024, 931.0 * 1024 * 1024);
    s.mounts.push_back(root);

    DiskMount shm;
    shm.dev = "";  // 归属不明 → “其他挂载”
    shm.mount = "/dev/shm";
    shm.fs = "tmpfs";
    shm.usage = gaugeOf(1.2 * 1024 * 1024, 16.0 * 1024 * 1024);
    s.mounts.push_back(shm);
    return s;
  }

 private:
  int rounds_ = 0;
};

// —— 网络：eth0（up，计数增长）+ wlan0（down，计数恒定 → 速率 0）——
// 可注入：计数回退（regressAt_ 轮）、eth0 消失区间 [absentFrom_, absentTo_]。
class FakeNetCollector : public INetCollector {
 public:
  const char* domain() const override { return "net"; }
  bool probe() override { return true; }
  NetSample sample() override {
    ++rounds_;
    NetSample s;
    const bool eth0Absent = rounds_ >= absentFrom_ && rounds_ <= absentTo_;
    uint64_t rx = rxBase_ + static_cast<uint64_t>(rounds_) * rxDelta_;
    uint64_t tx = txBase_ + static_cast<uint64_t>(rounds_) * txDelta_;
    if (rounds_ == regressAt_) {
      rx = 1;  // 计数回退 → hub 应判 -1
      tx = 1;
    }
    if (!eth0Absent) {
      NetIface eth0;
      eth0.name = "eth0";
      eth0.state = "up";
      eth0.rxBytes = rx;
      eth0.txBytes = tx;
      s.ifaces.push_back(eth0);
    }
    NetIface wlan0;
    wlan0.name = "wlan0";
    wlan0.state = "down";
    wlan0.rxBytes = 1000;  // 恒定：差分得 0，折线图平线
    wlan0.txBytes = 1000;
    s.ifaces.push_back(wlan0);
    return s;
  }

  uint64_t rxBase_ = 1000000;
  uint64_t txBase_ = 2000000;
  uint64_t rxDelta_ = 12000000;  // 每轮增量（字节）
  uint64_t txDelta_ = 2000000;
  int regressAt_ = -1;
  int absentFrom_ = -1;
  int absentTo_ = -1;

 private:
  int rounds_ = 0;
};

}  // namespace testing
}  // namespace hwmon

#endif  // HWMON_TESTS_FAKE_COLLECTORS_H_
