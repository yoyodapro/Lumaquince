// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/object_pool.h"

namespace {
using namespace Shader;

struct Instruction {
    u32 opcode;
    std::vector<u32> args;
};

std::vector<Instruction> Decode(const std::vector<u32>& code) {
    std::vector<Instruction> instructions;
    for (size_t offset = 5; offset < code.size();) {
        const u32 size = code[offset] >> 16;
        REQUIRE(size != 0);
        REQUIRE(offset + size <= code.size());
        instructions.push_back({code[offset] & 0xffffU,
                                {code.begin() + offset + 1, code.begin() + offset + size}});
        offset += size;
    }
    return instructions;
}

struct CasProgram {
    ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::Program program;
    Profile profile;

    CasProgram() {
        program.stage = Stage::Compute;
        program.workgroup_size = {32, 1, 1};
        program.blocks = {&block};
        program.post_order_blocks = {&block};
        program.syntax_list = {{{.block = &block}, IR::AbstractSyntaxNode::Type::Block},
                               {{}, IR::AbstractSyntaxNode::Type::Return}};
        profile.supported_spirv = 0x00010500;
    }

    void Storage() {
        program.info.used_storage_buffer_types = IR::Type::U32;
        program.info.storage_buffers_descriptors = {{0, 0, 1, true}};
    }

    std::vector<Instruction> Emit(const std::string& name) {
        const auto code = Backend::SPIRV::EmitSPIRV(profile, program);
        if (const char* directory = std::getenv("EDEN_CAS_TEST_OUTPUT")) {
            std::filesystem::create_directories(directory);
            std::ofstream file{std::filesystem::path{directory} / (name + ".spv"),
                               std::ios::binary};
            file.write(reinterpret_cast<const char*>(code.data()),
                       static_cast<std::streamsize>(code.size() * sizeof(u32)));
            REQUIRE(file.good());
        }
        return Decode(code);
    }
};

// Inspect the emitted instructions, including the memory operand types. In particular, native
// f16x2 arithmetic must still load and compare a u32 memory word, not an f16 vector from a u32*.
void CheckRetry(const std::vector<Instruction>& code, u32 expected_scope) {
    std::map<u32, u32> result_types;
    std::map<u32, u32> pointees;
    std::map<u32, u32> constants;
    u32 u32_type{};
    for (const auto& inst : code) {
        const auto& a = inst.args;
        if (inst.opcode == 21 && a[1] == 32 && a[2] == 0) { // OpTypeInt
            u32_type = a[0];
        } else if (inst.opcode == 32) { // OpTypePointer
            pointees[a[0]] = a[2];
        } else if (inst.opcode == 43) { // OpConstant
            constants[a[1]] = a[2];
        }
        switch (inst.opcode) {
        case 55: // OpFunctionParameter
        case 59: // OpVariable
        case 61: // OpLoad
        case 65: // OpAccessChain
        case 230: // OpAtomicCompareExchange
            result_types[a[1]] = a[0];
            break;
        }
    }
    REQUIRE(u32_type != 0);
    size_t function_start{};
    size_t loop_start{};
    size_t atomics{};
    for (size_t index = 0; index < code.size(); ++index) {
        const auto& inst = code[index];
        const auto& a = inst.args;
        if (inst.opcode == 54) { // OpFunction
            function_start = index;
            loop_start = 0;
        } else if (inst.opcode == 246) { // OpLoopMerge
            loop_start = index;
        } else if (inst.opcode == 61) {
            REQUIRE(pointees.at(result_types.at(a[2])) == a[0]);
        } else if (inst.opcode == 230) {
            ++atomics;
            REQUIRE(a[0] == u32_type);
            REQUIRE(pointees.at(result_types.at(a[2])) == u32_type);
            REQUIRE(constants.at(a[3]) == expected_scope);
            REQUIRE(constants.at(a[4]) == 0);
            REQUIRE(constants.at(a[5]) == 0);
            REQUIRE(loop_start > function_start);
            const u32 word_pointer = a[2];
            const u32 expected = a[7];
            const auto load = std::find_if(code.begin() + loop_start, code.begin() + index,
                                          [=](const auto& op) {
                                              return op.opcode == 61 && op.args[1] == expected;
                                          });
            REQUIRE(load != code.begin() + index);
            const u32 retry_variable = load->args[2];
            REQUIRE(retry_variable != word_pointer);
            REQUIRE(pointees.at(result_types.at(retry_variable)) == u32_type);

            // There is exactly one non-atomic read of the contested word, before the loop.
            size_t initial_reads{};
            u32 initial_value{};
            for (size_t before = function_start; before < index; ++before) {
                const auto& op = code[before];
                if (op.opcode == 61 && op.args[2] == word_pointer) {
                    REQUIRE(before < loop_start);
                    ++initial_reads;
                    initial_value = op.args[1];
                }
            }
            REQUIRE(initial_reads == 1);
            REQUIRE(std::any_of(code.begin() + function_start, code.begin() + loop_start,
                                [=](const auto& op) {
                                    return op.opcode == 62 && op.args[0] == retry_variable &&
                                           op.args[1] == initial_value;
                                }));
            // The feedback store precedes the branch. It is the exchange's returned word,
            // including neighboring bytes changed by another invocation, rather than our desired word.
            REQUIRE(index + 3 < code.size());
            REQUIRE(code[index + 1].opcode == 62); // OpStore
            REQUIRE((code[index + 1].args == std::vector<u32>{retry_variable, a[1]}));
            REQUIRE(code[index + 2].opcode == 170); // OpIEqual
            const auto& equal = code[index + 2].args;
            REQUIRE(((equal[2] == expected && equal[3] == a[1]) ||
                     (equal[2] == a[1] && equal[3] == expected)));
            REQUIRE(code[index + 3].opcode == 250); // OpBranchConditional
            REQUIRE(code[index + 3].args[0] == equal[1]);
        }
    }
    REQUIRE(atomics == 1);
}
} // namespace

TEST_CASE("SPIR-V arithmetic CAS retries from the returned memory word", "[video_core][spirv][atomic_cas]") {
    struct Variant {
        const char* name;
        bool Info::*flag;
        bool fp16;
    };
    for (const Variant variant : {
             Variant{"storage-inc", &Info::uses_global_increment, false},
             Variant{"storage-dec", &Info::uses_global_decrement, false},
             Variant{"storage-f32-add", &Info::uses_atomic_f32_add, false},
             Variant{"storage-f16x2-add", &Info::uses_atomic_f16x2_add, true},
             Variant{"storage-f16x2-min", &Info::uses_atomic_f16x2_min, true},
             Variant{"storage-f16x2-max", &Info::uses_atomic_f16x2_max, true},
             Variant{"storage-f32x2-add", &Info::uses_atomic_f32x2_add, false},
             Variant{"storage-f32x2-min", &Info::uses_atomic_f32x2_min, false},
             Variant{"storage-f32x2-max", &Info::uses_atomic_f32x2_max, false},
         }) {
        INFO(variant.name);
        CasProgram fixture;
        fixture.Storage();
        fixture.program.info.*variant.flag = true;
        fixture.program.info.uses_fp16 = variant.fp16;
        CheckRetry(fixture.Emit(variant.name), 1); // Device
    }
    for (bool explicit_layout : {false, true}) {
        for (bool increment : {false, true}) {
            const std::string name = std::string{"shared-"} + (increment ? "inc" : "dec") +
                                     (explicit_layout ? "-explicit" : "");
            INFO(name);
            CasProgram fixture;
            fixture.profile.support_explicit_workgroup_layout = explicit_layout;
            fixture.program.shared_memory_size = 128;
            fixture.program.info.uses_shared_increment = increment;
            fixture.program.info.uses_shared_decrement = !increment;
            CheckRetry(fixture.Emit(name), 2); // Workgroup
        }
    }
}

TEST_CASE("SPIR-V subword stores preserve the word returned by a failed exchange", "[video_core][spirv][atomic_cas]") {
    for (bool shared : {false, true}) {
        for (bool halfword : {false, true}) {
            const std::string name = std::string{shared ? "shared-store" : "storage-store"} +
                                     (halfword ? "16" : "8");
            INFO(name);
            CasProgram fixture;
            fixture.program.info.uses_int8 = !halfword;
            fixture.program.info.uses_int16 = halfword;
            if (shared) {
                fixture.program.shared_memory_size = 128;
                // Keep shared accesses emulated, but suppress the unused storage fallback helper.
                fixture.profile.support_descriptor_aliasing = true;
                fixture.profile.support_int8 = true;
                fixture.profile.support_int16 = true;
                fixture.profile.support_storage_buffer_8bit = true;
                fixture.profile.support_storage_buffer_16bit = true;
            } else {
                fixture.Storage();
            }
            CheckRetry(fixture.Emit(name), 1); // Existing device scope retained for subword writes.
        }
    }
}
