// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <vulkan/vulkan_core.h>

namespace Vulkan {

// Task/mesh slots are not tessellation stages and have no input assembly state.
constexpr std::optional<VkPrimitiveTopology> ResolveInputAssemblyTopology(
    VkPrimitiveTopology topology, bool is_mesh, bool has_tessellation) {
    if (is_mesh) {
        return std::nullopt;
    }
    if (has_tessellation) {
        return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    }
    return topology == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST
                                                       : topology;
}

constexpr bool InputAssemblyRestartEnabled(VkPrimitiveTopology topology, bool requested,
                                          bool molten_vk, bool list_restart, bool patch_restart) {
    if (molten_vk) {
        // MoltenVK/Metal always enables restart and cannot disable it.
        return true;
    }
    if (!requested) {
        return false;
    }
    switch (topology) {
    case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
        return patch_restart;
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
        return list_restart;
    default:
        return true;
    }
}

// Track the host state, including transitions through a mesh pipeline. A stage change can
// require a command even when the guest's raw primitive topology has not changed.
class InputAssemblyTopologyState {
public:
    bool Update(std::optional<VkPrimitiveTopology> topology) {
        const bool changed = current != topology;
        current = topology;
        return changed;
    }

    void Invalidate() {
        current.reset();
    }

private:
    std::optional<VkPrimitiveTopology> current;
};

} // namespace Vulkan
