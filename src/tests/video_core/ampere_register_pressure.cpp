// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
constexpr Instruction Exit{{0x794d, 0, 0x03800000, 0}};
constexpr Instruction UniformMarker{{0x0000fc02, 0, 0, 0}};

void SetBits(Instruction& instruction, u32 bit, u32 width, u32 value) {
    const u32 mask = ((1U << width) - 1) << (bit % 32);
    instruction.words[bit / 32] =
        (instruction.words[bit / 32] & ~mask) | ((value << (bit % 32)) & mask);
}

using Banks = std::array<std::array<u32, 256>, 3>;
Banks InitialBanks() {
    Banks banks;
    for (u32 bank = 0; bank < banks.size(); ++bank) {
        for (u32 reg = 0; reg < banks[bank].size(); ++reg) {
            banks[bank][reg] = 0x10000U * (bank + 1) + reg;
        }
    }
    return banks;
}

void CopyRegisters(Banks& banks, std::span<const u64> words) {
    for (const u64 word : words) {
        REQUIRE(((word >> 44) & 15) == 10);
        banks.at((word >> 8) & 3).at(word & 255) =
            banks.at((word >> 10) & 3).at((word >> 20) & 255);
    }
}
} // namespace

TEST_CASE("TXD register shadows preserve coordinates gradients results and borrowed registers",
          "[video_core][ampere][register_pressure]") {
    // Captured Star Fox TXD.B and Fast Fusion TXD, with register allocation varied independently.
    for (const Instruction captured : {Instruction{{0x14127f6c, 0xa0000410, 0x081e0f10, 0x001f4200}},
                                       Instruction{{0x10047b6c, 0x20800c04, 0x041e0707, 0x00014800}}}) {
        for (const u32 dimension : {1U, 5U}) {
            for (u32 mask = 1; mask < 16; ++mask) {
                for (const u32 high_operand : {0U, 16U, 24U, 32U, 64U}) {
                    auto instruction = captured;
                    const u32 channels = static_cast<u32>(std::popcount(mask));
                    SetBits(instruction, 61, 3, dimension);
                    SetBits(instruction, 72, 4, mask);
                    SetBits(instruction, 16, 8, high_operand == 0 || high_operand == 16 ? 240 : 4);
                    SetBits(instruction, 24, 8, high_operand == 0 ? 240 : high_operand == 24 ? 220 : 20);
                    SetBits(instruction, 32, 8, high_operand == 0 ? 240 : high_operand == 32 ? 232 : 16);
                    SetBits(instruction, 64, 8, channels <= 2 ? 255 : high_operand == 0 ? 242 : high_operand == 64 ? 244 : 8);
                    const auto pressure = Shader::Ampere::PrepareRegisterPressure(instruction, 216);
                    REQUIRE(pressure.valid);
                    const auto lowered = LowerToMaxwell(std::array{UniformMarker, instruction, Exit});
                    INFO(lowered.error);
                    REQUIRE(lowered);
                    const auto begin = lowered.instructions.begin() + lowered.source_instruction_offsets[1];
                    const auto end = lowered.instructions.begin() + lowered.source_instruction_offsets[2];
                    REQUIRE(static_cast<size_t>(end - begin) > pressure.prefix.size() + pressure.suffix.size());
                    CHECK(std::equal(pressure.prefix.begin(), pressure.prefix.end(), begin));
                    CHECK(std::equal(pressure.suffix.begin(), pressure.suffix.end(), end - pressure.suffix.size()));
                    const auto sample = std::find_if(begin, end, [](u64 word) { return (word >> 56) == 0xde; });
                    REQUIRE(sample != end);
                    CHECK(((*sample >> 20) & 255) == pressure.instruction.Bits(32, 8));
                    CHECK(((*sample >> 28) & 7) == (dimension == 5 ? 3U : 2U));
                    CHECK(((*sample >> 31) & 15) == mask);
                    if (instruction.Bits(0, 12) == 0xb6c) {
                        CHECK(((*sample >> 8) & 255) == pressure.instruction.Bits(24, 8));
                    } else {
                        for (u32 component = 0; component < (dimension == 5 ? 3U : 2U); ++component) {
                            const u64 copy = 0x5c98000000070000ULL | (249 + component) |
                                             (15ULL << 39) |
                                             (u64{pressure.instruction.Bits(24, 8) + component} << 20);
                            CHECK(std::find(begin, sample, copy) != sample);
                        }
                    }
                    for (const bool taken : {false, true}) {
                        auto banks = InitialBanks();
                        auto expected = banks;
                        CopyRegisters(banks, pressure.prefix);
                        for (const auto [bit, count] :
                             {std::pair{24U, dimension == 5 ? 3U : 2U}, std::pair{32U, 4U}}) {
                            const u32 source = instruction.Bits(bit, 8);
                            const u32 mapped = pressure.instruction.Bits(bit, 8);
                            for (u32 component = 0; component < count; ++component) {
                                CHECK(banks[0][mapped + component] ==
                                      expected[source >= 216 ? 1 : 0][source + component]);
                            }
                        }
                        if (taken) {
                            for (u32 channel = 0; channel < channels; ++channel) {
                                const u32 bit = channel < 2 ? 16 : 64;
                                const u32 component = channel < 2 ? channel : channel - 2;
                                const u32 destination = instruction.Bits(bit, 8) + component;
                                banks[0][pressure.instruction.Bits(bit, 8) + component] = 0xabcd + channel;
                                expected[destination >= 216 ? 1 : 0][destination] = 0xabcd + channel;
                            }
                        }
                        CopyRegisters(banks, pressure.suffix);
                        CHECK(banks[0] == expected[0]);
                        CHECK(banks[1] == expected[1]);
                    }
                }
            }
        }
    }
}

TEST_CASE("TXD shadow spans respect array layers partial results and reserved boundaries",
          "[video_core][ampere][register_pressure]") {
    Instruction instruction{{0x14127f6c, 0xa00004e8, 0x081e0710, 0x001f4200}};
    // Third channel alone can use R254; no fourth result may be inferred at R255.
    SetBits(instruction, 64, 8, 254);
    REQUIRE(Shader::Ampere::PrepareRegisterPressure(instruction, 216).valid);
    REQUIRE(LowerToMaxwell(std::array{instruction, Exit}));
    SetBits(instruction, 72, 4, 15);
    CHECK_FALSE(Shader::Ampere::PrepareRegisterPressure(instruction, 216).valid);
    SetBits(instruction, 72, 4, 7);
    SetBits(instruction, 64, 8, 16);
    for (const u32 coordinate : {214U, 222U, 254U}) {
        SetBits(instruction, 24, 8, coordinate);
        CHECK_FALSE(Shader::Ampere::PrepareRegisterPressure(instruction, 216).valid);
        CHECK_FALSE(LowerToMaxwell(std::array{instruction, Exit}));
    }
    SetBits(instruction, 24, 8, 20);
    for (const u32 gradient : {213U, 222U, 254U}) {
        SetBits(instruction, 32, 8, gradient);
        CHECK_FALSE(Shader::Ampere::PrepareRegisterPressure(instruction, 216).valid);
        CHECK_FALSE(LowerToMaxwell(std::array{instruction, Exit}));
    }
}

TEST_CASE("ATOMG RED and SURED high data operands reach emitted instructions through shadows",
          "[video_core][ampere][register_pressure]") {
    const std::array instructions{
        Instruction{{0xff0479a8, 0x000000f0, 0x081ee140, 0}},
        Instruction{{0x0400798e, 0x000000f0, 0x0010e100, 0}},
        Instruction{{0x0500759f, 0x20c002f0, 0x0010e000, 0x000fe200}},
    };
    for (auto instruction : instructions) {
        for (const u32 high : {216U, 240U, 254U}) {
            SetBits(instruction, 32, 8, high);
            const auto lowered = LowerToMaxwell(std::array{UniformMarker, instruction, Exit});
            INFO(lowered.error);
            REQUIRE(lowered);
            const auto begin = lowered.instructions.begin() + lowered.source_instruction_offsets[1];
            const auto end = lowered.instructions.begin() + lowered.source_instruction_offsets[2];
            const u32 opcode = instruction.Bits(0, 12);
            const auto operation = std::find_if(begin, end, [&](u64 word) {
                return opcode == 0x9a8 ? (word >> 56) == 0xed :
                       opcode == 0x98e ? (word >> 48) == 0xebf9 : (word >> 52) == 0xeb4;
            });
            REQUIRE(operation != end);
            const u32 data = static_cast<u32>((*operation >> (opcode == 0x9a8 ? 20 : 0)) & 255);
            REQUIRE(data < 216);
            CHECK(std::find(begin, operation, Shader::Ampere::NativeRegisterCopy(0, data, 1, high)) != operation);
            CHECK(std::find(operation, end, Shader::Ampere::NativeRegisterCopy(1, high, 0, data)) != end);
        }
    }
}

TEST_CASE("Logged Ocarina IMAD and SHF preserve all high register operands",
          "[video_core][ampere][register_pressure][oot]") {
    struct Fixture {
        Instruction instruction;
        std::vector<u32> fields;
    };
    // v07-final.log: native register shadowing mode at 0x33e0, 0x3390, 0x60f0 and 0x750.
    const std::array fixtures{
        Fixture{{{0x1de57c24, 0x0000000a, 0x0f8e02ff, 0x000fe200}}, {16, 24, 64}},
        Fixture{{{0x31e17c24, 0x0000000a, 0x0f8e02ff, 0x000fe200}}, {16, 24, 64}},
        Fixture{{{0xf0077219, 0x0000001b, 0x000012ef, 0x000fe400}}, {16, 24, 32, 64}},
        Fixture{{{0xffe47219, 0x000000e4, 0x00011e19, 0x000fe400}}, {16, 24, 32, 64}},
        // Equivalent operand forms keep the uniform or constant-buffer field untouched.
        Fixture{{{0x1de57e24, 0x0000000a, 0x0f8e02ef, 0x000fe200}}, {16, 24, 64}},
        Fixture{{{0xffe47c19, 0x0000000a, 0x08011e19, 0x000fe400}}, {16, 24, 64}},
        Fixture{{{0xffe47a19, 0x00000000, 0x00011e19, 0x000fe400}}, {16, 24, 64}},
    };
    for (const auto& fixture : fixtures) {
        const auto& instruction = fixture.instruction;
        CAPTURE(instruction.words);
        const auto pressure = Shader::Ampere::PrepareRegisterPressure(instruction, 216);
        REQUIRE(pressure.valid);
        const auto lowered = LowerToMaxwell(std::array{UniformMarker, instruction, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        const auto begin = lowered.instructions.begin() + lowered.source_instruction_offsets[1];
        const auto end = lowered.instructions.begin() + lowered.source_instruction_offsets[2];
        REQUIRE(static_cast<size_t>(end - begin) > pressure.prefix.size() + pressure.suffix.size());
        CHECK(std::equal(pressure.prefix.begin(), pressure.prefix.end(), begin));
        CHECK(std::equal(pressure.suffix.begin(), pressure.suffix.end(), end - pressure.suffix.size()));
        std::array<u32, 4> masks{};
        for (const auto field : fixture.fields) {
            masks[field / 32] |= 255U << (field % 32);
        }
        for (u32 word = 0; word < masks.size(); ++word) {
            CHECK((pressure.instruction.words[word] & ~masks[word]) ==
                  (instruction.words[word] & ~masks[word]));
        }
        for (bool taken : {false, true}) {
            auto banks = InitialBanks();
            auto expected = banks;
            CopyRegisters(banks, pressure.prefix);
            for (const auto field : fixture.fields) {
                if (field == 16) continue;
                const u32 original = instruction.Bits(field, 8);
                const u32 mapped = pressure.instruction.Bits(field, 8);
                if (original != 255) {
                    CHECK(banks[0][mapped] == expected[original >= 216 ? 1 : 0][original]);
                } else {
                    CHECK(mapped == 255);
                }
            }
            if (taken) {
                const u32 destination = instruction.Bits(16, 8);
                banks[0][pressure.instruction.Bits(16, 8)] = 0xabcdef01;
                expected[destination >= 216 ? 1 : 0][destination] = 0xabcdef01;
            }
            CopyRegisters(banks, pressure.suffix);
            CHECK(banks[0] == expected[0]);
            CHECK(banks[1] == expected[1]);
        }
    }
}

TEST_CASE("High uniform pairs leave a quartet aligned boundary for logged Ocarina LDG",
          "[video_core][ampere][register_pressure][oot]") {
    // The prior even-only boundary could be R214, so the high R236 destination was rejected
    // before the supported LDG operation. Exercise every high uniform bank extent.
    const Instruction load{{0xffec7981, 0x00002014, 0x0c1e8b00, 0x000f2800}};
    for (u32 uniform = 28; uniform < 63; ++uniform) {
        const Instruction vote{{0x00007886U | (uniform << 16), 0, 0x038e0100, 0x000fe400}};
        const auto first = Shader::Ampere::LowerToMaxwellWith(std::array{vote, load, Exit}, 0);
        REQUIRE_FALSE(first);
        REQUIRE(first.high_uniform_registers >= uniform - 27);
        REQUIRE(first.high_uniform_registers % 4 == 0);
        const auto lowered = LowerToMaxwell(std::array{vote, load, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        const u32 boundary = 216 - first.high_uniform_registers;
        const auto pressure = Shader::Ampere::PrepareRegisterPressure(load, boundary);
        REQUIRE(pressure.valid);
        const auto begin = lowered.instructions.begin() + lowered.source_instruction_offsets[1];
        const auto end = lowered.instructions.begin() + lowered.source_instruction_offsets[2];
        REQUIRE(static_cast<size_t>(end - begin) == pressure.prefix.size() + pressure.suffix.size() + 1);
        const auto memory = begin + pressure.prefix.size();
        CHECK(((*memory >> 8) & 255) == 236); // UR20 and UR21 keep their original pair mapping.
        CHECK((*memory & 255) == pressure.instruction.Bits(16, 8));
        CHECK(((*memory >> 20) & 0xffffff) == 0x20);
    }
}
