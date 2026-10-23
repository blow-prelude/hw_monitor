// 集成验收冒烟工具（02-collectors §11：采集器类本身不进单测，真机人工覆盖）。
// 对全部六域 probe + info + 两轮 sample（间隔 1.2s，观察 CPU 差分生效），
// 只读打印；任何域不可用仅显示留白，不崩溃即通过。
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "collectors/cpu/CpuCollector.h"
#include "collectors/disk/DiskCollector.h"
#include "collectors/mem/MemCollector.h"
#include "collectors/net/NetCollector.h"
#include "collectors/npu/NpuCollector.h"
#include "collectors/gpu/GpuCollector.h"

using namespace hwmon;

static void printGauge(const char* label, const Gauge& g) {
    if (!g.valid)
        std::printf("  %-14s = —\n", label);
    else
        std::printf("  %-14s = %.1f / %.1f\n", label, g.value, g.max);
}

int main() {
    CpuCollector cpu;        MemCollector mem;      DiskCollector disk;
    NetCollector net;        GpuCollector gpu;      NpuCollector npu;

    std::printf("== probe ==\n");
    std::printf("cpu=%d gpu=%d npu=%d mem=%d disk=%d net=%d\n",
                cpu.probe(), gpu.probe(), npu.probe(), mem.probe(), disk.probe(), net.probe());

    const CpuInfo ci = cpu.info();
    std::printf("\n== cpu info ==\narch=%s model=%s cores=%d maxFreqs=%zu\n",
                ci.arch.c_str(), ci.model.c_str(), ci.nLogicalCores, ci.maxFreqMHz.size());

    if (gpu.probe()) {
        const GpuInfo gi = gpu.info();
        std::printf("\n== gpu info ==\n");
        for (const auto& c : gi.cards) std::printf("card: [%s] %s\n", c.vendor.c_str(), c.name.c_str());
    }
    if (npu.probe()) {
        const NpuInfo ni = npu.info();
        std::printf("\n== npu info ==\nsupported=%d model=%s\n", ni.supported, ni.model.c_str());
    }

    for (int round = 1; round <= 2; ++round) {
        if (round == 2) std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        std::printf("\n== sample round %d ==\n", round);

        const CpuSample cs = cpu.sample();
        for (size_t i = 0; i < cs.coreFreq.size(); ++i) {
            char label[32];
            std::snprintf(label, sizeof label, "cpu%zu freq", i);
            if (i < 4 || i + 1 == cs.coreFreq.size()) printGauge(label, cs.coreFreq[i]);
            else if (i == 4) std::printf("  ... (%zu cores)\n", cs.coreFreq.size());
        }
        printGauge("totalUsage", cs.totalUsage);

        const MemSample ms = mem.sample();
        printGauge("mem used", ms.used);
        printGauge("swap", ms.swap);

        const DiskSample ds = disk.sample();
        for (const auto& b : ds.blocks) {
            std::printf("  disk %-12s size=%lluKiB r=%llu w=%llu\n",
                        b.dev.c_str(), (unsigned long long)b.sizeKiB,
                        (unsigned long long)b.rSectors, (unsigned long long)b.wSectors);
            std::printf("    usage"); printGauge("", b.usage);
        }
        for (const auto& m : ds.mounts) {
            std::printf("  mount %-24s %-10s", m.mount.c_str(), m.fs.c_str());
            printGauge("", m.usage);
        }

        const NetSample ns = net.sample();
        for (const auto& i : ns.ifaces)
            std::printf("  net %-12s %-8s rx=%llu tx=%llu\n", i.name.c_str(), i.state.c_str(),
                        (unsigned long long)i.rxBytes, (unsigned long long)i.txBytes);

        if (gpu.probe()) {
            for (const auto& g : gpu.sample()) {
                std::printf("  gpu %-20s temp=%d\n", g.name.c_str(), g.temperature);
                printGauge("util", g.util);
                printGauge("smClock", g.smClock);
                printGauge("memUsed", g.memUsed);
            }
        }
        if (npu.probe()) {
            const NpuSample ns2 = npu.sample();
            std::printf("  npu supported=%d\n", ns2.supported);
            printGauge("freq", ns2.freq);
            printGauge("load", ns2.load);
        }
    }
    std::printf("\nsmoke ok\n");
    return 0;
}
