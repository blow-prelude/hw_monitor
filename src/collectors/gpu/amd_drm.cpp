#include "collectors/gpu/amd_drm.h"

#include "collectors/util.h"

namespace hwmon {

AmdSampleData readAmdDevice(const std::string& deviceDir) {
    AmdSampleData d;
    if (const auto v = util::readNumberFile(deviceDir + "/gpu_busy_percent"))
        d.busyPercent = *v;
    // 显存节点单位 bytes → KiB（单位换算只发生一次，见 01-core §3）。
    if (const auto v = util::readNumberFile(deviceDir + "/mem_info_vram_used"))
        d.vramUsedKiB = *v / 1024.0;
    if (const auto v = util::readNumberFile(deviceDir + "/mem_info_vram_total"))
        d.vramTotalKiB = *v / 1024.0;
    return d;
}

std::optional<std::string> readProductName(const std::string& deviceDir) {
    const auto name = util::readFile(deviceDir + "/product_name");
    if (!name) return std::nullopt;
    const auto trimmed = std::string(util::trim(*name));
    if (trimmed.empty()) return std::nullopt;
    return trimmed;
}

}  // namespace hwmon
