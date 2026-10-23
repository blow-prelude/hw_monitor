#include "collectors/mem/MemCollector.h"
#include "collectors/util.h"
#include <doctest.h>

using namespace hwmon;

TEST_CASE("parseMemInfo：真实 fixture") {
    const auto content = util::readFile(FIXTURE_DIR "/meminfo");
    REQUIRE(content);
    const auto m = parseMemInfo(*content);
    REQUIRE(m.memTotal);
    CHECK(*m.memTotal > 0);
    REQUIRE(m.memAvailable);  // 现代内核必有；畸形行不崩溃
    CHECK(m.swapTotal);
    CHECK(m.swapFree);
}

TEST_CASE("parseMemInfo：字段缺失落 invalid 原料") {
    const auto m = parseMemInfo("garbage\nMemFree: notanumber kB\n");
    CHECK_FALSE(m.memTotal);
    CHECK_FALSE(m.memAvailable);
    CHECK_FALSE(m.swapTotal);
    CHECK_FALSE(m.swapFree);
}

TEST_CASE("parseMemInfo：kB 数值口径") {
    const auto m = parseMemInfo(
        "MemTotal:        1000 kB\n"
        "MemAvailable:     300 kB\n"
        "SwapTotal:        200 kB\n"
        "SwapFree:          50 kB\n");
    REQUIRE(m.memTotal);
    CHECK(*m.memTotal == 1000);
    REQUIRE(m.memAvailable);
    CHECK(*m.memAvailable == 300);
    REQUIRE(m.swapTotal);
    CHECK(*m.swapTotal == 200);
    REQUIRE(m.swapFree);
    CHECK(*m.swapFree == 50);
}
