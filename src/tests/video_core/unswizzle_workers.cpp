// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <latch>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/alignment.h"
#include "common/div_ceil.h"
#include "common/scope_exit.h"
#include "video_core/textures/decoders.h"
#include "video_core/textures/unswizzle_worker.h"
#include "video_core/textures/workers.h"

namespace {

using namespace Tegra::Texture;

struct Layout {
    u32 width;
    u32 height;
    u32 bpp;
    u32 block_height;
    u32 alignment;

    size_t GuestBytes() const {
        const size_t stride = size_t{Common::AlignUpLog2(width, alignment)} * bpp;
        return Common::DivCeil(stride, size_t{GOB_SIZE_X}) *
               Common::DivCeil(height, GOB_SIZE_Y << block_height) *
               (size_t{GOB_SIZE} << block_height);
    }

    size_t HostBytes() const {
        return size_t{width} * height * bpp;
    }

    void Serial(std::span<u8> output, std::span<const u8> input, bool native) const {
        UnswizzleTexture(output, input, bpp, width, height, 1, block_height, 0, alignment, native);
    }

    bool Parallel(std::span<u8> output, std::span<const u8> input, bool native,
                  UnswizzleWorker& worker, Common::ThreadWorker& pool, bool verify = false) const {
        return worker.TryUnswizzle(output, input, bpp, width, height, 1, block_height, 0, alignment,
                                   native, verify, &pool);
    }
};

void Fill(std::span<u8> bytes) {
    u32 state = 0x12345678;
    for (u8& byte : bytes) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        byte = static_cast<u8>(state);
    }
}

} // namespace

TEST_CASE("Owned unswizzle matches the original across GOB layouts", "[unswizzle_workers]") {
    Common::ThreadWorker pool{1, "UnswizzleTest"};
    UnswizzleWorker worker;
    for (const bool native : {false, true}) {
        for (const u32 bpp : {1U, 2U, 4U, 8U, 16U}) {
            for (const u32 block_height : {0U, 1U, 2U, 3U, 4U, 5U}) {
                for (const u32 spacing : {0U, 2U}) {
                    const Layout layout{2049 / bpp + 1, 1025, bpp, block_height,
                                        6 - static_cast<u32>(std::countr_zero(bpp)) + spacing};
                    CAPTURE(native, bpp, block_height, spacing);
                    std::vector<u8> input(layout.GuestBytes());
                    Fill(input);
                    std::vector<u8> expected(layout.HostBytes() + 64, 0xCD);
                    std::vector<u8> actual(expected.size(), 0xCD);
                    layout.Serial(std::span{expected}.subspan(32, layout.HostBytes()), input,
                                  native);
                    const bool taken =
                        layout.Parallel(std::span{actual}.subspan(32, layout.HostBytes()), input,
                                        native, worker, pool);
                    REQUIRE(taken == (native && bpp <= 2));
                    if (!taken) {
                        REQUIRE(std::ranges::all_of(actual, [](u8 byte) { return byte == 0xCD; }));
                        layout.Serial(std::span{actual}.subspan(32, layout.HostBytes()), input,
                                      native);
                    }
                    REQUIRE(actual == expected);
                }
            }
        }
    }
}

TEST_CASE("Small and unsplittable unswizzles leave the original path untouched",
          "[unswizzle_workers]") {
    Common::ThreadWorker pool{1, "UnswizzleTest"};
    UnswizzleWorker worker;
    for (const Layout layout :
         {Layout{256, 256, 4, 4, 4}, Layout{65537, 32, 1, 5, 6}, Layout{1023, 256, 4, 0, 4},
          Layout{65, 32769, 1, 0, 8}, Layout{8193, 4096, 1, 0, 6}}) {
        std::vector<u8> input(layout.GuestBytes());
        std::vector<u8> output(layout.HostBytes(), 0xCD);
        REQUIRE_FALSE(layout.Parallel(output, input, true, worker, pool));
        REQUIRE(std::ranges::all_of(output, [](u8 byte) { return byte == 0xCD; }));
    }
    const Layout layout{1025, 1024, 2, 0, 5};
    std::vector<u8> input(layout.GuestBytes());
    std::vector<u8> output(layout.HostBytes(), 0xCD);
    REQUIRE_FALSE(
        worker.TryUnswizzle(output, input, 2, 1025, 1024, 2, 0, 1, 5, true, false, &pool));
    REQUIRE_FALSE(
        layout.Parallel(output, std::span{input}.first(input.size() - 1), true, worker, pool));
    REQUIRE_FALSE(
        layout.Parallel(std::span{output}.first(output.size() - 1), input, true, worker, pool));
    REQUIRE(std::ranges::all_of(output, [](u8 byte) { return byte == 0xCD; }));
    REQUIRE(layout.Parallel(output, input, true, worker, pool, true));
}

TEST_CASE("Unswizzle leaves output untouched when a delayed worker saturates the pool",
          "[unswizzle_workers]") {
    using namespace std::chrono_literals;
    const Layout layout{1025, 1025, 2, 4, 5};
    std::vector<u8> input(layout.GuestBytes());
    Fill(input);
    std::vector<u8> expected(layout.HostBytes());
    layout.Serial(expected, input, true);
    for (int iteration = 0; iteration < 8; ++iteration) {
        Common::ThreadWorker pool{1, "DelayedUnswizzle"};
        UnswizzleWorker worker;
        std::promise<void> entered;
        std::promise<void> release;
        auto gate = release.get_future();
        pool.QueueWork([&] {
            entered.set_value();
            gate.wait();
        });
        entered.get_future().wait();
        std::vector<u8> actual(layout.HostBytes(), 0xCD);
        auto result = std::async(
            std::launch::async, [&] { return layout.Parallel(actual, input, true, worker, pool); });
        auto unblock = SCOPE_GUARD {
            release.set_value();
            pool.WaitForRequests();
        };
        REQUIRE(result.wait_for(2s) == std::future_status::ready);
        REQUIRE_FALSE(result.get());
        REQUIRE(std::ranges::all_of(actual, [](u8 byte) { return byte == 0xCD; }));
        release.set_value();
        unblock.Cancel();
        pool.WaitForRequests();
        REQUIRE(layout.Parallel(actual, input, true, worker, pool));
        REQUIRE(actual == expected);
    }
}

TEST_CASE("Texture worker admission reserves only available worker slots", "[unswizzle_workers]") {
    Common::ThreadWorker pool{2, "AdmissionTest"};
    std::latch entered{2};
    std::latch release{1};
    std::atomic<unsigned> completed{};
    auto unblock = SCOPE_GUARD {
        release.count_down();
        pool.WaitForRequests();
    };
    for (int i = 0; i < 2; ++i) {
        REQUIRE(pool.TryQueueWork([&] {
            entered.count_down();
            release.wait();
            ++completed;
        }));
    }
    entered.wait();
    REQUIRE_FALSE(pool.TryQueueWork([&] { ++completed; }));
    release.count_down();
    unblock.Cancel();
    pool.WaitForRequests();
    REQUIRE(completed == 2);
    REQUIRE(pool.TryQueueWork([&] { ++completed; }));
    pool.WaitForRequests();
    REQUIRE(completed == 3);
}

TEST_CASE("Owned snapshot survives overlapping source and destination", "[unswizzle_workers]") {
    Common::ThreadWorker pool{1, "UnswizzleTest"};
    UnswizzleWorker worker;
    for (const Layout layout :
         {Layout{2049, 1025, 1, 0, 8}, Layout{1025, 1025, 2, 4, 5}, Layout{4097, 1025, 1, 5, 6}}) {
        std::vector<u8> storage(layout.GuestBytes());
        Fill(storage);
        std::vector<u8> expected(layout.HostBytes());
        layout.Serial(expected, storage, true);
        const auto output = std::span{storage}.first(layout.HostBytes());
        REQUIRE(layout.Parallel(output, storage, true, worker, pool));
        REQUIRE(std::ranges::equal(output, expected));
    }
}

TEST_CASE("Texture worker admission rejects stopped pools and completes accepted work",
          "[unswizzle_workers]") {
    for (int iteration = 0; iteration < 64; ++iteration) {
        Common::ThreadWorker pool{1, "StoppingUnswizzle"};
        std::atomic<unsigned> completed{};
        REQUIRE(pool.TryQueueWork([&] { ++completed; }));
        std::stop_source stop;
        stop.request_stop();
        pool.WaitForRequests(stop.get_token());
        REQUIRE(completed == 1);
        REQUIRE_FALSE(pool.TryQueueWork([&] { ++completed; }));
        REQUIRE(completed == 1);
    }
    Common::ThreadWorker pool{1, "StoppedUnswizzle"};
    std::stop_source stop;
    stop.request_stop();
    pool.WaitForRequests(stop.get_token());
    const Layout layout{1025, 1025, 2, 4, 5};
    std::vector<u8> input(layout.GuestBytes());
    std::vector<u8> output(layout.HostBytes(), 0xCD);
    UnswizzleWorker worker;
    REQUIRE_FALSE(layout.Parallel(output, input, true, worker, pool));
    REQUIRE(std::ranges::all_of(output, [](u8 byte) { return byte == 0xCD; }));
}

TEST_CASE("Invalid unswizzle dimensions are rejected before shifts or size calculations",
          "[unswizzle_workers]") {
    Common::ThreadWorker pool{1, "BoundsUnswizzle"};
    UnswizzleWorker worker;
    std::vector<u8> input(64, 0xAB);
    std::vector<u8> output(64, 0xCD);
    constexpr u32 Max = std::numeric_limits<u32>::max();
    for (const u32 bad : {0U, Max}) {
        REQUIRE_FALSE(worker.TryUnswizzle(output, input, 1, bad, 1025, 1, 4, 0, 6,
                                          true, false, &pool));
        REQUIRE_FALSE(worker.TryUnswizzle(output, input, 2, 2049, bad, 1, 4, 0, 6,
                                          true, false, &pool));
        REQUIRE_FALSE(worker.TryUnswizzle(output, input, bad, 2049, 1025, 1, 4, 0, 6,
                                          true, false, &pool));
    }
    for (const u32 shift : {6U, 31U, 32U, Max}) {
        REQUIRE_FALSE(worker.TryUnswizzle(output, input, 1, 2049, 1025, 1, shift, 0, 6,
                                          true, false, &pool));
    }
    for (const u32 shift : {31U, 32U, Max}) {
        REQUIRE_FALSE(worker.TryUnswizzle(output, input, 1, 2049, 1025, 1, 4, 0, shift,
                                          true, false, &pool));
    }
    REQUIRE_FALSE(worker.TryUnswizzle(output, input, 2, Max, Max, 1, 4, 0, 6,
                                      true, false, &pool));
    REQUIRE(std::ranges::all_of(output, [](u8 byte) { return byte == 0xCD; }));
}

TEST_CASE("Accepted texture work survives pool destruction", "[unswizzle_workers]") {
    std::atomic<unsigned> completed{};
    for (unsigned iteration = 0; iteration < 64; ++iteration) {
        {
            Common::ThreadWorker pool{1, "DestroyingUnswizzle"};
            REQUIRE(pool.TryQueueWork([&] { ++completed; }));
        }
        REQUIRE(completed == iteration + 1);
    }
}

TEST_CASE("Stateful worker admission retains the worker state through cancellation",
          "[unswizzle_workers]") {
    Common::StatefulThreadWorker<int> pool{1, "StatefulAdmission", [] { return 42; }};
    int observed{};
    REQUIRE(pool.TryQueueWork([&](int* state) { observed = *state; }));
    std::stop_source stop;
    stop.request_stop();
    pool.WaitForRequests(stop.get_token());
    REQUIRE(observed == 42);
    REQUIRE_FALSE(pool.TryQueueWork([](int*) {}));
}

TEST_CASE("Stopping completes accepted work without destroying ordinary queued captures",
          "[unswizzle_workers]") {
    struct DestructionProbe {
        std::thread::id& destroyed_on;
        std::mutex& mutex;

        DestructionProbe(std::thread::id& thread, std::mutex& mutex_)
            : destroyed_on{thread}, mutex{mutex_} {}
        ~DestructionProbe() {
            std::scoped_lock lock{mutex};
            destroyed_on = std::this_thread::get_id();
        }
    };
    const auto owner = std::this_thread::get_id();
    std::array<std::thread::id, 2> destroyed_on{};
    std::mutex destroyed_mutex;
    std::atomic<unsigned> ordinary_completed{};
    std::atomic<unsigned> accepted_completed{};
    {
        std::latch started{2};
        std::latch release{1};
        Common::StatefulThreadWorker<int> pool{2, "MixedStop", [&] {
                                                 started.count_down();
                                                 release.wait();
                                                 return 42;
                                             }};
        auto unblock = SCOPE_GUARD { release.count_down(); };
        started.wait();
        pool.QueueWork(
            [&, probe = std::make_unique<DestructionProbe>(destroyed_on[0], destroyed_mutex)](int*) {
                ++ordinary_completed;
            });
        REQUIRE(pool.TryQueueWork([&](int* state) {
            if (*state == 42) {
                ++accepted_completed;
            }
        }));
        pool.QueueWork(
            [&, probe = std::make_unique<DestructionProbe>(destroyed_on[1], destroyed_mutex)](int*) {
                ++ordinary_completed;
            });
        pool.RequestStop();
        REQUIRE_FALSE(pool.TryQueueWork([](int*) {}));
        release.count_down();
        unblock.Cancel();
        std::stop_source stop;
        stop.request_stop();
        pool.WaitForRequests(stop.get_token());
        REQUIRE(accepted_completed == 1);
        REQUIRE(ordinary_completed == 0);
        {
            std::scoped_lock lock{destroyed_mutex};
            REQUIRE(destroyed_on[0] == std::thread::id{});
            REQUIRE(destroyed_on[1] == std::thread::id{});
        }
    }
    REQUIRE(destroyed_on[0] == owner);
    REQUIRE(destroyed_on[1] == owner);
}

TEST_CASE("Independent owners can share the texture pool", "[unswizzle_workers]") {
    const Layout layout{1025, 1025, 2, 4, 5};
    Common::ThreadWorker pool{2, "UnswizzleTest"};
    std::vector<u8> input(layout.GuestBytes());
    Fill(input);
    std::vector<u8> expected(layout.HostBytes());
    layout.Serial(expected, input, true);
    for (int iteration = 0; iteration < 4; ++iteration) {
        std::vector<std::future<std::vector<u8>>> results;
        for (int owner = 0; owner < 4; ++owner) {
            results.push_back(std::async(std::launch::async, [&] {
                UnswizzleWorker worker;
                std::vector<u8> actual(layout.HostBytes());
                if (!layout.Parallel(actual, input, true, worker, pool)) {
                    layout.Serial(actual, input, true);
                }
                return actual;
            }));
        }
        for (auto& result : results) {
            REQUIRE(result.get() == expected);
        }
    }
}

TEST_CASE("Admitted unswizzle waits for each new owned snapshot", "[unswizzle_workers]") {
    const Layout layout{4097, 2049, 1, 4, 6};
    Common::ThreadWorker pool{1, "SnapshotReady"};
    UnswizzleWorker worker;
    std::vector<u8> input(layout.GuestBytes());
    std::vector<u8> expected(layout.HostBytes());
    std::vector<u8> actual(layout.HostBytes());
    for (u8 generation = 1; generation <= 16; ++generation) {
        std::ranges::fill(input, generation);
        layout.Serial(expected, input, true);
        REQUIRE(layout.Parallel(actual, input, true, worker, pool));
        REQUIRE(actual == expected);
        pool.WaitForRequests();
    }
}

TEST_CASE("Saturated unswizzle admission and fallback microbenchmark",
          "[.unswizzle_saturated_benchmark]") {
    using Clock = std::chrono::steady_clock;
    Common::ThreadWorker pool{1, "SaturatedBenchmark"};
    UnswizzleWorker worker;
    std::printf("host_bytes,guest_bytes,serial_us,rejected_only_us,fallback_us,ratio\n");
    for (const Layout layout : {Layout{2049, 1025, 1, 4, 6},
                                Layout{8193, 2049, 1, 4, 6}}) {
        std::vector<u8> input(layout.GuestBytes());
        Fill(input);
        std::vector<u8> expected(layout.HostBytes());
        std::vector<u8> actual(layout.HostBytes());
        REQUIRE(layout.Parallel(actual, input, true, worker, pool));
        pool.WaitForRequests();
        std::latch entered{1};
        std::latch release{1};
        pool.QueueWork([&] { entered.count_down(); release.wait(); });
        auto unblock = SCOPE_GUARD {
            release.count_down();
            pool.WaitForRequests();
        };
        entered.wait();
        std::array<std::vector<double>, 3> times;
        for (int iteration = 0; iteration < 36; ++iteration) {
            for (int operation = 0; operation < 3; ++operation) {
                const int kind = (operation + iteration) % 3;
                const auto begin = Clock::now();
                bool admitted = false;
                if (kind == 0) {
                    layout.Serial(expected, input, true);
                } else {
                    admitted = layout.Parallel(actual, input, true, worker, pool);
                    if (kind == 2 && !admitted) {
                        layout.Serial(actual, input, true);
                    }
                }
                const double elapsed =
                    std::chrono::duration<double, std::micro>(Clock::now() - begin).count();
                REQUIRE_FALSE(admitted);
                if (iteration >= 4) {
                    times[kind].push_back(elapsed);
                }
            }
            REQUIRE(actual == expected);
        }
        for (auto& measurements : times) {
            std::ranges::sort(measurements);
        }
        const double serial = times[0][times[0].size() / 2];
        const double rejected = times[1][times[1].size() / 2];
        const double fallback = times[2][times[2].size() / 2];
        std::printf("%zu,%zu,%.2f,%.2f,%.2f,%.3f\n", layout.HostBytes(), layout.GuestBytes(),
                    serial, rejected, fallback, fallback / serial);
    }
}

TEST_CASE("Unswizzle end to end microbenchmark", "[.unswizzle_benchmark]") {
    using Clock = std::chrono::steady_clock;
    UnswizzleWorker worker;
    const Layout startup_layout{1025, 1025, 2, 4, 5};
    std::vector<u8> startup_input(startup_layout.GuestBytes());
    std::vector<u8> startup_output(startup_layout.HostBytes());
    Fill(startup_input);
    const auto startup_begin = Clock::now();
    auto& pool = GetThreadWorkers();
    REQUIRE(startup_layout.Parallel(startup_output, startup_input, true, worker, pool));
    const auto startup_us =
        std::chrono::duration<double, std::micro>(Clock::now() - startup_begin).count();
    std::printf("first_eligible_call_including_pool_startup_us=%.2f\n", startup_us);
    std::printf(
        "width,height,bpp,block_height,native,host_bytes,guest_bytes,parallel_used,serial_us,owned_"
        "parallel_us,ratio\n");
    for (const Layout layout :
         {Layout{256, 256, 4, 4, 4}, Layout{512, 512, 4, 4, 4}, Layout{1024, 1024, 4, 4, 4},
          Layout{2048, 2048, 4, 4, 4}, Layout{2049, 1025, 1, 0, 8}, Layout{513, 257, 16, 5, 4},
          Layout{513, 1025, 16, 5, 4}, Layout{1025, 1025, 2, 4, 5}, Layout{513, 1025, 4, 4, 4},
          Layout{257, 1025, 8, 4, 3}, Layout{4097, 1025, 1, 4, 6}, Layout{2049, 1025, 2, 4, 5},
          Layout{2050, 1025, 1, 4, 6}}) {
        for (const bool native : {false, true}) {
            std::vector<u8> input(layout.GuestBytes());
            Fill(input);
            std::vector<u8> expected(layout.HostBytes());
            std::vector<u8> actual(layout.HostBytes());
            std::vector<double> serial_times;
            std::vector<double> parallel_times;
            bool parallel_used = false;
            const auto serial = [&] { layout.Serial(expected, input, native); };
            const auto parallel = [&] {
                parallel_used = layout.Parallel(actual, input, native, worker, pool);
                if (!parallel_used) {
                    layout.Serial(actual, input, native);
                }
            };
            const auto measure = [](auto&& operation) {
                const auto begin = Clock::now();
                operation();
                return std::chrono::duration<double, std::micro>(Clock::now() - begin).count();
            };
            for (int i = 0; i < 44; ++i) {
                double s, p;
                if (i % 2 == 0) {
                    s = measure(serial);
                    p = measure(parallel);
                } else {
                    p = measure(parallel);
                    s = measure(serial);
                }
                REQUIRE(actual == expected);
                if (i >= 4) {
                    serial_times.push_back(s);
                    parallel_times.push_back(p);
                }
            }
            std::ranges::sort(serial_times);
            std::ranges::sort(parallel_times);
            const double s = serial_times[serial_times.size() / 2];
            const double p = parallel_times[parallel_times.size() / 2];
            std::printf("%u,%u,%u,%u,%d,%zu,%zu,%d,%.2f,%.2f,%.3f\n", layout.width, layout.height,
                        layout.bpp, layout.block_height, native, layout.HostBytes(),
                        layout.GuestBytes(), parallel_used, s, p, p / s);
        }
    }
}
