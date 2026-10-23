# 显示层（src/tui/）详细设计

- 上游文档：[DESIGN.md](../DESIGN.md)、[01-core.md](01-core.md)（View/Series/Gauge 定义）、[02-collectors.md](02-collectors.md)
- 本文是 TUI 层的**权威实现规格**。
- FTXUI：vendored 于 `3rdparty/FTXUI`（v7.0.3+，main @ bec373d6），`add_subdirectory` 引入；其 `cmake_minimum_required 3.28.2` 仅在 `FTXUI_BUILD_MODULES=ON` 时生效，默认关闭，系统 cmake 3.22.1 可用。

## 1. 定位与边界

只做三件事：**取视图（hub 拷贝）→ 算布局 → 画 Element**。不读系统文件、不持有跨帧可变业务状态（唯一的"状态"是滚动偏移等纯 UI 状态）、不做任何数值差分。

留白铁律：`Gauge.valid==false` / `Series` 含 -1 / `optional<XView>` 为空，都必须有确定的降级渲染（"—"、断线、整节隐藏），**任何数据形态都不能导致空白洞、崩溃或布局错乱**。

## 2. 文件清单

```
src/tui/
├── format.{h,cpp}        单位格式化（纯函数）
├── widgets/
│   ├── progress_bar.{h,cpp}   Gauge → Element（统一进度条）
│   └── line_chart.{h,cpp}     Series → Canvas 折线图
├── sections/
│   ├── cpu_section.{h,cpp}    gpu_section / npu_section / mem_disk_section / net_section 同构
├── app_ui.{h,cpp}        组件树组装、按键处理、刷新触发
└── CMakeLists.txt        库 hw_tui（链接 hw_core、FTXUI::component 等）
```

依赖方向：`app_ui → sections → widgets/format → core 视图类型`；全部只 include FTXUI 公共头与 `core/snapshot.h`，**禁止 include 任何 collectors 头**。

## 3. FTXUI 能力面（只用这些）

| 能力 | 用途 |
|---|---|
| `ScreenInteractive::Fullscreen()` | 主循环（备选 `TerminalOutput()`，见 §8） |
| `Renderer` / `CatchEvent` | 纯渲染组件 + 按键拦截 |
| `vbox / hbox / filler / separator` | 布局 |
| `gauge(ratio)` | 进度条基底（配 `color()` 着色） |
| `Canvas`（`DrawPointLine / DrawText / DrawBlockLine`） | 折线图自绘 |
| `Event::Custom`（`screen.PostEvent`） | 刷新线程触发重绘 |
| `text / bold / dim / color / bgcolor / border / hcenter` | 文本样式 |

不使用 FTXUI 的实验性/不稳定 API；Canvas 之外的绘制不用扩展字符集以外的转义序列。

## 4. 组件树（app_ui）

```
Renderer(main)                     ← 每帧：hub.cpu()/gpu()/… 取快照（nullopt 的域整节跳过）
└── vbox
    ├── header   "hw_monitor · <uptime 无> · <采集间隔>1s 刷新"（右端留 hint）
    ├── cpu_section(CpuView)          opt
    ├── hbox{ gpu_section(GpuView) | filler, npu_section(NpuView) }   各 opt
    ├── mem_disk_section(MemView, DiskView)
    ├── net_section(NetView)          每接口一小节
    └── footer   "q 退出 · 空格 暂停刷新"
CatchEvent: 'q'/'Q'/Ctrl+C → screen.ExitClosurePocket 触发退出
            ' ' → 切换 paused_（暂停时 UI 不再显示新数据，hub 照常跑）
```

- 每帧构造 Element 树为纯函数（输入 = 本次 hub 拷贝 + UI 状态），无缓存渲染对象。
- 宽度自适应：`terminal 宽 W` 决定每行放几个核条/几列信息（阈值见 §5.1）。
- v1 不做滚动（见 §9）；布局目标：32 核 + 1 GPU + 1 NPU + 2 盘 + 2 网卡在 24 行 × 100 列内完整呈现，超出时各节内部按宽度压缩行数（核条列数随宽增加）。

## 5. 各节渲染规格

### 5.1 CPU

```
CPU  x86_64 · AMD Ryzen 9 7950X · 32 逻辑核                 总占用 ▕███████░░░░▏ 42%
C0 ▕█████████░░▏4.21GHz  C1 ▕█████░░░░░▏3.10GHz  C2 ▕██░░░░░░░▏1.40GHz  …
```

- 核条网格：每条占约 `26` 列（含标签与频率文本）；列数 = `clamp(W/28, 1, 4)`；核数 > 列数×8 时增加行数自然折行（hbox 分组 + vbox 堆叠）。
- 核条进度 = `cur/max`；文本 `%.2fGHz`（<1GHz 显示 `%dMHz`）；`valid=false` → `C3 ▕----------▏ —`。
- 无 cpufreq（全部核 invalid）：整块核条网格替换为一行说明 `（频率信息不可用）`，总占用照常。

### 5.2 GPU（每卡两行）

```
GPU  NVIDIA GeForce RTX 4060
  使用率 ▕██████░░░░▏ 62%   频率 ▕███████░░▏ 1890/2595MHz   显存 ▕███████░░░▏ 6.1G/8.0G   温度 64°C
```

- 无 GPU（GpuView 空）→ 整节消失（不留空行）。
- AMD 卡：频率段显示 `—`；Intel 卡：整卡仅一行型号 + `（数据暂不支持）`。

### 5.3 NPU

```
NPU  Rockchip RK3588 NPU
  频率 ▕█████░░░░░▏ 600/1000MHz   负载 ▕████░░░░░░▏ 38%
```

- 未识别（supported=false 但域存在）：`NPU  未识别（本机设备树未报告受支持的 NPU）`，单行留白。
- x86（域不存在）→ 整节消失。

### 5.4 内存 + 硬盘

```
内存 ▕█████████░░▏ 21.3G / 31.2G     SWAP ▕█░░░░░░░▏ 1.2G / 8.0G
硬盘 nvme0n1 ▕██████░░░▏ 412G/931G  R 85.2MB/s · W 12.4MB/s
     / (ext4)   ▕███████░░▏ 612G / 931G
```

- 块设备行：容量条（usage invalid → 只显示总量）+ 速率（-1 → `R — · W —`）。
- 挂载点行缩进两列挂在对应块设备下（按设备名前缀匹配归属；归属不明的挂载点集中放"其他挂载"组）。

### 5.5 网络（每接口）

```
网络  eth0 (up)   RX 12.3MB/s   TX 2.1MB/s
  RX ▁▂▃▅▇▅▃▂▁▂▄▆▇▅▃▂▁▂   TX ▂▃▂▁▁▂▃▂▁▁▁▂▃▄▂▁▁            (120s 窗口)
  [折线图 Canvas：RX/TX 双线，见 §7]
```

- 接口 >3 个：默认只画前 2 个的折线图，其余接口只保留文字行（避免撑爆屏）；`空格` 外再留 `n` 键切换"展开全部/精简"（v1.1，M5 打磨项）。
- 接口 down：文字行照常（速率 0），折线图照常（平线）。

## 6. 进度条组件（widgets/progress_bar）

```cpp
enum class BarColor { Capacity, Frequency };        // DESIGN §6.2 两套配色
ftxui::Element gaugeBar(const hwmon::Gauge& g, int width, BarColor policy);
```

- `valid=false` → 全横线空条 + `—`（dim 色）。
- Capacity 策略：ratio = value/max，`<0.70 绿 / <0.90 黄 / ≥0.90 红`（fg 着色 `gauge`）。
- Frequency 策略：恒 Cyan。
- ratio 钳制 [0,1]；width 为字符数，`gauge(ratio)` 按宽度自适应。

## 7. 折线图组件（widgets/line_chart）

```cpp
// rx/tx 时间序（-1 = 无效断点）；返回带边框 Canvas Element，内部含图例与轴标注
ftxui::Element throughputChart(const hwmon::Series& rx, const hwmon::Series& tx,
                               int widthCells, int heightCells);
```

算法：

1. 取两组数据有效点最大值 `m`；nice 化到 1/2/5×10ⁿ（`k = floor(log10(m)); mant = m/10^k; nice = mant≤1?1: mant≤2?2: mant≤5?5:10 ×10^k`）。全无效 → nice=1，画空坐标系。
2. 归一化：`y = value / nice * height`；Canvas y 轴向下，绘制时翻转。
3. 相邻两点任一为 -1 → 断线（不连接）。
4. 绘制：RX 用 `Color::Green`、TX 用 `Color::Cyan`，`DrawPointLine` 连线；顶部左上标 nice 峰值（`"1.0GB/s"`），右上标 `(120s)`；图例 + 当前值放在图上方文字行（§5.5 格式），Canvas 内只画线与峰值。
5. 点数 < 窗口宽时右对齐绘制（最新点贴右缘），空位留白。

## 8. 格式化（format，纯函数）

```cpp
std::string bytesPerSec(double v);    // -1 → "—"；1234567 → "1.2MB/s"（1024 进制，1 位小数）
std::string bytesKiB(double v);       // KiB → "21.3G"；<1MiB 显示 KiB
std::string freqMHz(double v);        // 4210 → "4.21GHz"；600 → "600MHz"；invalid → "—"
std::string percent(double v);        // 42.0 → "42%"（整数）；invalid → "—"
```

1024 进制、单位符号 `K/M/G/T`（不用 KiB/MiB 后缀，屏幕宽度优先）。

## 9. 线程模型与刷新

```
[UI 主线程]                      [刷新线程（app_ui 启动）]
ScreenInteractive::Loop(main)     loop: sleep 200ms
  ← Event::Custom 触发重绘 ←──────   hub 未析构 → screen.PostEvent(Event::Custom)
按键: CatchEvent 同线程回调
```

- 退出序列：CatchEvent 收到 q → `exit_pocket_` 触发 → `Loop` 返回 → main 依次 `refreshThread.join()` → `hub.stop()` → 进程退出。**顺序铁律：先停刷新线程（它引用 screen），再停 hub**。
- 刷新 200ms 一帧（4–5fps 足够流畅且省 CPU）；数据本身 1s 一轮，帧间内容不变时 FTXUI 内部仍会整帧重绘，可接受。
- 终端模式：`Fullscreen()`（备选屏）。若用户环境备选屏异常（如某些串口终端），提供 `--no-alt` 参数切换 `TerminalOutput()`（M5 实现并在 README 记录）。
- v1 不做滚动：布局按 §4 目标紧凑设计。滚动（↑↓ 平移视口）列入 M5：实现方案为自绘 `Renderer` 内 y-offset 裁剪 + CatchEvent 调整 offset，不引入第三方组件。

## 10. 错误与边界渲染一览

| 输入形态 | 渲染 |
|---|---|
| `optional<XView>` == nullopt | 整节不渲染（无残留空行） |
| Gauge invalid | `▕----------▏ —` |
| Series 全 -1 | 空坐标系 + "等待数据…" |
| 终端过窄（<60 列） | 顶部显示一行提示"请加宽终端"，其余照常尽力渲染 |
| resize | FTXUI 自适应重排，无需处理 |

## 11. 验收（人工）

1. x86 开发机全量运行：六节渲染、q 退出、无内存增长（`valgrind --leak-check` 短跑）。
2. `stty cols 40` 窄终端：出现加宽提示，不崩溃。
3. 断网/拔线：折线图归零为平线，不出现断点异常。
4. 无 N 卡机器：GPU 节整体消失；`rmmod` 场景（无法模拟则跳过）。
5. 200ms 刷新下 CPU 占用 <2%（top 自测）。
