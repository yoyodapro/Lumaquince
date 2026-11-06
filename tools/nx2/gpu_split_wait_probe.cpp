// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Standalone Windows probe: cl /O2 /EHsc /std:c++20 /I src gpu_split_wait_probe.cpp
// CPU measurements use this probe's threads only. No emulator or host settings are changed.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <semaphore>
#include <thread>

#define NOMINMAX
#include <windows.h>

#include "common/bounded_spin_wait.h"

using Clock = std::chrono::steady_clock;

static double ThreadCpuMs() {
    FILETIME created{}, exited{}, kernel{}, user{};
    GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
    const auto ticks = [](FILETIME time) {
        return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 10000.0;
}

static void Probe(bool park, unsigned iterations, unsigned delay_us) {
    std::atomic<unsigned> completed{};
    std::atomic<u64> progress{};
    std::binary_semaphore requested{0};
    double waiter_cpu{};
    unsigned blocking_path{};
    const auto start = Clock::now();
    const double producer_start = ThreadCpuMs();
    std::thread waiter([&] {
        Common::BoundedSpinWait spin;
        const double cpu_start = ThreadCpuMs();
        for (unsigned i = 1; i <= iterations; ++i) {
            requested.release();
            if (park) {
                blocking_path += spin.Wait(progress, [&] {
                    return completed.load(std::memory_order_acquire) >= i;
                });
            } else {
                for (unsigned spins = 0; completed.load(std::memory_order_acquire) < i; ++spins) {
                    if (spins < 16384) {
                        _mm_pause();
                    } else {
                        std::this_thread::yield();
                    }
                }
            }
        }
        waiter_cpu = ThreadCpuMs() - cpu_start;
    });
    for (unsigned i = 1; i <= iterations; ++i) {
        requested.acquire();
        if (delay_us >= 1000) {
            std::this_thread::sleep_for(std::chrono::microseconds{delay_us});
        } else if (delay_us != 0) {
            const auto deadline = Clock::now() + std::chrono::microseconds{delay_us};
            while (Clock::now() < deadline) {
                _mm_pause();
            }
        }
        completed.store(i, std::memory_order_release);
        if (park) {
            progress.fetch_add(1, std::memory_order_release);
            progress.notify_one();
        }
    }
    waiter.join();
    const double producer_cpu = ThreadCpuMs() - producer_start;
    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    std::printf("%s,%u,%u,%.3f,%.3f,%.3f,%u\n", park ? "spin_park" : "spin_yield",
                iterations, delay_us, elapsed, waiter_cpu, producer_cpu, blocking_path);
}

int main() {
    std::puts("policy,iterations,delay_us,wall_ms,waiter_cpu_ms,producer_cpu_ms,blocking_path");
    for (unsigned trial = 0; trial < 5; ++trial) {
        for (unsigned delay : {0u, 2u, 5u, 10u, 20u, 1000u}) {
            const unsigned iterations = delay == 0 ? 20000 : delay < 1000 ? 5000 : 64;
            Probe(trial % 2 != 0, iterations, delay);
            Probe(trial % 2 == 0, iterations, delay);
        }
    }
}
