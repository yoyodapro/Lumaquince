// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <condition_variable>
#include <mutex>

#include "common/common_types.h"

namespace VideoCommon {

// The caller owns the caches; fence operations are released by a separate thread.
template <typename QueryCache, typename FenceManager, typename Scheduler>
void FlushQueryResults(QueryCache& queries, FenceManager& fences, Scheduler& scheduler,
                       DAddr address, u64 size) {
    if (address == 0 || size == 0 || !queries.SemiFlushRegion(address, size)) {
        return;
    }
    std::mutex mutex;
    std::condition_variable cv;
    bool published{};
    // A low-accuracy fence's own callback runs immediately. An operation instead follows
    // pending report writes and prevents SkipEmptyFence from skipping this fence.
    fences.SyncOperation([&] {
        std::scoped_lock lock{mutex};
        published = true;
        cv.notify_all();
    });
    fences.SignalFence([] {});
    // The split's back must submit before blocking: the fencing thread cannot submit for it.
    scheduler.FlushDeferred();
    std::unique_lock lock{mutex};
    cv.wait(lock, [&] { return published; });
}

} // namespace VideoCommon
