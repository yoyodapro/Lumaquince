// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <thread>
#include <utility>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "video_core/continuous_range_cache.h"

namespace {

// A byte-address translation oracle over independently mapped guest pages.
struct PageMap {
    u64 page_bits{12};
    u64 serial{};
    std::map<u64, u64> pages;
    std::vector<std::pair<u64, size_t>> walks;

    void Map(u64 page, u64 physical) {
        pages[page] = physical;
        ++serial;
    }
    void Unmap(u64 page) {
        pages.erase(page);
        ++serial;
    }
    std::optional<u64> Translate(u64 address) const {
        const auto it = pages.find(address >> page_bits);
        if (it == pages.end()) {
            return std::nullopt;
        }
        return (it->second << page_bits) + (address & ((u64{1} << page_bits) - 1));
    }
    size_t Expected(u64 address, size_t size) const {
        const auto first = Translate(address);
        if (!first) {
            return 0;
        }
        size_t covered = 0;
        while (covered < size) {
            const auto physical = Translate(address + covered);
            if (!physical || *physical != *first + covered) {
                break;
            }
            const size_t left_in_page = (u64{1} << page_bits) -
                                       ((address + covered) & ((u64{1} << page_bits) - 1));
            covered += std::min(size - covered, left_in_page);
        }
        return covered;
    }
    size_t Walk(u64 address, size_t size) {
        walks.emplace_back(address, size);
        return Expected(address, size);
    }
    size_t Query(VideoCommon::ContinuousRangeCache& cache, size_t owner, u64 address, size_t size) {
        return cache.Get(owner, serial, address, size, page_bits,
                         [this](u64 begin, size_t length) { return Walk(begin, length); });
    }
};

} // namespace

TEST_CASE("Page-run cache shares bounded walks and extends only when needed",
          "[video_core][continuous_range_cache]") {
    VideoCommon::ContinuousRangeCache cache;
    PageMap memory;
    for (u64 page = 0; page < 32; ++page) {
        memory.Map(page, page + 100);
    }
    CHECK(memory.Query(cache, 1, 0x1100, 0x100) == 0x100);
    REQUIRE(memory.walks.size() == 1);
    CHECK(memory.walks.back() == std::pair<u64, size_t>{0x1000, 0x200});
    CHECK(memory.Query(cache, 1, 0x1180, 0x40) == 0x40);
    CHECK(memory.walks.size() == 1);
    CHECK(memory.Query(cache, 1, 0x1200, 0x2500) == 0x2500);
    REQUIRE(memory.walks.size() == 2);
    CHECK(memory.walks.back() == std::pair<u64, size_t>{0x1000, 0x2700});
    CHECK(memory.Query(cache, 1, 0x1000, 0x1000) == 0x1000);
    CHECK(memory.walks.size() == 2);
}

TEST_CASE("Page-run cache respects fragmented allocations inside a big page",
          "[video_core][continuous_range_cache]") {
    VideoCommon::ContinuousRangeCache cache;
    PageMap memory;
    memory.Map(8, 100);
    memory.Map(9, 101);
    memory.Map(10, 500); // A physical discontinuity inside the same 64 KiB region.
    memory.Map(11, 501);
    CHECK(memory.Query(cache, 1, 0x8100, 0x6000) == 0x1f00);
    const auto calls = memory.walks.size();
    CHECK(memory.Query(cache, 1, 0x8200, 0x7000) == 0x1e00);
    CHECK(memory.walks.size() == calls); // A known break does not require extending.
    CHECK(memory.Query(cache, 1, 0xa100, 0x6000) == 0x1f00);
    CHECK(memory.Query(cache, 1, 0xc100, 0x100) == 0);
}

TEST_CASE("Every map epoch invalidates runs including a small unmap earlier in a big page",
          "[video_core][continuous_range_cache]") {
    VideoCommon::ContinuousRangeCache cache;
    PageMap memory;
    for (u64 page = 16; page < 32; ++page) {
        memory.Map(page, page + 100);
    }
    CHECK(memory.Query(cache, 1, 0x18000, 0x4000) == 0x4000);
    auto calls = memory.walks.size();
    memory.Map(100, 900); // Even unrelated changes invalidate the cached result.
    CHECK(memory.Query(cache, 1, 0x18000, 0x4000) == 0x4000);
    CHECK(memory.walks.size() > calls);
    calls = memory.walks.size();
    // Unmap's big-page path can clear the entire entry despite a small requested interval.
    // Model that effect and a new global epoch, not a request-range overlap heuristic.
    for (u64 page = 16; page < 32; ++page) {
        memory.Unmap(page);
    }
    CHECK(memory.Query(cache, 1, 0x18000, 0x4000) == 0);
    CHECK(memory.walks.size() > calls);
    memory.Map(24, 700);
    CHECK(memory.Query(cache, 1, 0x18000, 0x4000) == 0x1000);
    memory.Map(25, 701);
    CHECK(memory.Query(cache, 1, 0x18000, 0x4000) == 0x2000);
    memory.Map(25, 900); // Remapping an existing entry still changes the epoch.
    CHECK(memory.Query(cache, 1, 0x18000, 0x4000) == 0x1000);
}

TEST_CASE("Page runs are isolated by lifetime owner and survive cache collisions correctly",
          "[video_core][continuous_range_cache]") {
    VideoCommon::ContinuousRangeCache cache;
    PageMap first;
    first.Map(1, 10);
    first.Map(2, 11);
    PageMap second;
    second.Map(1, 20);
    second.Map(2, 30);
    REQUIRE(first.serial == second.serial);
    CHECK(first.Query(cache, 1, 0x1100, 0x1800) == 0x1800);
    CHECK(second.Query(cache, 2, 0x1100, 0x1800) == 0xf00);
    CHECK(first.Query(cache, 1, 0x1100, 0x1800) == 0x1800);
    // A replacement manager at the same object/ASID address receives a new unique owner.
    CHECK(second.Query(cache, 3, 0x1100, 0x1800) == 0xf00);
    first.Map(4097, 1000);
    CHECK(first.Query(cache, 1, 0x1100, 0x1800) == 0x1800);
    CHECK(first.Query(cache, 1, 4097 * 4096, 0x2000) == 0x1000);
    CHECK(first.Query(cache, 1, 0x1100, 0x1800) == 0x1800);
}

TEST_CASE("Page runs honor configured page size and empty or uncacheable requests",
          "[video_core][continuous_range_cache]") {
    VideoCommon::ContinuousRangeCache cache;
    PageMap memory;
    memory.page_bits = 16;
    memory.Map(1, 100);
    memory.Map(2, 101);
    CHECK(memory.Query(cache, 1, 0x18000, 0x10000) == 0x10000);
    CHECK(memory.Query(cache, 1, 0x1f000, 0x1000) == 0x1000);
    CHECK(memory.walks.size() == 1);
    CHECK(memory.Query(cache, 1, 0x18000, 0) == 0);
    CHECK(memory.walks.size() == 1);
    size_t calls = 0;
    const auto fallback = [&](u64, size_t) { ++calls; return size_t{7}; };
    CHECK(cache.Get(0, 0, 0x1000, 64, 12, fallback) == 7);
    CHECK(cache.Get(1, 0, 0x1000, 64, 64, fallback) == 7);
    CHECK(cache.Get(1, 0, std::numeric_limits<u64>::max() - 3, 8, 12, fallback) == 7);
    CHECK(calls == 3);
}

TEST_CASE("Page-run cache falls back when a run from the page start ends before the query",
          "[video_core][continuous_range_cache]") {
    VideoCommon::ContinuousRangeCache cache;
    size_t fallback_calls = 0;
    const auto walker = [&](u64 address, size_t size) {
        if (address == 0x1000) {
            return size_t{0};
        }
        ++fallback_calls;
        return size;
    };
    CHECK(cache.Get(1, 0, 0x1800, 64, 12, walker) == 64);
    CHECK(cache.Get(1, 0, 0x1800, 128, 12, walker) == 128);
    CHECK(fallback_calls == 2);
}

TEST_CASE("Random fragmented mappings agree with an independent address translation walk",
          "[video_core][continuous_range_cache]") {
    VideoCommon::ContinuousRangeCache cache;
    PageMap memory;
    std::mt19937 random{0x179};
    for (u64 page = 0; page < 64; ++page) {
        memory.Map(page, page + 100);
    }
    for (size_t iteration = 0; iteration < 2000; ++iteration) {
        if (iteration % 17 == 0) {
            const u64 page = random() % 64;
            if (random() % 3 == 0) {
                memory.Unmap(page);
            } else {
                memory.Map(page, random() % 200);
            }
        }
        const u64 address = random() % (64 * 4096);
        const size_t size = random() % (32 * 4096);
        CHECK(memory.Query(cache, 1, address, size) == memory.Expected(address, size));
    }
}

TEST_CASE("Independent thread-owned page caches do not exchange runs",
          "[video_core][continuous_range_cache]") {
    std::array<size_t, 2> results{};
    std::array<size_t, 2> walks{};
    std::vector<std::jthread> threads;
    for (size_t index = 0; index < 2; ++index) {
        threads.emplace_back([&, index] {
            thread_local VideoCommon::ContinuousRangeCache cache;
            const auto walker = [&](u64, size_t size) {
                ++walks[index];
                return index == 0 ? size : size_t{32};
            };
            for (size_t repeat = 0; repeat < 20; ++repeat) {
                results[index] = cache.Get(1, 1, 0x1000, 64, 12, walker);
            }
        });
    }
    threads.clear();
    CHECK(results == std::array<size_t, 2>{64, 32});
    CHECK(walks == std::array<size_t, 2>{1, 1});
}
