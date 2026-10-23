#include "collectors/gpu/GpuCollector.h"
#include <doctest.h>

using namespace hwmon;

TEST_CASE("stripHexPrefix：pci id 文本") {
    CHECK(stripHexPrefix("0x1002\n") == "1002");
    CHECK(stripHexPrefix("0x744C") == "744C");
    CHECK(stripHexPrefix("1002") == "1002");
    CHECK(stripHexPrefix("") == "");
}
