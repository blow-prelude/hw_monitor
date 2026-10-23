#include "collectors/net/NetCollector.h"
#include "collectors/util.h"
#include <doctest.h>

using namespace hwmon;

TEST_CASE("parseNetDev：真实 fixture（含畸形行）") {
    const auto content = util::readFile(FIXTURE_DIR "/netdev");
    REQUIRE(content);
    const auto m = parseNetDev(*content);

    REQUIRE(m.count("lo") == 1);       // lo 在解析结果中；collector 层过滤
    for (const auto& [name, ctr] : m) {
        CHECK_FALSE(name.empty());
    }
    CHECK_FALSE(m.count("eth9"));      // 字段不足的畸形行整行跳过
    CHECK_FALSE(m.count("bad"));       // 非数字计数跳过
    CHECK_FALSE(m.count("no-colon-line"));
}

TEST_CASE("parseNetDev：字段口径（冒号后第 1 / 第 9 字段）") {
    const auto m = parseNetDev(
        "Inter-|   Receive                                                "
        "|  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|"
        "bytes    packets errs drop fifo colls carrier compressed\n"
        "  eth0: 100 1 2 3 4 5 6 7 800 9 10 11 12 13 14 15 16\n"
        "wlan0:1122334455 1 2 3 4 5 6 7 7777888899 1 2 3 4 5 6 7\n");
    REQUIRE(m.size() == 2);
    REQUIRE(m.count("eth0") == 1);
    CHECK(m.at("eth0").first == 100);      // rx_bytes
    CHECK(m.at("eth0").second == 800);     // tx_bytes
    // ifname 与冒号无空格的写法
    CHECK(m.at("wlan0").first == 1122334455ull);
    CHECK(m.at("wlan0").second == 7777888899ull);
}
