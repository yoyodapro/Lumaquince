// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "video_core/pipeline_preload.h"

namespace {

std::string Index(u32 count, std::initializer_list<std::pair<u64, u8>> records,
                  u32 magic = VideoCommon::UsedPipelinesMagic) {
    std::ostringstream stream(std::ios::binary);
    stream.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    stream.write(reinterpret_cast<const char*>(&count), sizeof(count));
    for (const auto& [hash, age] : records) {
        stream.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
        stream.write(reinterpret_cast<const char*>(&age), sizeof(age));
    }
    return stream.str();
}

auto Read(const std::string& bytes, u8 max_age = 3) {
    std::istringstream stream(bytes, std::ios::binary);
    return VideoCommon::ReadUsedPipelineIndex(stream, max_age);
}

struct Key {
    u64 shader;
    std::array<u32, 3> dimensions;
    bool operator==(const Key&) const = default;
};

struct CollidingHash {
    size_t operator()(const Key&) const {
        return 0;
    }
};

} // namespace

TEST_CASE("Used pipeline index retains only live entries without changing duplicate precedence",
          "[pipeline_preload]") {
    const auto bytes = Index(5, {{11, 0}, {12, 2}, {13, 3}, {11, 1}, {14, 255}});
    const auto used = Read(bytes);
    REQUIRE(used.size() == 2);
    CHECK(used.at(11) == 0);
    CHECK(used.at(12) == 2);
    CHECK(Read(bytes, 0).empty());
    CHECK(Read(Index(0, {})).empty());
    CHECK(Read(Index(1, {{1, 0}}, 0)).empty());
}

TEST_CASE("Truncated used pipeline indexes cannot reserve from their declared count",
          "[pipeline_preload]") {
    const auto complete = Index(3, {{11, 0}, {12, 1}, {13, 2}});
    for (size_t length = 0; length < complete.size(); ++length) {
        INFO(length);
        CHECK(Read(complete.substr(0, length)).empty());
    }
    REQUIRE(Read(complete).size() == 3);
    // A tiny corrupt file previously reserved billions of buckets before its first failed read.
    CHECK(Read(Index(std::numeric_limits<u32>::max(), {})).empty());
    CHECK(Read(Index(std::numeric_limits<u32>::max(), {{11, 0}})).empty());
}

TEST_CASE("Preload limits count unique full keys rather than duplicate records or hashes",
          "[pipeline_preload]") {
    VideoCommon::PipelinePreloadQueue<Key, CollidingHash> queue{3};
    const Key first{42, {32, 1, 1}};
    const Key different_dimensions{42, {64, 1, 1}};
    const Key different_shader{43, {32, 1, 1}};
    REQUIRE(queue.TryQueue(first));
    for (size_t duplicate = 0; duplicate < 20; ++duplicate) {
        CHECK_FALSE(queue.TryQueue(first));
    }
    CHECK(queue.Size() == 1);
    CHECK(queue.Contains(first));
    CHECK_FALSE(queue.Contains(different_dimensions));
    REQUIRE(queue.TryQueue(different_dimensions));
    REQUIRE(queue.TryQueue(different_shader));
    CHECK(queue.Size() == 3);
    CHECK_FALSE(queue.TryQueue(Key{44, {32, 1, 1}}));
    CHECK_FALSE(queue.TryQueue(first));
    CHECK(queue.Contains(first));
}

TEST_CASE("Unlimited and empty preload budgets still deduplicate exact keys", "[pipeline_preload]") {
    const Key first{42, {32, 1, 1}};
    VideoCommon::PipelinePreloadQueue<Key, CollidingHash> unlimited;
    REQUIRE(unlimited.TryQueue(first));
    CHECK_FALSE(unlimited.TryQueue(first));
    VideoCommon::PipelinePreloadQueue<Key, CollidingHash> none{0};
    CHECK_FALSE(none.TryQueue(first));
    CHECK(none.Size() == 0);
    // A new preload invocation has no stale in-flight keys from a previous/cancelled load.
    VideoCommon::PipelinePreloadQueue<Key, CollidingHash> next;
    CHECK(next.TryQueue(first));
}
