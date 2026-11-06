// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vulkan/vulkan_core.h>
#include "common/common_types.h"

namespace Vulkan {

class IndexBindingState {
public:
    // Compare the final host binding, after any index conversion and null-buffer substitution.
    [[nodiscard]] bool Update(VkBuffer buffer, VkDeviceSize offset, VkIndexType type, u64 epoch) {
        if (valid && buffer == last_buffer && offset == last_offset && type == last_type &&
            epoch == last_epoch) {
            return false;
        }
        valid = true;
        last_buffer = buffer;
        last_offset = offset;
        last_type = type;
        last_epoch = epoch;
        return true;
    }

    void Invalidate() noexcept {
        valid = false;
    }

private:
    VkBuffer last_buffer{};
    VkDeviceSize last_offset{};
    VkIndexType last_type{};
    u64 last_epoch{};
    bool valid{};
};

} // namespace Vulkan
