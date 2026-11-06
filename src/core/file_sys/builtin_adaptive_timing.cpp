// SPDX-FileCopyrightText: Copyright 2026 Lumaquince contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include "core/file_sys/builtin_adaptive_timing.h"

namespace FileSys::BuiltinAdaptiveTiming {
namespace {

constexpr std::size_t TextEnd = 0x3C461D0;
constexpr std::size_t RoStart = 0x3C47000;
constexpr std::size_t DataStart = 0x484D000;
constexpr std::size_t DataEnd = 0x4D46B80;
constexpr std::size_t ModOffset = 0x3C4705C;
constexpr std::size_t ActualBssEnd = 0x4E9A4C8;

// Shakoquo's timing delta math; Lumaquince's state-preserving wrapper.
// Original FDIV first, 96-byte aligned stack frame, x10-x17/q2/q3/FPSR/NZCV restored,
// LR untouched. 50 ms cap, 1/8 smoothing, 3% deadband. Shared guest state is experimental.
// Payload SHA256: 1f91fe67c4ffab93503b149b6a65b55e9dc32b467667a5c777d776ea8527bbf2
constexpr std::array<std::uint32_t, 44> Payload{
    0x1E201828, 0xA9BA2FEA, 0xA90137EC, 0xA9023FEE, 0xA90347F0, 0xAD020FE2,
    0xD53B442F, 0xD53B4210, 0xB00092CA, 0x9138014A, 0xD53BE02B, 0xF940014C,
    0xF900014B, 0xB40002CC, 0xCB0C016B, 0xD53BE00D, 0xD280028E, 0x9ACE09AE,
    0xEB0E017F, 0x9A8B81CB, 0xF940054C, 0xB500004C, 0xAA0B03EC, 0xCB0C016E,
    0x8B8E0D8C, 0xF900054C, 0x9E230182, 0x9E2301A3, 0x1E231842, 0x1E210842,
    0x1C0001A3, 0x1E230903, 0x1E232040, 0x5400004D, 0x1E204048, 0xD51B442F,
    0xD51B4210, 0xAD420FE2, 0xA94347F0, 0xA9423FEE, 0xA94137EC, 0xA8C62FEA,
    0x172A8288, 0x3F83D70A,
};
static_assert(Payload.size() * sizeof(std::uint32_t) == CaveSize);
static_assert(TextEnd <= CaveOffset && CaveOffset + CaveSize <= RoStart);
static_assert(ActualBssEnd < StateOffset && StateOffset + StateSize <= ImageSize);

std::uint32_t Read32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::uint32_t{bytes[offset]} | (std::uint32_t{bytes[offset + 1]} << 8) |
           (std::uint32_t{bytes[offset + 2]} << 16) | (std::uint32_t{bytes[offset + 3]} << 24);
}

void Write32(std::span<std::uint8_t> bytes, std::size_t offset, std::uint32_t word) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>(word >> (8 * i));
    }
}

bool IsZero(std::span<const std::uint8_t> bytes) {
    return std::ranges::all_of(bytes, [](std::uint8_t byte) { return byte == 0; });
}

} // namespace

std::string_view Apply(std::span<std::uint8_t> image, const Options& options) {
    if (!options.enabled) {
        return "disabled in Add-Ons";
    }
    if (options.title_id != TitleId || options.module != "main") {
        return "unsupported title or module";
    }
    if (options.has_program_arguments) {
        return "guest program arguments change the allocation layout";
    }
    if (options.has_external_code_patch) {
        return "an enabled external code patch takes precedence";
    }
    if (image.size() != HeaderSize + ImageSize) {
        return "unexpected decompressed image size";
    }
    if (Read32(image, 0) != 0x304F534E || Read32(image, 4) != 0 ||
        Read32(image, 8) != 0 || Read32(image, 12) != 0xFF) {
        return "unexpected NSO header";
    }
    if (!std::equal(BuildId.begin(), BuildId.end(), image.begin() + 0x40)) {
        return "unsupported build id";
    }
    // Check the exact reviewed layout, including executable page padding and the RW tail.
    // Container offsets/compressed lengths are not addresses in this decompressed representation.
    if (Read32(image, 0x14) != 0 || Read32(image, 0x18) != TextEnd ||
        Read32(image, 0x1C) != 0x100 || Read32(image, 0x24) != RoStart ||
        Read32(image, 0x28) != 0xC05080 || Read32(image, 0x2C) != 4 ||
        Read32(image, 0x34) != DataStart || Read32(image, 0x38) != 0x4F9B80 ||
        Read32(image, 0x3C) != ImageSize - DataEnd) {
        return "unexpected NSO segment layout";
    }
    const auto module = image.subspan(HeaderSize);
    if (Read32(module, 4) != ModOffset || Read32(module, ModOffset) != 0x30444F4D ||
        Read32(module, ModOffset + 4) != 0x4C7D7B0 - ModOffset ||
        Read32(module, ModOffset + 8) != DataEnd - ModOffset ||
        Read32(module, ModOffset + 12) != ActualBssEnd - ModOffset ||
        Read32(module, ModOffset + 16) != 0x3F864C8 - ModOffset ||
        Read32(module, ModOffset + 20) != 0x4169C34 - ModOffset ||
        Read32(module, ModOffset + 24) != 0x4D46B88 - ModOffset) {
        return "unexpected MOD0 layout or runtime object";
    }
    if (Read32(module, HookOffset) != 0x1E201828) {
        return "original hook instruction does not match";
    }
    if (!IsZero(module.subspan(TextEnd, RoStart - TextEnd))) {
        return "executable padding is occupied";
    }
    if (!IsZero(module.subspan(ActualBssEnd, ImageSize - ActualBssEnd))) {
        return "post-MOD0 allocation tail is occupied";
    }

    // All checks precede both writes. No allocation, logging or fallible operation between them.
    for (std::size_t i = 0; i < Payload.size(); ++i) {
        Write32(module, CaveOffset + i * 4, Payload[i]);
    }
    Write32(module, HookOffset, 0x14D57D4F); // B, not BL: preserve the caller's LR.
    return {};
}

} // namespace FileSys::BuiltinAdaptiveTiming
