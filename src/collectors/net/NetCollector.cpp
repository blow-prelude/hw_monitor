#include "collectors/net/NetCollector.h"

#include <algorithm>
#include <filesystem>

#include "collectors/util.h"

namespace hwmon {
namespace {
namespace fs = std::filesystem;
}  // namespace

std::map<std::string, std::pair<unsigned long long, unsigned long long>>
parseNetDev(std::string_view content) {
    std::map<std::string, std::pair<unsigned long long, unsigned long long>> out;
    for (const std::string& line : util::splitLines(content)) {
        // 行格式 "  ifname: rx_bytes packets … tx_bytes …"；ifname 与冒号可能无空格，先按 ':' 切。
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;  // 表头等畸形行
        const auto name = util::trim(std::string_view(line).substr(0, colon));
        if (name.empty()) continue;
        const auto fields = util::splitWS(std::string_view(line).substr(colon + 1));
        if (fields.size() < 16) continue;  // 16 列计数字段不全 → 整行跳过
        const auto rx = util::parseUll(fields[0]);
        const auto tx = util::parseUll(fields[8]);
        if (!rx || !tx) continue;
        out[std::string(name)] = {*rx, *tx};
    }
    return out;
}

bool NetCollector::probe() {
    try {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator("/sys/class/net", ec)) {
            if (entry.path().filename() != "lo") return true;
        }
    } catch (...) {
    }
    return false;
}

NetSample NetCollector::sample() {
    NetSample s;
    try {
        // 1. 枚举接口名，排除 lo。
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator("/sys/class/net", ec)) {
            std::string name = entry.path().filename().string();
            if (name != "lo") names.push_back(std::move(name));
        }
        // 4. 按接口名字典序，保证 UI 顺序稳定。
        std::sort(names.begin(), names.end());

        const auto counters =
            parseNetDev(util::readFile("/proc/net/dev").value_or(""));

        for (const std::string& name : names) {
            NetIface ifc;
            ifc.name = name;
            // 3. operstate 原文；文件缺失 → "unknown"。
            ifc.state = std::string(util::trim(
                util::readFile("/sys/class/net/" + name + "/operstate").value_or("unknown")));
            if (ifc.state.empty()) ifc.state = "unknown";
            // 2. 计数器缺失（极罕见）→ 0，由 hub 的回退保护判定无效。
            const auto it = counters.find(name);
            if (it != counters.end()) {
                ifc.rxBytes = it->second.first;
                ifc.txBytes = it->second.second;
            }
            s.ifaces.push_back(std::move(ifc));
        }
    } catch (...) {
        // 契约：不抛异常；失败时 ifaces 可能为空。
    }
    return s;
}

}  // namespace hwmon
