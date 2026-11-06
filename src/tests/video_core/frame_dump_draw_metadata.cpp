// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "video_core/renderer_vulkan/frame_dump_draw_metadata.h"

TEST_CASE("Frame dump preserves every shader slot beyond the pass hash cap",
          "[video_core][frame_dump_metadata]") {
    Vulkan::FrameDumpDrawMetadata metadata;
    for (u32 draw = 0; draw < 80; ++draw) {
        const std::array<u64, 6> hashes{0, 0x1000000000000000ULL + draw, draw, 0,
                                      0xffffffffffffffffULL, 0xa240600c62ea441eULL};
        const auto line = metadata.Record(draw, draw / 40, hashes);
        REQUIRE(line);
        const auto record = nlohmann::json::parse(*line);
        CHECK(record.at("kind") == "draw");
        CHECK(record.at("draw") == draw);
        CHECK(record.at("pass") == draw / 40);
        REQUIRE(record.at("shader_slots").size() == 6);
        for (size_t slot = 0; slot < hashes.size(); ++slot) {
            const auto hash = record.at("shader_slots").at(slot).get<std::string>();
            CHECK(hash.size() == 16);
            CHECK(std::stoull(hash, nullptr, 16) == hashes[slot]);
        }
    }
}

TEST_CASE("Frame dump metadata has a separate explicit bound and resets per capture",
          "[video_core][frame_dump_metadata]") {
    Vulkan::FrameDumpDrawMetadata metadata;
    const std::array<u64, 6> hashes{};
    for (u32 draw = 0; draw < Vulkan::FrameDumpDrawMetadata::MaxDraws; ++draw) {
        REQUIRE(metadata.Record(draw, 0, hashes));
    }
    auto summary = nlohmann::json::parse("{" + metadata.Summary() + "}");
    CHECK_FALSE(summary.at("draw_metadata_truncated").get<bool>());
    for (u32 draw = 0; draw < 7; ++draw) {
        CHECK_FALSE(metadata.Record(Vulkan::FrameDumpDrawMetadata::MaxDraws + draw, 1, hashes));
    }
    summary = nlohmann::json::parse("{" + metadata.Summary() + "}");
    CHECK(summary.at("draw_metadata_limit") == Vulkan::FrameDumpDrawMetadata::MaxDraws);
    CHECK(summary.at("draw_metadata_records") == Vulkan::FrameDumpDrawMetadata::MaxDraws);
    CHECK(summary.at("draw_metadata_dropped") == 7);
    CHECK(summary.at("draw_metadata_truncated").get<bool>());
    metadata = {};
    REQUIRE(metadata.Record(0, 0, hashes));
    summary = nlohmann::json::parse("{" + metadata.Summary() + "}");
    CHECK(summary.at("draw_metadata_records") == 1);
    CHECK(summary.at("draw_metadata_dropped") == 0);
    CHECK_FALSE(summary.at("draw_metadata_truncated").get<bool>());
}
