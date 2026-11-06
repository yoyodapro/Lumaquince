// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "common/thread_worker.h"

namespace {
using namespace std::chrono_literals;

// All fixture waits are bounded. These gates establish ordering; there are no sleeps, platform
// condition-variable hooks or assumptions about how quickly another thread gets scheduled.
class Gate {
public:
    void Open() {
        std::scoped_lock lock{mutex};
        open = true;
        condition.notify_all();
    }

    bool Wait() {
        std::unique_lock lock{mutex};
        return condition.wait_for(lock, 5s, [&] { return open; });
    }

private:
    std::mutex mutex;
    std::condition_variable condition;
    bool open{};
};

struct Probe {
    Gate work_entered;
    Gate release_work;
    Gate state_exiting;
    Gate release_state;
    Gate publication_lock_attempted;
    std::atomic<bool> publishing{};
    std::atomic<bool> setup_ok{true};
    bool saw_false_predicate{};
    bool saw_publication_lock{};
    bool wait_completed{};
};

// Only construction uses this thread-local pointer. Each primitive stores its own reference
// before the worker starts, so separate test threads cannot select one another's fixture.
thread_local Probe* construction_probe{};

class ObservedMutex {
public:
    void lock() {
        if (probe.publishing.load()) {
            probe.publication_lock_attempted.Open();
        }
        mutex.lock();
    }
    void unlock() {
        mutex.unlock();
    }

private:
    Probe& probe{*construction_probe};
    std::mutex mutex;
};

class ObservedCondition {
public:
    template <class Predicate>
    void wait(std::unique_lock<ObservedMutex>& lock, Predicate predicate) {
        probe.saw_false_predicate = !predicate();
        // Cancellation has already acquired queue_mutex and returned. The worker's state
        // destructor is held immediately before stopped-worker publication. Keep queue_mutex
        // locked in the exact false-predicate-to-sleep window, then let that worker proceed.
        probe.release_state.Open();
        probe.saw_publication_lock = probe.publication_lock_attempted.Wait();
        // The worker must attempt the SAME mutex before changing workers_stopped. Only the
        // real CV wait releases it. Removing the publication lock fails the event assertion;
        // even that regression returns boundedly because the predicate is then already true.
        probe.wait_completed = condition.wait_for(lock, 5s, predicate);
    }
    void notify_all() {
        condition.notify_all();
    }

private:
    Probe& probe{*construction_probe};
    std::condition_variable_any condition;
};

struct ObservedSynchronization {
    using Mutex = ObservedMutex;
    using WaitCondition = ObservedCondition;
};

struct WorkerState {
    std::shared_ptr<Probe> probe;
    ~WorkerState() {
        if (!probe) {
            return;
        }
        probe->state_exiting.Open();
        if (!probe->release_state.Wait()) {
            probe->setup_ok = false;
        }
        probe->publishing = true;
    }
};
} // namespace

TEST_CASE("Cancelled workers publish stopped state under the waiter's mutex",
          "[common][thread_worker][worker_wake]") {
    auto probe = std::make_shared<Probe>();
    construction_probe = probe.get();
    std::atomic<unsigned> ordinary_completed{};
    {
        Common::StatefulThreadWorker<WorkerState, ObservedSynchronization> pool{
            1, "WorkerWakeTest", [probe] { return WorkerState{probe}; }};
        pool.QueueWork([probe](WorkerState*) {
            probe->work_entered.Open();
            if (!probe->release_work.Wait()) {
                probe->setup_ok = false;
            }
        });
        const bool started = probe->work_entered.Wait();
        // This pending cancelled job keeps work_done below work_scheduled. Completion must
        // therefore come from workers_stopped, not the ordinary-work completion predicate.
        pool.QueueWork([&](WorkerState*) { ++ordinary_completed; });
        pool.RequestStop(); // Never request cancellation while the test holds queue_mutex.
        probe->release_work.Open();
        const bool exiting = probe->state_exiting.Wait();
        pool.WaitForRequests();
        CHECK(started);
        CHECK(exiting);
        CHECK(probe->setup_ok.load());
        CHECK(probe->saw_false_predicate);
        CHECK(probe->saw_publication_lock);
        CHECK(probe->wait_completed);
        CHECK(ordinary_completed == 0);
    }
    construction_probe = nullptr;
}

TEST_CASE("Worker completion wakes all waiters after admitted cancellation",
          "[common][thread_worker][worker_wake]") {
    Common::ThreadWorker pool{1, "WorkerWaitersTest"};
    Gate entered;
    Gate release;
    Gate first_done;
    Gate second_done;
    std::atomic<bool> worker_ok{true};
    std::atomic<unsigned> completed{};
    const bool admitted = pool.TryQueueWork([&] {
        entered.Open();
        worker_ok = release.Wait();
        ++completed;
    });
    const bool started = entered.Wait();
    std::stop_source stop;
    stop.request_stop();
    // The production stop callback can safely acquire queue_mutex before either wait sleeps.
    std::jthread first{[&] {
        pool.WaitForRequests(stop.get_token());
        first_done.Open();
    }};
    std::jthread second{[&] {
        pool.WaitForRequests(stop.get_token());
        second_done.Open();
    }};
    release.Open();
    const bool first_completed = first_done.Wait();
    const bool second_completed = second_done.Wait();
    // A broken production indefinite wait must not hang the whole test process on jthread
    // destruction. The test runner also needs an outer timeout (documented in the handoff).
    if (!first_completed || !second_completed) {
        std::terminate();
    }
    CHECK(admitted);
    CHECK(started);
    CHECK(worker_ok.load());
    CHECK(completed == 1);
    CHECK_FALSE(pool.TryQueueWork([] {}));
}

TEST_CASE("Ordinary worker completion drains queued work without cancellation",
          "[common][thread_worker][worker_wake]") {
    Common::ThreadWorker pool{1, "WorkerDrainTest"};
    Gate entered;
    Gate release;
    Gate done;
    std::atomic<bool> worker_ok{true};
    std::atomic<unsigned> completed{};
    pool.QueueWork([&] {
        entered.Open();
        worker_ok = release.Wait();
        ++completed;
    });
    const bool started = entered.Wait();
    pool.QueueWork([&] { ++completed; });
    std::jthread waiter{[&] {
        pool.WaitForRequests();
        done.Open();
    }};
    release.Open();
    if (!done.Wait()) {
        std::terminate();
    }
    CHECK(started);
    CHECK(worker_ok.load());
    CHECK(completed == 2);
}
