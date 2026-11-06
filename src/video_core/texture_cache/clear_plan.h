// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <limits>
#include <utility>

#include "common/common_types.h"
#include "video_core/texture_cache/image_base.h"

namespace VideoCommon {

inline constexpr u32 ClearDepthStencilAttachment = (std::numeric_limits<u32>::max)();

struct ClearRect {
    s32 x{};
    s32 y{};
    u32 width{};
    u32 height{};
};

constexpr std::pair<s32, s32> ScissorYRange(s32 min_y, s32 max_y, bool lower_left,
                                          s32 clip_height) {
    if (lower_left) {
        return {(std::max)(clip_height - max_y, 0), (std::max)(clip_height - min_y, 0)};
    }
    return {(std::max)(min_y, 0), (std::max)(max_y, 0)};
}

// Intersect the backend's scissor with the actual framebuffer, without signed overflow.
constexpr ClearRect ClampClearRect(ClearRect rect, u32 width, u32 height) {
    const auto clamp_axis = [](s32& offset, u32& extent, u32 limit) {
        const s64 begin = std::clamp<s64>(offset, 0, limit);
        const s64 end = std::clamp<s64>(s64{offset} + extent, begin, limit);
        offset = static_cast<s32>(begin);
        extent = static_cast<u32>(end - begin);
    };
    clamp_axis(rect.x, rect.width, width);
    clamp_axis(rect.y, rect.height, height);
    return rect;
}

struct ClearImageCoverage {
    u32 attachment{};
    u32 width{};
    u32 height{};
    s32 levels{};
    s32 layers{};
    u32 samples{};
    SubresourceRange view{};
    bool is_2d{};
    bool storage_matches_view{};
    bool has_color{};
    bool has_depth{};
    bool has_stencil{};
};

// Filled from the resolved backend framebuffer and used for both cache preparation and clearing.
struct ClearPlan {
    ClearRect rect{};
    u32 base_layer{};
    u32 layer_count{};
    u32 color_attachment{ClearDepthStencilAttachment};
    u8 color_mask{};
    bool depth{};
    u8 stencil_mask{};

    [[nodiscard]] constexpr bool HasWork() const {
        return rect.width != 0 && rect.height != 0 && layer_count != 0 &&
               (color_mask != 0 || depth || stencil_mask != 0);
    }

    [[nodiscard]] constexpr bool Writes(u32 attachment) const {
        return HasWork() && (attachment == ClearDepthStencilAttachment
                                 ? depth || stencil_mask != 0
                                 : attachment == color_attachment && color_mask != 0);
    }

    [[nodiscard]] constexpr bool FullyOverwrites(const ClearImageCoverage& image) const {
        if (!Writes(image.attachment) || !image.is_2d || !image.storage_matches_view ||
            image.levels != 1 || image.layers != 1 || image.samples != 1 ||
            image.view.base.level != 0 || image.view.base.layer != 0 ||
            image.view.extent.levels != 1 || image.view.extent.layers != 1 || base_layer != 0 ||
            rect.x != 0 || rect.y != 0 || image.width == 0 || image.height == 0 ||
            rect.width < image.width || rect.height < image.height) {
            return false;
        }
        if (image.has_color) {
            return image.attachment != ClearDepthStencilAttachment && color_mask == 0xF;
        }
        return image.attachment == ClearDepthStencilAttachment &&
               (image.has_depth || image.has_stencil) && (!image.has_depth || depth) &&
               (!image.has_stencil || stencil_mask == 0xFF);
    }
};

template <typename Prepare>
void PrepareClearTarget(const ClearPlan& clear, const ClearImageCoverage& image, Prepare&& prepare) {
    if (!clear.FullyOverwrites(image)) {
        prepare(clear.Writes(image.attachment), false);
    }
}

template <typename Prepare>
void FinishClearTarget(const ClearPlan& recorded_clear, const ClearImageCoverage& image,
                       Prepare&& prepare) {
    if (recorded_clear.FullyOverwrites(image)) {
        prepare(true, true);
    }
}

// Keep the same ownership transition for normal uploads and proven full overwrites.
template <typename Image, typename Refresh, typename Track>
void PrepareImageContents(Image& image, bool invalidate, Refresh&& refresh, Track&& track) {
    if (!invalidate) {
        refresh();
        return;
    }
    image.flags &= ~(ImageFlagBits::CpuModified | ImageFlagBits::GpuModified);
    if (False(image.flags & ImageFlagBits::Tracked)) {
        track();
    }
}

} // namespace VideoCommon
