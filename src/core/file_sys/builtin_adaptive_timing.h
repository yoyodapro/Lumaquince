// SPDX-FileCopyrightText: Copyright 2026 Lumaquince contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace FileSys::BuiltinAdaptiveTiming {

inline constexpr std::string_view Name = "Adaptive Timing (Experimental)";
inline constexpr std::uint64_t TitleId = 0x0400486024DEC000;
inline constexpr std::array<std::uint8_t, 32> BuildId{
    0xE0, 0xF3, 0xCC, 0xFA, 0xF5, 0x00, 0x65, 0xAE, 0x4E, 0xA0,
    0x1E, 0xB5, 0xAE, 0xB1, 0x3B, 0x1A, 0x45, 0xDA, 0x33, 0x48,
};
inline constexpr std::size_t HeaderSize = 0x100;
inline constexpr std::size_t ImageSize = 0x4EA0000;
inline constexpr std::size_t HookOffset = 0x6E6CC4;
inline constexpr std::size_t CaveOffset = 0x3C46200;
inline constexpr std::size_t StateOffset = 0x4E9FE00;
inline constexpr std::size_t CaveSize = 176;
inline constexpr std::size_t StateSize = 16;

struct Options {
    std::uint64_t title_id;
    std::string_view module;
    bool enabled = true;
    bool has_program_arguments = false;
    bool has_external_code_patch = false;
};

// Accepts the loader's original NSO header followed by decompressed module memory.
// Returns an empty string only on success. Every refusal leaves the entire image unchanged.
// On success only the hook and cave change; state starts zero and is written by guest code.
[[nodiscard]] std::string_view Apply(std::span<std::uint8_t> image, const Options& options);

} // namespace FileSys::BuiltinAdaptiveTiming
