# 采集层（src/collectors/）详细设计

- 上游文档：[DESIGN.md](../DESIGN.md)、[01-core.md](01-core.md)（本文所有结构体/接口定义均见该文，不再重复）
- 本文是采集层的**权威实现规格**。

## 1. 定位与红线

采集层唯一职责：**只读地**从内核接口（procfs / sysfs / debugfs / device-tree）与厂商库（NVML）取数，换算到 core 约定单位，填充 core 定义的 Sample 结构。

**红线（每条都是评审必查项）：**

1. 只用只读打开方式：`std::ifstream`（内部 `open(O_RDONLY)`）。代码库禁止出现：写模式 open、`write()`、`ofstream`、`system()`、`popen()`、`fork()/exec*()`、任何 ioctl。
2. 不创建/修改任何文件（含临时文件、缓存、日志）。
3. NVML 仅 query 类 API（`nvmlInit` 是库初始化，不改硬件状态）。
4. 目录遍历用 `std::filesystem::directory_iterator`（只读语义）。

## 2. 文件清单

```
src/collectors/
├── util.h / util.cpp            只读文件读取 + 解析小工具
├── cpu/CpuCollector.{h,cpp}     /proc/cpuinfo /proc/stat /sys.../cpufreq
├── cpu/arm_model.{h,cpp}        ARM implementer/part 解码表
├── mem/MemCollector.{h,cpp}     /proc/meminfo
├── disk/DiskCollector.{h,cpp}   /sys/block /proc/diskstats /proc/self/mounts statvfs
├── net/NetCollector.{h,cpp}     /proc/net/dev /sys/class/net
├── gpu/GpuCollector.{h,cpp}     聚合器：NVML 优先，其余走 drm sysfs
├── gpu/nvml_loader.{h,cpp}      dlopen 封装 + 最小类型定义
├── gpu/amd_drm.{h,cpp}          AMD 卡 sysfs 读取
└── npu/NpuCollector.{h,cpp}     device-tree 识别 + RK devfreq/debugfs
```

编译为静态库 `hw_collectors`，链接 `hw_core`、`dl`（NVML dlopen 用）。

## 3. 公共工具（util）

```cpp
std::optional<std::string> readFile(const std::string& path);   // ifstream 全量读；失败 nullopt
std::vector<std::string>   splitLines(std::string_view);
std::vector<std::string>   splitWS(std::string_view);           // 任意空白切分
std::string_view           trim(std::string_view);
// "key: value" 行提取（cpuinfo / meminfo / mounts 通用）
std::optional<std::string> keyValue(std::string_view content, std::string_view key);
std::optional<double>      readNumberFile(const std::string& path);  // 单数值文件（sysfs 常见）
template <typename T> T clamp(T v, T lo, T hi);
```

纪律：解析函数签名一律 `（字符串或路径）→ 结构体/字段` 的**纯函数**（I/O 与解析分离），这是可测性的关键。

## 4. CpuCollector

### probe()
恒真（`/proc/stat` 必在）。probe 中完成 `info()` 缓存。

### info()

| 项 | 来源与规则 |
|---|---|
| arch | `uname(2)` → `machine` 原文（x86_64 / aarch64 / riscv64…） |
| model（x86） | `/proc/cpuinfo` 首个 `model name` |
| model（ARM） | 每 core 取 `CPU implementer` + `CPU part`，经 `arm_model` 解码表转名（如 0x41/0xd0a → "Cortex-A75"）；按型号分组计数，拼接 `"4×Cortex-A76 + 4×Cortex-A55"`；未命中显示 `0x<hex>` 原文 |
| nLogicalCores | `/proc/cpuinfo` 中 processor 条目数（`/sys/devices/system/cpu/present` 范围解析为校验，不一致取大者） |
| maxFreqMHz | 逐核 `cpuinfo_max_freq`（kHz→MHz）；该核无 cpufreq → 不入表 |

`arm_model.cpp`：两张 `constexpr` 表（implementer id→厂商名、(implementer, part)→核名），收录常见项（ARM A53/A55/A35/A72/A75/A76/A78/X1/Neoverse、高通、海思、NVIDIA Denver…），查表函数纯函数可测。

### sample()

- 每核频率：`/sys/devices/system/cpu/cpuN/cpufreq/scaling_cur_freq`（kHz→MHz）。**不读** `cpuinfo_cur_freq`（root-only）。文件缺失（离线核/VM）→ 该核 `valid=false`。
- 钳制：`value = min(cur, max)`（t boosting 波动时防溢出条）。
- 总使用率：`/proc/stat` 首行 `cpu  user nice system idle iowait irq softirq steal …`；`usage% = (Δtotal − Δ(idle+iowait)) / Δtotal × 100`。**采集器内部保存上一轮 total/idle**（首轮返回 `valid=false`——差分放采集器是因为 /proc/stat 只有累计值，hub 只对"字节计数"做差分；口径见 §9 说明）。钳制 [0,100]。

## 5. MemCollector

probe：恒真。sample：解析 `/proc/meminfo` 的 `MemTotal / MemAvailable / SwapTotal / SwapFree`（kB，原生 KiB 无需换算）。`used = (MemTotal − MemAvailable, max=MemTotal)`；无 SwapTotal=0 → swap invalid。

## 6. DiskCollector

### probe()
`/sys/block` 存在至少一个非过滤设备。

### sample() —— 块设备视图

1. 遍历 `/sys/block/`，过滤名字匹配 `loop* / ram* / zram* / sr* / md* / dm-*`（v1 不做 LVM/RAID 拆解，物理盘口径）。
2. 容量：`/sys/block/<d>/size`（512B 扇区数）→ KiB。
3. IO 计数：`/proc/diskstats` 行字段（1-based）：`name`=3，`sectors_read`=6，`sectors_written`=10；×512=字节。同名家目不存在 → 计数 0 且本轮速率由 hub 判无效。
4. 盘使用率（容量条）：取该盘全部分区（`/sys/block/<d>/<part>/`）对应挂载点的 statvfs used 之和 ÷ 盘容量；无任何挂载 → invalid。

### sample() —— 挂载点视图

1. 解析 `/proc/self/mounts`：字段 `dev mountpoint fs opts...`。
2. 过滤：dev 不以 `/dev/` 开头 → 跳过；fs 类型在黑名单 → 跳过（`proc sysfs devtmpfs devpts tmpfs securityfs cgroup cgroup2 pstore debugfs tracefs configfs bpf autofs mqueue hugetlbfs fusectl overlay squashfs efivarfs bpf_fs rpc_pipefs nsfs` 及 `fuse.*`）。
3. 同一 dev 多挂载点 → 保留第一个（去重）。
4. 容量：`statvfs(mount)`：`used = (f_blocks − f_bfree) × f_bsize`，`total = f_blocks × f_bsize`（→ KiB）。statvfs 失败 → invalid，条目保留。

## 7. NetCollector

probe：`/sys/class/net` 下存在非 `lo` 条目。

sample：

1. 遍历 `/sys/class/net/`，排除 `lo`（类型虚文件），得接口名集合。
2. 计数：`/proc/net/dev` 行格式 `  ifname: rx_bytes packets … tx_bytes …`——冒号后第 1 字段 = rx_bytes，第 9 字段 = tx_bytes。注意 ifname 与冒号可能无空格（`eth0:`），先按 `:` 切。
3. 状态：`/sys/class/net/<if>/operstate` 原文（up/down/unknown）。
4. 顺序按接口名字典序，保证 UI 顺序稳定。
5. 不做字节计数差分（hub 负责，见 01-core §8.3）；只保证计数器为无符号原始值。

## 8. GpuCollector（聚合器）与 NVML

### 8.1 探测与厂商优先级

```
probe():
  nvml_loader.init() 成功 且 nvmlDeviceGetCount>0  → NVIDIA 卡集（NVML 路径）
  遍历 /sys/class/drm/card[0-9]+/device/：
     vendor == "0x1002" → AMD 卡（sysfs 路径）
     vendor == "0x8086" → Intel 卡（仅识别，数据留白）
  cards 非空 → true
```

v1 简化：NVIDIA 可用时**只走 NVML**（不与 drm 卡去重）；否则显示 AMD/Intel 卡。双厂商混插机器极罕见，留待后续。

### 8.2 nvml_loader（dlopen 封装）

- `dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL)`；失败 → 整个 NVIDIA 路径静默不可用。
- `dlsym` 取符号表（全部判空，任一缺失即降级）：

```
nvmlInit_v2, nvmlShutdown, nvmlDeviceGetCount_v2, nvmlDeviceGetHandleByIndex_v2,
nvmlDeviceGetName, nvmlDeviceGetUtilizationRates, nvmlDeviceGetClockInfo,
nvmlDeviceGetMaxClockInfo, nvmlDeviceGetMemoryInfo, nvmlDeviceGetTemperature
```

- **不引入 NVIDIA 头文件**：`nvml_types.h` 自行定义所需最小 C 布局（源自 NVML 公开文档）：`nvmlUtilization_t{gpu,memory}`、`nvmlMemory_t{total,free,used}`（`unsigned long long`）及 `NVML_CLOCK_SM`、`NVML_TEMPERATURE_GPU` 常量。
- RAII：构造 init、析构 shutdown；全进程至多一个实例（GpuCollector 持有）。
- 温度 `nvmlDeviceGetTemperature(handle, NVML_TEMPERATURE_GPU)`；SM 频率 `GetClockInfo/GetMaxClockInfo(NVML_CLOCK_SM)`（MHz 原生）。
- 逐卡任一 API 返回非 SUCCESS → 该卡对应 Gauge invalid，不影响其他卡。
- 单位换算：显存 bytes → KiB（÷1024）。

### 8.3 AMD drm（amd_drm）

对每张 AMD 卡读 `/sys/class/drm/cardX/device/`：`gpu_busy_percent`（%）、`mem_info_vram_used/total`（bytes→KiB）、`product_name`（缺失则回退 `vendor+device id` 文本）。频率节点需 root → `smClock` 恒 invalid；无温度节点 → -1。

## 9. NpuCollector

### probe() 与型号识别

```
readFile("/proc/device-tree/compatible")
  ├─ 不可读（x86/无 DT）        → probe false（整域不出现）
  ├─ 含 "rockchip,rk" token     → supported=true, model 由映射表生成（rk3588 → "Rockchip RK3588 NPU"）
  └─ 其余（其他厂商 DT）        → supported=false, model="未识别"（probe true，UI 留白展示型号行）
```

`compatible` 是 NUL 分隔的多字符串，按 `\0` 切分后逐 token 前缀匹配。

### sample()（仅 supported=true 时读取；否则返回 supported=false 空快照）

| 项 | 路径 | 规则 |
|---|---|---|
| freq | `std::filesystem` 枚举 `/sys/class/devfreq/`，取目录名含 `npu`（大小写不敏感）者，读其 `cur_freq`、`max_freq`（**Hz** → MHz） | 无匹配目录或读失败 → invalid |
| load | `/sys/kernel/debug/rknpu/load`，解析其中首个整数（容忍 `load : 42` 等格式）为 % | debugfs 未挂载/无权限 → invalid（**不提示用户改权限**，仅留白） |

负载口径说明：RK 内核版本差异导致该文件可能是整体负载或分核数据；v1 统一解析**首个整数 = 整体负载**，分核展示留待后续版本（core 模型已随之简化为单一 `load` Gauge，见 01-core §5）。

## 10. 差分职责划分（明确一次）

- **hub 差分**：网络字节、磁盘扇区（计数器在 Sample 里随快照上报，hub 对比上一轮）。
- **采集器内差分**：CPU 使用率（/proc/stat 只有累计 jiffies，采集器持有上一轮值，首轮 invalid）。
- 原因：前者天然"每接口/每盘"需跨轮名字匹配，放 hub 便于统一历史管理；后者是单一标量，放采集器可让 CpuSample 自含语义。

## 11. 单元测试与 fixtures

`test/fixtures/` 提供静态样本（取自真实机器 + 手工构造畸形行）：

```
cpuinfo.x86  cpuinfo.arm  stat  meminfo  netdev  diskstats  mounts
devfreq_cur  devfreq_max  rknpu_load  compatible.rk  compatible.other
```

- 所有解析函数（`arm_model` 查表、cpuinfo/meminfo/netdev/diskstats/mounts 解析、compatible 识别、rknpu load 提取）针对 fixtures 做断言，**含畸形行不崩溃、字段缺失落 invalid** 的负面用例。
- 采集器类本身（I/O + 缓存）不进单测，路径注入仅在集成验收时人工覆盖：x86 开发机、无 cpufreq 的 VM、无 N 卡机器为必测三环境。
