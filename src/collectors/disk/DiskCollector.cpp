#include "collectors/disk/DiskCollector.h"

#include <sys/statvfs.h>

#include <algorithm>
#include <filesystem>
#include <set>

#include "collectors/util.h"

namespace hwmon {
namespace {
namespace fs = std::filesystem;

// 文件系统类型黑名单（02-collectors §6，v1 全表）。
const std::set<std::string>& fsBlacklist() {
    static const std::set<std::string> kBl = {
        "proc", "sysfs", "devtmpfs", "devpts", "tmpfs", "securityfs", "cgroup",
        "cgroup2", "pstore", "debugfs", "tracefs", "configfs", "bpf", "autofs",
        "mqueue", "hugetlbfs", "fusectl", "overlay", "squashfs", "efivarfs",
        "bpf_fs", "rpc_pipefs", "nsfs",
    };
    return kBl;
}

bool isVirtualFs(const std::string& fsType) {
    return fsBlacklist().count(fsType) > 0 || fsType.rfind("fuse.", 0) == 0;
}

// statvfs → (usedKiB, totalKiB)；失败 nullopt。
std::optional<std::pair<double, double>> statFsKiB(const std::string& mount) {
    struct statvfs st {};
    if (::statvfs(mount.c_str(), &st) != 0) return std::nullopt;
    const double bsize = static_cast<double>(st.f_bsize);
    if (bsize <= 0) return std::nullopt;
    const double total = static_cast<double>(st.f_blocks) * bsize / 1024.0;
    const double used = static_cast<double>(st.f_blocks - st.f_bfree) * bsize / 1024.0;
    return std::make_pair(used, total);
}

std::string basenameOfDev(const std::string& dev) {
    return dev.rfind("/dev/", 0) == 0 ? dev.substr(5) : dev;
}

}  // namespace

bool isFilteredBlockDevice(std::string_view name) {
    for (const std::string_view prefix : {"loop", "ram", "zram", "sr", "md", "dm-"}) {
        if (name.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

std::vector<MountEntry> parseMountsRaw(std::string_view content) {
    std::vector<MountEntry> out;
    for (const std::string& line : util::splitLines(content)) {
        const auto f = util::splitWS(line);
        if (f.size() < 3) continue;  // 畸形行
        out.push_back({f[0], decodeMountEscapes(f[1]), f[2]});
    }
    return out;
}

std::vector<MountEntry> filterMounts(const std::vector<MountEntry>& raw) {
    std::vector<MountEntry> out;
    std::set<std::string> seenDev;
    for (const MountEntry& m : raw) {
        if (m.dev.rfind("/dev/", 0) != 0) continue;   // 非 /dev/ 开头
        if (isVirtualFs(m.fs)) continue;              // 黑名单 / fuse.*
        if (!seenDev.insert(m.dev).second) continue;  // 同 dev 多挂载点 → 保留第一个
        out.push_back(m);
    }
    return out;
}

std::string decodeMountEscapes(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        // \ooo 三位八进制转义（\040 空格、\011 制表、\134 反斜杠 …）
        if (s[i] == '\\' && i + 3 < s.size()) {
            bool octal = true;
            int v = 0;
            for (size_t j = i + 1; j < i + 4; ++j) {
                if (s[j] < '0' || s[j] > '7') { octal = false; break; }
                v = v * 8 + (s[j] - '0');
            }
            if (octal) {
                out.push_back(static_cast<char>(v));
                i += 3;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

std::map<std::string, std::pair<uint64_t, uint64_t>>
parseDiskStats(std::string_view content) {
    std::map<std::string, std::pair<uint64_t, uint64_t>> out;
    for (const std::string& line : util::splitLines(content)) {
        const auto f = util::splitWS(line);
        if (f.size() < 10) continue;  // 畸形行
        // 1-based：name=3，sectors_read=6，sectors_written=10。
        const auto rd = util::parseUll(f[5]);
        const auto wr = util::parseUll(f[9]);
        if (!rd || !wr) continue;
        out[f[2]] = {*rd, *wr};
    }
    return out;
}

bool DiskCollector::probe() {
    try {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator("/sys/block", ec)) {
            if (!isFilteredBlockDevice(entry.path().filename().string())) return true;
        }
    } catch (...) {
    }
    return false;
}

DiskSample DiskCollector::sample() {
    DiskSample s;
    try {
        std::error_code ec;
        const auto stats =
            parseDiskStats(util::readFile("/proc/diskstats").value_or(""));
        const auto mounts = filterMounts(
            parseMountsRaw(util::readFile("/proc/self/mounts").value_or("")));

        // —— 块设备视图 ——
        for (const auto& entry : fs::directory_iterator("/sys/block", ec)) {
            const std::string dev = entry.path().filename().string();
            if (isFilteredBlockDevice(dev)) continue;

            DiskBlock b;
            b.dev = dev;
            // size = 512B 扇区数 → KiB。
            b.sizeKiB = util::readUllFile("/sys/block/" + dev + "/size").value_or(0) / 2;
            const auto it = stats.find(dev);
            if (it != stats.end()) {
                b.rSectors = it->second.first;
                b.wSectors = it->second.second;
            }  // 不存在 → 计数 0，本轮速率由 hub 判无效

            // 该盘分区：/sys/block/<d>/<part>/ 存在 partition 文件。
            std::set<std::string> partNames;
            std::error_code ec2;
            for (const auto& sub : fs::directory_iterator("/sys/block/" + dev, ec2)) {
                if (util::readUllFile(sub.path().string() + "/partition")) {
                    partNames.insert(sub.path().filename().string());
                }
            }

            // 盘容量条：分区挂载点的 statvfs used 之和（按挂载点去重）；无挂载 → invalid。
            double usedKiB = 0.0;
            bool anyMount = false;
            std::set<std::string> counted;
            for (const MountEntry& m : mounts) {
                if (!partNames.count(basenameOfDev(m.dev))) continue;
                if (!counted.insert(m.mount).second) continue;
                if (const auto u = statFsKiB(m.mount)) {
                    usedKiB += u->first;
                    anyMount = true;
                }
            }
            if (anyMount && b.sizeKiB > 0) {
                b.usage.value = util::clamp(usedKiB, 0.0, static_cast<double>(b.sizeKiB));
                b.usage.max = static_cast<double>(b.sizeKiB);
                b.usage.valid = true;
            }
            s.blocks.push_back(std::move(b));
        }
        std::sort(s.blocks.begin(), s.blocks.end(),
                  [](const DiskBlock& a, const DiskBlock& c) { return a.dev < c.dev; });

        // —— 挂载点视图 ——
        for (const MountEntry& m : mounts) {
            DiskMount dm;
            dm.mount = m.mount;
            dm.fs = m.fs;
            if (const auto u = statFsKiB(m.mount)) {
                dm.usage.value = util::clamp(u->first, 0.0, u->second);
                dm.usage.max = u->second;
                dm.usage.valid = u->second > 0;
            }  // statvfs 失败 → invalid，条目保留
            s.mounts.push_back(std::move(dm));
        }
    } catch (...) {
        // 契约：不抛异常；失败时返回已收集部分/空。
    }
    return s;
}

}  // namespace hwmon
