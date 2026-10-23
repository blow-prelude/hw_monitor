#pragma once
// 采集层公共小工具：只读文件读取 + 纯解析。
// 纪律（02-collectors §3）：解析函数一律“（字符串）→ 结果”的纯函数，I/O 与解析分离。
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hwmon::util {

// ifstream 全量只读读入；打开失败/读错误 → nullopt。兼容内容为空但可读的 sysfs 虚文件
// （st_size==0），以及含 NUL 字节的 device-tree 文件。
std::optional<std::string> readFile(const std::string& path);

std::vector<std::string> splitLines(std::string_view s);
std::vector<std::string> splitWS(std::string_view s);  // 任意空白切分
std::string_view trim(std::string_view s);

// "key: value" 行提取（cpuinfo / meminfo 通用）：逐行找第一个 ':'，
// trim 后的行首段 == key 即命中，返回 trim 后的冒号后内容；多行同名取首行。
std::optional<std::string> keyValue(std::string_view content, std::string_view key);

// 单数值文件（sysfs 常见）：读文件取首个数字 token。
std::optional<double> readNumberFile(const std::string& path);
// 同上但整型（计数器类：size/netdev/diskstats，double 有精度损失）。
std::optional<unsigned long long> readUllFile(const std::string& path);

// —— 纯解析（无 I/O，供单测）——
std::optional<unsigned long long> parseUll(std::string_view s);   // 十进制；拒绝负号
std::optional<unsigned long long> parseHex(std::string_view s);   // 十六进制，容忍 0x 前缀
std::optional<double>             parseDouble(std::string_view s);

template <typename T>
T clamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace hwmon::util
