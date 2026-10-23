#include "collectors/gpu/GpuCollector.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "collectors/util.h"

namespace hwmon {
namespace {
namespace fs = std::filesystem;

// card 名 "card12" → 12；非 card<数字> → nullopt。
std::optional<int> cardIndex(const std::string& name) {
    if (name.rfind("card", 0) != 0 || name.size() <= 4) return std::nullopt;
    const auto v = util::parseUll(std::string_view(name).substr(4));
    if (!v || *v > 1024 * 1024) return std::nullopt;
    return static_cast<int>(*v);
}

Gauge percentGauge(double v) {
    Gauge g;
    g.max = 100.0;
    g.value = util::clamp(v, 0.0, 100.0);
    g.valid = true;
    return g;
}

}  // namespace

std::string stripHexPrefix(std::string_view id) {
    const auto t = util::trim(id);
    return t.rfind("0x", 0) == 0 ? std::string(t.substr(2)) : std::string(t);
}

bool GpuCollector::probe() {
    if (probed_) return !info_.cards.empty();
    probed_ = true;

    // —— NVIDIA 优先（NVML 路径）——
    if (nvml_.ok()) {
        unsigned int count = 0;
        if (nvml_.getCount(&count) == nvml::NVML_SUCCESS && count > 0) {
            for (unsigned int i = 0; i < count; ++i) {
                nvml::nvmlDevice_t handle = nullptr;
                char name[96] = {};
                if (nvml_.getHandleByIndex(i, &handle) != nvml::NVML_SUCCESS)
                    continue;  // 拿不到 handle 的卡跳过（逐卡隔离）
                GpuCardInfo card;
                card.vendor = "NVIDIA";
                if (nvml_.getName(handle, name, sizeof name) == nvml::NVML_SUCCESS &&
                    name[0] != '\0') {
                    card.name = name;
                } else {
                    card.name = "NVIDIA GPU " + std::to_string(i);
                }
                CardRef ref;
                ref.nvml = true;
                ref.nvmlIndex = static_cast<int>(i);
                ref.handle = handle;
                info_.cards.push_back(std::move(card));
                cards_.push_back(std::move(ref));
            }
            if (!info_.cards.empty()) return true;
        }
        // NVML 可用但无卡 → 继续尝试 drm（spec：count>0 才走 NVML 路径）
    }

    // —— drm sysfs 路径：AMD 实读，Intel 仅识别 ——
    std::vector<std::string> cardNames;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/sys/class/drm", ec)) {
        if (cardIndex(entry.path().filename().string()))
            cardNames.push_back(entry.path().filename().string());
    }
    std::sort(cardNames.begin(), cardNames.end(), [](const std::string& a, const std::string& b) {
        return cardIndex(a) < cardIndex(b);  // card0, card1, … card10 数值序
    });

    for (const std::string& name : cardNames) {
        const std::string deviceDir = "/sys/class/drm/" + name + "/device";
        const auto vendor = util::readFile(deviceDir + "/vendor");
        if (!vendor) continue;  // 无 PCI vendor（聚合/虚拟节点）→ 跳过
        const auto vendorId = std::string(util::trim(*vendor));

        std::string vendorLabel;
        if (vendorId == "0x1002") vendorLabel = "AMD";
        else if (vendorId == "0x8086") vendorLabel = "Intel";
        else continue;  // 其他厂商 v1 不处理

        const auto deviceId = stripHexPrefix(
            util::readFile(deviceDir + "/device").value_or("?"));
        std::string displayName =
            readProductName(deviceDir).value_or(vendorLabel + " " + stripHexPrefix(vendorId) + ":" + deviceId);

        GpuCardInfo card;
        card.name = std::move(displayName);
        card.vendor = vendorLabel;
        CardRef ref;
        ref.nvml = false;
        ref.vendor = vendorLabel;
        ref.deviceDir = deviceDir;
        info_.cards.push_back(std::move(card));
        cards_.push_back(std::move(ref));
    }
    return !info_.cards.empty();
}

GpuInfo GpuCollector::info() const {
    try {
        return info_;
    } catch (...) {
        return GpuInfo{};
    }
}

std::vector<GpuSample> GpuCollector::sample() {
    std::vector<GpuSample> out;
    try {
        out.reserve(cards_.size());
        for (const CardRef& card : cards_) {
            GpuSample s;
            const size_t idx = out.size();
            if (idx < info_.cards.size()) s.name = info_.cards[idx].name;

            if (card.nvml && nvml_.ok()) {
                // NVIDIA：逐 API 判返回值，失败 → 该 Gauge invalid，不影响其他卡/字段。
                nvml::nvmlUtilization_t util{};
                if (nvml_.getUtilizationRates(card.handle, &util) == nvml::NVML_SUCCESS)
                    s.util = percentGauge(static_cast<double>(util.gpu));

                unsigned int cur = 0, mx = 0;
                if (nvml_.getClockInfo(card.handle, nvml::NVML_CLOCK_SM, &cur) == nvml::NVML_SUCCESS &&
                    nvml_.getMaxClockInfo(card.handle, nvml::NVML_CLOCK_SM, &mx) == nvml::NVML_SUCCESS &&
                    mx > 0) {
                    s.smClock.max = static_cast<double>(mx);
                    s.smClock.value = util::clamp(static_cast<double>(cur), 0.0, s.smClock.max);
                    s.smClock.valid = true;
                }

                nvml::nvmlMemory_t mem{};
                if (nvml_.getMemoryInfo(card.handle, &mem) == nvml::NVML_SUCCESS &&
                    mem.total > 0) {
                    s.memUsed.max = static_cast<double>(mem.total) / 1024.0;   // bytes→KiB
                    s.memUsed.value =
                        util::clamp(static_cast<double>(mem.used) / 1024.0, 0.0, s.memUsed.max);
                    s.memUsed.valid = true;
                }

                unsigned int temp = 0;
                if (nvml_.getTemperature(card.handle, nvml::NVML_TEMPERATURE_GPU, &temp) ==
                    nvml::NVML_SUCCESS)
                    s.temperature = static_cast<int>(temp);
            } else if (!card.nvml && card.vendor == "AMD") {
                // AMD：gpu_busy_percent + vram；频率需 root → 恒 invalid；无温度节点 → -1。
                const AmdSampleData amd = readAmdDevice(card.deviceDir);
                if (amd.busyPercent) s.util = percentGauge(*amd.busyPercent);
                if (amd.vramUsedKiB && amd.vramTotalKiB && *amd.vramTotalKiB > 0) {
                    s.memUsed.max = *amd.vramTotalKiB;
                    s.memUsed.value = util::clamp(*amd.vramUsedKiB, 0.0, s.memUsed.max);
                    s.memUsed.valid = true;
                }
            }
            // Intel（及 AMD 个别节点缺失）：字段保持 invalid / -1。
            out.push_back(std::move(s));
        }
    } catch (...) {
        // 契约：不抛异常；异常时清空避免半截数据。
        out.clear();
        out.resize(cards_.size());
        for (size_t i = 0; i < out.size() && i < info_.cards.size(); ++i)
            out[i].name = info_.cards[i].name;
    }
    return out;
}

}  // namespace hwmon
