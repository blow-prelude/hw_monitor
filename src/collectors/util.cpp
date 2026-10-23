#include "collectors/util.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

namespace hwmon::util {

std::optional<std::string> readFile(const std::string& path) {
    // 只读打开（ifstream 内部 open(O_RDONLY)）；不动文件内容、不创建文件。
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return std::nullopt;
    // 不用 ostream << rdbuf()：空内容时它会置 failbit，sysfs 虚文件常报 size 0，易误判。
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (f.bad()) return std::nullopt;
    return content;
}

std::vector<std::string> splitLines(std::string_view s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        size_t nl = s.find('\n', i);
        if (nl == std::string_view::npos) {
            out.emplace_back(s.substr(i));
            break;
        }
        size_t end = nl;
        if (end > i && s[end - 1] == '\r') --end;  // 容忍 CRLF
        out.emplace_back(s.substr(i, end - i));
        i = nl + 1;
    }
    return out;
}

std::vector<std::string> splitWS(std::string_view s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        size_t begin = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i > begin) out.emplace_back(s.substr(begin, i - begin));
    }
    return out;
}

std::string_view trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::optional<std::string> keyValue(std::string_view content, std::string_view key) {
    for (const std::string& line : splitLines(content)) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const auto k = trim(std::string_view(line).substr(0, colon));
        if (k == key) return std::string(trim(std::string_view(line).substr(colon + 1)));
    }
    return std::nullopt;
}

std::optional<double> readNumberFile(const std::string& path) {
    const auto content = readFile(path);
    if (!content) return std::nullopt;
    const auto toks = splitWS(*content);
    if (toks.empty()) return std::nullopt;
    return parseDouble(toks.front());
}

std::optional<unsigned long long> readUllFile(const std::string& path) {
    const auto content = readFile(path);
    if (!content) return std::nullopt;
    const auto toks = splitWS(*content);
    if (toks.empty()) return std::nullopt;
    return parseUll(toks.front());
}

std::optional<unsigned long long> parseUll(std::string_view s) {
    s = trim(s);
    if (s.empty() || s.front() == '-') return std::nullopt;
    std::string buf(s);
    char* end = nullptr;
    errno = 0;
    const unsigned long long v = std::strtoull(buf.c_str(), &end, 10);
    if (end == buf.c_str() || errno == ERANGE) return std::nullopt;
    return v;
}

std::optional<unsigned long long> parseHex(std::string_view s) {
    s = trim(s);
    if (s.empty() || s.front() == '-') return std::nullopt;
    std::string buf(s);
    char* end = nullptr;
    errno = 0;
    const unsigned long long v = std::strtoull(buf.c_str(), &end, 16);
    if (end == buf.c_str() || errno == ERANGE) return std::nullopt;
    return v;
}

std::optional<double> parseDouble(std::string_view s) {
    s = trim(s);
    if (s.empty() || s.front() == '-') return std::nullopt;
    std::string buf(s);
    char* end = nullptr;
    errno = 0;
    const double v = std::strtod(buf.c_str(), &end);
    if (end == buf.c_str()) return std::nullopt;
    return v;
}

}  // namespace hwmon::util
