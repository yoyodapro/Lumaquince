// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "video_core/query_cache/copy_batches.h"

namespace {
using VideoCommon::QueryCopy;

size_t CheckCopies(std::span<const QueryCopy> copies) {
    std::array<u8, 128> source;
    for (size_t i = 0; i < source.size(); ++i) {
        source[i] = static_cast<u8>(i * 17 + 3);
    }
    // Two obtained guest ranges may resolve to the same underlying buffer.
    constexpr std::array<size_t, 3> physical_buffer{0, 1, 0};
    std::array<std::vector<u8>, 2> expected{std::vector<u8>(80000, 0xcd),
                                         std::vector<u8>(80000, 0xcd)};
    auto actual = expected;
    const auto apply = [&](auto& output, const QueryCopy& copy) {
        REQUIRE(copy.src_offset + copy.size <= source.size());
        REQUIRE(copy.dst_offset + copy.size <= output[0].size());
        std::copy_n(source.begin() + copy.src_offset, copy.size,
                    output[physical_buffer[copy.destination]].begin() + copy.dst_offset);
    };
    // Independent byte-wise reference: guest writes take effect in submitted order.
    for (const auto& copy : copies) {
        for (size_t byte = 0; byte < copy.size; ++byte) {
            expected[physical_buffer[copy.destination]][copy.dst_offset + byte] =
                source[copy.src_offset + byte];
        }
    }
    size_t emitted = 0;
    size_t batches = 0;
    VideoCommon::ForEachQueryCopyBatch(copies, [&](auto batch) {
        ++batches;
        REQUIRE_FALSE(batch.empty());
        CHECK(batch.data() == copies.data() + emitted);
        emitted += batch.size();
        for (size_t i = 0; i < batch.size(); ++i) {
            CHECK(batch[i].destination == batch[0].destination);
            for (size_t j = 0; j < i; ++j) {
                CHECK((batch[i].dst_offset + batch[i].size <= batch[j].dst_offset ||
                       batch[j].dst_offset + batch[j].size <= batch[i].dst_offset));
            }
        }
        // Vulkan may execute regions of one command in any order. Reverse them deliberately.
        for (size_t i = batch.size(); i > 0; --i) {
            apply(actual, batch[i - 1]);
        }
    });
    CHECK(emitted == copies.size());
    CHECK(actual == expected);
    return batches;
}
} // namespace

TEST_CASE("Query copies retain repeated live validation destinations", "[video_core][query_copy]") {
    const std::array copies{
        QueryCopy{0, 0, 73792, 4}, QueryCopy{0, 4, 73792, 4},
        QueryCopy{0, 8, 75072, 8}, QueryCopy{0, 16, 75072, 8},
        QueryCopy{0, 24, 75072, 8},
    };
    CHECK(CheckCopies(copies) == 4);
}

TEST_CASE("Query copies split partial and nonadjacent overlaps", "[video_core][query_copy]") {
    const std::array copies{
        QueryCopy{0, 0, 16, 8}, QueryCopy{0, 16, 40, 8}, QueryCopy{0, 32, 20, 8},
        QueryCopy{0, 48, 12, 12}, QueryCopy{0, 64, 16, 4},
    };
    CHECK(CheckCopies(copies) == 4);
}

TEST_CASE("Query copies preserve interleaved aliased destination order", "[video_core][query_copy]") {
    const std::array copies{
        QueryCopy{0, 0, 16, 8}, QueryCopy{1, 8, 16, 8}, QueryCopy{2, 16, 20, 8},
        QueryCopy{0, 24, 16, 8},
    };
    CHECK(CheckCopies(copies) == 4);
}

TEST_CASE("Query copies batch disjoint and adjacent ranges with shared sources",
          "[video_core][query_copy]") {
    const std::array copies{
        QueryCopy{0, 0, 16, 8}, QueryCopy{0, 0, 24, 8}, QueryCopy{0, 4, 0, 8},
    };
    CHECK(CheckCopies(copies) == 1);
    CHECK(CheckCopies({}) == 0);
}

TEST_CASE("Query copy permutations match sequential byte writes", "[video_core][query_copy]") {
    constexpr std::array destinations{0U, 4U, 8U, 12U};
    for (const auto first : destinations) {
        for (const auto second : destinations) {
            for (const auto third : destinations) {
                for (const auto size : {4U, 8U}) {
                    const std::array copies{
                        QueryCopy{0, 0, first, 8}, QueryCopy{0, 16, second, size},
                        QueryCopy{0, 32, third, 8},
                    };
                    CheckCopies(copies);
                }
            }
        }
    }
}

TEST_CASE("Query copies retain a large disjoint batch", "[video_core][query_copy]") {
    constexpr size_t count = 16384;
    std::array<u8, 128> source;
    for (size_t i = 0; i < source.size(); ++i) {
        source[i] = static_cast<u8>(i * 17 + 3);
    }
    std::vector<QueryCopy> copies;
    copies.reserve(count);
    std::vector<u8> expected(count * 8, 0xcd);
    auto actual = expected;
    for (size_t i = 0; i < count; ++i) {
        // Visit disjoint ranges out of address order, leaving untouched gaps.
        const size_t destination = ((i * 8191) % count) * 8;
        const size_t source_offset = (i % 32) * 4;
        copies.push_back(QueryCopy{0, source_offset, destination, 4});
        for (size_t byte = 0; byte < 4; ++byte) {
            expected[destination + byte] = source[source_offset + byte];
        }
    }
    size_t batches = 0;
    VideoCommon::ForEachQueryCopyBatch(copies, [&](auto batch) {
        ++batches;
        REQUIRE(batch.data() == copies.data());
        REQUIRE(batch.size() == copies.size());
        for (size_t i = batch.size(); i > 0; --i) {
            const auto& copy = batch[i - 1];
            std::copy_n(source.begin() + copy.src_offset, copy.size,
                        actual.begin() + copy.dst_offset);
        }
    });
    CHECK(batches == 1);
    CHECK(actual == expected);
}
