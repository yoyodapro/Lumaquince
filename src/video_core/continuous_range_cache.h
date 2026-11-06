// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

#include "common/common_types.h"

namespace VideoCommon {

// Bounded page runs adapted from Lichii c38631643ae7, 8d14da5e4b33 and 16b828559647.
// Owned by one thread. The caller supplies a lifetime-unique owner and the complete map epoch;
// every mapping change invalidates the owner's runs, including changes outside the query.
class ContinuousRangeCache {
public:
    template <typename Walk>
    size_t Get(size_t owner, u64 serial, GPUVAddr address, size_t size, u64 page_bits,
               Walk&& walk) {
        if (size == 0) {
            return 0;
        }
        if (owner == 0 || page_bits >= 64 ||
            size > std::numeric_limits<GPUVAddr>::max() - address) {
            return walk(address, size);
        }
        const GPUVAddr page = address & ~((GPUVAddr{1} << page_bits) - 1);
        const GPUVAddr limit = address + size;
        Run& run = runs[(page >> page_bits) % runs.size()];
        if (run.owner != owner || run.page != page || run.serial != serial ||
            (!run.broken && run.end < limit)) {
            const size_t length = walk(page, limit - page);
            run = {owner, page, serial, page + length, page + length < limit};
        }
        if (run.end > address) {
            return std::min<size_t>(size, run.end - address);
        }
        // A hole or discontinuity before this address says nothing about its own run.
        return walk(address, size);
    }

private:
    struct Run {
        size_t owner{};
        GPUVAddr page{};
        u64 serial{};
        GPUVAddr end{};
        bool broken{};
    };
    std::array<Run, 4096> runs{};
};

} // namespace VideoCommon
