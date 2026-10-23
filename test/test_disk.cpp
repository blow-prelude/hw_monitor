#include "collectors/disk/DiskCollector.h"
#include "collectors/util.h"
#include <doctest.h>

using namespace hwmon;

TEST_CASE("isFilteredBlockDevice") {
    CHECK(isFilteredBlockDevice("loop0"));
    CHECK(isFilteredBlockDevice("ram0"));
    CHECK(isFilteredBlockDevice("zram0"));
    CHECK(isFilteredBlockDevice("sr0"));
    CHECK(isFilteredBlockDevice("md0"));
    CHECK(isFilteredBlockDevice("dm-0"));
    CHECK_FALSE(isFilteredBlockDevice("sda"));
    CHECK_FALSE(isFilteredBlockDevice("nvme0n1"));
    CHECK_FALSE(isFilteredBlockDevice("vda"));
    CHECK_FALSE(isFilteredBlockDevice("mmcblk0"));  // "md" 前缀不误伤
}

TEST_CASE("parseDiskStats：字段口径与畸形行") {
    const auto content = util::readFile(FIXTURE_DIR "/diskstats");
    REQUIRE(content);
    const auto m = parseDiskStats(*content);
    CHECK_FALSE(m.empty());
    CHECK_FALSE(m.count("sdb1"));  // fixture 追加的截断行被跳过

    // 1-based：name=3, sectors_read=6, sectors_written=10
    const auto one = parseDiskStats(
        "   8       0 sda 100 1 200 2 300 3 400 4 0 0 0 0 0 0 0\n");
    REQUIRE(one.count("sda") == 1);
    CHECK(one.at("sda").first == 200);
    CHECK(one.at("sda").second == 400);

    CHECK(parseDiskStats("   8      16 sdb1\n").empty());      // 字段不足
    CHECK(parseDiskStats("1 2 3 4 5 6 7 8 x y\n").empty());    // 非数字
}

TEST_CASE("decodeMountEscapes") {
    CHECK(decodeMountEscapes("/mnt/with\\040space") == "/mnt/with space");
    CHECK(decodeMountEscapes("plain/path") == "plain/path");
    CHECK(decodeMountEscapes("trunc\\04") == "trunc\\04");  // 不足三位原样
}

TEST_CASE("parseMountsRaw + filterMounts") {
    const auto content = util::readFile(FIXTURE_DIR "/mounts");
    REQUIRE(content);
    const auto raw = parseMountsRaw(content ? *content : "");
    CHECK_FALSE(raw.empty());

    // fixture 追加行：\040 解码、fuse.* 与非 /dev 的 dev 由 filter 剔除
    bool sawEscaped = false;
    for (const auto& m : raw)
        if (m.dev == "/dev/sdz1") {
            CHECK(m.mount == "/mnt/with space");
            sawEscaped = true;
        }
    CHECK(sawEscaped);
    const auto filtered = filterMounts(raw);
    for (const auto& m : filtered) {
        CHECK(m.dev.rfind("/dev/", 0) == 0);
        CHECK_FALSE(m.fs.rfind("fuse.", 0) == 0);
    }

    // 过滤与去重规则
    const std::vector<MountEntry> synthetic = {
        {"/dev/sda1", "/", "ext4"},
        {"tmpfs", "/tmp", "tmpfs"},
        {"proc", "/proc", "proc"},
        {"sysfs", "/sys", "sysfs"},
        {"overlay", "/var/lib/docker", "overlay"},
        {"/dev/sda1", "/mnt/other", "ext4"},   // 同 dev 多挂载点 → 保留第一个
        {"/dev/sdb1", "/snap", "fuse.snapfuse"},
        {"nodev", "/mnt/x", "ext4"},
        {"/dev/sdc1", "/mnt/w", "squashfs"},
    };
    const auto out = filterMounts(synthetic);
    REQUIRE(out.size() == 1);
    CHECK(out[0].dev == "/dev/sda1");
    CHECK(out[0].mount == "/");
    CHECK(out[0].fs == "ext4");
}
