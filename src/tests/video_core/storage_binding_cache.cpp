// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <random>
#include <catch2/catch_test_macros.hpp>

#include "video_core/buffer_cache/storage_binding_cache.h"

namespace {

using VideoCommon::NumericStorageBinding;
using VideoCommon::StorageBindingCache;
using VideoCommon::StorageBindingPolicy;

struct DescriptorMemory {
    size_t owner{};
    u64 serial{};
    std::array<u64, 2> descriptor{0x400000, 0x4000};
    std::array<u32, 2> reads{};

    template <typename T>
    T Read(u64 address) {
        const size_t word = (address & 15) / 8;
        ++reads[word];
        return static_cast<T>(descriptor[word]);
    }
    size_t GetID() const { return owner; }
    u64 MapSerial() const { return serial; }
};

// Deliberately include every numeric dependency; real MemoryManager/BufferCache policies are
// exercised separately in draw_preparation.cpp. This oracle detects stale cache answers.
NumericStorageBinding Resolve(const StorageBindingCache::Key& key) {
    return {key.gpu_addr + key.owner * 0x1000 + key.serial * 0x10000,
            key.gpu_addr & ~(u64{key.alignment} - 1),
            static_cast<u32>(key.next_qword ^ (key.next_qword >> 32)) + key.flags + 1};
}

NumericStorageBinding Expected(const DescriptorMemory& memory, u32 alignment,
                               StorageBindingPolicy policy) {
    if (memory.descriptor[0] == 0) {
        return {};
    }
    return Resolve({memory.owner, memory.serial, memory.descriptor[0],
                    policy.ReadsSizeWord() ? memory.descriptor[1] : 0, alignment, policy.Flags()});
}

} // namespace

TEST_CASE("Storage binding memoization rereads guest words and keys every dependency",
          "[video_core][storage_binding_cache]") {
    StorageBindingCache cache;
    DescriptorMemory memory;
    StorageBindingPolicy policy;
    u32 resolutions{};
    const auto query = [&] {
        return cache.Get(memory, 0, 256, policy, [&](const auto& key) {
            ++resolutions;
            return Resolve(key);
        });
    };
    CHECK(query() == Expected(memory, 256, policy));
    CHECK(query() == Expected(memory, 256, policy));
    CHECK(resolutions == 1);
    CHECK(memory.reads == std::array<u32, 2>{2, 2});
    memory.descriptor[1] |= u64{1} << 32; // Same low size word, different descriptor interpretation.
    CHECK(query() == Expected(memory, 256, policy));
    CHECK(resolutions == 2);
    ++memory.serial; // Even an unrelated map operation invalidates numeric answers.
    CHECK(query() == Expected(memory, 256, policy));
    CHECK(resolutions == 3);
    ++memory.owner; // Replacement address space at the same address and serial.
    CHECK(query() == Expected(memory, 256, policy));
    CHECK(resolutions == 4);
    memory.descriptor[0] += 4096;
    CHECK(query() == Expected(memory, 256, policy));
    CHECK(resolutions == 5);
    memory.descriptor[0] = 0;
    CHECK(query() == NumericStorageBinding{});
    CHECK(resolutions == 5);
    CHECK(memory.reads == std::array<u32, 2>{7, 6});
}

TEST_CASE("Storage binding memoization preserves native mapping-size and table policies",
          "[video_core][storage_binding_cache]") {
    StorageBindingCache cache;
    DescriptorMemory memory;
    u32 resolutions{};
    for (u32 flags = 0; flags < 16; ++flags) {
        const StorageBindingPolicy policy{bool(flags & 1), bool(flags & 2), bool(flags & 4),
                                          bool(flags & 8)};
        const bool reads_size = !((flags & 4) && !(flags & 1)) &&
                                !((flags & 2) && (flags & 8));
        CHECK(policy.ReadsSizeWord() == reads_size);
        CHECK(policy.Flags() == flags);
        for (u32 alignment : {16U, 256U, 4096U}) {
            memory.reads = {};
            const auto before = resolutions;
            for (int i = 0; i < 2; ++i) {
                CHECK(cache.Get(memory, 0, alignment, policy, [&](const auto& key) {
                    ++resolutions;
                    return Resolve(key);
                }) == Expected(memory, alignment, policy));
            }
            CHECK(resolutions == before + 1);
            CHECK(memory.reads[0] == 2);
            CHECK(memory.reads[1] == (reads_size ? 2U : 0U));
        }
    }
}

TEST_CASE("Storage binding cache has independent ownership and bounded replacement",
          "[video_core][storage_binding_cache]") {
    StorageBindingCache cache;
    StorageBindingCache other;
    DescriptorMemory memory;
    u32 resolutions{};
    auto resolve = [&](const auto& key) { ++resolutions; return Resolve(key); };
    const auto first = cache.Get(memory, 0, 256, {}, resolve);
    CHECK(cache.Get(memory, 0, 256, {}, resolve) == first);
    CHECK(resolutions == 1);
    CHECK(other.Get(memory, 0, 256, {}, resolve) == first);
    CHECK(resolutions == 2);

    std::mt19937 random{0x308b7};
    for (u32 i = 0; i < 25000; ++i) {
        memory.owner = random() % 4;
        memory.serial = i / 100;
        memory.descriptor[0] = (random() % 8192) * 4096ULL;
        memory.descriptor[1] = (random() % 8) * 65536ULL;
        const StorageBindingPolicy policy{bool(random() & 1), bool(random() & 1),
                                          bool(random() & 1), bool(random() & 1)};
        const u32 alignment = 1U << (4 + random() % 9);
        for (int repeat = 0; repeat < 2; ++repeat) {
            CHECK(cache.Get(memory, 0, alignment, policy, resolve) ==
                  Expected(memory, alignment, policy));
        }
    }
}
