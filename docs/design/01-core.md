# 核心层（src/core/）详细设计

- 上游文档：[DESIGN.md](../DESIGN.md)（总体架构）、[02-collectors.md](02-collectors.md)（采集层）、[03-tui.md](03-tui.md)（显示层）
- 本文是核心层的**权威实现规格**，与 DESIGN.md 冲突时以本文为准。

## 1. 定位与边界

核心层 = **接口契约 + 数据模型 + MonitorHub（轮询/差分/历史）**。它不读任何系统文件（读取全在采集层），不含任何 UI 概念（显示全在 TUI 层）。

| 属于本层 | 不属于本层 |
|---|---|
| `ICollector` 接口族（纯虚） | 打开 /proc、/sys 文件 |
| Sample / View 数据结构 | 单位换算成"好看的字符串"（那是 TUI 的事） |
| Gauge 通用指标载体 | 绘制进度条 / 折线图 |
| RingBuffer 环形历史 | 配置文件读写（本软件零写入） |
| MonitorHub：线程、轮询节奏、速率差分 | 硬件探测逻辑（探测在采集器 `probe()` 内实现，hub 只负责调度） |

## 2. 文件清单

```
src/core/
├── collector.h      ICollector + 六个域接口（仅头文件）
├── snapshot.h       Info / Sample / View 结构体 + Gauge（仅头文件）
├── ring_buffer.h    RingBuffer<T> 环形缓冲模板（仅头文件）
├── monitor_hub.h
└── monitor_hub.cpp  MonitorHub 实现（采集线程、差分、视图快照）
```

编译为静态库 `hw_core`，不链接 FTXUI、不链接 dl。

## 3. 单位与通用约定

**所有跨层传递的数值使用下表单位，换算只发生一次**（采集层负责把 sysfs 原始单位换算到本表；显示层负责把本表单位格式化成人类可读字符串）：

| 量 | 单位 | 备注 |
|---|---|---|
| 频率 | MHz | sysfs cpufreq/devfreq 为 kHz/Hz，采集层换算 |
| 内存 / 容量 / 显存 | KiB | meminfo 原生单位；NVML 字节 → 采集层 ÷1024 |
| 网络字节计数 | 字节 | /proc/net/dev 原生 |
| 速率 | 字节/秒 | hub 差分派生 |
| 使用率 / 负载 | %（0–100，double） | |
| 温度 | ℃（int，不可得 = -1） | |

**Gauge —— 一切进度条的数据载体**：

```cpp
struct Gauge {
    double value = 0;      // 当前值
    double max   = 0;      // 满刻度值；使用率类指标 value==max 有效时恒为 100
    bool   valid = false;  // false ⇒ 该项本轮不可得，UI 显示留白"—"
};
```

约定：`valid == true` 时 `max > 0` 且 `0 <= value <= max`（采集层负责钳制）。`valid == false` 时 value/max 无意义，接收方不得解读。

## 4. 接口契约（collector.h）

```cpp
namespace hwmon {

class ICollector {
public:
    virtual ~ICollector() = default;
    virtual const char* domain() const = 0;  // "cpu"/"gpu"/"npu"/"mem"/"disk"/"net"
    virtual bool probe() = 0;                // 只读探测；启动阶段由 main 调一次
    // 注意：sample() / info() 属于各域子接口，不放在根接口（避免 void* 泄漏类型）
};

class ICpuCollector : public ICollector {
public:
    virtual CpuInfo    info() const = 0;     // 静态信息，probe() 后即可调用，结果可缓存
    virtual CpuSample  sample() = 0;         // 动态快照；不可得项 valid=false
};
// IGpuCollector / INpuCollector / IMemCollector / IDiskCollector / INetCollector 同构：
// XInfo info() const;  XSample sample();
}  // namespace hwmon
```

实现纪律：

- `sample()` **不得抛异常、不得阻塞超过轮询周期**（当前 1s）；读文件失败一律落为 `valid=false` / 条目缺失。
- `info()` 可被多次调用且必须幂等；建议 probe 时缓存。
- 采集器对象只被采集线程访问（hub 保证），**无需自身加锁**。

## 5. 数据模型（snapshot.h，权威定义）

```cpp
namespace hwmon {

// —— CPU ——
struct CpuInfo {
    std::string arch;              // uname().machine，如 "x86_64" / "aarch64"
    std::string model;             // 解码后型号；大小核时 "4×Cortex-A76 + 4×Cortex-A55" 形式
    int         nLogicalCores = 0;
    std::vector<double> maxFreqMHz;  // 每逻辑核最大频率；无 cpufreq 时为空
};
struct CpuSample {
    std::vector<Gauge> coreFreq;   // size == nLogicalCores（在线核）
    Gauge              totalUsage; // 总使用率 %
};

// —— GPU（一次覆盖所有卡）——
struct GpuCardInfo { std::string name; std::string vendor; };  // vendor: "NVIDIA"/"AMD"/"Intel"
struct GpuInfo  { std::vector<GpuCardInfo> cards; };
struct GpuSample {                             // 与 GpuInfo::cards 一一对应
    std::string name;
    Gauge util;            // 使用率 %
    Gauge smClock;         // 当前/最大 SM 频率 MHz；AMD 不可得 → invalid
    Gauge memUsed;         // 显存已用 KiB（max=总量）
    int   temperature = -1;
};

// —— NPU ——
struct NpuInfo { bool supported = false; std::string model; };   // 未识别: model = "未识别"
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
    std::string mount, fs;               // 如 "/", "ext4"
    Gauge       usage;                   // statvfs：used KiB / total KiB
};
struct DiskSample { std::vector<DiskBlock> blocks; std::vector<DiskMount> mounts; };

// —— 网络 ——
struct NetIface {
    std::string name;                    // 不含 "lo"
    std::string state;                   // "up"/"down"/其他 operstate 原文
    uint64_t    rxBytes = 0, txBytes = 0;     // 原始计数（差分用）
};
struct NetSample { std::vector<NetIface> ifaces; };

}  // namespace hwmon
```

## 6. 视图（给显示层的只读快照）

```cpp
namespace hwmon {

constexpr int kHistPoints = 120;         // 折线图窗口 120 点（1s 采样 = 2 分钟）

// 时间序历史：chronological（旧→新），size() <= kHistPoints；拷贝出hub，UI 零状态
struct Series { std::vector<double> points; };

struct CpuView { CpuInfo info; CpuSample latest; };
struct GpuView { GpuInfo info; std::vector<GpuSample> latest; };
struct NpuView { NpuInfo info; NpuSample latest; };
struct MemView { MemSample latest; };

struct DiskView  { std::vector<DiskBlock> blocks;              // 附 diff 后速率：
                   std::vector<double> readBps, writeBps;      // 与 blocks 平行；无效点 = -1
                   std::vector<DiskMount> mounts; };
struct NetIfaceView { NetIface latest; double rxBps, txBps;    // 首轮/无效 = -1
                      Series rxHist, txHist; };                // 环形展开后的时间序
struct NetView  { std::vector<NetIfaceView> ifaces; };

}  // namespace hwmon
```

## 7. RingBuffer（ring_buffer.h）

```cpp
template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity);
    void push(T v);                       // 满则覆盖最旧
    size_t size() const;
    // 按 时间序 展开拷出（旧→新）；O(n)
    std::vector<T> snapshot() const;
private:
    std::vector<T> buf_; size_t head_ = 0, count_ = 0;   // head_ = 最旧元素下标
};
```

单线程使用（仅采集线程写、拷出时持 hub 锁），无需内部加锁。单测覆盖：空、未满、恰好满、覆盖回绕。

## 8. MonitorHub 详设（monitor_hub.h/.cpp）

### 8.1 公开接口

```cpp
class MonitorHub {
public:
    // —— main 线程：注册（探测成功的采集器才注入；未注入的域 = 该机器没有）——
    void setCpu(std::unique_ptr<ICpuCollector> c);   // 同型：setGpu/setNpu/setMem/setDisk/setNet
    void start(std::chrono::milliseconds interval = std::chrono::seconds{1});
    void stop() noexcept;                 // 幂等；join 采集线程

    // —— UI 线程：互斥保护，返回深拷贝 ——
    std::optional<CpuView> cpu() const;   // 域未注册/不可用 → nullopt（UI 整节隐藏）
    std::optional<GpuView> gpu() const;
    std::optional<NpuView> npu() const;
    std::optional<MemView> mem() const;
    std::optional<DiskView> disk() const;
    std::optional<NetView> net() const;
};
```

### 8.2 内部状态（全由 `mutable std::mutex mu_` 保护）

```cpp
std::thread worker_;  std::atomic<bool> running_{false};
std::chrono::milliseconds interval_;

std::unique_ptr<ICpuCollector> cpu_;   // …六域指针
std::optional<CpuView> cpuView_;       // …六域视图（采集线程写）

// 差分所需上一轮状态（仅采集线程访问，不与 UI 共享，无需入锁）：
struct PrevNet { bool has=false; std::chrono::steady_clock::time_point t;
                 std::map<std::string, std::pair<uint64_t,uint64_t>> ctr; };
struct PrevDisk { bool has=false; std::chrono::steady_clock::time_point t;
                  std::map<std::string, std::pair<uint64_t,uint64_t>> ctr; };
std::map<domain, int> failStreak_;     // 连续失败轮数（见 8.4）
```

### 8.3 采集线程主循环

```
start(): running_=true; worker_ = thread(loop)
loop():
  next = steady_clock::now()
  while (running_):
    pollOnce()                       // 全域采样 + 差分 + 更新视图（持锁写视图）
    next += interval_
    sleep_until(next)                // 分段睡眠：每 100ms 检查 running_，便于快速 stop
```

`pollOnce()` 流程：

1. 逐域调 `sample()`；`t = steady_clock::now()`。
2. **网络差分**：对每个 iface，若上一轮存在同名 iface 且 `t - prev.t > 0` 且新计数 ≥ 旧计数 → `rxBps = (rx - prev.rx) / dt`；否则（首轮 / iface 消失重现 / 计数回退）`rxBps = txBps = -1` 并重置该 iface 的 prev。速率写入 `latest` 并 `push` 进 rxHist/txHist（无效点 push -1，折线图跳过断点）。
3. **磁盘差分**：同上，按 dev 名匹配；`readBps = ΔrSectors×512/dt`。
4. 更新各域 `*View_`（持锁赋值，构造好整体再 swap，缩小临界区）。
5. 某域 `sample()` 抛异常（违反契约的兜底）/ 返回全 invalid → `failStreak_[domain]++`，达 3 → 该域视图置 nullopt 并停止采样该域（线程继续跑其余域）；成功则清零。

### 8.4 stop 与生命周期

- `stop()`：`running_=false` → join。因主循环分段睡眠，最大停止延迟 ~100ms。
- main 中顺序：`hub.stop()` 之后 `screen.Loop()` 返回、进程退出前完成；保证不 exit-while-locked。
- hub 不拷贝、不移动（采集器 unique_ptr 持有），main 中以对象生命周期管理。

### 8.5 精度说明

- 采样节奏用 `steady_clock`，不受系统时间跳变影响；速率计算用实际 `Δt` 而非标称 interval，睡眠漂移不累积误差。
- 折线图点距固定按 1 点/采样轮；轮次延迟只会整体后移，不产生错点。

## 9. 错误处理总则

- 接口边界**无异常**：实现内 catch-all 转为 invalid/缺失；hub 再兜一层。
- 所有 `std::optional<XView>` 访问器在域未注册或已判死时返回 `nullopt`，TUI 据此整节隐藏。
- hub 自身不打印日志（保持零输出纯净）；诊断靠各域降级文案。

## 10. 单元测试要点（配 doctest）

| 目标 | 用例 |
|---|---|
| RingBuffer | 空/未满/满/回绕覆盖/snapshot 顺序 |
| 差分逻辑 | 正常两轮、首轮（-1）、iface 消失重现、计数回退、Δt=0 保护 |
| 失败降级 | 连续 3 次失败后视图变 nullopt；恢复采样其余域 |
| 视图拷贝 | 并发读写用 TSAN 构建跑一遍冒烟 |

差分与降级逻辑需把 `pollOnce` 的依赖（时钟、collector）抽象为可注入 fake（测试构造用，不进产品头文件语义）。
