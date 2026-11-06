// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <limits>
#include <optional>
#include <string_view>

#include "common/common_types.h"

namespace VideoCommon {

enum class ShaderCbufReadStatus {
    Valid,
    InvalidBank,
    Disabled,
    OutOfBounds,
    AddressOverflow,
    Unmapped,
};

constexpr std::string_view ShaderCbufReadStatusName(ShaderCbufReadStatus status) {
    switch (status) {
    case ShaderCbufReadStatus::Valid: return "valid";
    case ShaderCbufReadStatus::InvalidBank: return "invalid-bank";
    case ShaderCbufReadStatus::Disabled: return "disabled";
    case ShaderCbufReadStatus::OutOfBounds: return "out-of-bounds";
    case ShaderCbufReadStatus::AddressOverflow: return "address-overflow";
    case ShaderCbufReadStatus::Unmapped: return "unmapped";
    }
    return "unknown";
}

struct ShaderCbufBinding {
    GPUVAddr address;
    u32 size;
    bool enabled;
};

struct ShaderCbufRead {
    u32 bank{};
    u32 offset{};
    GPUVAddr buffer_address{};
    GPUVAddr address{};
    u32 size{};
    u32 value{};
    std::optional<u32> raw_value;
    ShaderCbufReadStatus status{ShaderCbufReadStatus::InvalidBank};
};

// Resolve only after validating the bank: compute bindings also use the bank in a bit shift.
// ReadBlock reconciles GPU writes before a value becomes shader translation metadata.
template <typename Memory, typename ResolveBinding>
ShaderCbufRead ReadShaderCbuf(Memory& memory, size_t bank_count, ResolveBinding&& resolve,
                             u32 bank, u32 offset, bool trace_raw = false) {
    ShaderCbufRead result{.bank = bank, .offset = offset};
    if (bank >= bank_count) {
        return result;
    }
    const ShaderCbufBinding binding = resolve(bank);
    result.buffer_address = binding.address;
    result.size = binding.size;
    if (!binding.enabled) {
        result.status = ShaderCbufReadStatus::Disabled;
        return result;
    }
    if (offset > binding.size || binding.size - offset < sizeof(u32)) {
        result.status = ShaderCbufReadStatus::OutOfBounds;
        return result;
    }
    constexpr auto max_address = (std::numeric_limits<GPUVAddr>::max)();
    if (offset > max_address - binding.address) {
        result.status = ShaderCbufReadStatus::AddressOverflow;
        return result;
    }
    result.address = binding.address + offset;
    if (sizeof(u32) > max_address - result.address) {
        result.status = ShaderCbufReadStatus::AddressOverflow;
        return result;
    }
    if (!memory.IsFullyMappedRange(result.address, sizeof(u32))) {
        result.status = ShaderCbufReadStatus::Unmapped;
        return result;
    }
    if (trace_raw) {
        u32 raw{};
        // Unlike a scalar pointer read, this also supports unaligned words crossing GPU pages.
        memory.ReadBlockUnsafe(result.address, &raw, sizeof(raw));
        result.raw_value = raw;
    }
    memory.ReadBlock(result.address, &result.value, sizeof(result.value));
    result.status = ShaderCbufReadStatus::Valid;
    return result;
}

} // namespace VideoCommon
