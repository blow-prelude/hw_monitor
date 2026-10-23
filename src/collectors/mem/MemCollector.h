#pragma once
// 内存采集器（02-collectors §5）：/proc/meminfo。
#include <optional>
#include <string_view>

#include "core/collector.h"

namespace hwmon {

// meminfo 关键字段的纯解析；缺失字段为 nullopt（→ 对应 Gauge invalid）。
struct MemInfoParsed {
    std::optional<unsigned long long> memTotal;      // kB，原生 KiB 无需换算
    std::optional<unsigned long long> memAvailable;
    std::optional<unsigned long long> swapTotal;
    std::optional<unsigned long long> swapFree;
};
MemInfoParsed parseMemInfo(std::string_view content);

class MemCollector : public IMemCollector {
public:
    const char* domain() const override { return "mem"; }
    bool probe() override { return true; }  // /proc/meminfo 必在
    MemSample sample() override;
};

}  // namespace hwmon
