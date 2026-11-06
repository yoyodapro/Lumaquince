// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <charconv>
#include <string_view>
#include <thread>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include "common/common_types.h"

namespace Common {

/// The producer publishes state before incrementing and notifying progress. The predicate must
/// acquire that state and must not consume notifications. Each progress counter has one waiter.
class BoundedSpinWait {
public:
    static constexpr u32 DefaultSpins = 16384;

    /// Startup tuning accepts only a complete decimal integer within the inherited spin bound.
    static u32 ConfiguredSpins(const char* value, u32 baseline) {
        if (value == nullptr) {
            return baseline;
        }
        const std::string_view text{value};
        u32 spins{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), spins);
        return error == std::errc{} && end == text.data() + text.size() && spins <= baseline
                   ? spins
                   : baseline;
    }

    template <typename Ready>
    static bool Spin(Ready&& ready, u32 spins = DefaultSpins) {
        if (ready()) {
            return true;
        }
        for (u32 i = 0; i < spins; ++i) {
            Pause();
            if (ready()) {
                return true;
            }
        }
        return false;
    }

    /// Returns whether the blocking path was needed, not whether the OS actually descheduled us.
    template <typename Ready>
    static bool Wait(std::atomic<u64>& progress, Ready&& ready, u32 spins = DefaultSpins) {
        if (Spin(ready, spins)) {
            return false;
        }
        for (;;) {
            // Snapshot before the predicate: a publication between the check and wait changes
            // the value, and atomic::wait then returns without missing the notification.
            const u64 seen = progress.load(std::memory_order_acquire);
            if (ready()) {
                return true;
            }
            progress.wait(seen, std::memory_order_acquire);
        }
    }

private:
    static void Pause() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
        _mm_pause();
#elif defined(_MSC_VER) && defined(_M_ARM64)
        __yield();
#elif defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
#elif defined(__aarch64__)
        asm volatile("yield");
#else
        std::this_thread::yield();
#endif
    }
};

} // namespace Common
