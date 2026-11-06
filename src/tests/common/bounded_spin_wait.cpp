// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <future>
#include <semaphore>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "common/bounded_spin_wait.h"

TEST_CASE("Configured spin counts accept only bounded complete decimal integers",
          "[common][spin_wait]") {
    using Common::BoundedSpinWait;
    for (const u32 baseline : {16384U, 4096U, 8192U}) {
        CAPTURE(baseline);
        CHECK(BoundedSpinWait::ConfiguredSpins(nullptr, baseline) == baseline);
        CHECK(BoundedSpinWait::ConfiguredSpins("0", baseline) == 0);
        CHECK(BoundedSpinWait::ConfiguredSpins("1", baseline) == 1);
        CHECK(BoundedSpinWait::ConfiguredSpins("00064", baseline) == 64);
        CHECK(BoundedSpinWait::ConfiguredSpins(std::to_string(baseline).c_str(), baseline) == baseline);
        CHECK(BoundedSpinWait::ConfiguredSpins(std::to_string(baseline - 1).c_str(), baseline) ==
              baseline - 1);
        CHECK(BoundedSpinWait::ConfiguredSpins(std::to_string(baseline + 1).c_str(), baseline) ==
              baseline);
        for (const char* invalid : {"", " ", " 1", "1 ", "1\n", "+1", "-1", "1.0", "1e2",
                                    "0x20", "1junk", "true", "4294967296",
                                    "18446744073709551615"}) {
            CAPTURE(invalid);
            CHECK(BoundedSpinWait::ConfiguredSpins(invalid, baseline) == baseline);
        }
    }
}

TEST_CASE("Spin waits retain their configured bounded fast path", "[common][spin_wait]") {
    for (unsigned spins : {0u, 1u, 4096u, 16384u}) {
        unsigned calls{};
        REQUIRE_FALSE(Common::BoundedSpinWait::Spin([&] { ++calls; return false; }, spins));
        REQUIRE(calls == spins + 1);
        REQUIRE(Common::BoundedSpinWait::Spin([] { return true; }, spins));
    }
}

TEST_CASE("Spin waits cannot lose completion between the last predicate and park",
          "[common][spin_wait]") {
    Common::BoundedSpinWait wait;
    std::atomic<u64> progress{};
    unsigned calls{};
    bool complete{};
    REQUIRE(wait.Wait(progress, [&] {
        if (++calls == 16386) {
            complete = true;
            progress.fetch_add(1, std::memory_order_release);
            progress.notify_one();
            return false;
        }
        return complete;
    }));
    REQUIRE(calls == 16387);
    REQUIRE_FALSE(wait.Wait(progress, [&] { return complete; }));
}

TEST_CASE("Spin waits recheck completion after unrelated notifications", "[common][spin_wait]") {
    using namespace std::chrono_literals;
    std::atomic<u64> progress{};
    std::atomic<bool> complete{};
    std::binary_semaphore entered{0};
    std::promise<void> finished;
    auto result = finished.get_future();
    int payload{};
    int observed{};
    std::jthread thread([&] {
        Common::BoundedSpinWait wait;
        entered.release();
        wait.Wait(progress, [&] { return complete.load(std::memory_order_acquire); });
        observed = payload;
        finished.set_value();
    });
    entered.acquire();
    for (int i = 0; i < 64; ++i) {
        progress.fetch_add(1, std::memory_order_release);
        progress.notify_one();
    }
    const bool still_waiting = result.wait_for(20ms) == std::future_status::timeout;
    payload = 123;
    complete.store(true, std::memory_order_release);
    progress.fetch_add(1, std::memory_order_release);
    progress.notify_one();
    const bool completed = result.wait_for(2s) == std::future_status::ready;
    thread.join();
    REQUIRE(still_waiting);
    REQUIRE(completed);
    REQUIRE(observed == 123);
}

TEST_CASE("Spin waits preserve each publication through repeated concurrent handoffs",
          "[common][spin_wait]") {
    std::atomic<u64> progress{};
    std::atomic<unsigned> published{};
    std::binary_semaphore acknowledged{0};
    unsigned payload{};
    bool correct = true;
    std::jthread consumer([&] {
        Common::BoundedSpinWait wait;
        for (unsigned i = 1; i <= 5000; ++i) {
            wait.Wait(progress, [&] { return published.load(std::memory_order_acquire) >= i; });
            correct &= payload == i;
            acknowledged.release();
        }
    });
    for (unsigned i = 1; i <= 5000; ++i) {
        payload = i;
        published.store(i, std::memory_order_release);
        progress.fetch_add(1, std::memory_order_release);
        progress.notify_one();
        acknowledged.acquire();
    }
    consumer.join();
    REQUIRE(correct);
}
