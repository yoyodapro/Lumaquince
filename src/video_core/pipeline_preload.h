// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <istream>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "common/common_types.h"

namespace VideoCommon {

inline constexpr u32 UsedPipelinesMagic = 0x31505545; // "EUP1"

// Keep already compiled pipelines resident while the driver reports room for them. Allocator
// slack also occupies the heap, so reserve it before deciding whether a cache trim is needed.
// Unknown budgets retain the conservative age-based policy. Subtraction avoids overflowing on
// corrupt or transient driver values.
inline constexpr size_t MaxWarmGraphicsPipelines = 16384;

inline bool CanRetainUnusedPipelines(u64 usage, u64 budget, u64 slack, size_t resident_count) {
    constexpr u64 MaxReserve = 512ULL * 1024 * 1024;
    // Driver pipeline objects also retain host allocations which the VRAM budget omits.
    // Above this count resume age-based trimming even if the GPU still has headroom.
    if (resident_count > MaxWarmGraphicsPipelines || budget == 0 || usage >= budget) {
        return false;
    }
    const u64 available = budget - usage;
    if (slack >= available) {
        return false;
    }
    const u64 reserve = (std::min)(budget / 8, MaxReserve);
    return available - slack > reserve;
}

// This optional index must never allocate from an unchecked file-supplied count. Reject an
// incomplete index as a whole so startup falls back to the ordinary bounded preload policy.
inline std::unordered_map<u64, u8> ReadUsedPipelineIndex(std::istream& file, u8 max_age) {
    u32 magic{}, count{};
    if (!file.read(reinterpret_cast<char*>(&magic), sizeof(magic)) || magic != UsedPipelinesMagic ||
        !file.read(reinterpret_cast<char*>(&count), sizeof(count))) {
        return {};
    }
    const auto start = file.tellg();
    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    constexpr size_t RecordBytes = sizeof(u64) + sizeof(u8);
    if (start < std::streampos{0} || end < start ||
        static_cast<u64>(end - start) / RecordBytes < count) {
        return {};
    }
    file.seekg(start);
    std::unordered_map<u64, u8> used;
    for (u32 i = 0; i < count; ++i) {
        u64 hash{};
        u8 age{};
        if (!file.read(reinterpret_cast<char*>(&hash), sizeof(hash)) ||
            !file.read(reinterpret_cast<char*>(&age), sizeof(age))) {
            return {};
        }
        if (age < max_age) {
            used.emplace(hash, age);
        }
    }
    return used;
}

// Owned by the preload reader, never its workers. Retain full keys so hash collisions cannot
// discard a distinct pipeline, and count only unique work against the boot build budget.
template <typename Key, typename Hash = std::hash<Key>>
class PipelinePreloadQueue {
public:
    explicit PipelinePreloadQueue(size_t limit_ = std::numeric_limits<size_t>::max()) : limit{limit_} {}

    bool TryQueue(const Key& key) {
        if (keys.size() >= limit) {
            return false;
        }
        return keys.insert(key).second;
    }

    bool Contains(const Key& key) const {
        return keys.contains(key);
    }

    size_t Size() const {
        return keys.size();
    }

private:
    size_t limit;
    std::unordered_set<Key, Hash> keys;
};

} // namespace VideoCommon
