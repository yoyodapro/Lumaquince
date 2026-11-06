// SPDX-License-Identifier: GPL-3.0-or-later

#include <functional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/vk_memory_barriers.h"

namespace {
struct Barrier {
    VkPipelineStageFlags source;
    VkPipelineStageFlags destination;
    VkDependencyFlags flags;
    VkMemoryBarrier memory;
};

struct RecordingCommandBuffer {
    std::vector<Barrier>* barriers;
    std::vector<std::string>* order;

    void PipelineBarrier(VkPipelineStageFlags source, VkPipelineStageFlags destination,
                         VkDependencyFlags flags, const VkMemoryBarrier& memory) {
        barriers->push_back({source, destination, flags, memory});
        order->emplace_back("barrier");
    }
};
} // namespace

TEST_CASE("Compute read barriers cover indirect arguments and previous producers",
          "[video_core][vulkan][compute_memory_barriers]") {
    std::vector<Barrier> barriers;
    std::vector<std::string> order;
    const RecordingCommandBuffer cmdbuf{&barriers, &order};
    Vulkan::RecordComputeReadBarrier(cmdbuf);
    REQUIRE(barriers.size() == 1);
    const auto& barrier = barriers.front();
    CHECK((barrier.source & VK_PIPELINE_STAGE_TRANSFER_BIT) != 0);
    CHECK((barrier.source & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) != 0);
    CHECK((barrier.source & VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT) != 0);
    CHECK((barrier.destination & VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT) != 0);
    CHECK((barrier.destination & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) != 0);
    CHECK(barrier.memory.srcAccessMask == VK_ACCESS_MEMORY_WRITE_BIT);
    CHECK(barrier.memory.dstAccessMask == VK_ACCESS_MEMORY_READ_BIT);
    CHECK(barrier.flags == 0);
}

TEST_CASE("Compute writes reach the next recorded render pass across command batches",
          "[video_core][vulkan][compute_memory_barriers]") {
    Vulkan::ComputeWriteBarrierState state;
    std::vector<Barrier> barriers;
    std::vector<std::string> order;
    const RecordingCommandBuffer cmdbuf{&barriers, &order};
    std::vector<std::function<void()>> commands;
    const auto begin_pass = [&] {
        const bool pending = state.TakeForRenderPass();
        commands.emplace_back([&, pending] {
            if (pending) {
                Vulkan::RecordComputeToGraphicsBarrier(cmdbuf);
            }
            order.emplace_back("pass");
        });
    };
    begin_pass(); // No producer: no barrier.
    state.NoteDispatch();
    state.NoteDispatch(); // Multiple producers coalesce.
    commands.emplace_back([&] { order.emplace_back("submit"); });
    begin_pass(); // Producer state survives the command-buffer boundary.
    begin_pass(); // A second pass needs no duplicate barrier.
    state.NoteDispatch();
    begin_pass(); // Captured state must not alter the already queued passes.
    for (auto& command : commands) {
        command();
    }
    CHECK(order == std::vector<std::string>{"pass", "submit", "barrier", "pass", "pass",
                                           "barrier", "pass"});
    REQUIRE(barriers.size() == 2);
    for (const auto& barrier : barriers) {
        CHECK(barrier.source == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        CHECK(barrier.destination == VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
        CHECK(barrier.memory.srcAccessMask == VK_ACCESS_SHADER_WRITE_BIT);
        CHECK(barrier.memory.dstAccessMask ==
              (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT));
    }
}

TEST_CASE("Wait for idle makes writes visible to later work without a reusable event",
          "[video_core][vulkan][compute_memory_barriers]") {
    std::vector<Barrier> barriers;
    std::vector<std::string> order;
    const RecordingCommandBuffer cmdbuf{&barriers, &order};
    const auto sources = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                         VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT;
    Vulkan::RecordWaitForIdleBarrier(cmdbuf, sources);
    Vulkan::RecordWaitForIdleBarrier(cmdbuf, sources);
    REQUIRE(barriers.size() == 2);
    for (const auto& barrier : barriers) {
        CHECK(barrier.source == sources);
        CHECK(barrier.destination == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        CHECK(barrier.memory.srcAccessMask == VK_ACCESS_MEMORY_WRITE_BIT);
        CHECK(barrier.memory.dstAccessMask ==
              (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT));
        CHECK(barrier.memory.sType == VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        CHECK(barrier.memory.pNext == nullptr);
        CHECK(barrier.flags == 0);
    }
}
