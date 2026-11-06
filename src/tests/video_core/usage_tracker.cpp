// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <limits>
#include <random>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "video_core/buffer_cache/usage_tracker.h"

namespace {

// Independent occupancy oracle, with one boolean per 64-byte granule, no page/block masks.
class Occupancy {
public:
    explicit Occupancy(size_t size) : bits(size / 64) {}
    void Track(size_t offset, size_t size) {
        if (size != 0) {
            for (size_t bit = offset / 64; bit <= (offset + size - 1) / 64; ++bit) {
                bits.at(bit) = true;
            }
        }
    }
    bool IsUsed(size_t offset, size_t size) const {
        if (size != 0) {
            for (size_t bit = offset / 64; bit <= (offset + size - 1) / 64; ++bit) {
                if (bits.at(bit)) {
                    return true;
                }
            }
        }
        return false;
    }
    void Reset() {
        std::fill(bits.begin(), bits.end(), false);
    }
private:
    std::vector<bool> bits;
};

} // namespace

TEST_CASE("Usage tracking includes every intersected granule and no empty ranges",
          "[video_core][usage_tracker]") {
    VideoCommon::UsageTracker tracker{0x10000};
    CHECK_FALSE(tracker.IsUsed(0, 1));
    tracker.Track(0, 1);
    CHECK(tracker.IsUsed(0, 1));
    CHECK(tracker.IsUsed(63, 1));
    CHECK_FALSE(tracker.IsUsed(64, 1));
    CHECK_FALSE(tracker.IsUsed(0, 0));
    tracker.Reset();
    tracker.Track(63, 2);
    CHECK(tracker.IsUsed(0, 64));
    CHECK(tracker.IsUsed(64, 1));
    CHECK_FALSE(tracker.IsUsed(128, 1));
    tracker.Reset();
    tracker.Track(4095, 2);
    CHECK(tracker.IsUsed(4095, 1));
    CHECK(tracker.IsUsed(4096, 1));
    CHECK_FALSE(tracker.IsUsed(4031, 1));
    CHECK_FALSE(tracker.IsUsed(4160, 1));
    tracker.Reset();
    tracker.Track(0, 4096);
    CHECK(tracker.IsUsed(4095, 1));
    CHECK_FALSE(tracker.IsUsed(4096, 1));
    tracker.Track(4096, 0);
    CHECK_FALSE(tracker.IsUsed(4096, 1));
}

TEST_CASE("Full usage blocks preserve interior queries and reset all remembered ranges",
          "[video_core][usage_tracker]") {
    constexpr size_t capacity = 2 * 1024 * 1024;
    VideoCommon::UsageTracker tracker{capacity};
    Occupancy expected{capacity};
    tracker.Reset();
    for (const auto [offset, size] : {
             std::pair<size_t, size_t>{0, 129 * 4096}, {63 * 4096 + 1, 130 * 4096 + 3},
             {255 * 4096, 65 * 4096}, {400 * 4096 + 63, 2}}) {
        tracker.Track(offset, size);
        expected.Track(offset, size);
    }
    for (size_t offset = 0; offset < capacity; offset += 64) {
        CHECK(tracker.IsUsed(offset, 1) == expected.IsUsed(offset, 1));
    }
    tracker.Reset();
    tracker.Reset();
    CHECK_FALSE(tracker.IsUsed(0, capacity));
    tracker.Track(400 * 4096 + 63, 2); // Was in the recent-range cache before Reset.
    CHECK(tracker.IsUsed(400 * 4096 + 64, 1));
    CHECK_FALSE(tracker.IsUsed(64 * 4096, 1));
}

TEST_CASE("Mixed usage ranges agree with independent granule occupancy",
          "[video_core][usage_tracker]") {
    constexpr size_t capacity = 1024 * 1024;
    VideoCommon::UsageTracker tracker{capacity};
    Occupancy expected{capacity};
    std::mt19937 random{0x178};
    for (size_t iteration = 0; iteration < 1600; ++iteration) {
        if (iteration % 11 == 0) {
            tracker.Reset();
            expected.Reset();
        }
        const size_t offset = random() % capacity;
        const size_t size = random() % (capacity - offset + 1);
        tracker.Track(offset, size);
        tracker.Track(offset, size);
        expected.Track(offset, size);
        for (size_t query = 0; query < 8; ++query) {
            const size_t begin = random() % capacity;
            const size_t length = random() % (capacity - begin + 1);
            CHECK(tracker.IsUsed(begin, length) == expected.IsUsed(begin, length));
        }
    }
}

TEST_CASE("Full usage blocks cross the block-bitset word boundary",
          "[video_core][usage_tracker]") {
    constexpr u64 boundary = 64 * 64 * 4096;
    VideoCommon::UsageTracker tracker{boundary + 2 * 64 * 4096};
    const u64 begin = boundary - 65 * 4096;
    const u64 size = 130 * 4096;
    tracker.Track(begin, size);
    CHECK(tracker.IsUsed(boundary - 1, 1));
    CHECK(tracker.IsUsed(boundary, 1));
    CHECK(tracker.IsUsed(boundary + 63 * 4096, 1));
    CHECK_FALSE(tracker.IsUsed(begin - 1, 1));
    CHECK_FALSE(tracker.IsUsed(begin + size, 1));
    tracker.Reset();
    tracker.Reset();
    CHECK_FALSE(tracker.IsUsed(boundary, 1));
    tracker.Track(begin, size);
    CHECK(tracker.IsUsed(boundary, 1));
}

TEST_CASE("Usage tracking rejects overflowing and out-of-range spans",
          "[video_core][usage_tracker]") {
    VideoCommon::UsageTracker tracker{4096};
    tracker.Track(8192, 64);
    tracker.Track(10, std::numeric_limits<u64>::max());
    tracker.Track(std::numeric_limits<u64>::max(), 0);
    CHECK_FALSE(tracker.IsUsed(0, 4096));
    CHECK_FALSE(tracker.IsUsed(8192, 64));
    CHECK_FALSE(tracker.IsUsed(10, std::numeric_limits<u64>::max()));
    tracker.Reset();
    tracker.Track(0, 1);
    CHECK(tracker.IsUsed(0, 1));
}
