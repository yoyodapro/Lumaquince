// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>

#include "common/common_types.h"
#include "common/range_sets.inc"

namespace VideoCommon {

struct AsyncDownloadCandidates {
    u64 aligned_bytes{};
    u64 copies{};
};

template <typename Tracker, typename Func>
void ForEachAsyncDownloadRange(Tracker& tracker, const Common::RangeSet<DAddr>& dirty_ranges,
                               DAddr start, u64 size, Func&& func,
                               AsyncDownloadCandidates* candidates = nullptr) {
    if (size == 0) {
        return;
    }
    tracker.ForEachDownloadRange(start, size, false, [&](u64 page_start, u64 page_size) {
        // Only profiling pays for enumerating what the old page-expanded selection copied.
        if (candidates) {
            dirty_ranges.ForEachInRange(page_start, page_size, [&](DAddr lower, DAddr upper) {
                candidates->aligned_bytes += (upper - lower + 63) & ~u64{63};
                ++candidates->copies;
            });
        }
        // Tracker callbacks cover whole pages. Preserve the queued interval so separate writes
        // in one page do not each download every dirty span in that page.
        const DAddr lower = (std::max)(start, page_start);
        const DAddr upper = (std::min)(start + size, page_start + page_size);
        if (lower < upper) {
            dirty_ranges.ForEachInRange(lower, upper - lower, func);
        }
    });
}

} // namespace VideoCommon
