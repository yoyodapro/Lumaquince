// SPDX-FileCopyrightText: Copyright 2026 Lumaquince Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <utility>

#include "video_core/query_cache/query_base.h"

namespace VideoCommon {

inline bool QueryHasPendingGuestWrite(QueryFlagBits flags) {
    return True(flags & (QueryFlagBits::IsHostManaged | QueryFlagBits::IsFence)) &&
           False(flags & QueryFlagBits::IsGuestSynced);
}

struct QueryPublicationDecision {
    bool copy_value;
    bool wait;
};

inline QueryPublicationDecision GetQueryPublicationDecision(QueryFlagBits flags) {
    const bool unpublished = False(flags & QueryFlagBits::IsGuestSynced);
    const bool pending_fence = unpublished && True(flags & QueryFlagBits::IsFence);
    const bool copy_value = unpublished && !pending_fence &&
                            True(flags & QueryFlagBits::IsFinalValueSynced);
    return {
        .copy_value = copy_value,
        .wait = !copy_value && QueryHasPendingGuestWrite(flags),
    };
}

// Adapted from Lichii PR #337. A report fence orders guest-visible query writes even when
// no cache needs a download. Keep the scope local to the signaling thread and restore it
// on exceptions or nested calls; unrelated fences retain their configured behavior.
class QueryReportFenceScope {
public:
    QueryReportFenceScope() : previous{active} {
        active = true;
    }

    ~QueryReportFenceScope() {
        active = previous;
    }

    QueryReportFenceScope(const QueryReportFenceScope&) = delete;
    QueryReportFenceScope& operator=(const QueryReportFenceScope&) = delete;

    static bool Active() {
        return active;
    }

    static bool TakeRequest() {
        return std::exchange(active, false);
    }

private:
    inline static thread_local bool active{};
    const bool previous;
};

struct FenceSignalPolicy {
    bool delay_operation;
    bool force_flush;
};

inline FenceSignalPolicy GetFenceSignalPolicy(bool configured_delay) {
    // Only the directly signaled fence consumes the request. An old callback released
    // during SignalFence may itself signal an unrelated fence reentrantly.
    const bool query_report = QueryReportFenceScope::TakeRequest();
    return {
        .delay_operation = configured_delay || query_report,
        // Merely delaying the callback can still produce a stubbed fence for a payload
        // report. Require submission and a real host fence as well.
        .force_flush = query_report,
    };
}

template <typename Rasterizer, typename Operation>
void SignalQueryReportFence(Rasterizer& rasterizer, Operation&& operation) {
    const QueryReportFenceScope scope;
    rasterizer.SignalFence(std::forward<Operation>(operation));
}

// A query-only payload fence has no download mask, but its host fence still orders the
// report. The synchronous release path must honor it just like the asynchronous path.
template <bool force_wait, typename IsSignaled, typename Wait>
bool PrepareFenceRelease(bool is_stubbed, bool cache_wait, IsSignaled&& is_signaled, Wait&& wait) {
    if ((!is_stubbed || cache_wait) && !is_signaled()) {
        if constexpr (force_wait) {
            wait();
        } else {
            return false;
        }
    }
    return true;
}

} // namespace VideoCommon
