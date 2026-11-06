// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"

namespace VideoCommon {

/// A completed barrier stays useful until the next depth write or target change. The overlap
/// predicate must inspect the current draw's views: unchanged TIC/TSC table addresses do not
/// imply unchanged handles or descriptor contents.
class FeedbackLoopState {
public:
    template <class Overlaps>
    bool NeedsBarrier(u64 target_serial, bool writes_depth, Overlaps&& overlaps) {
        if (target_serial != last_target_serial) {
            depth_dirty = true;
        }
        const bool barrier = depth_dirty && overlaps();
        depth_dirty = (depth_dirty && !barrier) || writes_depth;
        last_target_serial = target_serial;
        return barrier;
    }

private:
    u64 last_target_serial{};
    bool depth_dirty{true};
};

} // namespace VideoCommon
