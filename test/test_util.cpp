#include <string>

#include "collectors/util.h"
#include <doctest.h>

using namespace hwmon::util;

TEST_CASE("trim") {
    CHECK(trim("  hello \t\n") == "hello");
    CHECK(trim("plain") == "plain");
    CHECK(trim("   ") == "");
    CHECK(trim("") == "");
}

TEST_CASE("splitLines") {
    auto v = splitLines("a\nbb\r\nccc");
    REQUIRE(v.size() == 3);
    CHECK(v[0] == "a");
    CHECK(v[1] == "bb");
    CHECK(v[2] == "ccc");
    CHECK(splitLines("").empty());
    CHECK(splitLines("x\n").size() == 1);  // 尾换行不产生空行
}

TEST_CASE("splitWS") {
    auto v = splitWS("  a\t bb \n ccc ");
    REQUIRE(v.size() == 3);
    CHECK(v[0] == "a");
    CHECK(v[1] == "bb");
    CHECK(v[2] == "ccc");
    CHECK(splitWS("   ").empty());
}

TEST_CASE("keyValue") {
    const std::string content =
        "processor\t: 0\nmodel name\t: Intel(R) Test\nmodel name\t: Second\nflags\t\t: fpu\n";
    CHECK(keyValue(content, "model name") == "Intel(R) Test");  // 同名取首行
    CHECK(keyValue(content, "processor") == "0");
    CHECK_FALSE(keyValue(content, "missing"));
    CHECK_FALSE(keyValue("no colon line\n", "x"));
}

TEST_CASE("parseUll / parseHex / parseDouble") {
    CHECK(parseUll("42") == 42ull);
    CHECK(parseUll(" 16308216 kB") == 16308216ull);
    CHECK_FALSE(parseUll("-5"));
    CHECK_FALSE(parseUll("abc"));
    CHECK_FALSE(parseUll(""));
    CHECK(parseHex("0x41") == 0x41ull);
    CHECK(parseHex("0d0a") == 0xd0aull);
    CHECK_FALSE(parseHex("xyz"));
    CHECK(parseDouble("123.5") == 123.5);
    CHECK(parseDouble("900000000") == 900000000.0);
    CHECK_FALSE(parseDouble("kb"));
}

TEST_CASE("readNumberFile / readUllFile 对 fixtures 读数") {
    const auto cur = readNumberFile(FIXTURE_DIR "/devfreq_cur");
    REQUIRE(cur);
    CHECK(*cur == 900000000.0);  // devfreq Hz 原文
    const auto mx = readUllFile(FIXTURE_DIR "/devfreq_max");
    REQUIRE(mx);
    CHECK(*mx == 1000000000ull);
    CHECK_FALSE(readNumberFile(FIXTURE_DIR "/no_such_file"));
}

TEST_CASE("readFile 容忍空内容与畸形行并存") {
    const auto s = readFile(FIXTURE_DIR "/rknpu_load");
    REQUIRE(s);
    CHECK(*s == "load : 42\n");
}
