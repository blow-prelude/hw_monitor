#include "collectors/npu/NpuCollector.h"
#include "collectors/util.h"
#include <doctest.h>

using namespace hwmon;

TEST_CASE("parseCompatible：RK 识别（NUL 分多串）") {
    const auto content = util::readFile(FIXTURE_DIR "/compatible.rk");
    REQUIRE(content);
    const auto r = parseCompatible(*content);
    CHECK(r.kind == NpuKind::Rockchip);
    CHECK(r.chip == "rk3588s");  // 首 token 优先
    CHECK(rkModelName(r.chip) == "Rockchip RK3588S NPU");

    const auto r2 = parseCompatible(std::string("rockchip,rk3588"));  // 末尾无 NUL 也可
    CHECK(r2.kind == NpuKind::Rockchip);
    CHECK(r2.chip == "rk3588");
    CHECK(rkModelName("rk3588") == "Rockchip RK3588 NPU");
}

TEST_CASE("parseCompatible：其他厂商 / 无 DT") {
    const auto content = util::readFile(FIXTURE_DIR "/compatible.other");
    REQUIRE(content);
    const auto r = parseCompatible(*content);
    CHECK(r.kind == NpuKind::Unrecognized);
    CHECK(r.chip.empty());

    CHECK(parseCompatible("").kind == NpuKind::NoDeviceTree);
    CHECK(rkModelName("rk1234") == "Rockchip RK1234 NPU");  // 未收录 → 大写回退
}

TEST_CASE("parseRknpuLoad：容忍多种格式") {
    const auto content = util::readFile(FIXTURE_DIR "/rknpu_load");
    REQUIRE(content);
    REQUIRE(parseRknpuLoad(*content));
    CHECK(*parseRknpuLoad(*content) == 42);          // "load : 42"

    CHECK(*parseRknpuLoad("rknpu load : 87%\n") == 87);
    CHECK(*parseRknpuLoad("Full Load : 0") == 0);
    CHECK_FALSE(parseRknpuLoad("load : "));
    CHECK_FALSE(parseRknpuLoad("garbage no digits"));
}
