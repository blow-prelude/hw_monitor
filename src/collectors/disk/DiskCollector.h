#pragma once
// 硬盘采集器（02-collectors §6）：/sys/block、/proc/diskstats、/proc/self/mounts、statvfs。
// 块设备视图（容量/IO 计数/容量条）+ 挂载点视图（statvfs）。
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/collector.h"

namespace hwmon {

struct MountEntry {  // /proc/self/mounts 一行的前三个字段
    std::string dev, mount, fs;
};

// —— 纯解析（无 I/O，供单测）——
// 块设备名过滤：loop* / ram* / zram* / sr* / md* / dm-*（v1 物理盘口径）。
bool isFilteredBlockDevice(std::string_view name);
// 全量解析（≥3 字段才收；畸形行跳过）。
std::vector<MountEntry> parseMountsRaw(std::string_view content);
// 过滤：dev 须以 /dev/ 开头；fs 在黑名单（含 fuse.*）→ 跳过；同一 dev 多挂载点保留第一个。
std::vector<MountEntry> filterMounts(const std::vector<MountEntry>& raw);
// mounts 第二列的八进制转义解码（\040 空格、\011、\012、\134 …）。
std::string decodeMountEscapes(std::string_view s);
// /proc/diskstats：dev 名 → (sectors_read, sectors_written)（1-based 字段 6 / 10）。
std::map<std::string, std::pair<uint64_t, uint64_t>>
parseDiskStats(std::string_view content);

class DiskCollector : public IDiskCollector {
public:
    const char* domain() const override { return "disk"; }
    bool probe() override;  // /sys/block 下存在至少一个非过滤设备
    DiskSample sample() override;
};

}  // namespace hwmon
