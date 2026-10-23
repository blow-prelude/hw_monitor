// RingBuffer 单测（01-core §10：空/未满/满/回绕覆盖/snapshot 顺序）
#include "core/ring_buffer.h"

#include <vector>

#include "doctest.h"

using hwmon::RingBuffer;

TEST_CASE("RingBuffer：空缓冲") {
    RingBuffer<int> rb(4);
    CHECK(rb.size() == 0);
    CHECK(rb.snapshot().empty());
}

TEST_CASE("RingBuffer：未满按时间序拷出") {
    RingBuffer<int> rb(4);
    rb.push(1);
    rb.push(2);
    CHECK(rb.size() == 2);
    CHECK(rb.snapshot() == std::vector<int>{1, 2});
}

TEST_CASE("RingBuffer：恰好满") {
    RingBuffer<int> rb(3);
    for (int v : {1, 2, 3}) rb.push(v);
    CHECK(rb.size() == 3);
    CHECK(rb.snapshot() == std::vector<int>{1, 2, 3});
}

TEST_CASE("RingBuffer：满后回绕覆盖最旧") {
    RingBuffer<int> rb(3);
    for (int v : {1, 2, 3, 4, 5}) rb.push(v);
    CHECK(rb.size() == 3);
    CHECK(rb.snapshot() == std::vector<int>{3, 4, 5});
}

TEST_CASE("RingBuffer：容量 1 恒存最新") {
    RingBuffer<int> rb(1);
    rb.push(7);
    rb.push(9);
    CHECK(rb.size() == 1);
    CHECK(rb.snapshot() == std::vector<int>{9});
}
