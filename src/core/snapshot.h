#pragma once
// 数据模型（01-core §5 / §6，权威定义）。
//
// 单位约定（换算只发生一次，见 01-core §3）：
//   频率 MHz；内存/容量/显存 KiB；网络计数与速率 字节（/秒）；
//   使用率/负载 %（0–100，double）；温度 ℃（int，不可得 = -1）。

#include <cstdint>
#include <string>
#include <vector>

namespace hwmon {

// 一切进度条的数据载体：当前/最大 + 有效性。
// valid == true 时约定 max > 0 且 0 <= value <= max（采集层负责钳制）；
// valid == false 时 value/max 无意义，接收方不得解读（UI 显示留白"—"）。
struct Gauge {
    double value = 0;      // 当前值
    double max   = 0;      // 满刻度值；使用率类指标 value==max 有效时恒为 100
    bool   valid = false;  // false ⇒ 该项本轮不可得，UI 显示留白"—"
};

// —— CPU ——
struct CpuInfo {
    std::string arch;                // uname().machine，如 "x86_64" / "aarch64"
    std::string model;               // 解码后型号；大小核时 "4×Cortex-A76 + 4×Cortex-A55" 形式
    int         nLogicalCores = 0;
    std::vector<double> maxFreqMHz;  // 每逻辑核最大频率；无 cpufreq 时为空
};
struct CpuSample {
    std::vector<Gauge> coreFreq;   // size == nLogicalCores（在线核）
    Gauge              totalUsage; // 总使用率 %
};

// —— GPU（一次覆盖所有卡）——
struct GpuCardInfo {
    std::string name;    // 型号
    std::string vendor;  // "NVIDIA"/"AMD"/"Intel"
};
struct GpuInfo { std::vector<GpuCardInfo> cards; };
struct GpuSample {                     // 与 GpuInfo::cards 一一对应
    std::string name;
    Gauge util;            // 使用率 %
    Gauge smClock;         // 当前/最大 SM 频率 MHz；AMD 不可得 → invalid
    Gauge memUsed;         // 显存已用 KiB（max=总量）
    int   temperature = -1;
};

// —— NPU ——
struct NpuInfo {
    bool supported = false;
    std::string model;     // 未识别: model = "未识别"
};
struct NpuSample {
    bool  supported = false;
    Gauge freq;            // devfreq 当前/最大频率 MHz
    Gauge load;            // 整体负载 %；debugfs 不可读 → invalid
};

// —— 内存 ——
struct MemSample {
    Gauge used;            // KiB（max = MemTotal；used = Total − Available）
    Gauge swap;            // KiB（max = SwapTotal；无 swap → invalid）
};

// —— 硬盘 ——
struct DiskBlock {                       // 块设备行
    std::string dev;                     // 如 "nvme0n1"
    uint64_t    sizeKiB = 0;
    Gauge       usage;                   // 该盘上已挂载文件系统 used 之和 / 盘容量；无挂载 → invalid
    uint64_t    rSectors = 0, wSectors = 0;   // /proc/diskstats 原始扇区计数（hub 差分用，×512=字节）
};
struct DiskMount {                       // 挂载点行
    std::string dev;                     // 挂载来源设备名（如 "nvme0n1p2"）；无 /dev/ 来源为空
    std::string mount, fs;               // 如 "/", "ext4"
    Gauge       usage;                   // statvfs：used KiB / total KiB
};
struct DiskSample {
    std::vector<DiskBlock> blocks;
    std::vector<DiskMount> mounts;
};

// —— 网络 ——
struct NetIface {
    std::string name;                    // 不含 "lo"
    std::string state;                   // "up"/"down"/其他 operstate 原文
    uint64_t    rxBytes = 0, txBytes = 0;     // 原始计数（差分用）
};
struct NetSample { std::vector<NetIface> ifaces; };

// ==================== 视图（给显示层的只读快照，01-core §6） ====================

constexpr int kHistPoints = 120;         // 折线图窗口 120 点（1s 采样 = 2 分钟）

// 时间序历史：chronological（旧→新），size() <= kHistPoints；拷出 hub，UI 零状态
struct Series { std::vector<double> points; };

struct CpuView { CpuInfo info; CpuSample latest; };
struct GpuView { GpuInfo info; std::vector<GpuSample> latest; };
struct NpuView { NpuInfo info; NpuSample latest; };
struct MemView { MemSample latest; };

struct DiskView {
    std::vector<DiskBlock> blocks;       // 附 diff 后速率：
    std::vector<double> readBps, writeBps;   // 与 blocks 平行；无效点 = -1
    std::vector<DiskMount> mounts;
};
struct NetIfaceView {
    NetIface latest;
    double rxBps, txBps;                 // 首轮/无效 = -1
    Series rxHist, txHist;               // 环形展开后的时间序
};
struct NetView { std::vector<NetIfaceView> ifaces; };

}  // namespace hwmon
