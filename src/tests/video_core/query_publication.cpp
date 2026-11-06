// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <functional>
#include <future>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "video_core/query_cache/query_publication.h"
#include "video_core/query_cache/query_report_fence.h"

namespace {

struct QueryCache {
    bool SemiFlushRegion(DAddr address, u64 size) {
        ranges.emplace_back(address, size);
        if (finalized) {
            guest = host;
        }
        return !finalized;
    }
    bool finalized{};
    u32 guest{7};
    u32 host{42};
    std::vector<std::pair<DAddr, u64>> ranges;
};

struct FenceManager {
    void SyncOperation(std::function<void()>&& operation) {
        operations.push_back(std::move(operation));
    }
    void SignalFence(std::function<void()>&& callback) {
        ++signals;
        if (immediate_callback) {
            callback();
            callback_ran = true;
        } else {
            SyncOperation(std::move(callback));
        }
    }
    void Complete() {
        for (auto& operation : operations) {
            operation();
        }
        operations.clear();
    }
    bool immediate_callback{true};
    bool callback_ran{};
    u32 signals{};
    std::vector<std::function<void()>> operations;
};

struct Scheduler {
    void FlushDeferred() {
        ++flushes;
        submit();
    }
    u32 flushes{};
    std::function<void()> submit;
};

} // namespace

TEST_CASE("Query publication skips empty and finalized regions",
          "[video_core][query_publication]") {
    QueryCache queries;
    FenceManager fences;
    Scheduler scheduler;
    VideoCommon::FlushQueryResults(queries, fences, scheduler, 0, 16);
    VideoCommon::FlushQueryResults(queries, fences, scheduler, 0x1000, 0);
    CHECK(queries.ranges.empty());
    queries.finalized = true;
    VideoCommon::FlushQueryResults(queries, fences, scheduler, 0x1234, 16);
    REQUIRE(queries.ranges.size() == 1);
    CHECK(queries.ranges.front() == std::pair<DAddr, u64>{0x1234, 16});
    CHECK(queries.guest == 42);
    CHECK(fences.signals == 0);
    CHECK(fences.operations.empty());
    CHECK(scheduler.flushes == 0);
}

TEST_CASE("Query publication tolerates completion before entering its wait",
          "[video_core][query_publication]") {
    for (const bool immediate : {false, true}) {
        QueryCache queries;
        FenceManager fences;
        fences.immediate_callback = immediate;
        fences.SyncOperation([&] { queries.guest = queries.host; });
        Scheduler scheduler{0, [&] { fences.Complete(); }};
        VideoCommon::FlushQueryResults(queries, fences, scheduler, 0x1000, 4);
        CHECK(queries.guest == 42);
        CHECK(fences.signals == 1);
        CHECK(fences.operations.empty());
        CHECK(scheduler.flushes == 1);
    }
}

TEST_CASE("Query publication waits for report writes despite an immediate fence callback",
          "[video_core][query_publication]") {
    using namespace std::chrono_literals;
    QueryCache queries;
    FenceManager fences;
    fences.SyncOperation([&] { queries.guest = queries.host; });
    std::promise<void> submitted;
    auto submission = submitted.get_future();
    Scheduler scheduler{0, [&] { submitted.set_value(); }};
    std::promise<u32> result;
    auto finished = result.get_future();
    std::jthread caller([&] {
        VideoCommon::FlushQueryResults(queries, fences, scheduler, 0x1000, 4);
        result.set_value(queries.guest);
    });
    submission.wait();
    const bool immediate_callback_ran = fences.callback_ran;
    const u32 value_before_completion = queries.guest;
    const auto before_completion = finished.wait_for(20ms);
    // Always release the caller before assertions, including when the wait regresses.
    fences.Complete();
    caller.join();
    CHECK(immediate_callback_ran);
    CHECK(value_before_completion == 7);
    CHECK(before_completion == std::future_status::timeout);
    CHECK(finished.get() == 42);
    CHECK(scheduler.flushes == 1);
}

namespace {

// The production policy and release decision run against a GPU whose completion is
// controlled by the test. No cache download is needed for these payload reports.
struct ReportFenceFixture {
    struct Pending {
        bool stubbed;
        std::function<void()> operation;
    };

    void SignalFence(std::function<void()>&& operation) {
        const auto policy = VideoCommon::GetFenceSignalPolicy(configured_delay);
        if (during_signal) {
            auto nested = std::exchange(during_signal, {});
            nested();
        }
        const bool needs_submission = cache_flush || policy.force_flush;
        submissions += needs_submission;
        if (policy.delay_operation) {
            pending.push_back({!needs_submission, std::move(operation)});
        } else {
            operation();
        }
    }

    template <bool force_wait = false>
    void Release() {
        while (!pending.empty()) {
            if (!VideoCommon::PrepareFenceRelease<force_wait>(
                    pending.front().stubbed, cache_flush,
                    [&] { return gpu_complete; }, [&] {
                        ++waits;
                        gpu_complete = true;
                    })) {
                return;
            }
            auto operation = std::move(pending.front().operation);
            pending.erase(pending.begin());
            operation();
        }
    }

    bool configured_delay{};
    bool cache_flush{};
    bool gpu_complete{};
    unsigned submissions{};
    unsigned waits{};
    std::function<void()> during_signal;
    std::vector<Pending> pending;
};

} // namespace

TEST_CASE("Query-only payload fences wait for the host GPU without cache downloads",
          "[video_core][query_report_fence]") {
    ReportFenceFixture fences;
    u32 guest = 3;
    VideoCommon::SignalQueryReportFence(fences, [&] { guest = 7; });
    CHECK_FALSE(VideoCommon::QueryReportFenceScope::Active());
    REQUIRE(fences.pending.size() == 1);
    CHECK_FALSE(fences.pending.front().stubbed);
    CHECK(fences.submissions == 1);
    fences.Release();
    CHECK(guest == 3);
    CHECK(fences.pending.size() == 1);
    fences.gpu_complete = true;
    fences.Release();
    CHECK(guest == 7);
    CHECK(fences.pending.empty());
}

TEST_CASE("Query report fences preserve pending report order through a forced GPU wait",
          "[video_core][query_report_fence]") {
    ReportFenceFixture fences;
    std::vector<u32> published;
    VideoCommon::SignalQueryReportFence(fences, [&] { published.push_back(11); });
    VideoCommon::SignalQueryReportFence(fences, [&] { published.push_back(29); });
    fences.Release();
    CHECK(published.empty());
    CHECK(fences.pending.size() == 2);
    fences.Release<true>();
    CHECK(published == (std::vector<u32>{11, 29}));
    CHECK(fences.waits == 1);
    CHECK(fences.submissions == 2);
    CHECK(fences.pending.empty());
}

TEST_CASE("Query report scope restores ordinary fence policy after nesting and exceptions",
          "[video_core][query_report_fence]") {
    CHECK_FALSE(VideoCommon::QueryReportFenceScope::Active());
    {
        const VideoCommon::QueryReportFenceScope outer;
        CHECK(VideoCommon::QueryReportFenceScope::Active());
        CHECK_THROWS_AS([] {
            const VideoCommon::QueryReportFenceScope inner;
            throw 1;
        }(), int);
        CHECK(VideoCommon::QueryReportFenceScope::Active());
        auto other_thread = std::async(std::launch::async, [] {
            return VideoCommon::QueryReportFenceScope::Active();
        });
        CHECK_FALSE(other_thread.get());
    }
    CHECK_FALSE(VideoCommon::QueryReportFenceScope::Active());
    ReportFenceFixture ordinary;
    bool immediate{};
    ordinary.SignalFence([&] { immediate = true; });
    CHECK(immediate);
    CHECK(ordinary.submissions == 0);
    CHECK(ordinary.pending.empty());

    ordinary.configured_delay = true;
    bool deferred{};
    ordinary.SignalFence([&] { deferred = true; });
    CHECK_FALSE(deferred);
    REQUIRE(ordinary.pending.size() == 1);
    CHECK(ordinary.pending.front().stubbed);
    ordinary.Release();
    CHECK(deferred);
    CHECK(ordinary.submissions == 0);
}

TEST_CASE("A known payload value cannot bypass its pending report fence during semi-flush",
          "[video_core][query_report_fence]") {
    VideoCommon::GuestQuery query{false, 0x1000, 47};
    query.flags |= VideoCommon::QueryFlagBits::IsFence;
    u64 guest = 5;
    const auto semi_flush = [&] {
        const auto decision = VideoCommon::GetQueryPublicationDecision(query.flags);
        if (decision.copy_value) {
            guest = query.value;
        }
        return decision.wait;
    };
    CHECK(VideoCommon::QueryHasPendingGuestWrite(query.flags));
    CHECK(semi_flush());
    CHECK(guest == 5);
    ReportFenceFixture fences;
    VideoCommon::SignalQueryReportFence(fences, [&] {
        guest = query.value;
        query.flags |= VideoCommon::QueryFlagBits::IsGuestSynced;
    });
    fences.Release();
    CHECK(semi_flush());
    CHECK(guest == 5);
    fences.gpu_complete = true;
    fences.Release();
    CHECK_FALSE(VideoCommon::QueryHasPendingGuestWrite(query.flags));
    CHECK_FALSE(semi_flush());
    CHECK(guest == 47);

    // Ordinary finalized reports can still use the existing synchronous publication path.
    query.flags = VideoCommon::QueryFlagBits::IsFinalValueSynced;
    query.value = 53;
    CHECK_FALSE(semi_flush());
    CHECK(guest == 53);
}

TEST_CASE("A query report request does not change an unrelated reentrant fence",
          "[video_core][query_report_fence]") {
    ReportFenceFixture fences;
    bool ordinary_ran{};
    bool nested_report_ran{};
    bool report_ran{};
    fences.during_signal = [&] {
        fences.SignalFence([&] { ordinary_ran = true; });
        VideoCommon::SignalQueryReportFence(fences, [&] { nested_report_ran = true; });
    };
    VideoCommon::SignalQueryReportFence(fences, [&] { report_ran = true; });
    CHECK(ordinary_ran);
    CHECK_FALSE(nested_report_ran);
    CHECK_FALSE(report_ran);
    CHECK(fences.submissions == 2);
    REQUIRE(fences.pending.size() == 2);
    CHECK_FALSE(fences.pending.front().stubbed);
    CHECK_FALSE(fences.pending.back().stubbed);
    fences.gpu_complete = true;
    fences.Release();
    CHECK(nested_report_ran);
    CHECK(report_ran);
    CHECK_FALSE(VideoCommon::QueryReportFenceScope::Active());
}
