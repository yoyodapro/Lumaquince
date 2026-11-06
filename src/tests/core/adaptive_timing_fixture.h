// SPDX-FileCopyrightText: Copyright 2026 Lumaquince contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include "core/file_sys/builtin_adaptive_timing.h"

namespace AdaptiveTimingTest {
namespace Timing = FileSys::BuiltinAdaptiveTiming;

inline void Put32(std::vector<std::uint8_t>& image, std::size_t offset, std::uint32_t word) {
    for (std::size_t i = 0; i < 4; ++i) {
        image[offset + i] = static_cast<std::uint8_t>(word >> (i * 8));
    }
}

inline std::uint32_t Get32(const std::vector<std::uint8_t>& image, std::size_t offset) {
    std::uint32_t word{};
    for (std::size_t i = 0; i < 4; ++i) {
        word |= std::uint32_t{image[offset + i]} << (i * 8);
    }
    return word;
}

// Synthetic zero memory and minimal metadata; no extracted game data.
inline std::vector<std::uint8_t> Fixture() {
    std::vector<std::uint8_t> image(Timing::HeaderSize + Timing::ImageSize);
    Put32(image, 0, 0x304F534E);
    Put32(image, 0xC, 0xFF);
    Put32(image, 0x18, 0x3C461D0);
    Put32(image, 0x1C, 0x100);
    Put32(image, 0x24, 0x3C47000);
    Put32(image, 0x28, 0xC05080);
    Put32(image, 0x2C, 4);
    Put32(image, 0x34, 0x484D000);
    Put32(image, 0x38, 0x4F9B80);
    Put32(image, 0x3C, 0x159480);
    std::copy(Timing::BuildId.begin(), Timing::BuildId.end(), image.begin() + 0x40);
    constexpr std::size_t mod = 0x3C4705C;
    Put32(image, 0x100 + 4, mod);
    Put32(image, 0x100 + mod, 0x30444F4D);
    Put32(image, 0x100 + mod + 4, 0x4C7D7B0 - mod);
    Put32(image, 0x100 + mod + 8, 0x4D46B80 - mod);
    Put32(image, 0x100 + mod + 12, 0x4E9A4C8 - mod);
    Put32(image, 0x100 + mod + 16, 0x3F864C8 - mod);
    Put32(image, 0x100 + mod + 20, 0x4169C34 - mod);
    Put32(image, 0x100 + mod + 24, 0x4D46B88 - mod);
    Put32(image, Timing::HeaderSize + Timing::HookOffset, 0x1E201828);
    image[Timing::HeaderSize + Timing::HookOffset - 1] = 0xA5;
    image[Timing::HeaderSize + Timing::HookOffset + 4] = 0x5A;
    image[0x100 + 0x3C461CF] = 0xC3;
    image[0x100 + 0x3C47000] = 0x3C;
    image[0x100 + 0x4E9A4C7] = 0x7E;
    return image;
}

} // namespace AdaptiveTimingTest
