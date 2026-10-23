#pragma once
// AMD 卡 sysfs 读取（02-collectors §8.3）：/sys/class/drm/cardX/device/ 下节点。
// 频率节点需 root → smClock 恒 invalid；无温度节点 → -1（由 GpuCollector 兜住）。
#include <optional>
#include <string>

namespace hwmon {

struct AmdSampleData {
    std::optional<double> busyPercent;   // gpu_busy_percent（0–100）
    std::optional<double> vramUsedKiB;   // mem_info_vram_used，bytes→KiB
    std::optional<double> vramTotalKiB;  // mem_info_vram_total，bytes→KiB
};

// 读单张 AMD 卡的动态数据；节点缺失/不可读 → 对应 nullopt。
AmdSampleData readAmdDevice(const std::string& deviceDir);

// product_name 节点内容（trim 后）；缺失 → nullopt。
std::optional<std::string> readProductName(const std::string& deviceDir);

}  // namespace hwmon
