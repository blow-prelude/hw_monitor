#include "core/monitor_hub.h"

#include <algorithm>

namespace hwmon {
namespace {

constexpr int kFailLimit = 3;      // 连续失败轮数阈值：达 3 → 整域隐藏并停止采样（01-core §8.3）
constexpr auto kSleepSlice = std::chrono::milliseconds{100};  // 分段睡眠粒度（§8.4）
constexpr double kInvalidRate = -1.0;  // 速率的"本轮无效"标记（折线图断点）
constexpr double kSectorBytes = 512.0; // /proc/diskstats 扇区 → 字节

// —— 各域"全 invalid"判据（§8.3 第 5 步：返回 false 记一次失败）——
// 部分/常态性降级不算失败：无 cpufreq 的 VM 频率全 invalid、无 swap、
// NPU debugfs 不可读、GPU 个别卡无数据等，只影响对应 Gauge 留白。

bool sampleAlive(const CpuSample& s) { return s.totalUsage.valid; }

bool sampleAlive(const std::vector<GpuSample>& cards) {
    return !cards.empty() && std::any_of(cards.begin(), cards.end(),
                                         [](const GpuSample& c) { return c.util.valid; });
}

bool sampleAlive(const NpuSample& s) { return s.supported; }

bool sampleAlive(const MemSample& s) { return s.used.valid; }

bool sampleAlive(const DiskSample& s) { return !s.blocks.empty() || !s.mounts.empty(); }

bool sampleAlive(const NetSample& s) { return !s.ifaces.empty(); }

std::chrono::steady_clock::time_point nowSteady() { return std::chrono::steady_clock::now(); }

}  // namespace

MonitorHub::MonitorHub(Clock clock)
    : clock_(clock ? std::move(clock) : Clock(&nowSteady)) {}

MonitorHub::~MonitorHub() { stop(); }

// —— 注册 ——

void MonitorHub::setCpu(std::unique_ptr<ICpuCollector> c) {
    std::lock_guard<std::mutex> lk(mu_);
    cpu_ = std::move(c);
    cpuView_.reset();
    failStreak_[kCpu] = 0;
}

void MonitorHub::setGpu(std::unique_ptr<IGpuCollector> c) {
    std::lock_guard<std::mutex> lk(mu_);
    gpu_ = std::move(c);
    gpuView_.reset();
    failStreak_[kGpu] = 0;
}

void MonitorHub::setNpu(std::unique_ptr<INpuCollector> c) {
    std::lock_guard<std::mutex> lk(mu_);
    npu_ = std::move(c);
    npuView_.reset();
    failStreak_[kNpu] = 0;
}

void MonitorHub::setMem(std::unique_ptr<IMemCollector> c) {
    std::lock_guard<std::mutex> lk(mu_);
    mem_ = std::move(c);
    memView_.reset();
    failStreak_[kMem] = 0;
}

void MonitorHub::setDisk(std::unique_ptr<IDiskCollector> c) {
    std::lock_guard<std::mutex> lk(mu_);
    disk_ = std::move(c);
    diskView_.reset();
    failStreak_[kDisk] = 0;
}

void MonitorHub::setNet(std::unique_ptr<INetCollector> c) {
    std::lock_guard<std::mutex> lk(mu_);
    net_ = std::move(c);
    netView_.reset();
    failStreak_[kNet] = 0;
}

// —— 生命周期 ——

void MonitorHub::start(std::chrono::milliseconds interval) {
    std::lock_guard<std::mutex> lk(mu_);
    if (running_) return;
    interval_ = interval > std::chrono::milliseconds::zero() ? interval : std::chrono::seconds{1};
    running_ = true;
    worker_ = std::thread(&MonitorHub::loop, this);
}

void MonitorHub::stop() noexcept {
    running_ = false;  // atomic：采集线程在睡眠分段与轮首观察到
    std::thread worker;
    {
        std::lock_guard<std::mutex> lk(mu_);
        worker = std::move(worker_);
    }
    if (worker.joinable()) worker.join();  // 锁外 join：期间 UI 访问器仍可并发返回
}

// —— 视图访问（UI 线程）——

std::optional<CpuView> MonitorHub::cpu() const {
    std::lock_guard<std::mutex> lk(mu_);
    return cpuView_;
}

std::optional<GpuView> MonitorHub::gpu() const {
    std::lock_guard<std::mutex> lk(mu_);
    return gpuView_;
}

std::optional<NpuView> MonitorHub::npu() const {
    std::lock_guard<std::mutex> lk(mu_);
    return npuView_;
}

std::optional<MemView> MonitorHub::mem() const {
    std::lock_guard<std::mutex> lk(mu_);
    return memView_;
}

std::optional<DiskView> MonitorHub::disk() const {
    std::lock_guard<std::mutex> lk(mu_);
    return diskView_;
}

std::optional<NetView> MonitorHub::net() const {
    std::lock_guard<std::mutex> lk(mu_);
    return netView_;
}

// —— 采集线程 ——

void MonitorHub::loop() {
    auto next = clock_();
    while (running_) {
        pollOnce();
        next += interval_;  // interval_ 由 start() 在线程创建前写入，此后只读
        segmentSleep(next);
    }
}

void MonitorHub::segmentSleep(std::chrono::steady_clock::time_point next) {
    while (running_) {
        auto now = clock_();
        if (now >= next) return;
        auto remain = next - now;
        if (remain > kSleepSlice) remain = kSleepSlice;
        std::this_thread::sleep_for(remain);
    }
}

void MonitorHub::pollOnce() {
    const auto t = clock_();  // 本轮统一时间戳；速率用实际 Δt，睡眠漂移不累积（§8.5）
    pollCpu();
    pollGpu();
    pollNpu();
    pollMem();
    pollDisk(t);
    pollNet(t);
}

void MonitorHub::pollCpu() {
    if (!cpu_ || dead(kCpu)) return;
    bool ok = false;
    try {
        CpuSample s = cpu_->sample();
        if (sampleAlive(s)) {
            CpuView v;
            v.info = cpu_->info();  // 幂等，采集器内部缓存
            v.latest = std::move(s);
            std::lock_guard<std::mutex> lk(mu_);
            cpuView_ = std::move(v);  // 构造好整体再赋值，缩小临界区
            ok = true;
        }
    } catch (...) {  // 违反"sample() 不抛异常"契约的兜底（§9）
    }
    noteResult(kCpu, ok);
}

void MonitorHub::pollGpu() {
    if (!gpu_ || dead(kGpu)) return;
    bool ok = false;
    try {
        std::vector<GpuSample> s = gpu_->sample();
        if (sampleAlive(s)) {
            GpuView v;
            v.info = gpu_->info();
            v.latest = std::move(s);
            std::lock_guard<std::mutex> lk(mu_);
            gpuView_ = std::move(v);
            ok = true;
        }
    } catch (...) {
    }
    noteResult(kGpu, ok);
}

void MonitorHub::pollNpu() {
    if (!npu_ || dead(kNpu)) return;
    bool ok = false;
    try {
        NpuSample s = npu_->sample();
        if (sampleAlive(s)) {
            NpuView v;
            v.info = npu_->info();
            v.latest = std::move(s);
            std::lock_guard<std::mutex> lk(mu_);
            npuView_ = std::move(v);
            ok = true;
        }
    } catch (...) {
    }
    noteResult(kNpu, ok);
}

void MonitorHub::pollMem() {
    if (!mem_ || dead(kMem)) return;
    bool ok = false;
    try {
        MemSample s = mem_->sample();
        if (sampleAlive(s)) {
            MemView v;
            v.latest = std::move(s);
            std::lock_guard<std::mutex> lk(mu_);
            memView_ = std::move(v);
            ok = true;
        }
    } catch (...) {
    }
    noteResult(kMem, ok);
}

void MonitorHub::pollDisk(std::chrono::steady_clock::time_point t) {
    if (!disk_ || dead(kDisk)) return;
    bool ok = false;
    try {
        DiskSample s = disk_->sample();
        const bool alive = sampleAlive(s);

        DiskView v;
        v.blocks = std::move(s.blocks);
        v.mounts = std::move(s.mounts);
        v.readBps.assign(v.blocks.size(), kInvalidRate);
        v.writeBps.assign(v.blocks.size(), kInvalidRate);

        // 差分（§8.3 第 3 步）：按 dev 名匹配；Δt>0 且读写计数均未回退才有效，
        // 否则（首轮 / dev 消失重现 / 计数回退）该盘速率 = -1。
        const bool canDiff = prevDisk_.has && t > prevDisk_.t;
        const double dt = canDiff ? std::chrono::duration<double>(t - prevDisk_.t).count() : 0.0;
        if (canDiff) {
            for (size_t i = 0; i < v.blocks.size(); ++i) {
                const DiskBlock& b = v.blocks[i];
                auto it = prevDisk_.ctr.find(b.dev);
                if (it == prevDisk_.ctr.end()) continue;
                if (b.rSectors >= it->second.first && b.wSectors >= it->second.second) {
                    v.readBps[i] =
                        static_cast<double>(b.rSectors - it->second.first) * kSectorBytes / dt;
                    v.writeBps[i] =
                        static_cast<double>(b.wSectors - it->second.second) * kSectorBytes / dt;
                }
            }
        }

        // 以本轮重建差分基线（消失的 dev 自然移除；重现/回退者按本轮重新起算）
        PrevDisk prev;
        prev.has = true;
        prev.t = t;
        for (const DiskBlock& b : v.blocks) prev.ctr[b.dev] = {b.rSectors, b.wSectors};
        prevDisk_ = std::move(prev);

        if (alive) {
            std::lock_guard<std::mutex> lk(mu_);
            diskView_ = std::move(v);
            ok = true;
        }
    } catch (...) {
    }
    noteResult(kDisk, ok);
}

void MonitorHub::pollNet(std::chrono::steady_clock::time_point t) {
    if (!net_ || dead(kNet)) return;
    bool ok = false;
    try {
        NetSample s = net_->sample();
        const bool alive = sampleAlive(s);

        // 差分（§8.3 第 2 步）：上一轮存在同名 iface、Δt>0 且收发计数均未回退
        // 才有效；否则（首轮 / iface 消失重现 / 计数回退）rxBps = txBps = -1。
        const bool canDiff = prevNet_.has && t > prevNet_.t;
        const double dt = canDiff ? std::chrono::duration<double>(t - prevNet_.t).count() : 0.0;

        NetView v;
        if (alive) {
            v.ifaces.reserve(s.ifaces.size());
            std::map<std::string, std::pair<RingBuffer<double>, RingBuffer<double>>> hist;
            for (const NetIface& if_ : s.ifaces) {
                NetIfaceView iv;
                iv.latest = if_;
                iv.rxBps = kInvalidRate;
                iv.txBps = kInvalidRate;
                auto it = prevNet_.ctr.find(if_.name);
                if (canDiff && it != prevNet_.ctr.end() && if_.rxBytes >= it->second.first &&
                    if_.txBytes >= it->second.second) {
                    iv.rxBps = static_cast<double>(if_.rxBytes - it->second.first) / dt;
                    iv.txBps = static_cast<double>(if_.txBytes - it->second.second) / dt;
                }
                // 历史随接口名迁移（新接口从空环形缓冲开始）；无效点 push -1，
                // 折线图据其跳过断点。
                auto node = netHist_.extract(if_.name);
                if (node) {
                    hist.insert(std::move(node));
                } else {
                    hist.try_emplace(if_.name, RingBuffer<double>(kHistPoints),
                                     RingBuffer<double>(kHistPoints));
                }
                auto& rings = hist.at(if_.name);
                rings.first.push(iv.rxBps);
                rings.second.push(iv.txBps);
                iv.rxHist.points = rings.first.snapshot();
                iv.txHist.points = rings.second.snapshot();
                v.ifaces.push_back(std::move(iv));
            }
            netHist_ = std::move(hist);  // 丢弃已消失接口的历史
        }

        // 以本轮重建差分基线（空样本同样重建：iface 消失后重现按首轮处理）
        PrevNet prev;
        prev.has = true;
        prev.t = t;
        for (const NetIface& if_ : s.ifaces) prev.ctr[if_.name] = {if_.rxBytes, if_.txBytes};
        prevNet_ = std::move(prev);

        if (alive) {
            std::lock_guard<std::mutex> lk(mu_);
            netView_ = std::move(v);
            ok = true;
        }
    } catch (...) {
    }
    noteResult(kNet, ok);
}

// —— 失败降级 ——

void MonitorHub::noteResult(Dom d, bool ok) {
    std::lock_guard<std::mutex> lk(mu_);
    if (ok) {
        failStreak_[d] = 0;
        return;
    }
    if (++failStreak_[d] >= kFailLimit) clearView(d);  // 判死：整域隐藏并停止采样
}

bool MonitorHub::dead(Dom d) const {
    std::lock_guard<std::mutex> lk(mu_);
    return failStreak_[d] >= kFailLimit;
}

void MonitorHub::clearView(Dom d) {  // 前置：已持 mu_
    switch (d) {
        case kCpu: cpuView_.reset(); break;
        case kGpu: gpuView_.reset(); break;
        case kNpu: npuView_.reset(); break;
        case kMem: memView_.reset(); break;
        case kDisk: diskView_.reset(); break;
        case kNet: netView_.reset(); break;
        case kDomCount: break;
    }
}

}  // namespace hwmon
