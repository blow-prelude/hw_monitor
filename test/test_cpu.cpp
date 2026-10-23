#include <string>

#include "collectors/cpu/CpuCollector.h"
#include "collectors/cpu/arm_model.h"
#include "collectors/util.h"
#include <doctest.h>

using namespace hwmon;

TEST_CASE("parseStatFirstLine：真实 fixture 首行") {
    const auto content = util::readFile(FIXTURE_DIR "/stat");
    REQUIRE(content);
    const auto t = parseStatFirstLine(*content);
    REQUIRE(t);
    CHECK(t->total > 0);
    CHECK(t->idleAll <= t->total);
}

TEST_CASE("parseStatFirstLine：数值口径与畸形行") {
    // user=10 nice=20 system=30 idle=40 iowait=50 → total=150, idleAll=90
    const auto t = parseStatFirstLine("cpu  10 20 30 40 50 0 0 0 100 100");
    REQUIRE(t);
    CHECK(t->total == 150);      // guest(100) 已计入 user/nice，不重复累加
    CHECK(t->idleAll == 90);

    CHECK_FALSE(parseStatFirstLine("cpuX 1 2 3"));     // fixture 中的畸形聚合行
    CHECK_FALSE(parseStatFirstLine("intr 123"));
    CHECK_FALSE(parseStatFirstLine(""));
    CHECK_FALSE(parseStatFirstLine("cpu 1 2"));
    CHECK_FALSE(parseStatFirstLine("cpu 1 2 3 x 5"));
}

TEST_CASE("parseCpuPresent：范围解析") {
    CHECK(parseCpuPresent("0-7\n") == 8);
    CHECK(parseCpuPresent("0-3,8-11\n") == 8);  // 4 + 4
    CHECK(parseCpuPresent("5\n") == 1);
    CHECK_FALSE(parseCpuPresent("x"));
    CHECK_FALSE(parseCpuPresent("4-2"));
    CHECK_FALSE(parseCpuPresent(""));
}

TEST_CASE("parseCpuModel：x86") {
    const auto content = util::readFile(FIXTURE_DIR "/cpuinfo.x86");
    REQUIRE(content);
    const auto model = parseCpuModel(*content, "x86_64");
    // fixture 末尾追加了空值 "model name :"，首行匹配优先，不受影响
    const auto expected = util::keyValue(*content, "model name");
    REQUIRE(expected);
    CHECK(model == *expected);
    CHECK(parseCpuModel("processor\t: 0\n", "x86_64") == "unknown");  // 字段缺失
}

TEST_CASE("parseCpuModel：ARM 大小核分组") {
    const auto content = util::readFile(FIXTURE_DIR "/cpuinfo.arm");
    REQUIRE(content);
    CHECK(parseCpuModel(*content, "aarch64") == "4×Cortex-A76 + 4×Cortex-A55");

    // 单核单组：不带计数前缀
    const std::string single =
        "processor\t: 0\nCPU implementer\t: 0x41\nCPU part\t: 0xd03\n";
    CHECK(parseCpuModel(single, "aarch64") == "Cortex-A53");

    // 未命中型号 → 0x<hex> 原文
    const std::string unknown =
        "processor\t: 0\nCPU implementer\t: 0x41\nCPU part\t: 0x999\n";
    CHECK(parseCpuModel(unknown, "aarch64") == "0x999");

    // 无 implementer/part → 回退 Hardware 字段
    const std::string legacy = "processor\t: 0\nHardware\t: Rockchip RK3588\n";
    CHECK(parseCpuModel(legacy, "armv8l") == "Rockchip RK3588");
}

TEST_CASE("arm_model 查表") {
    CHECK(std::string(armCoreName(0x41, 0xd0a)) == "Cortex-A75");
    CHECK(std::string(armCoreName(0x41, 0xd03)) == "Cortex-A53");
    CHECK(std::string(armCoreName(0x41, 0xd05)) == "Cortex-A55");
    CHECK(std::string(armCoreName(0x41, 0xd44)) == "Cortex-X1");
    CHECK(std::string(armCoreName(0x4e, 0x003)) == "Denver");
    CHECK(armCoreName(0x41, 0x999) == nullptr);
    CHECK(std::string(armImplementerName(0x41)) == "ARM");
    CHECK(std::string(armImplementerName(0x48)) == "HiSilicon");
    CHECK(armImplementerName(0xfe) == nullptr);
}
