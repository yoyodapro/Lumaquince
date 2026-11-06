// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstddef>

#include <catch2/catch_test_macros.hpp>

#include "video_core/compatible_formats.h"
#include "video_core/surface.h"
#include "video_core/texture_cache/format_lookup_table.h"

TEST_CASE("ASTC view compatibility follows the compressed block footprint",
          "[video_core][compatible_formats]") {
    using namespace VideoCore::Surface;

    for (std::size_t image_index = 0; image_index < MaxPixelFormat; ++image_index) {
        const auto image_format = static_cast<PixelFormat>(image_index);
        if (!IsPixelFormatASTC(image_format)) {
            continue;
        }
        for (std::size_t view_index = 0; view_index < MaxPixelFormat; ++view_index) {
            const auto view_format = static_cast<PixelFormat>(view_index);
            if (!IsPixelFormatASTC(view_format)) {
                continue;
            }
            const bool same_footprint =
                DefaultBlockWidth(image_format) == DefaultBlockWidth(view_format) &&
                DefaultBlockHeight(image_format) == DefaultBlockHeight(view_format);
            CAPTURE(image_index, view_index);
            for (const bool native_bgr : std::array{false, true}) {
                CHECK(IsViewCompatible(image_format, view_format, false, native_bgr) ==
                      same_footprint);
                CHECK(IsViewCompatible(image_format, view_format, true, native_bgr) ==
                      (image_format == view_format));
            }
        }
        CHECK_FALSE(IsViewCompatible(image_format, PixelFormat::BC3_UNORM, false, true));
        CHECK_FALSE(IsViewCompatible(image_format, PixelFormat::BC3_SRGB, false, true));
    }
}

TEST_CASE("ASTC 8x6 TIC sRGB conversion preserves a compatible distinct view",
          "[video_core][compatible_formats]") {
    using Tegra::Texture::ComponentType;
    using Tegra::Texture::TextureFormat;
    using namespace VideoCore::Surface;

    const auto format_for = [](bool srgb) {
        return VideoCommon::PixelFormatFromTextureInfo(
            TextureFormat::ASTC_2D_8X6, ComponentType::UNORM, ComponentType::UNORM,
            ComponentType::UNORM, ComponentType::UNORM, srgb);
    };
    const auto linear = format_for(false);
    const auto srgb = format_for(true);
    REQUIRE(linear == PixelFormat::ASTC_2D_8X6_UNORM);
    REQUIRE(srgb == PixelFormat::ASTC_2D_8X6_SRGB);
    CHECK_FALSE(IsPixelFormatSRGB(linear));
    CHECK(IsPixelFormatSRGB(srgb));
    CHECK(IsViewCompatible(linear, srgb, false, true));
    CHECK(IsViewCompatible(srgb, linear, false, true));
}
