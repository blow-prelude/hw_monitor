#pragma once
// CPU 采集器（02-collectors §4）：/proc/cpuinfo、/proc/stat、/sys/devices/system/cpu/*/cpufreq。
#include <optional>
#include <string>
#include <string_view>

#include "core/collector.h"

namespace hwmon {

// /proc/stat 首行解析结果：累计 jiffies 的总量与空闲量（idle+iowait）。
struct StatTick {
    unsigned long long total = 0;    // user+nice+system+idle+iowait+irq+softirq+steal
    unsigned long long idleAll = 0;  // idle+iowait
};

// —— 纯解析（无 I/O，供单测）——
// 首行须为 "cpu  user nice system idle iowait ..."；畸形 → nullopt。
std::optional<StatTick> parseStatFirstLine(std::string_view content);
// /sys/devices/system/cpu/present 范围解析（"0-3"、"0-3,8-11"）→ 核数；畸形 → nullopt。
std::optional<int> parseCpuPresent(std::string_view content);
// 型号解码：x86 取首个 model name；ARM 按 implementer+part 查表分组
// （"4×Cortex-A76 + 4×Cortex-A55"，未命中显示 0x<hex>）。
std::string parseCpuModel(std::string_view cpuinfo, const std::string& arch);

class CpuCollector : public ICpuCollector {
public:
    const char* domain() const override { return "cpu"; }
    bool probe() override;  // 恒真（/proc/stat 必在）；完成 info() 缓存
    CpuInfo   info() const override;
    CpuSample sample() override;

private:
    void cacheInfo();

    CpuInfo info_;
    bool probed_ = false;
    // /proc/stat 只有累计值，使用率差分放采集器内（02-collectors §10）；首轮 invalid。
    bool hasPrev_ = false;
    unsigned long long prevTotal_ = 0, prevIdleAll_ = 0;
};

}  // namespace hwmon
