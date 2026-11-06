// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <chrono>
#include <future>
#include <semaphore>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "core/core.h"
#include "video_core/gpu_split.h"

namespace VideoCommon {

// Exercise the production ring and Stop without starting an emulator or its engine thread.
struct GpuSplitTestAccess {
    static void Start(GpuSplit& split, bool near_wrap = false) {
        split.ring = std::make_unique<std::byte[]>(GpuSplit::RingWords * sizeof(u64));
        const u64 position = near_wrap ? GpuSplit::RingWords - GpuSplit::FunctionWords - 1 : 0;
        split.write_position = position;
        split.published_local = position;
        split.consumed_cache = position;
        split.published.store(position);
        split.consumed.store(position);
        split.active = true;
        // No consumer is allowed to race the test's deterministic execution/cancellation.
        split.back_thread = std::jthread([] {});
    }

    static void StartBack(GpuSplit& split, Core::System& system) {
        split.system = &system;
        split.ring = std::make_unique<std::byte[]>(GpuSplit::RingWords * sizeof(u64));
        split.active = true;
        split.back_thread = std::jthread(
            [&split](std::stop_token stop_token) { split.BackLoop(stop_token); });
    }

    static void RequestStop(GpuSplit& split) {
        split.back_thread.request_stop();
    }

    static u64 Consumed(const GpuSplit& split) {
        return split.consumed.load(std::memory_order_acquire);
    }

    static void ExecuteOne(GpuSplit& split) {
        const u64 position = split.consumed.load();
        const size_t index = position & GpuSplit::RingMask;
        const size_t size = split.Execute(split.EntryAt(index));
        split.consumed.store(position + (size != 0 ? size : GpuSplit::RingWords - index));
    }

    static void QueueEngine(GpuSplit& split) {
        split.SwitchEngine(nullptr);
    }

    static u64 Published(const GpuSplit& split) {
        return split.published.load();
    }

    static u64 Written(const GpuSplit& split) {
        return split.write_position;
    }

    static size_t FunctionCapacity() {
        return GpuSplit::RingWords / GpuSplit::FunctionWords;
    }

    static u64 WaitTarget(const GpuSplit& split) {
        return split.wait_target.load(std::memory_order_acquire);
    }

    static bool BackWaiting(const GpuSplit& split) {
        return split.back_waiting.load(std::memory_order_acquire);
    }

    static bool Reset(const GpuSplit& split) {
        return !split.ring && !split.back_thread.joinable() && !split.active &&
               split.write_position == 0 && split.published.load() == 0 &&
               split.consumed.load() == 0 && split.back_quiet.load() &&
               !split.hook_requested.load() && split.wait_target.load() == ~u64{0};
    }
};

} // namespace VideoCommon

namespace {

using VideoCommon::GpuSplit;
using Access = VideoCommon::GpuSplitTestAccess;

struct Capture {
    explicit Capture(int& destroyed_) : destroyed{destroyed_} {}
    ~Capture() {
        ++destroyed;
    }
    int& destroyed;
};

std::weak_ptr<Capture> QueueOwned(GpuSplit& split, int& calls, int& destroyed) {
    auto capture = std::make_shared<Capture>(destroyed);
    const std::weak_ptr<Capture> weak = capture;
    split.DeferFunction([capture = std::move(capture), &calls] { ++calls; });
    return weak;
}

} // namespace

TEST_CASE("GPU split cancels published and unpublished owned callbacks on shutdown",
          "[video_core][gpu_split_lifetime]") {
    GpuSplit split;
    int calls{};
    int destroyed{};
    Access::Start(split);
    const auto published = QueueOwned(split, calls, destroyed);
    split.Publish();
    const auto unpublished = QueueOwned(split, calls, destroyed);
    REQUIRE(Access::Published(split) < Access::Written(split));
    REQUIRE_FALSE(published.expired());
    REQUIRE_FALSE(unpublished.expired());

    split.Stop();
    REQUIRE(calls == 0);
    REQUIRE(destroyed == 2);
    REQUIRE(published.expired());
    REQUIRE(unpublished.expired());
    REQUIRE(Access::Reset(split));
    split.Stop();
    REQUIRE(destroyed == 2);
}

TEST_CASE("GPU split executes owned callbacks once and cancels only the unconsumed tail",
          "[video_core][gpu_split_lifetime]") {
    GpuSplit split;
    int calls{};
    int destroyed{};
    Access::Start(split);
    const auto completed = QueueOwned(split, calls, destroyed);
    split.Publish();
    Access::ExecuteOne(split);
    REQUIRE(completed.expired());
    REQUIRE(calls == 1);
    REQUIRE(destroyed == 1);
    const auto cancelled = QueueOwned(split, calls, destroyed);
    split.Publish();

    split.Stop();
    REQUIRE(calls == 1);
    REQUIRE(destroyed == 2);
    REQUIRE(cancelled.expired());
}

TEST_CASE("GPU split cancellation walks mixed entries and ring wraps without executing work",
          "[video_core][gpu_split_lifetime]") {
    GpuSplit split;
    int calls{};
    int destroyed{};
    Access::Start(split, true);
    const auto before_wrap = QueueOwned(split, calls, destroyed);
    split.Defer([&calls] { ++calls; }); // Three words: forces a Wrap after the owned entry.
    Access::QueueEngine(split);
    const GPUVAddr segment = 0x1234;
    split.Method(nullptr, 1, 2, true, nullptr);
    split.Method(nullptr, 1, 2, true, &segment);
    split.MacroWrite(nullptr, 1, 2);
    const std::array<u32, 3> words{1, 2, 3};
    split.MultiMethod(nullptr, 1, words.data(), 2, 2, nullptr);
    split.MultiMethod(nullptr, 1, words.data(), 3, 3, &segment);
    const std::vector<u32> long_words(8193, 42);
    split.MultiMethod(nullptr, 1, long_words.data(), 8193, 8193, nullptr);
    const auto after_wrap = QueueOwned(split, calls, destroyed);
    split.Publish();

    split.Stop();
    REQUIRE(calls == 0);
    REQUIRE(destroyed == 2);
    REQUIRE(before_wrap.expired());
    REQUIRE(after_wrap.expired());
    REQUIRE(Access::Reset(split));
}

TEST_CASE("GPU split repeated sessions release every owned capture exactly once",
          "[video_core][gpu_split_lifetime]") {
    GpuSplit split;
    int calls{};
    int destroyed{};
    for (int session = 0; session < 64; ++session) {
        Access::Start(split);
        QueueOwned(split, calls, destroyed);
        split.Publish();
        Access::ExecuteOne(split);
        const auto published = QueueOwned(split, calls, destroyed);
        split.Publish();
        const auto unpublished = QueueOwned(split, calls, destroyed);
        split.Stop();
        REQUIRE(calls == session + 1);
        REQUIRE(destroyed == (session + 1) * 3);
        REQUIRE(published.expired());
        REQUIRE(unpublished.expired());
        REQUIRE(Access::Reset(split));
    }
}

TEST_CASE("GPU split Drain waits for an in-flight idle hook after all entries are consumed",
          "[video_core][gpu_split_lifetime]") {
    using namespace std::chrono_literals;
    Core::System system;
    GpuSplit split;
    struct Hook {
        std::binary_semaphore entered{0};
        std::binary_semaphore release{0};
        bool entered_once{};
    } hook;
    const auto budgets = GENERATE(GpuSplit::SpinBudgets{}, GpuSplit::SpinBudgets{0, 0, 0},
                                  GpuSplit::SpinBudgets{64, 32, 128});
    REQUIRE(split.SetSpinBudgets(budgets));
    split.SetIdleHook([](void* argument) {
        auto& state = *static_cast<Hook*>(argument);
        if (state.entered_once) {
            return;
        }
        state.entered_once = true;
        state.entered.release();
        state.release.acquire();
    }, &hook);
    Access::StartBack(split, system);
    std::atomic<int> calls{};
    split.Defer([&calls] { ++calls; });
    split.Publish();
    const bool hook_entered = hook.entered.try_acquire_for(2s);
    const bool all_consumed = Access::Consumed(split) == Access::Published(split);
    std::promise<void> drain_started;
    std::promise<void> drain_finished;
    auto started = drain_started.get_future();
    auto finished = drain_finished.get_future();
    std::jthread front([&] {
        drain_started.set_value();
        split.Drain();
        drain_finished.set_value();
    });
    started.wait();
    const bool waited_for_hook = finished.wait_for(20ms) == std::future_status::timeout;
    hook.release.release();
    const bool finished_after_hook = finished.wait_for(2s) == std::future_status::ready;
    if (!finished_after_hook) {
        Access::RequestStop(split);
        split.RequestIdleHook();
    }
    front.join();
    split.Stop();
    REQUIRE(hook_entered);
    REQUIRE(all_consumed);
    REQUIRE(calls.load() == 1);
    REQUIRE(waited_for_hook);
    REQUIRE(finished_after_hook);
    REQUIRE(Access::Reset(split));
}

TEST_CASE("GPU split runs its final hook as owner and cancels the tail across real back sessions",
          "[video_core][gpu_split_lifetime]") {
    using namespace std::chrono_literals;
    Core::System system;
    GpuSplit split;
    struct Hook {
        GpuSplit& split;
        int calls{};
        bool ran_as_owner{true};
    } hook{split};
    const auto budgets = GENERATE(GpuSplit::SpinBudgets{}, GpuSplit::SpinBudgets{0, 0, 0});
    REQUIRE(split.SetSpinBudgets(budgets));
    split.SetIdleHook([](void* argument) {
        auto& state = *static_cast<Hook*>(argument);
        ++state.calls;
        state.ran_as_owner &= state.split.BackRunning() && GpuSplit::OnBackThread();
    }, &hook);
    for (int session = 0; session < 8; ++session) {
        std::binary_semaphore entered{0};
        std::binary_semaphore release{0};
        Access::StartBack(split, system);
        split.Defer([&entered, &release] {
            entered.release();
            release.acquire();
        });
        split.Publish();
        const bool callback_entered = entered.try_acquire_for(2s);
        int tail_calls{};
        int tail_destroyed{};
        const auto published_tail = QueueOwned(split, tail_calls, tail_destroyed);
        split.Publish();
        const auto unpublished_tail = QueueOwned(split, tail_calls, tail_destroyed);
        // Stop during the callback, before the back can reach another idle iteration.
        Access::RequestStop(split);
        release.release();
        split.Stop();
        REQUIRE(callback_entered);
        REQUIRE(hook.calls == session + 1);
        REQUIRE(hook.ran_as_owner);
        REQUIRE(tail_calls == 0);
        REQUIRE(tail_destroyed == 2);
        REQUIRE(published_tail.expired());
        REQUIRE(unpublished_tail.expired());
        REQUIRE(Access::Reset(split));
    }
}

TEST_CASE("GPU split parks a target wait and wakes before later callbacks finish",
          "[video_core][gpu_split_lifetime]") {
    using namespace std::chrono_literals;
    Core::System system;
    GpuSplit split;
    std::binary_semaphore first_entered{0};
    const auto budgets = GENERATE(GpuSplit::SpinBudgets{}, GpuSplit::SpinBudgets{0, 0, 0},
                                  GpuSplit::SpinBudgets{64, 32, 128});
    REQUIRE(split.SetSpinBudgets(budgets));
    std::binary_semaphore first_release{0};
    std::binary_semaphore tail_entered{0};
    std::binary_semaphore tail_release{0};
    Access::StartBack(split, system);
    split.Defer([&] { first_entered.release(); first_release.acquire(); });
    split.NoteSync();
    const u64 target = Access::Written(split);
    split.Defer([&] { tail_entered.release(); tail_release.acquire(); });
    split.Publish();
    const bool first_started = first_entered.try_acquire_for(2s);
    std::promise<bool> finished;
    auto result = finished.get_future();
    std::jthread front([&] { finished.set_value(split.WaitForSync()); });
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (Access::WaitTarget(split) != target && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    const bool target_registered = Access::WaitTarget(split) == target;
    const bool waited = result.wait_for(20ms) == std::future_status::timeout;
    split.SetSpinBudgets({0, 0, 0});
    const bool policy_did_not_complete = result.wait_for(20ms) == std::future_status::timeout;
    first_release.release();
    const bool tail_started = tail_entered.try_acquire_for(2s);
    const bool target_finished = result.wait_for(2s) == std::future_status::ready;
    tail_release.release();
    front.join();
    split.Drain();
    split.Stop();
    REQUIRE(first_started);
    REQUIRE(target_registered);
    REQUIRE(waited);
    REQUIRE(policy_did_not_complete);
    REQUIRE(tail_started);
    REQUIRE(target_finished);
    REQUIRE(result.get());
    REQUIRE(Access::Reset(split));
}

TEST_CASE("GPU split ring capacity waits preserve inline callbacks across wrap and reuse",
          "[video_core][gpu_split_lifetime]") {
    using namespace std::chrono_literals;
    Core::System system;
    GpuSplit split;
    std::binary_semaphore entered{0};
    std::binary_semaphore release{0};
    std::atomic<size_t> calls{};
    const auto budgets = GENERATE(GpuSplit::SpinBudgets{}, GpuSplit::SpinBudgets{0, 0, 0},
                                  GpuSplit::SpinBudgets{64, 32, 128});
    REQUIRE(split.SetSpinBudgets(budgets));
    Access::StartBack(split, system);
    split.Defer([&] { entered.release(); release.acquire(); });
    split.Publish();
    const bool started = entered.try_acquire_for(2s);
    const size_t count = Access::FunctionCapacity() * 2 + 1;
    std::promise<void> finished;
    auto result = finished.get_future();
    std::jthread front([&] {
        for (size_t i = 0; i < count; ++i) {
            split.DeferFunction([&calls] { ++calls; });
        }
        split.Drain();
        finished.set_value();
    });
    const bool waited = result.wait_for(20ms) == std::future_status::timeout;
    release.release();
    const bool completed = result.wait_for(10s) == std::future_status::ready;
    front.join();
    split.Stop();
    REQUIRE(started);
    REQUIRE(waited);
    REQUIRE(completed);
    REQUIRE(calls.load() == count);
    REQUIRE(Access::Reset(split));
}

TEST_CASE("GPU split zero-spin back wakes on publication and preserves coherent policy snapshots",
          "[video_core][gpu_split_lifetime][spin_budgets]") {
    using namespace std::chrono_literals;
    Core::System system;
    GpuSplit split;
    REQUIRE(split.SetSpinBudgets({0, 0, 0}));
    Access::StartBack(split, system);
    bool parked = true;
    size_t calls{};
    for (int i = 0; i < 64; ++i) {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!Access::BackWaiting(split) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        parked &= Access::BackWaiting(split);
        split.Defer([&calls] { ++calls; });
        split.Publish();
        split.Drain();
    }
    split.Stop();
    REQUIRE(parked);
    REQUIRE(calls == 64);
    REQUIRE(Access::Reset(split));

    std::atomic<bool> done{};
    std::jthread writer([&] {
        for (int i = 0; i < 10000; ++i) {
            split.SetSpinBudgets({});
            split.SetSpinBudgets({0, 0, 0});
        }
        done.store(true, std::memory_order_release);
    });
    bool coherent = true;
    do {
        const auto value = split.GetSpinBudgets();
        coherent &= value == GpuSplit::SpinBudgets{} || value == GpuSplit::SpinBudgets{0, 0, 0};
    } while (!done.load(std::memory_order_acquire));
    writer.join();
    REQUIRE(coherent);
}

TEST_CASE("GPU split inline callback storage retains large and over-aligned captures",
          "[video_core][gpu_split_lifetime]") {
    struct alignas(64) LargeCapture {
        std::array<u64, 64> data{};
        std::shared_ptr<Capture> owner;
    } capture;
    GpuSplit split;
    int destroyed{};
    int calls{};
    capture.data.back() = 123;
    capture.owner = std::make_shared<Capture>(destroyed);
    const auto weak = std::weak_ptr{capture.owner};
    Access::Start(split, true);
    split.DeferFunction([saved = std::move(capture), &calls] { calls += int(saved.data.back()); });
    Access::ExecuteOne(split);
    REQUIRE(calls == 123);
    REQUIRE(weak.expired());
    REQUIRE(destroyed == 1);
    split.Stop();
}
