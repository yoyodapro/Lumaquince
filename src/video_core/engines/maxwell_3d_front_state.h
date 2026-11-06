// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "video_core/engines/maxwell_3d.h"

namespace Tegra::Engines {

// PR #178 (0f2af4fc): filter redundant instance-loop writes, with mode tracking shared by
// scalar and batched front calls. The mode is initialized from the drained draw manager.
struct Maxwell3DFrontDrawState {
    using DrawMode = Maxwell3D::DrawManager::DrawMode;
    DrawMode mode{DrawMode::General};

    void Observe(u32 method, u32 value) {
        switch (method) {
        case MAXWELL3D_REG_INDEX(draw.begin):
            // Unchanged and reserved instance ids preserve the draw manager's previous mode.
            if (((value >> 26) & 3) == 0) {
                mode = DrawMode::General;
            } else if (((value >> 26) & 3) == 1) {
                mode = DrawMode::Instance;
            }
            break;
        case MAXWELL3D_REG_INDEX(draw_inline_index):
        case MAXWELL3D_REG_INDEX(inline_index_2x16.even):
        case MAXWELL3D_REG_INDEX(inline_index_4x8.index0):
            mode = DrawMode::InlineIndex;
            break;
        case MAXWELL3D_REG_INDEX(vertex_array_instance_first):
        case MAXWELL3D_REG_INDEX(vertex_array_instance_subsequent):
            mode = DrawMode::Instance;
            break;
        }
    }

    [[nodiscard]] bool CanSkip(u32 method, u32 previous, u32 value, bool shadow_kept,
                               bool skip_same) const {
        if (!skip_same || !shadow_kept || previous != value) {
            return false;
        }
        switch (method) {
        case MAXWELL3D_REG_INDEX(draw.end):
            return mode == DrawMode::Instance;
        case MAXWELL3D_REG_INDEX(vertex_buffer.first):
        case MAXWELL3D_REG_INDEX(vertex_buffer.count):
            return true;
        default:
            // index_buffer.first must still re-dirty IndexBuffer, even if unchanged.
            return false;
        }
    }
};

} // namespace Tegra::Engines
