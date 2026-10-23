#pragma once
// GPU 聚合器（02-collectors §8）：NVML 优先，NVIDIA 不可用时走 /sys/class/drm（AMD 实读、
// Intel 仅识别）。v1 简化：NVIDIA 可用时只走 NVML，不与 drm 卡去重。
#include <string>
#include <vector>

#include "collectors/gpu/amd_drm.h"
#include "collectors/gpu/nvml_loader.h"
#include "core/collector.h"

namespace hwmon {

// pci id 文本（"0x1002"）→ 展示用 id（"1002"）；纯函数。
std::string stripHexPrefix(std::string_view id);

class GpuCollector : public IGpuCollector {
public:
    GpuCollector() = default;
    ~GpuCollector() override = default;

    const char* domain() const override { return "gpu"; }
    bool probe() override;  // 探测 + 构建 info() 缓存
    GpuInfo info() const override;
    std::vector<GpuSample> sample() override;

private:
    struct CardRef {
        bool nvml = false;          // true: 走 NVML（index）；false: drm sysfs（deviceDir）
        int nvmlIndex = 0;
        std::string vendor;         // drm 路径的 "AMD"/"Intel"（Intel 仅识别）
        std::string deviceDir;      // /sys/class/drm/cardX/device
        nvml::nvmlDevice_t handle = nullptr;
    };

    bool probed_ = false;
    nvml::NvmlLoader nvml_;         // RAII；全进程至多一实例（本 collector 持有）
    std::vector<CardRef> cards_;    // 与 info_.cards 一一对应
    GpuInfo info_;
};

}  // namespace hwmon
