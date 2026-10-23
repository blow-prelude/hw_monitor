#include "collectors/npu/NpuCollector.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

#include "collectors/util.h"

namespace hwmon {
namespace {
namespace fs = std::filesystem;

constexpr std::string_view kRockchipPrefix = "rockchip,rk";
constexpr std::string_view kRockchipVendor = "rockchip,";

std::string toUpper(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

}  // namespace

CompatibleResult parseCompatible(std::string_view content) {
    if (content.empty()) return {NpuKind::NoDeviceTree, {}};
    // NUL 分隔多字符串；末尾可能无 NUL，逐 token 检查。
    size_t begin = 0;
    while (begin <= content.size()) {
        size_t end = content.find('\0', begin);
        if (end == std::string_view::npos) end = content.size();
        const auto token = util::trim(content.substr(begin, end - begin));
        if (token.rfind(kRockchipPrefix, 0) == 0)
            return {NpuKind::Rockchip, std::string(token.substr(kRockchipVendor.size()))};
        if (end == content.size()) break;
        begin = end + 1;
    }
    return {NpuKind::Unrecognized, {}};
}

std::string rkModelName(const std::string& chip) {
    static const std::map<std::string, std::string> kModels = {
        {"rk1808", "Rockchip RK1808 NPU"}, {"rk3288", "Rockchip RK3288 NPU"},
        {"rk3399", "Rockchip RK3399 NPU"}, {"rk3562", "Rockchip RK3562 NPU"},
        {"rk3566", "Rockchip RK3566 NPU"}, {"rk3568", "Rockchip RK3568 NPU"},
        {"rk3576", "Rockchip RK3576 NPU"}, {"rk3588", "Rockchip RK3588 NPU"},
        {"rk3588s", "Rockchip RK3588S NPU"},
    };
    const auto it = kModels.find(chip);
    if (it != kModels.end()) return it->second;
    return "Rockchip " + toUpper(chip) + " NPU";
}

std::optional<int> parseRknpuLoad(std::string_view content) {
    // 提取首个连续数字段（容忍 "load : 42"、"rknpu load : 42%" 等）。
    size_t i = 0;
    while (i < content.size()) {
        if (std::isdigit(static_cast<unsigned char>(content[i]))) {
            size_t j = i;
            while (j < content.size() && std::isdigit(static_cast<unsigned char>(content[j]))) ++j;
            const auto v = util::parseUll(content.substr(i, j - i));
            if (!v) return std::nullopt;
            return static_cast<int>(*v);
        }
        ++i;
    }
    return std::nullopt;
}

bool NpuCollector::probe() {
    if (probed_) return info_.supported;
    probed_ = true;

    const auto compatible = util::readFile("/proc/device-tree/compatible");
    if (!compatible) {
        info_ = NpuInfo{};  // x86 / 无 DT → probe false（整域不出现）
        return false;
    }
    const CompatibleResult r = parseCompatible(*compatible);
    if (r.kind == NpuKind::Rockchip) {
        info_.supported = true;
        info_.model = rkModelName(r.chip);
        return true;
    }
    if (r.kind == NpuKind::Unrecognized) {
        // 其他厂商 DT：probe true，UI 留白展示型号行。
        info_.supported = false;
        info_.model = "未识别";
        return true;
    }
    return false;
}

NpuInfo NpuCollector::info() const {
    try {
        if (!probed_) const_cast<NpuCollector*>(this)->probe();
        return info_;
    } catch (...) {
        return NpuInfo{};
    }
}

NpuSample NpuCollector::sample() {
    NpuSample s;
    try {
        if (!info_.supported) {
            s.supported = false;  // 未识别 → 空快照
            return s;
        }
        s.supported = true;

        // freq：/sys/class/devfreq/ 下目录名含 "npu"（大小写不敏感）者，Hz→MHz。
        std::error_code ec;
        std::vector<std::string> devfreqDirs;
        for (const auto& entry : fs::directory_iterator("/sys/class/devfreq", ec)) {
            std::string name = entry.path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (name.find("npu") != std::string::npos)
                devfreqDirs.push_back(entry.path().string());
        }
        std::sort(devfreqDirs.begin(), devfreqDirs.end());  // 多个匹配取字典序首个，顺序稳定
        for (const std::string& dir : devfreqDirs) {
            const auto cur = util::readNumberFile(dir + "/cur_freq");
            const auto max = util::readNumberFile(dir + "/max_freq");
            if (cur && max && *max > 0) {
                s.freq.max = *max / 1e6;  // Hz → MHz
                s.freq.value = util::clamp(*cur / 1e6, 0.0, s.freq.max);
                s.freq.valid = true;
                break;
            }
        }

        // load：debugfs 未挂载/无权限 → invalid（不提示用户改权限，仅留白）。
        if (const auto load = util::readFile("/sys/kernel/debug/rknpu/load")) {
            if (const auto v = parseRknpuLoad(*load)) {
                s.load.max = 100.0;
                s.load.value = util::clamp(static_cast<double>(*v), 0.0, 100.0);
                s.load.valid = true;
            }
        }
    } catch (...) {
        // 契约：不抛异常；失败项保持 invalid。
    }
    return s;
}

}  // namespace hwmon
