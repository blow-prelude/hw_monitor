# hw_monitor 设计文档

- 版本：v0.1（2026-10-23）
- 状态：已评审待实现（三项关键决策已由需求方拍板，见 §2）
- 阅读顺序建议：§2 决策 → §3 架构 → §5 采集层（数据源权威表）→ §6 显示层
- **分层详细设计**（实现以分层文档为准）：
  - [design/01-core.md](design/01-core.md) 核心层 —— 接口契约、数据模型、MonitorHub
  - [design/02-collectors.md](design/02-collectors.md) 采集层 —— 各硬件采集器实现规格
  - [design/03-tui.md](design/03-tui.md) 显示层 —— 组件树、进度条/折线图、刷新模型

## 1. 目标与非目标

### 目标

- 实时监控 CPU / GPU / NPU / 内存 / 硬盘 / 网络的参数与当前状态，TUI 呈现。
- 目标平台：Debian 系 Linux（开发机 Ubuntu 22.04 jammy 为基准）。
- 启动时自动检测硬件，默认启用全部检测到的监控项。
- 对系统**只读不写**（不可违反的底线，见 §8）。

### 非目标（本期）

- GUI 模式（架构已为其预留：显示层可替换，core/collectors 复用）。
- 配置文件、远程监控、历史持久化（保持"零写入"纯净性）。
- 非 RK 系列 NPU 的深度适配（识别后留白，见 §5.3）。

## 2. 已拍板的决策

| # | 决策 | 内容 |
|---|---|---|
| 1 | TUI 框架 | FTXUI，已 vendored 至 `3rdparty/FTXUI`（v7.0.3+），CMake `add_subdirectory` 引入（系统 cmake 3.22.1 满足其默认要求） |
| 2 | NVIDIA GPU | 通过 NVML（C API），运行时 `dlopen("libnvidia-ml.so.1")`，无卡/无驱动时优雅降级，不产生编译期依赖 |
| 3 | NPU | 先识别型号：仅 **RK（Rockchip）系列**做实际读取（纯 sysfs，无子进程）；其他型号留白 |
| 4 | 进度条统一 | GPU/NPU 频率、内存/硬盘容量与 CPU 频率采用**同一套进度条规范**（§6.2） |

## 3. 总体架构

### 3.1 分层与依赖方向

```
                    ┌──────────────────────────────┐
   渲染，不读系统    │ 显示层 src/tui/               │ ← 未来 src/gui/ 平级新增
                    └──────────────┬───────────────┘
                                   │ 只依赖 core 接口与视图类型
                    ┌──────────────┴───────────────┐
   接口 + 模型      │ 核心层 src/core/               │
   轮询/历史/差分    │  ICollector 纯虚接口族         │
                    │  Sample 快照结构体             │
                    │  MonitorHub（探测/注册/轮询）   │
                    └──────────────┬───────────────┘
                                   │ 实现 core 接口
                    ┌──────────────┴───────────────┐
   只读系统数据源    │ 采集层 src/collectors/         │
                    │  cpu/ gpu/ npu/ mem/ disk/ net/│
                    └──────────────────────────────┘
```

- 依赖方向**单向向下**；显示层不 include 任何采集器头文件，采集器不 include 任何 UI 头文件。
- 具体实现与接口的绑定只发生在 `src/app/main.cpp`（依赖注入），任何一层可独立替换。

### 3.2 线程模型与数据流

```
[采集线程]                    [UI 线程（主）]
MonitorHub::pollOnce()        ftxui 主循环
  ├─ 逐域调 collector->sample()   ├─ ~50ms 响应按键/终端事件
  ├─ 与上一轮计数差分出速率        ├─ 每帧从 MonitorHub 拷贝最新视图（小结构，开销可忽略）
  ├─ 追加各域环形历史              └─ 渲染（进度条/折线图）
  └─ 每 1s 一轮
        └── mutex 保护 ──────────────┘
```

- 采集间隔默认 **1s**（网络折线图 1 点/秒，窗口 120 点 = 2 分钟）。
- v1 单采集线程顺序轮询即可（/proc 读取 <1ms；NVML 每轮 ~ms 级）；若未来某采集器变慢，再按域拆线程，接口不变。
- 速率类指标（网络吞吐、磁盘 IO）由 **MonitorHub 差分**派生：采集器只返回原始计数器与单调时间戳。差分公式 `rate = (cur - prev) / Δt`，计数器回绕/回退时该点作废置 invalid。

## 4. 核心层（src/core/）

### 4.1 接口族

```cpp
// collector.h —— 所有采集器的根接口
class ICollector {
public:
    virtual ~ICollector() = default;
    virtual const char* domain() const = 0;  // "cpu"/"gpu"/"npu"/"mem"/"disk"/"net"
    virtual bool probe() = 0;                // 只读探测硬件是否可监控（启动时调一次）
};

// 每域一个子接口（示例，其余同理）
class ICpuCollector : public ICollector {
public:
    virtual CpuInfo  info() const = 0;   // 静态信息：架构/型号/逻辑核数（probe 后缓存）
    virtual CpuSample sample() = 0;      // 动态快照（不可得项置 invalid）
};
```

六个域接口：`ICpuCollector / IGpuCollector / INpuCollector / IMemCollector / IDiskCollector / INetCollector`。**禁止**绕过接口直接把采集器类型泄漏给显示层。

### 4.2 数据模型

```cpp
// 通用"当前/最大"型指标：所有进度条的统一数据载体
struct Gauge {
    double value = 0;      // 当前值（单位随指标：MHz / KiB / % 等）
    double max   = 0;
    bool   valid = false;  // false => 该项数据不可得，UI 留白显示 "—"
};

struct CpuSample {                 // —— CPU
    std::vector<Gauge> coreFreq;   // 每核：value=当前频率, max=该核最大频率（无 cpufreq 则 valid=false）
    Gauge totalUsage;              // 总使用率（value=max=0~100）
};
struct GpuSample {                 // —— GPU（每卡一个）
    std::string name;
    Gauge util, smClock, memUsed;  // 使用率 / SM频率 / 显存（显存以 MiB 计）
    int   temperature = -1;        // ℃，-1=不可得
};
struct NpuSample {                 // —— NPU
    bool supported = false;        // 是否为受支持型号（RK 系列）
    std::string model;             // 如 "Rockchip RK3588 NPU"；未识别为空
    Gauge freq;                    // devfreq 当前/最大频率
    Gauge load;                    // 整体负载 %；debugfs 不可读 => invalid（留白）
};
struct MemSample  { Gauge used, swap; uint64_t totalKiB=0, availKiB=0; };
struct DiskEntry  { std::string dev, mount, fs; Gauge usage;                 // 挂载点视图
                    uint64_t rBytes=0, wBytes=0; };                          // IO 原始计数器（差分成速率）
struct NetIface   { std::string name, state;                                 // state: up/down
                    uint64_t rxBytes=0, txBytes=0; };                        // 原始计数器
struct NetSample  { std::vector<NetIface> ifaces; };
```

### 4.3 MonitorHub

```cpp
class MonitorHub {   // core 层唯一的有状态组件
public:
    void setCpu(std::unique_ptr<ICpuCollector> c);   // main 注入；未注入的域 = 未检测到
    // ... setGpu / setNpu / setMem / setDisk / setNet
    void start(std::chrono::milliseconds interval = 1s);   // 启动采集线程
    void stop();
    // —— UI 线程调用（mutex 保护，返回拷贝）——
    CpuView cpu() const;    // 静态信息 + 最新快照
    GpuView gpu() const;    // 每卡快照
    NetView net() const;    // 每接口：最新速率 + double[120] 历史序列（折线图直接可用）
    // mem()/disk()/npu() 同理
};
```

- `probe()` 由 main 在注册阶段调用：探测成功的采集器才注入 hub，即实现"自动检测、默认全开"。
- 环形历史与速率差分都在 hub 内（显示层零状态、纯渲染，GUI 可直接复用）。

## 5. 采集层（src/collectors/）

### 5.1 CPU（`/proc` + `/sys`）

| 数据 | 来源 | 说明 |
|---|---|---|
| 架构 | `uname()` machine | x86_64 / aarch64 … |
| 型号 | `/proc/cpuinfo` | x86：`model name`；ARM：`CPU implementer`+`CPU part` 查内置解码表（0x41=ARM、0x51=高通、0x48=海思…），未命中显示原始 hex |
| 每核频率 | `/sys/devices/system/cpu/cpuN/cpufreq/scaling_cur_freq`（kHz） | **优先 scaling_cur_freq**；`cpuinfo_cur_freq` 常为 root-only，不依赖 |
| 每核最大频率 | 同目录 `cpuinfo_max_freq` | 进度条分母 |
| 总使用率 | `/proc/stat` 首行差分 | `(总-空闲)/总`，相邻两轮计算 |

探测条件：`/proc/stat` 可读（恒真）。VM/容器无 cpufreq 时：使用率正常显示，频率条全 valid=false 留白。

### 5.2 GPU

| 卡 | 数据 | 来源 |
|---|---|---|
| NVIDIA（决策 2） | 型号/使用率/SM频率+最大值/显存 used+total/温度 | NVML：`nvmlDeviceGetName / GetUtilizationRates / GetClockInfo+GetMaxClockInfo(CLOCK_SM) / GetMemoryInfo / GetTemperature` |
| AMD | 型号/使用率/显存 | `/sys/class/drm/card*/device/`：`gpu_busy_percent`、`mem_info_vram_{used,total}`、`product_name`；频率多需 root → 留白 |
| Intel i915 | 仅识别 | 数据留白（后续版本） |

NVIDIA 探测：`dlopen("libnvidia-ml.so.1")` 成功且 `nvmlDeviceGetCount>0`。任一步失败 → 整域隐藏（视为无 GPU），**永不导致崩溃或编译失败**。多卡：逐卡全部显示。

### 5.3 NPU（决策 3）

```
型号识别:  读 /proc/device-tree/compatible（无此文件 = 无设备树，如 x86 → 未识别）
           ├── 含 "rockchip,rk35*" 前缀 → RK 系列，启用 RK NPU 采集器
           └── 其他/无匹配            → 显示"NPU：未识别（留白）"，不读取任何硬件
```

RK 系列数据源（全部 sysfs，**零子进程**）：

| 数据 | 来源 | 降级 |
|---|---|---|
| 型号 | compatible 字符串映射（rk3588→"RK3588 NPU（3 核）"等） | — |
| 频率 | `/sys/class/devfreq/*npu*/cur_freq` 与 `max_freq`（Hz） | 不可读 → 频率条留白 |
| 每核负载 | `/sys/kernel/debug/rknpu/load` | debugfs 常需 root，不可读 → 负载留白（仅显示型号+频率） |

devfreq 节点按名字匹配 `*npu*`（RK3588 为 `fdab0000.npu`），不写死地址。昇腾等其他 NPU 留待后续版本（届时评估 `npu-smi info` 只读子进程白名单，本期无任何子进程调用）。

### 5.4 内存

| 数据 | 来源 |
|---|---|
| used/total | `/proc/meminfo`：`MemTotal`、`MemAvailable`（used = Total − Available，与 htop 口径一致） |
| swap | `SwapTotal`、`SwapFree` |

### 5.5 硬盘

两个子视图：

1. **块设备**（容量进度条 + IO 速率）：遍历 `/sys/block/`（过滤 `loop* ram* zram* sr*`），容量 = `/sys/block/<d>/size`×512；IO 计数取 `/proc/diskstats` 同名条目（分区归并到父设备，父子关系查 `/sys/class/block/<part>/`）。
2. **挂载点**（使用率进度条）：`/proc/self/mounts` 过滤真实设备（`/dev/` 开头、非 bind）与伪文件系统；用量 `statvfs()`。

探测条件：`/sys/block` 下存在未被过滤的设备。

### 5.6 网络

- 接口与计数器：`/proc/net/dev`（排除 `lo`），字段取收发字节数。
- 状态：`/sys/class/net/<if>/operstate`（up/down，down 的接口速率照常显示为 0）。
- 吞吐 = 相邻两轮字节差 ÷ 间隔（hub 完成），送折线图。
- v1 不做 per-interface 过滤配置，全部非 lo 接口都显示（纵向堆叠，超出滚动）。

## 6. 显示层（src/tui/）

### 6.1 布局线框

```
┌ hw_monitor ──────────────────────────────────────────────────────┐
│ CPU  x86_64 · AMD Ryzen 9 7950X · 32T                     总占用 42%│
│  C0 ▕█████████░░▏4.2GHz   C1 ▕█████░░░░░▏3.1GHz   C2 ▕██░░░░░░░▏1.4GHz│
│  …（每核一个进度条，进度 = 当前频率 / 该核最大频率，自动折行）          │
│                                                                  │
│ GPU  NVIDIA GeForce RTX 4060                                     │
│  使用率 ▕██████░░░░▏62%     核心频率 ▕███████░░▏1.9/2.5GHz         │
│  显存   ▕███████░░░▏6.1/8.0G     温度 64°C                        │
│ NPU  Rockchip RK3588 NPU（3 核）                                  │
│  频率 ▕█████░░░░░▏600/1000MHz    负载 core0 42% · core1 35% · core2 28%│
│                                                                  │
│ 内存 ▕█████████░░░▏ 21.3G / 31.2G        SWAP ▕█░░░░░░░▏1.2G / 8.0G│
│ 硬盘 nvme0n1  ▕██████░░░▏412G / 931G   R 85.2MB/s · W 12.4MB/s    │
│      /(ext4)  ▕███████░░▏ 612G / 931G                             │
│                                                                  │
│ 网络  eth0 (up)     RX 12.3MB/s      TX 2.1MB/s                   │
│  RX ▁▂▃▅▇▅▃▂▁▂▄▆▇▅▃▂▁▂   TX ▂▃▂▁▁▂▃▂▁▁▁▂▃▄▂▁▁             (120s)│
└─ q 退出 · ↑↓ 滚动 · 1s 刷新 ───────────────────────────────────────┘
```

FTXUI 组件树：`vbox{ header, cpuSection, hbox{gpuSection, npuSection}, memDiskSection, netSection(n个), footer }`，外层 `Renderer | CatchEvent`，支持任意终端尺寸（内容超出可滚动）。

### 6.2 进度条统一规范（决策 4）

所有"当前/最大"型指标走同一 Gauge → 进度条渲染函数，仅配色策略不同：

| 类别 | 指标 | 配色 | 语义 |
|---|---|---|---|
| 容量类 | 内存/SWAP/显存/硬盘容量 | 绿 <70% · 黄 70–90% · 红 ≥90% | 接近满 = 风险，需告警色 |
| 频率/速率类 | CPU/GPU/NPU 频率、使用率 | 青色单色填充 | 高频≠风险，无告警语义 |
| 数据不可得 | 任何 valid=false | 空条 + "—" | 自动留白 |

### 6.3 折线图组件（LineChart）

- 输入：`double[120]` 速率序列（hub 供给），每接口 RX/TX 两条线。
- 实现：FTXUI `Canvas` 自绘，自动纵轴缩放（取窗口内最大值向上取整到 1/2/5×10ⁿ），横轴固定 120s。
- 数值格式化：B/s → 自动 KB/MB/GB（保留 1 位小数）。
- 图例：RX/TX 双色，右上角当前值。

### 6.4 交互

| 键 | 动作 |
|---|---|
| `q` / `Ctrl+C` | 退出（先 stop 采集线程再退出） |
| `↑` `↓` / `PgUp` `PgDn` | 滚动查看（核多/卡多/接口多时） |

## 7. 错误处理与降级

- 采集层**不抛异常跨接口**：任何文件缺失/解析失败 → 对应 `Gauge.valid=false` 或条目缺失，返回完整快照。
- 某域连续 3 轮采样全失败 → 该域显示"不可用"，采集线程继续跑其余域。
- dlopen/NVML 调用全部判空；解析器对畸形行跳过不崩溃。
- 显示层对任何 invalid/空数据都有确定的留白渲染，不留空白洞。

## 8. 只读保障清单（评审必查）

1. 全部系统文件以 `std::ifstream` / `open(O_RDONLY)` 打开，源码中**禁止出现**写模式 open / `write()` / `system()` / `popen()`（本期连只读子进程也没有）。
2. 仅使用只读系统调用：`read / stat / statvfs / uname`；不使用任何 ioctl 写请求（本期连 ioctl 都不需要）。
3. 不创建、不修改任何文件（含用户家目录——无配置、无缓存、无日志文件）。
4. NVML 仅调用 query 类 API；`nvmlInit` 属于库初始化，不改变硬件状态。

## 9. 项目结构与构建

```
CMakeLists.txt            # 根：C++17、GCC、add_subdirectory(3rdparty/FTXUI)、Threads
3rdparty/FTXUI/           # vendored 依赖（勿修改）
src/core/CMakeLists.txt   # 库 hw_core（collector.h / snapshot.h / monitor_hub.* / registry）
src/collectors/           # 库 hw_collectors：cpu/ gpu/ npu/ mem/ disk/ net/ 各一采集器
src/tui/                  # 库 hw_tui：app_ui.* progress_bar.* line_chart.* sections/…
src/app/                  # 可执行 hw_monitor：main.cpp（探测 + 依赖注入 + 启动）
test/                     # doctest（header-only）+ fixtures/（样本 /proc、/sys 文件）
```

```bash
# 构建（GCC 工具链）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # FTXUI 来自 3rdparty/FTXUI（add_subdirectory）
cmake --build build -j$(nproc)
./build/hw_monitor                                # 运行 TUI
ctest --test-dir build                            # 单测（解析器纯函数 + fixtures）
```

测试策略：解析函数全部写成"文件路径/istream → 结构体"的纯函数，用 `test/fixtures/` 下的静态样本（含畸形行样本）覆盖；采集线程与 UI 不做自动化测试，靠运行验收。

## 10. 里程碑

| 阶段 | 内容 | 验收 |
|---|---|---|
| M0 | CMake 骨架 + core 接口/快照/hub 空实现 + TUI 空界面 | 空界面能启动、q 退出 |
| M1 | CPU + 内存采集与渲染（每核频率条、总占用、内存条） | 本机与无 cpufreq 的 VM 各验证一次 |
| M2 | 网络 + 硬盘（折线图、容量条、IO 速率） | eth0 折线随 iperf/下载变化 |
| M3 | GPU（NVML dlopen，N/A 时整域隐藏） | 有 N 卡机器验证；无卡机器确认不崩 |
| M4 | NPU（DT 识别，RK 读取，其他留白） | RK3588 实机；x86 机器确认留白 |
| M5 | 打磨：滚动、resize、配色、降级文案 | 全尺寸终端 + 异常路径走查 |

## 11. 参考资料

- FTXUI：<https://github.com/ArthurSonzogni/FTXUI>（组件/Canvas 文档见 wiki）
- NVML：<https://developer.nvidia.com/nvidia-management-library-nvml>（头文件与 API 参考）
- cpufreq sysfs ABI：<https://docs.kernel.org/admin-guide/ABI/testing/sysfs-devices-system-cpu.html>
