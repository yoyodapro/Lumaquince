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
