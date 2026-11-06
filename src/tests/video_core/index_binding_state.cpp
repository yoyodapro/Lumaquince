// SPDX-License-Identifier: GPL-3.0-or-later

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include "video_core/renderer_vulkan/index_binding_state.h"

TEST_CASE("Index binding reuse requires all host state and recording epoch", "[video_core]") {
    Vulkan::IndexBindingState state;
    const auto first = std::bit_cast<VkBuffer>(u64{1});
    const auto second = std::bit_cast<VkBuffer>(u64{2});
    REQUIRE(state.Update(first, 32, VK_INDEX_TYPE_UINT16, 0));
    REQUIRE_FALSE(state.Update(first, 32, VK_INDEX_TYPE_UINT16, 0));
    REQUIRE(state.Update(second, 32, VK_INDEX_TYPE_UINT16, 0));
    REQUIRE(state.Update(second, 64, VK_INDEX_TYPE_UINT16, 0));
    REQUIRE(state.Update(second, 64, VK_INDEX_TYPE_UINT32, 0));
    REQUIRE_FALSE(state.Update(second, 64, VK_INDEX_TYPE_UINT32, 0));
    REQUIRE(state.Update(second, 64, VK_INDEX_TYPE_UINT32, 1));
    REQUIRE_FALSE(state.Update(second, 64, VK_INDEX_TYPE_UINT32, 1));
    // Nonstandard host-u8 and quad paths replace the recorded binding independently.
    state.Invalidate();
    REQUIRE(state.Update(second, 64, VK_INDEX_TYPE_UINT32, 1));
    REQUIRE_FALSE(state.Update(second, 64, VK_INDEX_TYPE_UINT32, 1));
}
