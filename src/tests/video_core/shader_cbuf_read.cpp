// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstring>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "video_core/shader_cbuf_read.h"

namespace {

using VideoCommon::ReadShaderCbuf;
using VideoCommon::ShaderCbufBinding;
using VideoCommon::ShaderCbufReadStatus;

struct CbufMemory {
    static constexpr GPUVAddr Base = 0x1000;
    std::array<u8, 32> cpu{};
    std::array<u8, 32> coherent{};
    std::array<bool, 32> mapped{};
    std::vector<char> calls;

    CbufMemory() {
        mapped.fill(true);
    }

    bool IsFullyMappedRange(GPUVAddr address, size_t size) {
        calls.push_back('M');
        if (address < Base || address - Base > mapped.size() ||
            size > mapped.size() - (address - Base)) {
            return false;
        }
        for (size_t i = 0; i < size; ++i) {
            if (!mapped[address - Base + i]) {
                return false;
            }
        }
        return true;
    }

    void ReadBlockUnsafe(GPUVAddr address, void* value, size_t size) {
        calls.push_back('R');
        std::memcpy(value, cpu.data() + address - Base, size);
    }

    void ReadBlock(GPUVAddr address, void* value, size_t size) {
        calls.push_back('C');
        // Model flushing the GPU-owned range to guest memory before reading it.
        std::memcpy(cpu.data() + address - Base, coherent.data() + address - Base, size);
        std::memcpy(value, cpu.data() + address - Base, size);
    }

    void SetWord(size_t offset, u32 raw, u32 fresh) {
        std::memcpy(cpu.data() + offset, &raw, sizeof(raw));
        std::memcpy(coherent.data() + offset, &fresh, sizeof(fresh));
    }
};

} // namespace

TEST_CASE("Shader CBuf metadata observes coherent memory", "[shader_cbuf_read]") {
    CbufMemory memory;
    memory.SetWord(4, 0xdeadbeef, 0x12345678);
    const auto resolve = [](u32) { return ShaderCbufBinding{CbufMemory::Base, 32, true}; };

    SECTION("Normal translation does not sample raw memory") {
        const auto read = ReadShaderCbuf(memory, 8, resolve, 2, 4);
        CHECK(read.status == ShaderCbufReadStatus::Valid);
        CHECK(read.value == 0x12345678);
        CHECK_FALSE(read.raw_value);
        CHECK(memory.calls == std::vector<char>{'M', 'C'});
    }
    SECTION("Trace retains the raw value before coherence flushes it") {
        const auto read = ReadShaderCbuf(memory, 8, resolve, 2, 4, true);
        CHECK(read.status == ShaderCbufReadStatus::Valid);
        CHECK(read.value == 0x12345678);
        REQUIRE(read.raw_value);
        CHECK(*read.raw_value == 0xdeadbeef);
        CHECK(memory.calls == std::vector<char>{'M', 'R', 'C'});
    }
    SECTION("Earlier valid samples do not exhaust mismatch sampling") {
        for (u32 i = 0; i < 64; ++i) {
            const auto read = ReadShaderCbuf(memory, 8, resolve, 2, 0, true);
            REQUIRE(read.raw_value == read.value);
        }
        const auto read = ReadShaderCbuf(memory, 8, resolve, 2, 4, true);
        CHECK(read.raw_value == 0xdeadbeef);
        CHECK(read.value == 0x12345678);
    }
}

TEST_CASE("Shader CBuf validates the bank before resolving it", "[shader_cbuf_read]") {
    CbufMemory memory;
    u32 resolutions{};
    const auto resolve = [&](u32 bank) {
        ++resolutions;
        // This models the compute enable mask: an invalid bank must never reach the shift.
        return ShaderCbufBinding{CbufMemory::Base, 32, ((0xffU >> bank) & 1) != 0};
    };
    for (u32 bank : {8U, 31U, 32U, (std::numeric_limits<u32>::max)()}) {
        const auto read = ReadShaderCbuf(memory, 8, resolve, bank, 0, true);
        CHECK(read.status == ShaderCbufReadStatus::InvalidBank);
        CHECK(read.value == 0);
        CHECK_FALSE(read.raw_value);
    }
    CHECK(resolutions == 0);
    CHECK(memory.calls.empty());
}

TEST_CASE("Shader CBuf requires an enabled complete word", "[shader_cbuf_read]") {
    CbufMemory memory;
    ShaderCbufBinding binding{CbufMemory::Base, 16, true};
    u32 offset = 12;
    auto expected = ShaderCbufReadStatus::OutOfBounds;
    SECTION("Last complete word") {
        expected = ShaderCbufReadStatus::Valid;
    }
    SECTION("One byte of the word is outside the buffer") { offset = 13; }
    SECTION("Offset equals size") { offset = 16; }
    SECTION("Offset exceeds size") { offset = 17; }
    SECTION("Offset cannot wrap past the size check") {
        offset = (std::numeric_limits<u32>::max)();
    }
    SECTION("Buffer shorter than one word") {
        offset = 0;
        binding.size = 3;
    }
    SECTION("Disabled buffer") {
        binding.enabled = false;
        expected = ShaderCbufReadStatus::Disabled;
    }
    const auto read = ReadShaderCbuf(memory, 8, [&](u32) { return binding; }, 0, offset, true);
    CHECK(read.status == expected);
    CHECK(read.value == 0);
    if (expected == ShaderCbufReadStatus::Valid) {
        CHECK(read.raw_value == 0U);
        CHECK(memory.calls == std::vector<char>{'M', 'R', 'C'});
    } else {
        CHECK_FALSE(read.raw_value);
        CHECK(memory.calls.empty());
    }
}

TEST_CASE("Shader CBuf checks address and exclusive end overflow", "[shader_cbuf_read]") {
    CbufMemory memory;
    constexpr auto max_address = (std::numeric_limits<GPUVAddr>::max)();
    ShaderCbufBinding binding{max_address - 1, 32, true};
    u32 offset{};
    SECTION("Address plus offset overflows") { offset = 4; }
    SECTION("Word end overflows") { offset = 0; }
    SECTION("Unaligned final word has overflowing exclusive end") {
        binding.address = max_address - 3;
    }
    const auto read = ReadShaderCbuf(memory, 8, [&](u32) { return binding; }, 0, offset, true);
    CHECK(read.status == ShaderCbufReadStatus::AddressOverflow);
    CHECK(read.value == 0);
    CHECK_FALSE(read.raw_value);
    CHECK(memory.calls.empty());
}

TEST_CASE("Shader CBuf requires all four bytes mapped and preserves unaligned words",
          "[shader_cbuf_read]") {
    CbufMemory memory;
    // Cross an artificial mapping boundary at byte 16, with distinct byte values.
    memory.SetWord(15, 0x11223344, 0x55667788);
    const auto resolve = [](u32) { return ShaderCbufBinding{CbufMemory::Base, 32, true}; };
    auto expected = ShaderCbufReadStatus::Valid;
    SECTION("Unaligned mapped word across boundary") {}
    SECTION("Mapped start but unmapped final byte") {
        memory.mapped[18] = false;
        expected = ShaderCbufReadStatus::Unmapped;
    }
    SECTION("Unmapped starting byte") {
        memory.mapped[15] = false;
        expected = ShaderCbufReadStatus::Unmapped;
    }
    const auto read = ReadShaderCbuf(memory, 8, resolve, 1, 15, true);
    CHECK(read.status == expected);
    if (expected == ShaderCbufReadStatus::Valid) {
        CHECK(read.value == 0x55667788);
        CHECK(read.raw_value == 0x11223344U);
        CHECK(memory.calls == std::vector<char>{'M', 'R', 'C'});
    } else {
        CHECK(read.value == 0);
        CHECK_FALSE(read.raw_value);
        CHECK(memory.calls == std::vector<char>{'M'});
    }
}
