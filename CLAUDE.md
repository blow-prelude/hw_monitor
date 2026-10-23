# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

hw_monitor：Linux 硬件实时监控工具，监控 CPU / GPU / NPU / 内存 / 硬盘 / 网络的参数与当前状态。目标平台为 Debian 系 Linux（deb 系发行版）。产品形态分 TUI 与 GUI 两种，**先实现 TUI**，GUI 后续再做。

详细设计文档：[docs/DESIGN.md](docs/DESIGN.md) —— 总体架构与数据源权威表。分层实现规格在其下：[01-core.md](docs/design/01-core.md)（核心层：接口契约、数据模型、MonitorHub）、[02-collectors.md](docs/design/02-collectors.md)（采集层）、[03-tui.md](docs/design/03-tui.md)（显示层）。实现前先读；分层文档与 DESIGN.md 冲突时以分层文档为准。

## 硬性约束（违反即 bug）

- **只读不写**：本工具对系统只做读取。禁止向任何系统文件 / 系统路径写入（/proc、/sys、/dev、/etc 等）。打开文件一律只读（`std::ifstream` / `open(O_RDONLY)`）；不得调用任何改变系统状态的接口；源码中禁止出现 `system()` / `popen()` / 写模式 open / `write()`（v1 全程无子进程调用；若未来引入昇腾等需 `npu-smi info`，仅限只读命令白名单）。
- **技术栈固定**：C++17、CMake、GCC（g++）构建，不引入其他主语言。
- **分层架构**：下层（采集层）只做读取，上层（显示层）只做显示；层与层、模块与模块之间只通过抽象接口（纯虚基类）通信。显示层不得直接读系统文件；采集层不得包含任何 UI 头文件。

## 功能需求

- **自动检测**：启动时探测本机存在哪些硬件，默认把检测到的监控项全部打开。
- **CPU**：显示架构（x86_64 / ARMv8 等）与型号；频率展示为"一个核一个进度条"，进度 = 当前频率 / 该核最大频率。
- **进度条统一**：GPU/NPU 频率、内存/硬盘容量与 CPU 频率用同一套"当前/最大"进度条规范（Gauge 数据载体 + 分类配色，见 DESIGN.md §6.2）。
- **网络**：以折线图展示实时吞吐量（上下行），基于历史采样环形缓冲绘制。
- **GPU / NPU / 内存 / 硬盘**：显示关键参数与当前状态（使用率、温度、频率、容量等，按硬件实际可得项展示）。
- **NPU 型号识别优先**：先识别型号，仅 RK（Rockchip）系列做实际读取，其他型号留白。

## 架构

三段式单向依赖：`collectors → core（接口 + 模型） ← tui`，具体实现在 main 中组装（依赖注入）。

```
┌────────────────────────────────┐
│ 显示层 src/tui/（未来 src/gui/）│  只渲染快照，不碰系统文件
├────────────────────────────────┤
│ 核心层 src/core/                │  ICollector 等纯虚接口、Sample 快照结构体、
│                                │  Registry（硬件探测与注册）
├────────────────────────────────┤
│ 采集层 src/collectors/          │  每个硬件域一个 Collector，只读
│                                │  sysfs/procfs/厂商库/厂商 CLI，产出快照
└────────────────────────────────┘
```

关键约定：

- 每个采集器实现 core 中定义的接口（如 `ICpuCollector::sample() -> CpuSample`），返回**不可变快照**；采集器自身不持有 UI 概念。
- 自动检测 = main 逐个探测采集器 `probe()`，探测成功的注入 MonitorHub，默认启用全部可用项。
- 采集层只提供原始计数；"速率"类指标（网络吞吐、磁盘 IO 速率）由 core 层 MonitorHub 用相邻两轮快照差分得出。
- 环形历史缓冲与速率差分归 core 层 MonitorHub 维护；TUI 每帧拉取视图纯渲染，零状态（GUI 复用同一 hub）。

### 目录约定（按此创建）

```
CMakeLists.txt
3rdparty/FTXUI/  vendored 依赖（勿修改、勿升级大版本除非必要）
src/core/        接口、数据模型、MonitorHub
src/collectors/  cpu/ gpu/ npu/ mem/ disk/ net/
src/tui/         TUI 渲染（布局、进度条、折线图）
src/app/         main()：组装采集器与 UI
```

## 数据源（全部只读）

| 硬件 | 来源 |
|---|---|
| CPU 信息 | `/proc/cpuinfo`（型号/架构）、`/proc/stat`（使用率） |
| CPU 频率 | `/sys/devices/system/cpu/cpuN/cpufreq/scaling_cur_freq`、`cpuinfo_max_freq`（注意 `cpuinfo_cur_freq` 常需 root，优先用 `scaling_cur_freq`） |
| 内存 | `/proc/meminfo` |
| 硬盘 | `/proc/diskstats`、`/sys/block/*`；文件系统容量用 `statvfs()` |
| 网络 | `/proc/net/dev`（计数差分得吞吐；过滤 `lo`） |
| GPU | NVIDIA：运行时 dlopen `libnvidia-ml.so.1`（NVML）；AMD/Intel：`/sys/class/drm/` 下相应节点 |
| NPU | 先读 `/proc/device-tree/compatible` 识别型号：含 `rockchip,rk*` → RK 系列，读 devfreq（`/sys/class/devfreq/*npu*/cur_freq`、`max_freq`）得频率进度条，`/sys/kernel/debug/rknpu/load` 得每核负载（常需 root，不可读则留白）；其他厂商或无设备树 → 留白 |

## 构建与运行

GCC + CMake，C++17（构建脚本须按此约定创建）：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/hw_monitor        # TUI 模式
```

Debian 系构建依赖：`g++ cmake`，外加 TUI 库（见下）。

## 选型说明

- **TUI 框架用 FTXUI**（纯现代 C++ 实现、CMake target、自带 gauge 进度条与布局系统；折线图用 Canvas 自绘）。已 vendored 至 `3rdparty/FTXUI`，CMake `add_subdirectory` 引入，**不要**用 FetchContent 重复拉取。ncurses 为备选，不作首选。
- GPU/NPU 依赖一律**运行时可选加载**（dlopen 或子进程）：厂商组件缺失不得导致编译失败或启动崩溃，仅该监控项显示"不可用"。
