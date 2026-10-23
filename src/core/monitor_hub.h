#pragma once
// MonitorHub：核心层唯一的有状态组件（01-core §8）。
// 职责：轮询节奏、速率差分、环形历史、视图快照；不读任何系统文件、不含 UI 概念。
//
// 线程模型：
//   main 线程 ：probe 成功的采集器经 setXxx() 注入（未注入的域 = 该机器没有），随后 start()。
//   采集线程   ：start() 内部创建，每 interval 调一次 pollOnce()（采样 + 差分 + 视图更新）。
//   UI 线程   ：cpu()/gpu()/… 互斥保护下返回深拷贝；域未注册/已判死 → nullopt（UI 整节隐藏）。
//
// hub 不拷贝、不移动（内部以 unique_ptr 持有采集器），以对象生命周期管理；
// 自身不打印日志（零输出纯净，01-core §9）。

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "core/collector.h"
#include "core/ring_buffer.h"
#include "core/snapshot.h"

namespace hwmon {

class MonitorHub {
public:
    // 时钟源：默认 steady_clock（不受系统时间跳变影响）；单测注入虚拟时钟
    // 以获得确定性 Δt（01-core §10"可注入 fake"）。
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    explicit MonitorHub(Clock clock = Clock{});
    ~MonitorHub();  // 兜底 stop()，保证不 exit-while-locked / 不泄漏采集线程

    MonitorHub(const MonitorHub&) = delete;
    MonitorHub& operator=(const MonitorHub&) = delete;

    // —— main 线程：注册（探测成功的采集器才注入；未注入的域 = 该机器没有）——
    // 重复注入会替换该域采集器并清空其视图/失败计数；应在 start() 之前完成。
    void setCpu(std::unique_ptr<ICpuCollector> c);   // 同型：setGpu/setNpu/setMem/setDisk/setNet
    void setGpu(std::unique_ptr<IGpuCollector> c);
    void setNpu(std::unique_ptr<INpuCollector> c);
    void setMem(std::unique_ptr<IMemCollector> c);
    void setDisk(std::unique_ptr<IDiskCollector> c);
    void setNet(std::unique_ptr<INetCollector> c);

    void start(std::chrono::milliseconds interval = std::chrono::seconds{1});
    void stop() noexcept;  // 幂等；join 采集线程（分段睡眠，最大延迟 ~100ms + 一轮采样）

    // —— UI 线程：互斥保护，返回深拷贝；域未注册/不可用 → nullopt（UI 整节隐藏）——
    std::optional<CpuView> cpu() const;
    std::optional<GpuView> gpu() const;
    std::optional<NpuView> npu() const;
    std::optional<MemView> mem() const;
    std::optional<DiskView> disk() const;
    std::optional<NetView> net() const;

    // 跑一轮"全域采样 + 差分 + 视图更新"。产品代码仅由采集线程调用；公开只为
    // 单测逐轮驱动。注意：不得与运行中的采集线程并发调用。
    void pollOnce();

private:
    enum Dom { kCpu, kGpu, kNpu, kMem, kDisk, kNet, kDomCount };

    // 差分所需上一轮状态（仅采集线程访问，不与 UI 共享，无需入锁）
    struct PrevNet {
        bool has = false;
        std::chrono::steady_clock::time_point t{};
        std::map<std::string, std::pair<uint64_t, uint64_t>> ctr;  // iface 名 → (rx, tx) 字节计数
    };
    struct PrevDisk {
        bool has = false;
        std::chrono::steady_clock::time_point t{};
        std::map<std::string, std::pair<uint64_t, uint64_t>> ctr;  // dev 名 → (读, 写) 扇区计数
    };

    void loop();
    void segmentSleep(std::chrono::steady_clock::time_point next);  // 分段睡眠，快速响应 stop
    void pollCpu();
    void pollGpu();
    void pollNpu();
    void pollMem();
    void pollDisk(std::chrono::steady_clock::time_point t);
    void pollNet(std::chrono::steady_clock::time_point t);
    void noteResult(Dom d, bool ok);  // 成功清零失败计数；连续失败达阈值 → 判死并隐藏视图
    bool dead(Dom d) const;
    void clearView(Dom d);            // 前置：已持 mu_

    Clock clock_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::chrono::milliseconds interval_{1000};  // start() 写入（先于线程创建），采集线程只读

    mutable std::mutex mu_;  // 保护以下全部状态（prevNet_/prevDisk_/netHist_ 除外：仅采集线程访问）
    std::unique_ptr<ICpuCollector> cpu_;   // …六域指针
    std::unique_ptr<IGpuCollector> gpu_;
    std::unique_ptr<INpuCollector> npu_;
    std::unique_ptr<IMemCollector> mem_;
    std::unique_ptr<IDiskCollector> disk_;
    std::unique_ptr<INetCollector> net_;
    std::optional<CpuView> cpuView_;       // …六域视图（采集线程写，UI 读）
    std::optional<GpuView> gpuView_;
    std::optional<NpuView> npuView_;
    std::optional<MemView> memView_;
    std::optional<DiskView> diskView_;
    std::optional<NetView> netView_;
    std::array<int, kDomCount> failStreak_{};  // 连续失败轮数（01-core §8.3 第 5 步）

    // 差分基线与每接口历史（仅采集线程访问）
    PrevNet prevNet_;
    PrevDisk prevDisk_;
    // 每 iface 的 RX/TX 环形历史；每轮按现存接口重建（消失的接口历史随之丢弃）
    std::map<std::string, std::pair<RingBuffer<double>, RingBuffer<double>>> netHist_;
};

}  // namespace hwmon
