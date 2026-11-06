// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <utility>
#include <vulkan/vulkan.h>

#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

// The recording thread owns this state. Submission/state invalidation must not clear it: a
// compute producer and its first graphics consumer may be in different command buffers.
class ComputeWriteBarrierState {
public:
    void NoteDispatch() {
        pending = true;
    }

    bool TakeForRenderPass() {
        return std::exchange(pending, false);
    }

private:
    bool pending{};
};

template <typename CommandBuffer>
void RecordComputeReadBarrier(CommandBuffer cmdbuf) {
    static constexpr VkMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
    };
    cmdbuf.PipelineBarrier(vk::PIPELINE_STAGE_GRAPHICS_COMPUTE_TRANSFER,
                           VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           0, barrier);
}

template <typename CommandBuffer>
void RecordComputeToGraphicsBarrier(CommandBuffer cmdbuf) {
    static constexpr VkMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
    };
    cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, barrier);
}

template <typename CommandBuffer>
void RecordWaitForIdleBarrier(CommandBuffer cmdbuf, VkPipelineStageFlags source_stages) {
    static constexpr VkMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
    };
    cmdbuf.PipelineBarrier(source_stages, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, barrier);
}

} // namespace Vulkan
