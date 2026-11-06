// SPDX-License-Identifier: GPL-3.0-or-later
#include <utility>
#include <catch2/catch_test_macros.hpp>
#include "common/range_sets.h"
#include "common/range_sets.inc"

TEST_CASE("Empty range queries visit no GPU-owned spans", "[range_move][range_set]") {
    Common::RangeSet<u64> ranges;
    ranges.Add(0x1000, 0x1000);
    ranges.Add(0x4000, 0x800);
    for (u64 address : {0ULL, 0x1000ULL, 0x1800ULL, 0x2000ULL, 0x3000ULL, 0x4800ULL}) {
        INFO("empty query at " << address);
        ranges.ForEachInRange(address, 0, [](u64, u64) { FAIL("Empty query visited a range"); });
        CHECK_FALSE(ranges.Intersects(address, 0));
    }
    u64 bytes{};
    ranges.ForEachInRange(0x1800, 0x3000, [&](u64 begin, u64 end) {
        REQUIRE(begin < end);
        bytes += end - begin;
    });
    CHECK(bytes == 0x1000);
}

TEST_CASE("Moved cache range sets are empty, reusable and self-assignable", "[range_move]") {
    Common::RangeSet<u64> first;
    first.Add(100, 10);
    Common::RangeSet<u64> second{std::move(first)};
    REQUIRE(first.Empty());
    REQUIRE(second.Intersects(100, 10));
    first.Add(300, 5);
    CHECK_FALSE(first.Intersects(100, 10));
    auto& assigned = (first = std::move(second));
    CHECK(&assigned == &first);
    CHECK(second.Empty());
    CHECK(first.Intersects(100, 10));
    CHECK_FALSE(first.Intersects(300, 5));
    auto& self = first;
    first = std::move(self);
    CHECK(first.Intersects(100, 10));

    Common::OverlapRangeSet<u64> overlapping;
    overlapping.Add(10, 10);
    overlapping.Add(15, 10);
    Common::OverlapRangeSet<u64> moved{std::move(overlapping)};
    CHECK(overlapping.Empty());
    CHECK_FALSE(moved.Empty());
    auto& result = (overlapping = std::move(moved));
    CHECK(&result == &overlapping);
    CHECK(moved.Empty());
    int ranges = 0;
    overlapping.ForEach([&](u64, u64, s32 count) {
        CHECK(count >= 1);
        ++ranges;
    });
    CHECK(ranges == 3);
    auto& overlap_self = overlapping;
    overlapping = std::move(overlap_self);
    CHECK_FALSE(overlapping.Empty());
}
