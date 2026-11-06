// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/framebuffer_clear_layers.h"
#include "video_core/texture_cache/clear_plan.h"

namespace {
using namespace VideoCommon;

ClearImageCoverage ColorImage(u32 width = 4, u32 height = 4) {
    return {.attachment = 0, .width = width, .height = height, .levels = 1, .layers = 1,
            .samples = 1, .view = {}, .is_2d = true, .storage_matches_view = true,
            .has_color = true};
}

ClearPlan FullClear(u32 width = 4, u32 height = 4) {
    return {.rect = {0, 0, width, height}, .layer_count = 1, .color_attachment = 0,
            .color_mask = 0xF};
}

// Exercise the production prepare/finish dispatch and ownership transition with visible data.
// The callbacks stand in for the runtime upload, tracking, and recorded clear commands.
struct PendingImage {
    ImageFlagBits flags{ImageFlagBits::CpuModified};
    std::array<std::array<u8, 4>, 16> guest{};
    std::array<std::array<u8, 4>, 16> host{};
    unsigned uploads{};
    unsigned tracks{};

    PendingImage() {
        for (auto& pixel : guest) {
            pixel = {11, 22, 33, 44};
        }
        for (auto& pixel : host) {
            pixel.fill(0xCD);
        }
    }

    void Prepare(bool modified, bool invalidate) {
        PrepareImageContents(*this, invalidate, [&] {
            if (True(flags & ImageFlagBits::CpuModified)) {
                host = guest;
                ++uploads;
                flags &= ~ImageFlagBits::CpuModified;
                Track();
            }
        }, [&] { Track(); });
        if (modified) {
            flags |= ImageFlagBits::GpuModified;
        }
    }

    void Track() {
        if (False(flags & ImageFlagBits::Tracked)) {
            ++tracks;
            flags |= ImageFlagBits::Tracked;
        }
    }

    void Begin(const ClearPlan& clear, const ClearImageCoverage& image) {
        PrepareClearTarget(clear, image, [&](bool modified, bool invalidate) {
            Prepare(modified, invalidate);
        });
    }

    void Finish(const ClearPlan& clear, const ClearImageCoverage& image) {
        FinishClearTarget(clear, image, [&](bool modified, bool invalidate) {
            Prepare(modified, invalidate);
        });
    }

    void RecordColorClear(const ClearPlan& clear) {
        if (!clear.Writes(0)) {
            return;
        }
        for (u32 y = clear.rect.y; y < clear.rect.y + clear.rect.height; ++y) {
            for (u32 x = clear.rect.x; x < clear.rect.x + clear.rect.width; ++x) {
                for (u32 c = 0; c < 4; ++c) {
                    if ((clear.color_mask & (1U << c)) != 0) {
                        host.at(y * 4 + x)[c] = 99;
                    }
                }
            }
        }
    }
};
} // namespace

TEST_CASE("Full clear uploads are skipped only after the clear is recorded", "[video_core][clear_plan]") {
    const auto clear = FullClear();
    const auto coverage = ColorImage();
    PendingImage image;
    image.Begin(clear, coverage);
    REQUIRE(image.uploads == 0);
    REQUIRE(True(image.flags & ImageFlagBits::CpuModified));
    REQUIRE(False(image.flags & ImageFlagBits::GpuModified));
    image.RecordColorClear(clear);
    image.Finish(clear, coverage);
    REQUIRE(image.uploads == 0);
    REQUIRE(image.tracks == 1);
    REQUIRE(False(image.flags & ImageFlagBits::CpuModified));
    REQUIRE(True(image.flags & ImageFlagBits::GpuModified));
    for (const auto& pixel : image.host) {
        REQUIRE(pixel == std::array<u8, 4>{99, 99, 99, 99});
    }
}

TEST_CASE("Partial clear preserves uploaded pixels and masked channels", "[video_core][clear_plan]") {
    PendingImage image;
    auto clear = FullClear();
    SECTION("Framebuffer surface clip is smaller than the image") {
        clear.rect = ClampClearRect(clear.rect, 2, 2);
    }
    SECTION("A smaller bound attachment limits the framebuffer") {
        clear.rect = ClampClearRect(clear.rect, 4, 2);
    }
    SECTION("A lower-left scissor becomes an offset partial clear") {
        const auto [min_y, max_y] = ScissorYRange(0, 4, true, 6);
        clear.rect = ClampClearRect({0, min_y, 4, u32(max_y - min_y)}, 4, 4);
    }
    SECTION("Partial color mask preserves the other channels") {
        clear.color_mask = 1;
    }
    image.Begin(clear, ColorImage());
    REQUIRE(image.uploads == 1);
    image.RecordColorClear(clear);
    image.Finish(clear, ColorImage());
    REQUIRE(image.uploads == 1);
    for (u32 y = 0; y < 4; ++y) {
        for (u32 x = 0; x < 4; ++x) {
            for (u32 c = 0; c < 4; ++c) {
                const bool overwritten = x >= u32(clear.rect.x) && y >= u32(clear.rect.y) &&
                    x < u32(clear.rect.x) + clear.rect.width &&
                    y < u32(clear.rect.y) + clear.rect.height && (clear.color_mask & (1U << c));
                REQUIRE(image.host[y * 4 + x][c] == (overwritten ? 99 : image.guest[y * 4 + x][c]));
            }
        }
    }
}

TEST_CASE("Rejected clears retain CPU ownership for a subsequent read", "[video_core][clear_plan]") {
    PendingImage image;
    const auto clear = FullClear();
    image.Begin(clear, ColorImage());
    auto recorded = clear;
    SECTION("Backend did not record the attachment") {
        recorded.color_mask = 0;
    }
    SECTION("Clipped layers reject the command") {
        recorded.layer_count = Vulkan::GetFramebufferClearLayers(1, 1, 1).count;
    }
    SECTION("Empty rectangle rejects the command") {
        recorded.rect = ClampClearRect({4, 0, 4, 4}, 4, 4);
    }
    image.Finish(recorded, ColorImage());
    REQUIRE(True(image.flags & ImageFlagBits::CpuModified));
    REQUIRE(image.uploads == 0);
    image.Prepare(false, false);
    REQUIRE(image.uploads == 1);
    REQUIRE(image.host == image.guest);
}

TEST_CASE("Depth-only and masked stencil clears preserve the other aspect", "[video_core][clear_plan]") {
    auto coverage = ColorImage();
    coverage.attachment = ClearDepthStencilAttachment;
    coverage.has_color = false;
    coverage.has_depth = true;
    coverage.has_stencil = true;
    auto clear = FullClear();
    clear.color_mask = 0;
    clear.depth = true;
    SECTION("Depth only") {}
    SECTION("Partial stencil") { clear.stencil_mask = 0x0F; }
    PendingImage image;
    image.Begin(clear, coverage);
    REQUIRE(image.uploads == 1);
    for (auto& pixel : image.host) {
        pixel[0] = 99;
        pixel[1] = (pixel[1] & ~clear.stencil_mask) | (0xAB & clear.stencil_mask);
    }
    image.Finish(clear, coverage);
    REQUIRE(image.host[0][1] == ((22 & ~clear.stencil_mask) | (0xAB & clear.stencil_mask)));
    REQUIRE(image.host[0][2] == 33);
    clear.stencil_mask = 0xFF;
    REQUIRE(clear.FullyOverwrites(coverage));
}

TEST_CASE("Clear discard requires the exact attachment and complete storage", "[video_core][clear_plan]") {
    auto clear = FullClear();
    auto coverage = ColorImage();
    SECTION("Unselected color target") { coverage.attachment = 1; }
    SECTION("Disabled color") { clear.color_mask = 0; }
    SECTION("No layers") { clear.layer_count = 0; }
    SECTION("Nonzero clear layer") { clear.base_layer = 1; }
    SECTION("Multiple image layers") { coverage.layers = 2; }
    SECTION("Multiple mip levels") { coverage.levels = 2; }
    SECTION("Multisample storage") { coverage.samples = 4; }
    SECTION("Nonzero view mip") { coverage.view.base.level = 1; }
    SECTION("Nonzero view layer") { coverage.view.base.layer = 1; }
    SECTION("Partial view") { coverage.view.extent.layers = 0; }
    SECTION("Three-dimensional storage") { coverage.is_2d = false; }
    SECTION("Alias, sparse, or reinterpreted storage") { coverage.storage_matches_view = false; }
    SECTION("Scaled host image exceeds the clear") { coverage.width = 8; coverage.height = 8; }
    PendingImage image;
    image.Begin(clear, coverage);
    REQUIRE(image.uploads == 1);
    REQUIRE(image.host == image.guest);
}

TEST_CASE("Resolved clear rectangles clamp signed and empty boundaries", "[video_core][clear_plan]") {
    auto rect = ClampClearRect({-2, -2, 6, 6}, 4, 4);
    REQUIRE(rect.x == 0);
    REQUIRE(rect.y == 0);
    REQUIRE(rect.width == 4);
    REQUIRE(rect.height == 4);
    rect = ClampClearRect({(std::numeric_limits<s32>::min)(), 0,
                           (std::numeric_limits<u32>::max)(), 4}, 4, 4);
    REQUIRE(rect.width == 4);
    rect = ClampClearRect({0, 0, 4, 4}, 0, 0);
    REQUIRE(rect.width == 0);
    REQUIRE(rect.height == 0);
    REQUIRE(ScissorYRange(0, 4, true, 4) == std::pair<s32, s32>{0, 4});
}
