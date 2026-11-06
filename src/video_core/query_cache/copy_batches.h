// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <iterator>
#include <map>
#include <span>

#include "common/common_types.h"

namespace VideoCommon {

struct QueryCopy {
    size_t destination;
    u64 src_offset;
    u64 dst_offset;
    u64 size;
};

// Query sources are separate staging/resolve buffers. Retain every write in input order;
// overlapping destinations must be separate, synchronized Vulkan commands.
template <typename Emit>
void ForEachQueryCopyBatch(std::span<const QueryCopy> copies, Emit&& emit) {
    std::map<u64, u64> destinations;
    size_t first = 0;
    for (size_t i = 0; i < copies.size(); ++i) {
        const auto& next = copies[i];
        bool split = next.destination != copies[first].destination;
        if (!split) {
            // Existing intervals are disjoint: only the two neighbors can overlap.
            const auto after = destinations.lower_bound(next.dst_offset);
            split = (after != destinations.end() && after->first < next.dst_offset + next.size) ||
                    (after != destinations.begin() && std::prev(after)->second > next.dst_offset);
        }
        if (split) {
            emit(copies.subspan(first, i - first));
            first = i;
            destinations.clear();
        }
        destinations.emplace(next.dst_offset, next.dst_offset + next.size);
    }
    if (first < copies.size()) {
        emit(copies.subspan(first));
    }
}

} // namespace VideoCommon
