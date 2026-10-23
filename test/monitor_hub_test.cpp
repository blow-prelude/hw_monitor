// MonitorHub 单测（01-core §10：差分逻辑、失败降级、视图拷贝冒烟）。
// 依赖（时钟、采集器）以可注入 fake 驱动：虚拟时钟逐轮推时间，采集器返回脚本化快照。
#include "core/monitor_hub.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "doctest.h"

using hwmon::CpuInfo;
using hwmon::CpuSample;
using hwmon::DiskBlock;
using hwmon::DiskMount;
using hwmon::DiskSample;
using hwmon::Gauge;
using hwmon::MemSample;
using hwmon::MonitorHub;
using hwmon::NetIface;
using hwmon::NetSample;

namespace {

constexpr double kInvalid = -1.0;  // 与 hub 的"本轮无效"标记一致

using TP = std::chrono::steady_clock::time_point;
TP at(int seconds) { return TP{std::chrono::seconds{seconds}}; }

// 虚拟时钟：测试逐轮推 fc.now，hub 内的差分 Δt 因此确定
struct FakeClock {
    TP now = at(0);
};
auto clockOf(FakeClock& fc) { return [&fc] { return fc.now; }; }

// —— 样本构造小工具 ——

NetIface iface(const char* name, uint64_t rx, uint64_t tx, const char* state = "up") {
    NetIface i;
    i.name = name;
    i.state = state;
    i.rxBytes = rx;
    i.txBytes = tx;
    return i;
}

NetSample netOf(std::vector<NetIface> v) {
    NetSample s;
    s.ifaces = std::move(v);
    return s;
}

DiskBlock block(const char* dev, uint64_t rSectors, uint64_t wSectors) {
    DiskBlock b;
    b.dev = dev;
    b.sizeKiB = 1000;
    b.usage = Gauge{10.0, 100.0, true};
    b.rSectors = rSectors;
    b.wSectors = wSectors;
    return b;
}

DiskMount mount(const char* mnt, const char* fs, double usedKiB, double totalKiB) {
    DiskMount m;
    m.mount = mnt;
    m.fs = fs;
    m.usage = Gauge{usedKiB, totalKiB, true};
    return m;
}

DiskSample diskOf(std::vector<DiskBlock> b, std::vector<DiskMount> m) {
    DiskSample s;
    s.blocks = std::move(b);
    s.mounts = std::move(m);
    return s;
}

CpuSample goodCpu(double usage = 42.0) {
    CpuSample s;
    s.totalUsage = Gauge{usage, 100.0, true};
    s.coreFreq.assign(4, Gauge{1500.0, 3000.0, true});
    return s;
}

// —— fake 采集器（测试构造用，不进产品代码）——

class FakeCpuCollector : public hwmon::ICpuCollector {
public:
    const char* domain() const override { return "cpu"; }
    bool probe() override { return true; }
    CpuInfo info() const override { return info_; }
    CpuSample sample() override {
        ++calls;
        if (throws) throw std::runtime_error("cpu boom");
        return sample_;
    }

    CpuInfo info_{"x86_64", "Fake CPU", 4, {3000.0, 3000.0, 3000.0, 3000.0}};
    CpuSample sample_{};  // 默认全 invalid，直接用于"全 invalid"降级用例
    bool throws = false;
    int calls = 0;
};

class FakeMemCollector : public hwmon::IMemCollector {
public:
    const char* domain() const override { return "mem"; }
    bool probe() override { return true; }
    MemSample sample() override {
        ++calls;
        return sample_;
    }
    MemSample sample_{};
    int calls = 0;
};

class FakeDiskCollector : public hwmon::IDiskCollector {
public:
    const char* domain() const override { return "disk"; }
    bool probe() override { return true; }
    DiskSample sample() override {
        ++calls;
        if (throws) throw std::runtime_error("disk boom");
        if (idx < script.size()) return script[idx++];
        return script.empty() ? DiskSample{} : script.back();
    }
    std::vector<DiskSample> script;
    size_t idx = 0;
    bool throws = false;
    int calls = 0;
};

class FakeNetCollector : public hwmon::INetCollector {
public:
    const char* domain() const override { return "net"; }
    bool probe() override { return true; }
    NetSample sample() override {
        ++calls;
        if (throws) throw std::runtime_error("net boom");
        if (live) {  // 活水模式：每轮计数自增，用于长时间冒烟
            eth.rxBytes += stepRx;
            eth.txBytes += stepTx;
            return netOf({eth});
        }
        if (idx < script.size()) return script[idx++];
        return script.empty() ? NetSample{} : script.back();
    }

    NetIface eth{"eth0", "up", 0, 0};
    uint64_t stepRx = 0, stepTx = 0;
    bool live = false;
    bool throws = false;
    std::vector<NetSample> script;
    size_t idx = 0;
    int calls = 0;
};

}  // namespace

// ==================== 差分逻辑 ====================

TEST_CASE("未注册域返回 nullopt；空跑一轮不崩") {
    MonitorHub hub;
    hub.pollOnce();
    CHECK_FALSE(hub.cpu().has_value());
    CHECK_FALSE(hub.gpu().has_value());
    CHECK_FALSE(hub.npu().has_value());
    CHECK_FALSE(hub.mem().has_value());
    CHECK_FALSE(hub.disk().has_value());
    CHECK_FALSE(hub.net().has_value());
}

TEST_CASE("网络差分：首轮速率为 -1 并写入历史") {
    FakeClock fc;
    MonitorHub hub(clockOf(fc));
    auto net = std::make_unique<FakeNetCollector>();
    net->script.push_back(netOf({iface("eth0", 1000, 500)}));
    hub.setNet(std::move(net));

    hub.pollOnce();
    auto v = hub.net();
    REQUIRE(v.has_value());
    REQUIRE(v->ifaces.size() == 1);
    CHECK(v->ifaces[0].latest.name == "eth0");
    CHECK(v->ifaces[0].latest.state == "up");
    CHECK(v->ifaces[0].rxBps == kInvalid);
    CHECK(v->ifaces[0].txBps == kInvalid);
    CHECK(v->ifaces[0].rxHist.points == std::vector<double>{kInvalid});
}

TEST_CASE("网络差分：正常两轮，速率 = Δ计数/Δt") {
    FakeClock fc;
    MonitorHub hub(clockOf(fc));
    auto net = std::make_unique<FakeNetCollector>();
    net->script.push_back(netOf({iface("eth0", 1000, 500)}));
    net->script.push_back(netOf({iface("eth0", 3000, 1500)}));
    hub.setNet(std::move(net));

    hub.pollOnce();
    fc.now = at(2);  // Δt = 2s
    hub.pollOnce();

    auto v = hub.net();
    REQUIRE(v.has_value());
    CHECK(v->ifaces[0].rxBps == doctest::Approx(1000.0));  // (3000-1000)/2
    CHECK(v->ifaces[0].txBps == doctest::Approx(500.0));   // (1500-500)/2
    CHECK(v->ifaces[0].rxHist.points == std::vector<double>{kInvalid, 1000.0});
}

TEST_CASE("网络差分：计数回退 → 双向速率作废并重置基线") {
    FakeClock fc;
    MonitorHub hub(clockOf(fc));
    auto net = std::make_unique<FakeNetCollector>();
    net->script.push_back(netOf({iface("eth0", 5000, 2500)}));
    net->script.push_back(netOf({iface("eth0", 4000, 3000)}));  // rx 回退
    net->script.push_back(netOf({iface("eth0", 6000, 4000)}));
    hub.setNet(std::move(net));

    hub.pollOnce();
    fc.now = at(1);
    hub.pollOnce();
    auto v1 = hub.net();
    REQUIRE(v1.has_value());
    CHECK(v1->ifaces[0].rxBps == kInvalid);
    CHECK(v1->ifaces[0].txBps == kInvalid);

    fc.now = at(2);
    hub.pollOnce();  // 与回退轮重置后的基线差分
    auto v2 = hub.net();
    REQUIRE(v2.has_value());
    CHECK(v2->ifaces[0].rxBps == doctest::Approx(2000.0));  // (6000-4000)/1
    CHECK(v2->ifaces[0].txBps == doctest::Approx(1000.0));  // (4000-3000)/1
}

TEST_CASE("网络差分：iface 消失重现按首轮处理（-1 断点）") {
    FakeClock fc;
    MonitorHub hub(clockOf(fc));
    auto net = std::make_unique<FakeNetCollector>();
    net->script.push_back(netOf({iface("eth0", 1000, 500)}));
    net->script.push_back(NetSample{});                          // 整轮无接口
    net->script.push_back(netOf({iface("eth0", 3000, 1000)}));   // 重现
    net->script.push_back(netOf({iface("eth0", 5000, 2000)}));
    hub.setNet(std::move(net));

    hub.pollOnce();
    fc.now = at(1);
    hub.pollOnce();  // 空轮：不更新视图（保留上一轮好数据），但基线重建为空
    auto v1 = hub.net();
    REQUIRE(v1.has_value());
    CHECK(v1->ifaces[0].latest.rxBytes == 1000);  // 仍是第 1 轮的数据

    fc.now = at(2);
    hub.pollOnce();  // 重现：无同名 prev → -1
    auto v2 = hub.net();
    REQUIRE(v2.has_value());
    CHECK(v2->ifaces[0].rxBps == kInvalid);
    CHECK(v2->ifaces[0].rxHist.points == std::vector<double>{kInvalid, kInvalid});

    fc.now = at(3);
    hub.pollOnce();
    auto v3 = hub.net();
    REQUIRE(v3.has_value());
    CHECK(v3->ifaces[0].rxBps == doctest::Approx(2000.0));  // (5000-3000)/1
    CHECK(v3->ifaces[0].rxHist.points == std::vector<double>{kInvalid, kInvalid, 2000.0});
}

TEST_CASE("网络差分：Δt=0 保护（同刻两轮不作差分）") {
    FakeClock fc;
    MonitorHub hub(clockOf(fc));
    auto net = std::make_unique<FakeNetCollector>();
    net->script.push_back(netOf({iface("eth0", 100, 50)}));
    net->script.push_back(netOf({iface("eth0", 200, 100)}));
    net->script.push_back(netOf({iface("eth0", 1200, 150)}));
    hub.setNet(std::move(net));

    hub.pollOnce();
    hub.pollOnce();  // 同一刻：canDiff=false → -1
    auto v = hub.net();
    REQUIRE(v.has_value());
    CHECK(v->ifaces[0].rxBps == kInvalid);
    CHECK(v->ifaces[0].txBps == kInvalid);

    fc.now = at(1);
    hub.pollOnce();  // 与同刻基线差分：Δ1200-200=1000B / 1s
    v = hub.net();
    REQUIRE(v.has_value());
    CHECK(v->ifaces[0].rxBps == doctest::Approx(1000.0));
}

TEST_CASE("磁盘差分：首轮 -1、正常差分（×512）、计数回退作废、新盘 -1") {
    FakeClock fc;
    MonitorHub hub(clockOf(fc));
    auto disk = std::make_unique<FakeDiskCollector>();
    disk->script.push_back(diskOf({block("nvme0n1", 100, 50)}, {mount("/", "ext4", 612, 931)}));
    disk->script.push_back(diskOf({block("nvme0n1", 300, 150)}, {mount("/", "ext4", 612, 931)}));
    disk->script.push_back(
        diskOf({block("nvme0n1", 200, 120), block("sda", 10, 5)}, {}));  // 回退 + 新盘
    hub.setDisk(std::move(disk));

    hub.pollOnce();
    auto v0 = hub.disk();
    REQUIRE(v0.has_value());
    CHECK(v0->readBps[0] == kInvalid);
    CHECK(v0->writeBps[0] == kInvalid);
    REQUIRE(v0->mounts.size() == 1);
    CHECK(v0->mounts[0].usage.valid);
    CHECK(v0->mounts[0].usage.value == doctest::Approx(612.0));

    fc.now = at(2);  // Δt = 2s
    hub.pollOnce();
    auto v1 = hub.disk();
    REQUIRE(v1.has_value());
    CHECK(v1->readBps[0] == doctest::Approx(200 * 512.0 / 2));   // 51200 B/s
    CHECK(v1->writeBps[0] == doctest::Approx(100 * 512.0 / 2));  // 25600 B/s

    fc.now = at(4);
    hub.pollOnce();
    auto v2 = hub.disk();
    REQUIRE(v2.has_value());
    REQUIRE(v2->blocks.size() == 2);
    CHECK(v2->blocks[0].dev == "nvme0n1");
    CHECK(v2->readBps[0] == kInvalid);  // 扇区计数回退
    CHECK(v2->writeBps[0] == kInvalid);
    CHECK(v2->readBps[1] == kInvalid);  // 新出现的 dev：首轮 -1
    CHECK(v2->writeBps[1] == kInvalid);
}

// ==================== 视图内容与降级 ====================

TEST_CASE("CPU 视图：info + latest，逐轮更新") {
    FakeClock fc;
    MonitorHub hub(clockOf(fc));
    auto cpu = std::make_unique<FakeCpuCollector>();
    cpu->sample_ = goodCpu(42.0);
    FakeCpuCollector* cp = cpu.get();
    hub.setCpu(std::move(cpu));

    hub.pollOnce();
    auto v = hub.cpu();
    REQUIRE(v.has_value());
    CHECK(v->info.arch == "x86_64");
    CHECK(v->info.nLogicalCores == 4);
    CHECK(v->latest.totalUsage.value == doctest::Approx(42.0));
    REQUIRE(v->latest.coreFreq.size() == 4);
    CHECK(v->latest.coreFreq[0].valid);

    cp->sample_ = goodCpu(77.0);
    fc.now = at(1);
    hub.pollOnce();
    v = hub.cpu();
    REQUIRE(v.has_value());
    CHECK(v->latest.totalUsage.value == doctest::Approx(77.0));
}

TEST_CASE("CPU 部分 invalid（频率不可得）不算失败") {
    FakeClock fc;
    MonitorHub hub(clockOf(fc));
    auto cpu = std::make_unique<FakeCpuCollector>();
    CpuSample s;
    s.totalUsage = Gauge{30.0, 100.0, true};
    s.coreFreq.assign(4, Gauge{});  // 无 cpufreq 场景：频率全 invalid
    cpu->sample_ = s;
    hub.setCpu(std::move(cpu));

    for (int i = 0; i < 5; ++i) hub.pollOnce();
    auto v = hub.cpu();
    REQUIRE(v.has_value());  // 依然存活
    CHECK(v->latest.totalUsage.valid);
    CHECK_FALSE(v->latest.coreFreq[0].valid);
}

TEST_CASE("失败降级：连续 3 轮全 invalid → 视图 nullopt 并停止采样") {
    FakeClock fc;
    MonitorHub hub(clockOf(fc));
    auto cpu = std::make_unique<FakeCpuCollector>();  // 默认全 invalid
    FakeCpuCollector* cp = cpu.get();
    hub.setCpu(std::move(cpu));

    hub.pollOnce();
    hub.pollOnce();
    CHECK_FALSE(hub.cpu().has_value());  // 未达阈值，也无好数据可显示
    CHECK(cp->calls == 2);

    hub.pollOnce();  // 第 3 次失败 → 判死
    CHECK_FALSE(hub.cpu().has_value());
    CHECK(cp->calls == 3);

    hub.pollOnce();
    hub.pollOnce();
    CHECK(cp->calls == 3);  // 判死后不再采样
    CHECK_FALSE(hub.cpu().has_value());
}

TEST_CASE("失败降级：sample() 抛异常同样计失败，其余域继续采样") {
    FakeClock fc;
    MonitorHub hub(clockOf(fc));
    auto cpu = std::make_unique<FakeCpuCollector>();
    cpu->throws = true;
    FakeCpuCollector* cp = cpu.get();
    hub.setCpu(std::move(cpu));
    auto net = std::make_unique<FakeNetCollector>();
    FakeNetCollector* np = net.get();
    net->live = true;
    net->stepRx = 1000;
    net->stepTx = 500;
    hub.setNet(std::move(net));

    for (int i = 0; i < 5; ++i) {
        hub.pollOnce();
        fc.now = at(i + 1);
    }
    CHECK_FALSE(hub.cpu().has_value());  // cpu 判死
    CHECK(cp->calls == 3);               // 判死后停采
    CHECK(np->calls == 5);               // 其余域不受影响，继续采样

    auto v = hub.net();  // net 未受影响：5 轮历史完整，速率正常
    REQUIRE(v.has_value());
    REQUIRE(v->ifaces.size() == 1);
    CHECK(v->ifaces[0].rxBps == doctest::Approx(1000.0));
    CHECK(v->ifaces[0].rxHist.points ==
          std::vector<double>{kInvalid, 1000.0, 1000.0, 1000.0, 1000.0});
}

TEST_CASE("失败降级：瞬时失败后成功即清零计数，不判死") {
    FakeClock fc;
    MonitorHub hub(clockOf(fc));
    auto cpu = std::make_unique<FakeCpuCollector>();
    FakeCpuCollector* cp = cpu.get();
    cp->sample_ = CpuSample{};  // 失败
    hub.setCpu(std::move(cpu));

    cp->sample_ = CpuSample{};
    hub.pollOnce();
    hub.pollOnce();  // 连败 2 轮
    cp->sample_ = goodCpu();
    hub.pollOnce();  // 成功 → 清零
    cp->sample_ = CpuSample{};
    hub.pollOnce();
    hub.pollOnce();  // 再败 2 轮（累计 2 < 3）
    cp->sample_ = goodCpu();
    hub.pollOnce();

    CHECK(cp->calls == 6);  // 从未判死停采
    auto v = hub.cpu();
    REQUIRE(v.has_value());
    CHECK(v->latest.totalUsage.value == doctest::Approx(42.0));
}

// ==================== 生命周期与并发 ====================

TEST_CASE("生命周期：start 幂等、stop 幂等、析构自动 join") {
    MonitorHub hub;
    auto cpu = std::make_unique<FakeCpuCollector>();
    cpu->sample_ = goodCpu();
    FakeCpuCollector* cp = cpu.get();
    hub.setCpu(std::move(cpu));

    hub.start(std::chrono::milliseconds{5});
    hub.start(std::chrono::milliseconds{5});  // 二次 no-op
    std::this_thread::sleep_for(std::chrono::milliseconds{80});
    REQUIRE(hub.cpu().has_value());

    hub.stop();
    hub.stop();  // 幂等
    CHECK(cp->calls >= 1);  // join 后读取 fake 状态，无竞态
    const int callsAtStop = cp->calls;
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    CHECK(cp->calls == callsAtStop);  // 已停采

    {  // 析构兜底 stop：不挂起、不崩溃
        MonitorHub hub2;
        auto cpu2 = std::make_unique<FakeCpuCollector>();
        cpu2->sample_ = goodCpu();
        hub2.setCpu(std::move(cpu2));
        hub2.start(std::chrono::milliseconds{5});
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
}

// TSAN 构建下运行以检查视图并发读写（01-core §10）。
TEST_CASE("并发冒烟：采集线程与 UI 访问器并发") {
    MonitorHub hub;
    auto cpu = std::make_unique<FakeCpuCollector>();
    cpu->sample_ = goodCpu();
    hub.setCpu(std::move(cpu));
    auto net = std::make_unique<FakeNetCollector>();
    net->live = true;
    net->stepRx = 1500;
    net->stepTx = 700;
    hub.setNet(std::move(net));
    auto mem = std::make_unique<FakeMemCollector>();
    mem->sample_.used = Gauge{1024.0, 4096.0, true};
    mem->sample_.swap = Gauge{0.0, 0.0, false};
    hub.setMem(std::move(mem));
    auto disk = std::make_unique<FakeDiskCollector>();
    disk->script.push_back(diskOf({block("sda", 8, 4)}, {mount("/", "ext4", 10, 100)}));
    hub.setDisk(std::move(disk));
    // gpu/npu 不注入：访问器应持续返回 nullopt

    hub.start(std::chrono::milliseconds{2});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{120};
    unsigned reads = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        reads += hub.cpu().has_value();
        reads += hub.gpu().has_value();
        reads += hub.npu().has_value();
        reads += hub.mem().has_value();
        reads += hub.disk().has_value();
        reads += hub.net().has_value();
    }
    hub.stop();
    CHECK(reads > 0);
    CHECK(hub.cpu().has_value());
    REQUIRE(hub.net().has_value());
    CHECK(hub.net()->ifaces.size() == 1);
    CHECK_FALSE(hub.gpu().has_value());
    CHECK_FALSE(hub.npu().has_value());
}
