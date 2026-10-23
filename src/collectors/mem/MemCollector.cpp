#include "collectors/mem/MemCollector.h"

#include "collectors/util.h"

namespace hwmon {

MemInfoParsed parseMemInfo(std::string_view content) {
    const auto field = [&](std::string_view key) -> std::optional<unsigned long long> {
        const auto v = util::keyValue(content, key);
        return v ? util::parseUll(*v) : std::nullopt;
    };
    MemInfoParsed m;
    m.memTotal     = field("MemTotal");
    m.memAvailable = field("MemAvailable");
    m.swapTotal    = field("SwapTotal");
    m.swapFree     = field("SwapFree");
    return m;
}

MemSample MemCollector::sample() {
    MemSample s;
    try {
        const auto content = util::readFile("/proc/meminfo");
        if (!content) return s;  // 全 invalid
        const MemInfoParsed m = parseMemInfo(*content);

        if (m.memTotal && m.memAvailable && *m.memTotal > 0) {
            Gauge g;
            g.max = static_cast<double>(*m.memTotal);
            // used = Total − Available，钳到 [0, Total]（Available 理论上不超 Total，防御性）。
            g.value = util::clamp(static_cast<double>(*m.memTotal - *m.memAvailable), 0.0, g.max);
            g.valid = true;
            s.used = g;
        }
        if (m.swapTotal && *m.swapTotal > 0 && m.swapFree) {
            Gauge g;
            g.max = static_cast<double>(*m.swapTotal);
            g.value = util::clamp(static_cast<double>(*m.swapTotal - *m.swapFree), 0.0, g.max);
            g.valid = true;
            s.swap = g;
        }
    } catch (...) {
        // 契约：不抛异常，失败项保持 valid=false。
    }
    return s;
}

}  // namespace hwmon
