// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstring>
#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "video_core/texture_cache/descriptor_table.h"

namespace {
struct AddressMemory {
    const u8* GetPointer(GPUVAddr) const {
        return nullptr;
    }

    void ReadBlockUnsafe(GPUVAddr address, void* output, size_t size) const {
        std::memcpy(output, &address, size);
    }
};
} // namespace

TEST_CASE("Descriptor synchronization initializes the default address and limit",
          "[descriptor_table][memory]") {
    VideoCommon::DescriptorTable<u64> table;
    AddressMemory memory;
    REQUIRE(table.Synchronize(0, 0));
    REQUIRE(table.descriptors.size() == 1);
    REQUIRE(table.bound_ids.size() == 1);
    REQUIRE(table.read_descriptors.size() == 1);
    REQUIRE(table.LiveBoundIds().size() == 1);

    const auto [descriptor, changed] = table.Read(memory, 0);
    REQUIRE(descriptor == 0);
    REQUIRE(changed);
    table.bound_ids[0] = 42;

    const auto* storage = table.descriptors.data();
    REQUIRE_FALSE(table.Synchronize(0, 0));
    REQUIRE(table.descriptors.data() == storage);
    REQUIRE_FALSE(table.Read(memory, 0).second);
    REQUIRE(table.bound_ids[0] == 42);
}

TEST_CASE("Descriptor storage uses bounded geometric growth for inclusive limits",
          "[descriptor_table][memory]") {
    using Table = VideoCommon::DescriptorTable<u64>;
    constexpr std::array<u32, 13> limits{0,   1,   62,   63,     64,      127,    128,
                                       255, 256, 1024, 0x7fff, 0x7ffff, 0x80000};
    AddressMemory memory;
    for (const u32 limit : limits) {
        Table table;
        table.Refresh(0x1000, limit);
        const size_t live = size_t{limit} + 1;
        REQUIRE(table.descriptors.size() >= live);
        REQUIRE(table.descriptors.size() < live * 2);
        REQUIRE(table.bound_ids.size() == table.descriptors.size());
        REQUIRE(table.read_descriptors.size() == (table.descriptors.size() + 63) / 64);
        REQUIRE(table.LiveBoundIds().size() == live);
        REQUIRE(table.Read(memory, limit).first == 0x1000 + size_t{limit} * sizeof(u64));
        REQUIRE_FALSE(table.Read(memory, limit).second);
    }

    VideoCommon::DescriptorTable<std::array<u64, 4>> small;
    small.Refresh(0x1000, 255);
    REQUIRE(small.descriptors.size() == 256);
    REQUIRE(small.descriptors.capacity() * sizeof(std::array<u64, 4>) <= 16 * 1024);
    REQUIRE(small.bound_ids.size() == 256);
    REQUIRE(small.read_descriptors.size() == 4);
    REQUIRE(Table::CapacityForLimit(std::numeric_limits<u32>::max()) == 0x1'0000'0000ULL);
}

TEST_CASE("Descriptor storage retains allocations across limit oscillation",
          "[descriptor_table][memory]") {
    VideoCommon::DescriptorTable<u64> table;
    AddressMemory memory;
    table.Refresh(0x1000, 2047);
    const auto* descriptors = table.descriptors.data();
    const auto* bound_ids = table.bound_ids.data();
    const auto* read_descriptors = table.read_descriptors.data();
    const auto descriptor_capacity = table.descriptors.capacity();
    const auto id_capacity = table.bound_ids.capacity();
    const auto bitmap_capacity = table.read_descriptors.capacity();
    constexpr std::array<u32, 8> limits{0, 63, 64, 127, 128, 255, 1024, 2047};
    for (unsigned iteration = 0; iteration < 64; ++iteration) {
        for (const u32 limit : limits) {
            const GPUVAddr address = iteration % 2 ? 0x1000 : 0x2000;
            REQUIRE(table.Synchronize(address, limit));
            REQUIRE(table.LiveBoundIds().size() == size_t{limit} + 1);
            REQUIRE(table.Read(memory, limit).second);
            REQUIRE(table.bound_ids[limit] == 0);
            table.bound_ids[limit] = 42;
            REQUIRE_FALSE(table.Read(memory, limit).second);
            REQUIRE(table.bound_ids[limit] == 42);
            table.Invalidate();
            table.Invalidate();
            REQUIRE(table.bound_ids[limit] == 42);
            REQUIRE(table.Read(memory, limit).second);
            REQUIRE(table.bound_ids[limit] == 0);
            REQUIRE(table.descriptors.data() == descriptors);
            REQUIRE(table.bound_ids.data() == bound_ids);
            REQUIRE(table.read_descriptors.data() == read_descriptors);
            REQUIRE(table.descriptors.capacity() == descriptor_capacity);
            REQUIRE(table.bound_ids.capacity() == id_capacity);
            REQUIRE(table.read_descriptors.capacity() == bitmap_capacity);
            REQUIRE(table.descriptors.size() == 2048);
        }
    }
}

TEST_CASE("Descriptor invalidation clears live words and newly exposed entries",
          "[descriptor_table][memory]") {
    VideoCommon::DescriptorTable<u64> table;
    AddressMemory memory;
    table.Refresh(0x1000, 255);
    REQUIRE(table.Read(memory, 255).second);
    table.bound_ids[255] = 42;
    table.Refresh(0x1000, 63);
    REQUIRE(table.read_descriptors[3] != 0);
    REQUIRE(table.bound_ids[255] == 42);
    table.Invalidate();
    REQUIRE(table.Read(memory, 63).second);
    REQUIRE(table.read_descriptors[3] != 0);
    REQUIRE(table.bound_ids[255] == 42);
    REQUIRE(table.Synchronize(0x1000, 255));
    REQUIRE(table.read_descriptors[3] == 0);
    REQUIRE(table.bound_ids[255] == 0);
    REQUIRE(table.Read(memory, 255).second);
    REQUIRE_FALSE(table.Read(memory, 255).second);
}

TEST_CASE("Incremental descriptor growth needs logarithmically many allocations",
          "[descriptor_table][memory]") {
    VideoCommon::DescriptorTable<u64> table;
    size_t descriptor_allocations{};
    size_t id_allocations{};
    size_t bitmap_allocations{};
    for (u32 limit = 0; limit < 4096; ++limit) {
        const auto descriptor_capacity = table.descriptors.capacity();
        const auto id_capacity = table.bound_ids.capacity();
        const auto bitmap_capacity = table.read_descriptors.capacity();
        table.Refresh(0x1000, limit);
        descriptor_allocations += table.descriptors.capacity() != descriptor_capacity;
        id_allocations += table.bound_ids.capacity() != id_capacity;
        bitmap_allocations += table.read_descriptors.capacity() != bitmap_capacity;
        REQUIRE(table.descriptors.size() >= size_t{limit} + 1);
        REQUIRE(table.descriptors.size() < (size_t{limit} + 1) * 2);
    }
    REQUIRE(descriptor_allocations <= 13);
    REQUIRE(id_allocations <= 13);
    REQUIRE(bitmap_allocations <= 7);
}
