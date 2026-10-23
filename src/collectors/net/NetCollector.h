#pragma once
// 网络采集器（02-collectors §7）：/proc/net/dev + /sys/class/net。
// 只上报原始字节计数；吞吐差分归 core 层 MonitorHub（01-core §8.3）。
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/collector.h"

namespace hwmon {

// /proc/net/dev 纯解析：接口名 → (rxBytes, txBytes)（冒号后第 1 / 第 9 字段）。
// 畸形行（无冒号 / 字段不足 / 非数字）整行跳过。
std::map<std::string, std::pair<unsigned long long, unsigned long long>>
parseNetDev(std::string_view content);

class NetCollector : public INetCollector {
public:
    const char* domain() const override { return "net"; }
    bool probe() override;  // /sys/class/net 下存在非 lo 条目
    NetSample sample() override;
};

}  // namespace hwmon
