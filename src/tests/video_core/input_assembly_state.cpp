// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "video_core/renderer_vulkan/input_assembly_state.h"

TEST_CASE("Input assembly follows tessellation when guest topology is unchanged",
          "[video_core][input_assembly]") {
    Vulkan::InputAssemblyTopologyState state;
    std::vector<VkPrimitiveTopology> commands;
    for (const bool tessellation : {false, false, true, true, false}) {
        const auto topology = Vulkan::ResolveInputAssemblyTopology(
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false, tessellation);
        if (state.Update(topology) && topology) {
            commands.push_back(*topology);
        }
    }
    const std::vector<VkPrimitiveTopology> expected{
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    CHECK(commands == expected);

    state.Invalidate();
    CHECK(state.Update(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST));
    CHECK_FALSE(state.Update(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST));
}

TEST_CASE("Patch fallback and mesh slot reuse preserve input assembly boundaries",
          "[video_core][input_assembly]") {
    Vulkan::InputAssemblyTopologyState state;
    const auto points = Vulkan::ResolveInputAssemblyTopology(
        VK_PRIMITIVE_TOPOLOGY_PATCH_LIST, false, false);
    const auto patches = Vulkan::ResolveInputAssemblyTopology(
        VK_PRIMITIVE_TOPOLOGY_PATCH_LIST, false, true);
    REQUIRE(points == VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
    REQUIRE(patches == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
    CHECK(state.Update(points));
    CHECK(state.Update(patches));

    // A mesh module in a tessellation slot must neither select PATCH_LIST nor emit IA state.
    for (const bool occupied_tessellation_slot : {false, true}) {
        const auto mesh = Vulkan::ResolveInputAssemblyTopology(
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, occupied_tessellation_slot);
        REQUIRE_FALSE(mesh.has_value());
        CHECK(state.Update(mesh));
        CHECK_FALSE(state.Update(mesh));
        // Returning to an ordinary pipeline must restore topology even with unchanged raw state.
        CHECK(state.Update(patches));
    }
}

TEST_CASE("Primitive restart uses the effective patch topology",
          "[video_core][input_assembly]") {
    const auto triangles = Vulkan::ResolveInputAssemblyTopology(
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false, false);
    const auto patches = Vulkan::ResolveInputAssemblyTopology(
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false, true);
    REQUIRE(triangles.has_value());
    REQUIRE(patches.has_value());
    // List support does not grant patch restart; patch support does not grant list restart.
    CHECK(Vulkan::InputAssemblyRestartEnabled(*triangles, true, false, true, false));
    CHECK_FALSE(Vulkan::InputAssemblyRestartEnabled(*patches, true, false, true, false));
    CHECK_FALSE(Vulkan::InputAssemblyRestartEnabled(*triangles, true, false, false, true));
    CHECK(Vulkan::InputAssemblyRestartEnabled(*patches, true, false, false, true));

    for (const auto topology : {*triangles, *patches, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP}) {
        CHECK_FALSE(Vulkan::InputAssemblyRestartEnabled(topology, false, false, true, true));
        // Preserve the existing MoltenVK policy even if the guest disables restart.
        CHECK(Vulkan::InputAssemblyRestartEnabled(topology, false, true, false, false));
    }
    CHECK(Vulkan::InputAssemblyRestartEnabled(
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, true, false, false, false));
}

TEST_CASE("Ordinary input assembly topologies retain their host mapping",
          "[video_core][input_assembly]") {
    constexpr std::array ordinary{
        VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
        VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
        VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY,
        VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY,
    };
    for (const auto topology : ordinary) {
        CHECK(Vulkan::ResolveInputAssemblyTopology(topology, false, false) == topology);
        CHECK(Vulkan::ResolveInputAssemblyTopology(topology, false, true) ==
              VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
    }
}
