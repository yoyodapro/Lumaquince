// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"

namespace {
using namespace Shader;

struct Instruction {
    u32 opcode;
    std::vector<u32> args;
};

struct Module {
    std::vector<Instruction> instructions;

    explicit Module(const std::vector<u32>& code) {
        REQUIRE(code.size() > 5);
        REQUIRE(code[0] == 0x07230203U);
        for (size_t offset = 5; offset < code.size();) {
            const u32 size = code[offset] >> 16;
            REQUIRE(size != 0);
            REQUIRE(offset + size <= code.size());
            instructions.push_back({code[offset] & 0xffffU,
                                    {code.begin() + offset + 1, code.begin() + offset + size}});
            offset += size;
        }
    }

    const Instruction& Definition(u32 id) const {
        const auto found = std::ranges::find_if(instructions, [=](const auto& op) {
            switch (op.opcode) {
            case 43:  // OpConstant
            case 59:  // OpVariable
            case 61:  // OpLoad
            case 65:  // OpAccessChain
            case 80:  // OpCompositeConstruct
            case 124: // OpBitcast
            case 128: // OpIAdd
            case 132: // OpIMul
                return op.args[1] == id;
            default:
                return false;
            }
        });
        REQUIRE(found != instructions.end());
        return *found;
    }

    const Instruction& Type(u32 id) const {
        const auto found = std::ranges::find_if(instructions, [=](const auto& op) {
            return op.opcode >= 19 && op.opcode <= 39 && op.args[0] == id;
        });
        REQUIRE(found != instructions.end());
        return *found;
    }

    std::optional<u32> Constant(u32 id) const {
        const auto found = std::ranges::find_if(instructions, [=](const auto& op) {
            return op.opcode == 43 && op.args[1] == id;
        });
        if (found == instructions.end()) {
            return std::nullopt;
        }
        return found->args[2];
    }

    u32 Builtin(u32 builtin) const {
        u32 result{};
        for (const auto& op : instructions) {
            if (op.opcode == 71 && op.args.size() >= 3 && op.args[1] == 11 &&
                op.args[2] == builtin) { // OpDecorate BuiltIn
                REQUIRE(result == 0);
                result = op.args[0];
            }
        }
        return result;
    }

    const Instruction& Array(u32 variable) const {
        const auto& var = Definition(variable);
        REQUIRE(var.opcode == 59);
        const auto& pointer = Type(var.args[0]);
        REQUIRE(pointer.opcode == 32);
        const auto& array = Type(pointer.args[2]);
        REQUIRE(array.opcode == 28);
        return array;
    }

    std::vector<const Instruction*> Stores(u32 variable) const {
        std::vector<const Instruction*> result;
        for (const auto& op : instructions) {
            if (op.opcode != 62) { // OpStore
                continue;
            }
            const auto& pointer = Definition(op.args[0]);
            if (pointer.opcode == 65 && pointer.args[2] == variable) {
                result.push_back(&op);
            }
        }
        return result;
    }

    // Recover the emitted map address as coefficient * primitive + constant. This checks
    // the dataflow used by index loads, rather than merely looking for an arity constant.
    std::pair<u32, u32> Address(u32 id, u32 primitive) const {
        if (id == primitive) {
            return {1, 0};
        }
        if (const auto value = Constant(id)) {
            return {0, *value};
        }
        const auto& op = Definition(id);
        REQUIRE((op.opcode == 128 || op.opcode == 132));
        const auto lhs = Address(op.args[2], primitive);
        const auto rhs = Address(op.args[3], primitive);
        if (op.opcode == 128) {
            return {lhs.first + rhs.first, lhs.second + rhs.second};
        }
        REQUIRE((lhs.first == 0 || rhs.first == 0));
        return {lhs.first * rhs.second + rhs.first * lhs.second, lhs.second * rhs.second};
    }
};

Module EmitMesh(u32 topology, u32 arity, std::optional<float> fixed_size,
                bool authored_size, const char* name) {
    ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::Program program;
    program.stage = Stage::Mesh;
    program.info.is_mesh = true;
    program.info.uses_local_invocation_id = true;
    program.workgroup_size = {32, 1, 1};
    program.mesh.topology = topology;
    program.mesh.max_vertices = 32;
    program.mesh.max_primitives = 32;
    program.mesh.payload_words = 32;
    program.mesh.vertex_lines = {static_cast<u32>(IR::Attribute::PositionX),
                                 static_cast<u32>(IR::Attribute::PositionY),
                                 static_cast<u32>(IR::Attribute::PositionZ),
                                 static_cast<u32>(IR::Attribute::PositionW)};
    if (authored_size) {
        program.mesh.vertex_lines.push_back(static_cast<u32>(IR::Attribute::PointSize));
    }
    program.blocks = {&block};
    program.post_order_blocks = {&block};
    program.syntax_list = {{{.block = &block}, IR::AbstractSyntaxNode::Type::Block},
                           {{}, IR::AbstractSyntaxNode::Type::Return}};
    IR::IREmitter ir{block};
    ir.Prologue();
    const auto vertex = ir.LocalInvocationIdX();
    for (u32 line = 0; line < program.mesh.vertex_lines.size(); ++line) {
        const auto address = ir.IAdd(ir.IMul(vertex, ir.Imm32(4)), ir.Imm32(line * 128));
        const float value = line == 4 ? 3.0f : line == 3 ? 1.0f : 0.0f;
        ir.IsbeWriteAttribute(address, ir.Imm32(std::bit_cast<u32>(value)));
    }
    ir.IsbeWriteMapU32(ir.Imm32(3), ir.Imm32(32));
    for (u32 element = 0; element < arity; ++element) {
        ir.IsbeWriteMapU8(ir.IAdd(ir.IMul(vertex, ir.Imm32(arity)), ir.Imm32(4 + element)),
                          vertex);
    }
    ir.Epilogue();
    Profile profile{};
    profile.supported_spirv = 0x00010500;
    RuntimeInfo runtime{};
    runtime.fixed_state_point_size = fixed_size;
    Backend::Bindings bindings{};
    const auto code = Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
    if (const char* directory = std::getenv("EDEN_MESH_TEST_OUTPUT")) {
        std::filesystem::create_directories(directory);
        std::ofstream file{std::filesystem::path{directory} / (std::string{name} + ".spv"),
                           std::ios::binary};
        file.write(reinterpret_cast<const char*>(code.data()),
                   static_cast<std::streamsize>(code.size() * sizeof(u32)));
        REQUIRE(file.good());
    }
    return Module{code};
}

void CheckTopology(const Module& module, u32 arity, u32 mode, u32 builtin) {
    size_t topology_modes{};
    size_t set_outputs{};
    for (const auto& op : module.instructions) {
        if (op.opcode == 16 && (op.args[1] == 27 || op.args[1] == 5269 || op.args[1] == 5298)) {
            ++topology_modes;
            CHECK(op.args[1] == mode);
        } else if (op.opcode == 5295) { // OpSetMeshOutputsEXT
            ++set_outputs;
            CHECK(module.Constant(op.args[0]) == 32U);
        }
    }
    CHECK(topology_modes == 1);
    CHECK(set_outputs == 1);
    const u32 indices = module.Builtin(builtin);
    REQUIRE(indices != 0);
    for (const u32 other : {5294U, 5295U, 5296U}) {
        CHECK((module.Builtin(other) != 0) == (other == builtin));
    }
    const auto& array = module.Array(indices);
    CHECK(module.Constant(array.args[2]) == 32U);
    const auto& element = module.Type(array.args[1]);
    if (arity != 1) {
        REQUIRE(element.opcode == 23); // OpTypeVector
        CHECK(element.args[2] == arity);
    }
    const auto& scalar = arity == 1 ? element : module.Type(element.args[1]);
    REQUIRE(scalar.opcode == 21); // OpTypeInt
    CHECK(scalar.args[1] == 32U);
    CHECK(scalar.args[2] == 0U);

    const auto stores = module.Stores(indices);
    REQUIRE(stores.size() == 1);
    const auto& output_pointer = module.Definition(stores[0]->args[0]);
    REQUIRE(output_pointer.args.size() == 4);
    const u32 primitive = output_pointer.args[3];
    std::vector<u32> values{stores[0]->args[1]};
    if (arity != 1) {
        const auto& composite = module.Definition(values[0]);
        REQUIRE(composite.opcode == 80); // OpCompositeConstruct
        values.assign(composite.args.begin() + 2, composite.args.end());
    }
    REQUIRE(values.size() == arity);
    u32 map{};
    for (u32 index = 0; index < arity; ++index) {
        const auto& load = module.Definition(values[index]);
        REQUIRE(load.opcode == 61);
        const auto& pointer = module.Definition(load.args[2]);
        REQUIRE(pointer.opcode == 65);
        REQUIRE(pointer.args.size() == 4);
        if (index == 0) {
            map = pointer.args[2];
        }
        CHECK(pointer.args[2] == map);
        CHECK(module.Address(pointer.args[3], primitive) == std::pair(arity, 4U + index));
    }
    const auto& map_array = module.Array(map);
    CHECK(module.Constant(map_array.args[2]) == 4U + arity * 32U);
}

void CheckPointSize(const Module& module, float expected, bool authored) {
    const u32 point_size = module.Builtin(1);
    REQUIRE(point_size != 0);
    const auto& array = module.Array(point_size);
    CHECK(module.Constant(array.args[2]) == 32U);
    const auto& scalar = module.Type(array.args[1]);
    REQUIRE(scalar.opcode == 22); // OpTypeFloat
    CHECK(scalar.args[1] == 32U);
    const auto stores = module.Stores(point_size);
    REQUIRE(stores.size() == (authored ? 2U : 1U));
    CHECK(module.Constant(stores[0]->args[1]) == std::bit_cast<u32>(expected));
    if (authored) {
        const auto& override_value = module.Definition(stores[1]->args[1]);
        REQUIRE(override_value.opcode == 124); // OpBitcast, after the fixed-size store
        const auto& load = module.Definition(override_value.args[2]);
        REQUIRE(load.opcode == 61);
        const auto& pointer = module.Definition(load.args[2]);
        REQUIRE(pointer.opcode == 65);
        const auto& line_address = module.Definition(pointer.args[3]);
        REQUIRE(line_address.opcode == 128);
        CHECK(module.Constant(line_address.args[3]) == 128U); // Fifth 32-word attribute line.
        CHECK(module.Definition(stores[0]->args[0]).args[3] ==
              module.Definition(stores[1]->args[0]).args[3]);
    }
}
} // namespace

TEST_CASE("Mesh SPIR-V matches primitive topology and map index layout",
          "[video_core][spirv][mesh_topology]") {
    struct Case {
        u32 topology, arity, mode, builtin;
        const char* name;
    };
    for (const Case test : {Case{0, 1, 27, 5294, "mesh-points-default"},
                            Case{1, 2, 5269, 5295, "mesh-lines"},
                            Case{4, 3, 5298, 5296, "mesh-triangles"}}) {
        INFO(test.name);
        const auto module = EmitMesh(test.topology, test.arity, std::nullopt, false, test.name);
        CheckTopology(module, test.arity, test.mode, test.builtin);
        if (test.topology == 0) {
            CheckPointSize(module, 1.0f, false);
        } else {
            CHECK(module.Builtin(1) == 0U);
        }
    }
}

TEST_CASE("Mesh point size initializes every vertex before authored size wins",
          "[video_core][spirv][mesh_topology]") {
    for (const bool authored : {false, true}) {
        const auto module = EmitMesh(0, 1, 2.5f, authored,
                                     authored ? "mesh-points-authored" : "mesh-points-fixed");
        CheckTopology(module, 1, 27, 5294);
        CheckPointSize(module, 2.5f, authored);
    }
}
