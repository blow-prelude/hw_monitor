# hw_monitor

Linux 硬件实时监控工具（TUI）：监控 CPU / GPU / NPU / 内存 / 硬盘 / 网络的参数与当前状态。

```
┌ hw_monitor ──────────────────────────────────────────────────────┐
│ CPU  x86_64 · Intel i9-12900K · 24T                       总占用 42%│
│  C0 ▕█████████░░▏4.2GHz   C1 ▕█████░░░░░▏3.1GHz   C2 ▕██░░░░░░░▏…    │
│ GPU  NVIDIA GeForce RTX 3090                                        │
│  使用率 ▕██████░░░░▏62%     核心频率 ▕███████░░▏1.9/2.5GHz            │
│ 内存 ▕█████████░░░▏ 21.3G / 31.2G        硬盘 nvme0n1  R 85.2MB/s   │
│ 网络  eth0 (up)     RX ▁▂▃▅▇▅▃▂▁▂▄▆▇▅▃▂▁▂   TX ▂▃▂▁▁▂▃▂▁▁▁▂▃▄▂▁▁     │
└─ q 退出 · ↑↓ 滚动 · 1s 刷新 ───────────────────────────────────────┘
```

特性：

- **自动检测**：启动时探测本机存在的硬件，检测到的监控项默认全部打开；不存在的域整节隐藏
- **每核一个频率进度条**（当前/该核最大），CPU/GPU/NPU 频率、内存/硬盘/显存容量统一同一套 Gauge 规范
- **网络吞吐折线图**：上下行速率，120s 历史窗口（1 点/秒）
- **优雅降级**：单项不可得显示"—"（如 VM 无 cpufreq、NPU debugfs 无权限）；某域连续 3 轮采样失败自动隐藏该域
- **只读不写**：全部数据源为只读（procfs / sysfs / debugfs / device-tree / NVML），不创建任何文件，无配置、无缓存、无日志

目标平台为 Debian 系 Linux。

## 环境搭建

构建依赖（Debian/Ubuntu）：

```bash
sudo apt update
sudo apt install -y g++ cmake git
```

| 依赖 | 版本要求 | 说明 |
|---|---|---|
| GCC (g++) | ≥ 9（C++17） | 也可用匹配标准的 Clang |
| CMake | ≥ 3.16 | |
| FTXUI | — | 已 vendored 至 `3rdparty/FTXUI`，克隆后**无需额外拉取**，可离线构建 |
| doctest | — | 已 vendored 单头文件至 `test/doctest.h`，仅跑单测时用到 |

可选硬件支持（缺失不报错，对应监控节自动隐藏或留白）：

- **NVIDIA GPU**：安装官方驱动（自带 `libnvidia-ml.so.1`），运行时 dlopen，无编译期依赖；无卡/无驱动时 GPU 节隐藏
- **AMD GPU**：读 `/sys/class/drm/` 相关节点（使用率/显存；频率多需 root → 留白）
- **NPU**：仅 Rockchip RK 系列（需设备树机器）实际读取频率/负载；其他型号仅识别并留白。RK 负载来自 `/sys/kernel/debug/rknpu/load`，常需 root，不可读则负载留白

## Quick Start

```bash
git clone <repo-url> hw_monitor
cd hw_monitor
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/src/app/hw_monitor
```

启动后自动检测并显示全部可用监控项。

按键：

| 键 | 动作 |
|---|---|
| `q` / `Ctrl+C` | 退出（先停采集线程再退出） |
| `↑` `↓` / `PgUp` `PgDn` | 滚动查看（核多/卡多/接口多时） |

终端备选屏（alternate screen）渲染异常时，可用 `--no-alt` 以普通屏模式运行。

## 运行测试

```bash
ctest --test-dir build --output-on-failure    # 三套：core / collectors / tui_render
```

人工验收工具（不入 ctest）：

```bash
./build/test/collectors_smoke   # 真机采集冒烟：打印各域探测/采样结果（只读）
./build/test/hw_tui_demo        # 假采集器驱动的 TUI 全链路演示（真实终端）
```

## 监控项与数据来源

| 域 | 内容 | 来源（全部只读） |
|---|---|---|
| CPU | 架构、型号、逻辑核数；每核频率进度条；总使用率 | `/proc/cpuinfo`、`/proc/stat`、`/sys/.../cpufreq` |
| GPU | 型号、使用率、SM 频率、显存、温度 | NVIDIA：NVML（dlopen）；AMD：`/sys/class/drm/` |
| NPU | 型号识别；RK 系列频率、负载；其他留白 | `/proc/device-tree/compatible`、devfreq、debugfs |
| 内存 | used / swap 容量进度条 | `/proc/meminfo` |
| 硬盘 | 块设备容量与 IO 速率；挂载点使用率 | `/sys/block`、`/proc/diskstats`、`statvfs()` |
| 网络 | 接口状态、上下行速率折线图（过滤 `lo`） | `/proc/net/dev`、`/sys/class/net/*/operstate` |

速率类指标（网络吞吐、磁盘 IO）由核心层用相邻两轮快照差分得出，采集层只上报原始计数。

## 架构

三段式单向依赖：`collectors → core ← tui`，具体绑定只发生在 `src/app`（依赖注入）：

```
src/
├── core/        接口契约（ICollector 族）+ 数据模型（Gauge/快照/视图）
│                + MonitorHub（轮询线程、速率差分、环形历史、失败降级）→ hw_core
├── collectors/  每个硬件域一个只读采集器 → hw_collectors
├── tui/         组件树、进度条、折线图，纯渲染快照零状态 → hw_tui
└── app/         main()：探测 + 注入 + 启动 → hw_monitor
```

显示层不读系统文件，采集层不含 UI；GUI 可复用 core + collectors 平级新增（预留）。

详细设计与实现规格见 [docs/DESIGN.md](docs/DESIGN.md)（总体架构与数据源权威表）及分层规格 [docs/design/01-core.md](docs/design/01-core.md)、[02-collectors.md](docs/design/02-collectors.md)、[03-tui.md](docs/design/03-tui.md)。

## 只读保障

- 所有系统文件以 `std::ifstream` / `open(O_RDONLY)` 打开；源码禁止出现写模式 open、`write()`、`system()`、`popen()`（v1 无任何子进程调用）
- 不创建、不修改任何文件（含用户家目录）
- NVML 仅调用 query 类 API
