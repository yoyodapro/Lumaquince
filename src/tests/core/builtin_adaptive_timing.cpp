// SPDX-FileCopyrightText: Copyright 2026 Lumaquince contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdint>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "core/file_sys/builtin_adaptive_timing.h"
#include "adaptive_timing_fixture.h"

namespace {
namespace Timing = FileSys::BuiltinAdaptiveTiming;

using AdaptiveTimingTest::Fixture;
using AdaptiveTimingTest::Get32;
using AdaptiveTimingTest::Put32;

Timing::Options Options() {
    return {.title_id = Timing::TitleId, .module = "main"};
}

bool EqualRange(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b,
                std::size_t begin, std::size_t end) {
    return std::equal(a.begin() + begin, a.begin() + end, b.begin() + begin);
}
} // namespace

TEST_CASE("Adaptive timing changes exactly two reviewed ranges", "[patch_manager][adaptive_timing]") {
    auto image = Fixture();
    const auto before = image;
    REQUIRE(Timing::Apply(image, Options()).empty());
    constexpr auto hook = Timing::HeaderSize + Timing::HookOffset;
    constexpr auto cave = Timing::HeaderSize + Timing::CaveOffset;
    CHECK(EqualRange(image, before, 0, hook));
    CHECK(EqualRange(image, before, hook + 4, cave));
    CHECK(EqualRange(image, before, cave + Timing::CaveSize, image.size()));
    CHECK(Get32(image, hook) == 0x14D57D4F); // Non-linking B to the cave.
    CHECK(Get32(image, cave) == 0x1E201828); // Original operation is first.
    CHECK(Get32(image, cave + 4) == 0xA9BA2FEA); // Allocate/save 96-byte frame.
    CHECK(Get32(image, cave + 0xA4) == 0xA8C62FEA); // Restore/deallocate the frame.
    CHECK(Get32(image, cave + 0xA8) == 0x172A8288); // B to hook + 4, LR untouched.
    CHECK(Get32(image, cave + 0xAC) == 0x3F83D70A); // 1.03f deadband literal.
    CHECK(Get32(image, Timing::HeaderSize + Timing::StateOffset) == 0);

    const auto already_patched = image;
    CHECK_FALSE(Timing::Apply(image, Options()).empty());
    CHECK(image == already_patched);
}

TEST_CASE("Adaptive timing refuses incompatible input atomically",
          "[patch_manager][adaptive_timing]") {
    auto image = Fixture();
    auto options = Options();
    SECTION("Disabled Add-On") { options.enabled = false; }
    SECTION("Other title") { ++options.title_id; }
    SECTION("Other module") { options.module = "subsdk0"; }
    SECTION("Program arguments") { options.has_program_arguments = true; }
    SECTION("External patch precedence, including overlaps") { options.has_external_code_patch = true; }
    SECTION("Truncated header") { image.resize(0xFF); }
    SECTION("Truncated state tail") { image.resize(0x100 + Timing::StateOffset + 8); }
    SECTION("Unexpected enlarged allocation") { image.push_back(0); }
    SECTION("Wrong magic") { image[0] ^= 1; }
    SECTION("Wrong version") { image[4] = 1; }
    SECTION("Reserved header field") { image[8] = 1; }
    SECTION("Wrong header flags") { image[12] = 0; }
    SECTION("Wrong build id prefix") { image[0x40] ^= 1; }
    SECTION("Wrong build id zero suffix") { image[0x5F] = 1; }
    SECTION("Moved text") { Put32(image, 0x14, 0x1000); }
    SECTION("Larger initialized text") { Put32(image, 0x18, Timing::CaveOffset + 4); }
    SECTION("Wrong text alignment") { Put32(image, 0x1C, 0x1000); }
    SECTION("Moved rodata") { Put32(image, 0x24, Timing::CaveOffset); }
    SECTION("Changed rodata size") { image[0x28] ^= 1; }
    SECTION("Changed rodata fourth word") { image[0x2C] ^= 1; }
    SECTION("Moved data") { image[0x34] ^= 1; }
    SECTION("Changed initialized data") { image[0x38] ^= 1; }
    SECTION("Changed BSS allocation") { image[0x3C] ^= 1; }
    SECTION("Bad MOD0 pointer") { Put32(image, 0x104, 0xFFFFFFFF); }
    SECTION("Bad MOD0 magic") { image[0x100 + 0x3C4705C] ^= 1; }
    SECTION("Changed dynamic section") { image[0x100 + 0x3C4705C + 4] ^= 1; }
    SECTION("Changed MOD0 BSS start") { image[0x100 + 0x3C4705C + 8] ^= 1; }
    SECTION("BSS overlaps timing state") {
        Put32(image, 0x100 + 0x3C4705C + 12, Timing::StateOffset + 16 - 0x3C4705C);
    }
    SECTION("Changed unwind extent") { image[0x100 + 0x3C4705C + 16] ^= 1; }
    SECTION("Runtime object overlaps timing state") {
        Put32(image, 0x100 + 0x3C4705C + 24, Timing::StateOffset - 0x3C4705C);
    }
    SECTION("Changed hook") { image[Timing::HeaderSize + Timing::HookOffset] ^= 1; }
    SECTION("Occupied cave first byte") { image[Timing::HeaderSize + Timing::CaveOffset] = 1; }
    SECTION("Occupied cave last byte") {
        image[Timing::HeaderSize + Timing::CaveOffset + Timing::CaveSize - 1] = 1;
    }
    SECTION("Other executable padding occupied") { image[0x100 + 0x3C46FFF] = 1; }
    SECTION("Occupied state first byte") { image[Timing::HeaderSize + Timing::StateOffset] = 1; }
    SECTION("Occupied state last byte") { image[Timing::HeaderSize + Timing::StateOffset + 15] = 1; }
    SECTION("Other post-MOD0 tail occupied") { image.back() = 1; }
    const auto before = image;
    CHECK_FALSE(Timing::Apply(image, options).empty());
    CHECK(image == before);
}
