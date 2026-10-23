#include "collectors/cpu/CpuCollector.h"

#include <sys/utsname.h>

#include "collectors/cpu/arm_model.h"

#include <algorithm>
#include <cstdio>
#include <map>

#include "collectors/util.h"

namespace hwmon {
namespace {

std::optional<std::string> unameMachine() {
    struct utsname uts {};
    if (::uname(&uts) != 0) return std::nullopt;
    return std::string(uts.machine);
}

// cpuinfo 中 "processor : N" 条目数。
int countProcessorEntries(std::string_view cpuinfo) {
    int n = 0;
    for (const std::string& line : util::splitLines(cpuinfo)) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        if (util::trim(std::string_view(line).substr(0, colon)) == "processor") ++n;
    }
    return n;
}

bool isArmArch(const std::string& arch) {
    return arch.rfind("aarch64", 0) == 0 || arch.rfind("arm", 0) == 0;
}

}  // namespace

std::optional<StatTick> parseStatFirstLine(std::string_view content) {
    const auto lines = util::splitLines(content);
    if (lines.empty()) return std::nullopt;
    const auto toks = util::splitWS(lines.front());
    if (toks.size() < 5 || toks[0] != "cpu") return std::nullopt;
    // 字段：user nice system idle iowait irq softirq steal（guest 已计入 user/nice，不重复累加）。
    StatTick t;
    for (int i = 1; i <= 8; ++i) {
        const auto v = util::parseUll(toks[i]);
        if (!v) return std::nullopt;
        t.total += *v;
        if (i == 4 || i == 5) t.idleAll += *v;  // idle / iowait
    }
    return t;
}

std::optional<int> parseCpuPresent(std::string_view content) {
    int count = 0;
    for (const auto& tok : util::splitWS(content)) {  // 形如 "0-7" / "0-3,8-11"
        size_t begin = 0;
        while (begin <= tok.size()) {
            size_t comma = tok.find(',', begin);
            if (comma == std::string_view::npos) comma = tok.size();
            const auto piece = tok.substr(begin, comma - begin);
            if (!piece.empty()) {
                const size_t dash = piece.find('-');
                if (dash == std::string_view::npos) {
                    if (!util::parseUll(piece)) return std::nullopt;
                    count += 1;  // 单个 cpu 编号 = 1 核在线
                } else {
                    const auto lo = util::parseUll(piece.substr(0, dash));
                    const auto hi = util::parseUll(piece.substr(dash + 1));
                    if (!lo || !hi || *hi < *lo) return std::nullopt;
                    count += static_cast<int>(*hi - *lo + 1);
                }
            }
            if (comma == tok.size()) break;
            begin = comma + 1;
        }
    }
    return count > 0 ? std::optional<int>(count) : std::nullopt;
}

std::string parseCpuModel(std::string_view cpuinfo, const std::string& arch) {
    if (!isArmArch(arch)) {
        const auto name = util::keyValue(cpuinfo, "model name");
        return name && !name->empty() ? *name : "unknown";
    }

    // ARM：逐 processor 块取 CPU implementer + CPU part，查表解码后按型号分组计数。
    struct Group { std::string name; int count; };  // 保持首次出现顺序（大核通常在前）
    std::vector<Group> groups;
    std::map<std::string, size_t> index;
    unsigned impl = 0, part = 0;
    bool haveImpl = false, havePart = false;

    const auto flush = [&]() {
        if (!haveImpl || !havePart) return;
        const char* decoded = armCoreName(impl, part);
        char hex[16];
        std::snprintf(hex, sizeof hex, "0x%03x", part);
        std::string name = decoded ? decoded : hex;
        auto it = index.find(name);
        if (it == index.end()) {
            index.emplace(name, groups.size());
            groups.push_back({std::move(name), 1});
        } else {
            ++groups[it->second].count;
        }
        haveImpl = havePart = false;
    };

    for (const std::string& line : util::splitLines(cpuinfo)) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const auto key = util::trim(std::string_view(line).substr(0, colon));
        const auto value = util::trim(std::string_view(line).substr(colon + 1));
        if (key == "processor") {
            flush();
        } else if (key == "CPU implementer") {
            if (auto v = util::parseHex(value)) { impl = static_cast<unsigned>(*v); haveImpl = true; }
        } else if (key == "CPU part") {
            if (auto v = util::parseHex(value)) { part = static_cast<unsigned>(*v); havePart = true; }
        }
    }
    flush();

    if (groups.empty()) {
        // 无 implementer/part（老内核）：退回 Hardware 字段，再退 unknown。
        const auto hw = util::keyValue(cpuinfo, "Hardware");
        return hw && !hw->empty() ? *hw : "unknown";
    }

    std::string model;
    for (const Group& g : groups) {
        if (!model.empty()) model += " + ";
        if (g.count > 1) model += std::to_string(g.count) + "×";
        model += g.name;
    }
    return model;
}

bool CpuCollector::probe() {
    if (!probed_) cacheInfo();
    probed_ = true;
    return true;  // /proc/stat 必在
}

void CpuCollector::cacheInfo() {
    CpuInfo info;
    if (auto arch = unameMachine()) info.arch = *arch;

    const auto cpuinfo = util::readFile("/proc/cpuinfo").value_or("");
    info.nLogicalCores = countProcessorEntries(cpuinfo);
    // /sys/.../present 为交叉校验，不一致取大者。
    if (auto present = util::readFile("/sys/devices/system/cpu/present")) {
        if (auto n = parseCpuPresent(*present)) {
            info.nLogicalCores = std::max(info.nLogicalCores, *n);
        }
    }
    info.model = parseCpuModel(cpuinfo, info.arch);

    // 逐核 cpuinfo_max_freq（kHz→MHz）；该核无 cpufreq → 占位 0（与 sample 的 coreFreq
    // 下标对齐）；整机皆无 → 保持空向量（VM 场景，02-collectors §4“无 cpufreq 时为空”）。
    if (info.nLogicalCores > 0) {
        std::vector<double> maxFreq(static_cast<size_t>(info.nLogicalCores), 0.0);
        bool any = false;
        for (size_t i = 0; i < maxFreq.size(); ++i) {
            const auto path = "/sys/devices/system/cpu/cpu" + std::to_string(i) +
                              "/cpufreq/cpuinfo_max_freq";
            if (const auto khz = util::readUllFile(path)) {
                maxFreq[i] = static_cast<double>(*khz) / 1000.0;
                any = true;
            }
        }
        if (any) info.maxFreqMHz = std::move(maxFreq);
    }
    info_ = std::move(info);
}

CpuInfo CpuCollector::info() const {
    try {
        if (!probed_) const_cast<CpuCollector*>(this)->cacheInfo();
        return info_;
    } catch (...) {
        return CpuInfo{};
    }
}

CpuSample CpuCollector::sample() {
    CpuSample s;
    try {
        if (!probed_) cacheInfo();
        const int n = info_.nLogicalCores;
        if (n <= 0) return s;  // coreFreq 空、usage invalid

        // 每核频率：scaling_cur_freq（kHz→MHz）；不读 root-only 的 cpuinfo_cur_freq。
        s.coreFreq.resize(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            Gauge g;
            const auto path = "/sys/devices/system/cpu/cpu" + std::to_string(i) +
                              "/cpufreq/scaling_cur_freq";
            const double maxMHz = i < static_cast<int>(info_.maxFreqMHz.size())
                                      ? info_.maxFreqMHz[static_cast<size_t>(i)]
                                      : 0.0;
            if (const auto khz = util::readUllFile(path); khz && maxMHz > 0.0) {
                g.max = maxMHz;
                // 钳制：turbo 波动防溢出条（02-collectors §4）。
                g.value = util::clamp(static_cast<double>(*khz) / 1000.0, 0.0, maxMHz);
                g.valid = true;
            }
            s.coreFreq[static_cast<size_t>(i)] = g;
        }

        // 总使用率：与上一轮 /proc/stat 累计值差分；首轮 invalid。
        if (const auto content = util::readFile("/proc/stat")) {
            if (const auto tick = parseStatFirstLine(*content)) {
                if (hasPrev_ && tick->total > prevTotal_) {
                    const double dt = static_cast<double>(tick->total - prevTotal_);
                    const double dIdle = static_cast<double>(tick->idleAll - prevIdleAll_);
                    if (dIdle <= dt) {  // 回绕/异常时自然落 invalid
                        Gauge g;
                        g.max = 100.0;
                        g.value = util::clamp((dt - dIdle) * 100.0 / dt, 0.0, 100.0);
                        g.valid = true;
                        s.totalUsage = g;
                    }
                }
                prevTotal_ = tick->total;
                prevIdleAll_ = tick->idleAll;
                hasPrev_ = true;
            }
        }
    } catch (...) {
        // 契约：sample() 不抛异常；失败项保持 valid=false。
    }
    return s;
}

}  // namespace hwmon
