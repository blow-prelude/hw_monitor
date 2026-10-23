#pragma once
// NPU 采集器（02-collectors §9）：device-tree 型号识别优先，仅 RK 系列实际读取。
#include <optional>
#include <string>
#include <string_view>

#include "core/collector.h"

namespace hwmon {

enum class NpuKind { NoDeviceTree, Rockchip, Unrecognized };

struct CompatibleResult {
    NpuKind kind = NpuKind::NoDeviceTree;
    std::string chip;  // Rockchip 时为 "rk3588" 等
};

// compatible 是 NUL 分隔的多字符串；切分后逐 token 前缀匹配 "rockchip,rk"。
CompatibleResult parseCompatible(std::string_view content);  // 纯函数

// 型号映射表：rk3588 → "Rockchip RK3588 NPU"；未收录 → "Rockchip <大写 chip> NPU"。纯函数。
std::string rkModelName(const std::string& chip);

// rknpu load 文本提取首个整数（容忍 "load : 42" 等格式）；无数字 → nullopt。纯函数。
std::optional<int> parseRknpuLoad(std::string_view content);

class NpuCollector : public INpuCollector {
public:
    const char* domain() const override { return "npu"; }
    bool probe() override;
    NpuInfo info() const override;
    NpuSample sample() override;

private:
    NpuInfo info_;
    bool probed_ = false;
};

}  // namespace hwmon
