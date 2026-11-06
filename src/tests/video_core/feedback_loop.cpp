// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <ranges>
#include <catch2/catch_test_macros.hpp>

#include "video_core/texture_cache/feedback_loop.h"

TEST_CASE("Feedback barriers follow current views with unchanged descriptor tables",
          "[video_core][feedback_loop]") {
    VideoCommon::FeedbackLoopState state;
    std::array<u32, 2> image_ids{12, 24};
    constexpr u32 depth_image = 42;
    const auto overlaps = [&] { return std::ranges::find(image_ids, depth_image) != image_ids.end(); };
    constexpr u64 target_serial = 3;
    CHECK_FALSE(state.NeedsBarrier(target_serial, true, overlaps));
    // Same targets and TIC/TSC base/limit, different per-draw handle/view.
    image_ids[1] = depth_image;
    CHECK(state.NeedsBarrier(target_serial, false, overlaps));
    CHECK_FALSE(state.NeedsBarrier(target_serial, false, overlaps));
    // A depth write after the barrier invalidates it, even if its own views don't overlap.
    image_ids[1] = 24;
    CHECK_FALSE(state.NeedsBarrier(target_serial, true, overlaps));
    image_ids[1] = depth_image;
    CHECK(state.NeedsBarrier(target_serial, false, overlaps));
    CHECK(state.NeedsBarrier(target_serial + 1, false, overlaps));
    CHECK_FALSE(state.NeedsBarrier(target_serial + 1, false, overlaps));
}
