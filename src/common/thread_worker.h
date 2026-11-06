// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#include <queue>

#include "common/polyfill_thread.h"
#include "common/thread.h"
#include "common/unique_function.h"

namespace Common {

// Defaults keep the normal pool's synchronization unchanged. Tests can substitute forwarding
// primitives to observe the predicate-to-wait transition without platform CV interposition.
struct ThreadWorkerSynchronization {
    using Mutex = std::mutex;
    using WaitCondition = std::condition_variable;
};

template <class StateType = void, class Synchronization = ThreadWorkerSynchronization>
class StatefulThreadWorker {
    static constexpr bool with_state = !std::is_same_v<StateType, void>;

    struct DummyCallable {
        int operator()() const noexcept {
            return 0;
        }
    };

    using Task =
        std::conditional_t<with_state, UniqueFunction<void, StateType*>, UniqueFunction<void>>;
    using StateMaker = std::conditional_t<with_state, std::function<StateType()>, DummyCallable>;

public:
    explicit StatefulThreadWorker(size_t num_workers, std::string name, StateMaker func = {},
                                  ThreadPlacement placement = ThreadPlacement::Default)
        : workers_queued{num_workers}, thread_name{std::move(name)} {
        const auto lambda = [this, func, placement](std::stop_token stop_token) {
            Common::SetCurrentThreadName(thread_name.c_str());
            if (placement != ThreadPlacement::Default) {
                Common::SetCurrentThreadPriority(ThreadPriority::Low);
            }
            switch (placement) {
            case ThreadPlacement::Efficiency:
                Common::SetCurrentThreadToEfficiencyCores();
                break;
            case ThreadPlacement::Background:
                Common::SetCurrentThreadToBackgroundWork();
                break;
            default:
                Common::SetCurrentThreadToAllCores();
                break;
            }
            {
                [[maybe_unused]] std::conditional_t<with_state, StateType, int> state{func()};
                while (true) {
                    Task task;
                    {
                        std::unique_lock lock{queue_mutex};
                        if (requests.empty() && accepted_requests.empty()) {
                            wait_condition.notify_all();
                        }
                        condition.wait(lock, stop_token, [this] {
                            return !requests.empty() || !accepted_requests.empty();
                        });
                        // Ordinary cancelled captures keep their original lifetime: they stay
                        // queued until the owner's thread destroys this worker pool.
                        if (accepted_requests.empty() && stop_token.stop_requested()) {
                            break;
                        }
                        auto& queue = accepted_requests.empty() ? requests : accepted_requests;
                        task = std::move(queue.front());
                        queue.pop();
                    }
                    if constexpr (with_state) {
                        task(&state);
                    } else {
                        task();
                    }
                    ++work_done;
                }
            }
            {
                std::unique_lock lock{queue_mutex};
                ++workers_stopped;
            }
            wait_condition.notify_all();
        };
        threads.reserve(num_workers);
        for (size_t i = 0; i < num_workers; ++i) {
            threads.emplace_back(lambda);
        }
    }

    StatefulThreadWorker& operator=(const StatefulThreadWorker&) = delete;
    StatefulThreadWorker(const StatefulThreadWorker&) = delete;

    StatefulThreadWorker& operator=(StatefulThreadWorker&&) = delete;
    StatefulThreadWorker(StatefulThreadWorker&&) = delete;

    void QueueWork(Task work) {
        {
            std::unique_lock lock{queue_mutex};
            requests.emplace(std::move(work));
            ++work_scheduled;
        }
        condition.notify_one();
    }

    /// Admit work only into an available slot, guaranteeing execution even during cancellation.
    bool TryQueueWork(Task work) {
        {
            std::unique_lock lock{queue_mutex};
            if (stopping || work_scheduled - work_done >= workers_queued) {
                return false;
            }
            // Keep these bounded jobs separate, so stopping never scans or destroys the
            // potentially unbounded ordinary queue to find work a caller is waiting for.
            accepted_requests.emplace(std::move(work));
            ++work_scheduled;
        }
        condition.notify_one();
        return true;
    }

    void RequestStop() {
        std::unique_lock lock{queue_mutex};
        stopping = true;
        for (auto& thread : threads) {
            thread.request_stop();
        }
    }

    void WaitForRequests(std::stop_token stop_token = {}) {
        std::stop_callback callback(stop_token, [this] { RequestStop(); });
        std::unique_lock lock{queue_mutex};
        wait_condition.wait(lock, [this] {
            return workers_stopped >= workers_queued || work_done >= work_scheduled;
        });
    }

private:
    std::queue<Task> requests;
    std::queue<Task> accepted_requests;
    bool stopping{};
    typename Synchronization::Mutex queue_mutex;
    std::condition_variable_any condition;
    typename Synchronization::WaitCondition wait_condition;
    std::atomic<size_t> work_scheduled{};
    std::atomic<size_t> work_done{};
    std::atomic<size_t> workers_stopped{};
    std::atomic<size_t> workers_queued{};
    std::string thread_name;
    std::vector<std::jthread> threads;
};

using ThreadWorker = StatefulThreadWorker<>;

} // namespace Common
