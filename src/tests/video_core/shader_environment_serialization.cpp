// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "common/cityhash.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/ampere/program_address_map.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/host_translate_info.h"
#include "video_core/pipeline_cache_files.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache_prebuild.h"
#include "video_core/shader_environment.h"

namespace {

class CapturedEnvironment final : public VideoCommon::GenericEnvironment {
public:
    explicit CapturedEnvironment(Shader::Stage shader_stage, bool proprietary = true,
                                 bool hle = true) {
        stage = shader_stage;
        mesh_registers = {4U | (37U << 4) | (23U << 13), 32U << 20,
                          16U | (3U << 12) | (2U << 22)};
        std::array<u32, sizeof(Shader::ProgramHeader) / sizeof(u32)> header{};
        for (size_t i = 0; i < header.size(); ++i) {
            header[i] = static_cast<u32>(i * 0x01010101);
        }
        std::memcpy(&sph, header.data(), sizeof(sph));
        for (auto& word : header) {
            word ^= 0xfedcba98;
        }
        std::memcpy(&mesh_primitive_header, header.data(), sizeof(mesh_primitive_header));
        gp_passthrough_mask = {1, 3, 5, 7, 11, 13, 17, 19};
        code.resize(14, 0);
        code[10] = 0xe30000000007000fULL;
        const std::array<u32, 1> offsets{0};
        Shader::Ampere::AppendNativeAddressMap(code, offsets);
        native_source_size = 144;
        cached_lowest = 0;
        cached_highest = static_cast<u32>((code.size() - 1) * sizeof(u64));
        initial_offset = sizeof(sph);
        local_memory_size = 512;
        shared_memory_size = 4096;
        workgroup_size = {32, 2, 3};
        texture_bound = 7;
        viewport_transform_state = 0;
        is_proprietary_driver = proprietary;
        has_hle_engine_state = hle;
        texture_types[42] = Shader::TextureType::ColorArray2D;
        texture_pixel_formats[42] = Shader::TexturePixelFormat::A8B8G8R8_UNORM;
        cbuf_values[(u64{1} << 32) | 16] = 0xabcdef01;
        storage_pointer_types[(u64{1} << 32) | 16] = 1;
        storage_pointer_types[(u64{1} << 63) | (u64{32} << 40) | (u64{1} << 32) | 16] = 0;
        cbuf_replacements[(u64{1} << 32) | 16] = Shader::ReplaceConstant::BaseVertex;
    }

    u32 ReadCbufValue(u32 bank, u32 offset) override {
        return cbuf_values.at((u64{bank} << 32) | offset);
    }
    Shader::TextureType ReadTextureType(u32 handle) override {
        return texture_types.at(handle);
    }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32 handle) override {
        return texture_pixel_formats.at(handle);
    }
    bool IsTexturePixelFormatInteger(u32) override { return false; }
    u32 ReadViewportTransformState() override { return viewport_transform_state; }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32 bank, u32 offset) override {
        return cbuf_replacements.at((u64{bank} << 32) | offset);
    }
    void AddTrapPrefix() {
        auto lowered = std::make_shared<VideoCommon::LoweredNativeCode>();
        lowered->trap_prefix.emplace_back();
        lowered_native = std::move(lowered);
    }
    void SetTranslationHeaders() {
        sph = {};
        mesh_primitive_header = {};
        sph.vtg.omap_systemb.raw = 0xf0;
        sph.vtg.omap_generic_vector[0] = 0xf;
        mesh_primitive_header.vtg.omap_generic_vector[0] = 1;
    }
    void ChangeMeshState() {
        mesh_registers[0] += 1U << 4;
        mesh_registers[1] = 24U << 20;
        mesh_registers[2] = 8U | (4U << 12) | (2U << 22);
        mesh_primitive_header.vtg.omap_generic_vector[0] = 3;
    }
    void SetMeshShaderA(u32 value) {
        mesh_registers[0] = value;
    }
};

std::string Serialize(const CapturedEnvironment& env) {
    std::ostringstream stream(std::ios::binary);
    env.Serialize(stream);
    return std::move(stream).str();
}

void Deserialize(std::string bytes, VideoCommon::FileEnvironment& env) {
    std::istringstream stream(std::move(bytes), std::ios::binary);
    env.Deserialize(stream);
}

constexpr size_t VersionOffset = 8;
constexpr size_t SizeOffset = VersionOffset + sizeof(u32);
constexpr size_t ChecksumOffset = SizeOffset + sizeof(u64);
constexpr size_t PayloadOffset = ChecksumOffset + sizeof(u64);

void UpdateChecksum(std::string& bytes) {
    const u64 checksum = Common::CityHash64(bytes.data() + PayloadOffset,
                                          bytes.size() - PayloadOffset);
    std::memcpy(bytes.data() + ChecksumOffset, &checksum, sizeof(checksum));
}

struct Translation {
    Shader::Stage stage;
    std::array<u32, 3> workgroup;
    u32 topology;
    u32 vertices;
    u32 primitives;
    u32 payload_words;
    std::vector<u32> vertex_lines;
    std::vector<u32> primitive_lines;
    std::vector<u32> spirv;
    bool operator==(const Translation&) const = default;
};

Translation Translate(Shader::Environment& env) {
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, Shader::Maxwell::Location{0x50}};
    Shader::ObjectPool<Shader::IR::Inst> inst_pool;
    Shader::ObjectPool<Shader::IR::Block> block_pool;
    auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg,
                                                     Shader::HostTranslateInfo{});
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010400;
    return {program.stage, program.workgroup_size, program.mesh.topology, program.mesh.max_vertices,
            program.mesh.max_primitives, program.mesh.payload_words,
            {program.mesh.vertex_lines.begin(), program.mesh.vertex_lines.end()},
            {program.mesh.primitive_lines.begin(), program.mesh.primitive_lines.end()},
            Shader::Backend::SPIRV::EmitSPIRV(profile, program)};
}

} // namespace

TEST_CASE("Shader environments preserve task and mesh translation inputs",
          "[shader_environment][pipeline_cache]") {
    const auto stage = GENERATE(Shader::Stage::Task, Shader::Stage::Mesh, Shader::Stage::Compute,
                                Shader::Stage::Geometry, Shader::Stage::VertexB);
    const bool proprietary = GENERATE(false, true);
    const bool hle = GENERATE(false, true);
    CapturedEnvironment source(stage, proprietary, hle);
    VideoCommon::FileEnvironment restored;
    Deserialize(Serialize(source), restored);
    CHECK(restored.ShaderStage() == stage);
    CHECK(restored.StartAddress() == source.StartAddress());
    CHECK(restored.MeshRegisters() == source.MeshRegisters());
    CHECK(std::memcmp(&restored.SPH(), &source.SPH(), sizeof(Shader::ProgramHeader)) == 0);
    CHECK(std::memcmp(&restored.MeshPrimitiveHeader(), &source.MeshPrimitiveHeader(),
                      sizeof(Shader::ProgramHeader)) == 0);
    CHECK(restored.GpPassthroughMask() == source.GpPassthroughMask());
    CHECK(restored.LocalMemorySize() == source.LocalMemorySize());
    CHECK(restored.SharedMemorySize() == source.SharedMemorySize());
    CHECK(restored.WorkgroupSize() == source.WorkgroupSize());
    CHECK(restored.TextureBoundBuffer() == source.TextureBoundBuffer());
    CHECK(restored.ReadViewportTransformState() == source.ReadViewportTransformState());
    CHECK(restored.IsProprietaryDriver() == proprietary);
    CHECK(restored.HasHLEMacroState() == hle);
    CHECK(restored.HasNativeInstructions());
    CHECK(restored.IsNativeProgram());
    CHECK(restored.TranslateProgramAddress(0) == source.TranslateProgramAddress(0));
    CHECK(restored.ReadInstruction(80) == source.ReadInstruction(80));
    CHECK(restored.ReadTextureType(42) == source.ReadTextureType(42));
    CHECK(restored.ReadTexturePixelFormat(42) == source.ReadTexturePixelFormat(42));
    CHECK(restored.ReadCbufValue(1, 16) == source.ReadCbufValue(1, 16));
    CHECK(restored.IsCbufStorageBufferPointer(1, 16));
    CHECK_FALSE(restored.IsCbufIndirectStorageBufferPointer(1, 16, 32));
    CHECK(restored.GetReplaceConstBuffer(1, 16) == source.GetReplaceConstBuffer(1, 16));
    CHECK_THROWS(restored.ReadInstruction(81));
    CHECK_THROWS(restored.ReadInstruction(0x10000));
}

TEST_CASE("Shader environment rejects incomplete and corrupt records before translation",
          "[shader_environment][pipeline_cache]") {
    CapturedEnvironment source(Shader::Stage::Mesh);
    const auto original = Serialize(source);
    VideoCommon::FileEnvironment restored;
    SECTION("Every truncated prefix") {
        for (size_t size = 0; size < original.size(); ++size) {
            CHECK_THROWS_AS(Deserialize(original.substr(0, size), restored), std::ios_base::failure);
        }
    }
    SECTION("Wrong magic") {
        auto bytes = original;
        bytes[0] ^= 1;
        CHECK_THROWS_AS(Deserialize(bytes, restored), std::ios_base::failure);
    }
    SECTION("Unknown version") {
        auto bytes = original;
        bytes[VersionOffset] ^= 1;
        CHECK_THROWS_AS(Deserialize(bytes, restored), std::ios_base::failure);
    }
    SECTION("Oversized payload") {
        auto bytes = original;
        const u64 size = ~u64{};
        std::memcpy(bytes.data() + SizeOffset, &size, sizeof(size));
        CHECK_THROWS_AS(Deserialize(bytes, restored), std::ios_base::failure);
    }
    SECTION("Payload corruption") {
        auto bytes = original;
        bytes.back() ^= 1;
        CHECK_THROWS_AS(Deserialize(bytes, restored), std::ios_base::failure);
    }
    SECTION("Invalid code length with a valid checksum") {
        auto bytes = original;
        const u64 size = ~u64{};
        std::memcpy(bytes.data() + PayloadOffset, &size, sizeof(size));
        UpdateChecksum(bytes);
        CHECK_THROWS_AS(Deserialize(bytes, restored), std::ios_base::failure);
    }
    SECTION("Invalid initial offset with a valid checksum") {
        auto bytes = original;
        u64 code_size{};
        std::memcpy(&code_size, bytes.data() + PayloadOffset, sizeof(code_size));
        const u32 offset = GENERATE_COPY(81U, static_cast<u32>(code_size), ~u32{});
        std::memcpy(bytes.data() + bytes.size() - 2 * sizeof(u32), &offset, sizeof(offset));
        UpdateChecksum(bytes);
        CHECK_THROWS_AS(Deserialize(bytes, restored), std::ios_base::failure);
    }
    SECTION("Trap-dependent environments remain excluded") {
        source.AddTrapPrefix();
        CHECK_FALSE(source.CanBeSerialized());
        CHECK_THROWS_AS(Serialize(source), std::ios_base::failure);
    }
}

TEST_CASE("Cached environment storage participates in the prebuild capture budget",
          "[shader_environment][pipeline_prebuild]") {
    CapturedEnvironment source(Shader::Stage::VertexB);
    VideoCommon::FileEnvironment restored;
    const auto empty_bytes = restored.MemorySize();
    Deserialize(Serialize(source), restored);
    const auto capture_bytes = restored.MemorySize();
    REQUIRE(capture_bytes > empty_bytes);
    Vulkan::PipelinePrebuild<int, int> too_small{1, capture_bytes - 1, false};
    REQUIRE_FALSE(too_small.Add(1, capture_bytes, [] { return std::make_unique<int>(42); }));
    Vulkan::PipelinePrebuild<int, int> exact{1, capture_bytes, false};
    REQUIRE(exact.Add(1, capture_bytes, [env = std::move(restored)]() mutable {
        return std::make_unique<int>(env.ReadCbufValue(1, 16) == 0xabcdef01 ? 42 : 0);
    }));
    exact.Stop();
}

TEST_CASE("Task and mesh translation and SPIRV are identical after cache reload",
          "[shader_environment][pipeline_cache]") {
    const auto stage = GENERATE(Shader::Stage::Task, Shader::Stage::Mesh);
    CapturedEnvironment source(stage);
    source.SetTranslationHeaders();
    VideoCommon::FileEnvironment restored;
    const auto original = Translate(source);
    Deserialize(Serialize(source), restored);
    CHECK(Translate(restored) == original);
    REQUIRE(original.spirv.size() > 5U);
    CHECK(original.spirv[0] == 0x07230203U);
    CHECK(original.workgroup[0] == (stage == Shader::Stage::Task ? 16U : 32U));
    CHECK(original.topology == 4U);
    CHECK(original.vertices == 37U);
    CHECK(original.primitives == 23U);
    CHECK(original.payload_words == 96U);
    if (stage == Shader::Stage::Mesh) {
        CHECK(original.vertex_lines.size() == 8U);
        CHECK(original.primitive_lines.size() == 1U);
    }
    source.ChangeMeshState();
    const auto changed = Translate(source);
    CHECK(changed != original);
    CHECK(changed.spirv != original.spirv);
    Deserialize(Serialize(source), restored);
    CHECK(Translate(restored) == changed);
}

TEST_CASE("Mesh topology registers survive translation and environment reload",
          "[shader_environment][mesh_topology]") {
    CapturedEnvironment source(Shader::Stage::Mesh);
    source.SetTranslationHeaders();
    VideoCommon::FileEnvironment restored;
    std::vector<u32> previous_spirv;
    for (const u32 topology : {0U, 1U, 4U}) {
        // The points value is the exact SET_MESH_SHADER_A from the Ocarina rejection.
        source.SetMeshShaderA(0x00040200U | topology);
        const auto original = Translate(source);
        CHECK(original.topology == topology);
        CHECK(original.vertices == 32U);
        CHECK(original.primitives == 32U);
        CHECK(original.spirv != previous_spirv);
        previous_spirv = original.spirv;
        Deserialize(Serialize(source), restored);
        CHECK(restored.MeshRegisters()[0] == (0x00040200U | topology));
        CHECK(Translate(restored) == original);
    }
}

TEST_CASE("Unsupported mesh primitive topologies remain explicit failures",
          "[shader_environment][mesh_topology]") {
    CapturedEnvironment source(Shader::Stage::Mesh);
    source.SetTranslationHeaders();
    for (u32 topology = 0; topology < 16; ++topology) {
        if (topology == 0 || topology == 1 || topology == 4) {
            continue;
        }
        INFO(topology);
        source.SetMeshShaderA(0x00040200U | topology);
        CHECK_THROWS_AS(Translate(source), Shader::NotImplementedException);
    }
}

TEST_CASE("Task and mesh pipelines reload with their key and reject an old cache version",
          "[shader_environment][pipeline_cache]") {
    struct CacheFile {
        std::filesystem::path path = std::filesystem::current_path() /
            ("shader-environment-test-" + std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
        ~CacheFile() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cache;
    CapturedEnvironment task(Shader::Stage::Task);
    CapturedEnvironment mesh(Shader::Stage::Mesh);
    CapturedEnvironment fragment(Shader::Stage::Fragment);
    const std::array<u64, 6> key{0, 0x1234, 0, 0x5678, 0, 0x9abc};
    const std::array<const VideoCommon::GenericEnvironment*, 3> envs{&task, &mesh, &fragment};
    constexpr auto version = VideoCommon::VulkanPipelineCacheVersion;
    VideoCommon::SerializePipeline(key, envs, cache.path, version);
    REQUIRE(std::filesystem::exists(cache.path));
    bool loaded{};
    const auto load = [&](u32 expected_version) {
        VideoCommon::LoadPipelines({}, cache.path, expected_version,
            [](std::ifstream&, VideoCommon::FileEnvironment) { FAIL("Unexpected compute pipeline"); },
            [&](std::ifstream& file, std::vector<VideoCommon::FileEnvironment> restored) {
                loaded = true;
                std::array<u64, 6> restored_key{};
                file.read(reinterpret_cast<char*>(restored_key.data()), sizeof(restored_key));
                CHECK(restored_key == key);
                REQUIRE(restored.size() == 3U);
                CHECK(restored[0].ShaderStage() == Shader::Stage::Task);
                CHECK(restored[1].ShaderStage() == Shader::Stage::Mesh);
                CHECK(restored[2].ShaderStage() == Shader::Stage::Fragment);
                CHECK(restored[0].MeshRegisters() == task.MeshRegisters());
                CHECK(restored[1].MeshRegisters() == mesh.MeshRegisters());
                CHECK(std::memcmp(&restored[1].MeshPrimitiveHeader(), &mesh.MeshPrimitiveHeader(),
                                  sizeof(Shader::ProgramHeader)) == 0);
            });
    };
    SECTION("Matching ABI") {
        load(version);
        CHECK(loaded);
    }
    SECTION("Mismatched ABI") {
        load(version - 1);
        CHECK_FALSE(loaded);
    }
    SECTION("Zero shader count") {
        std::fstream file(cache.path, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(12);
        const u32 count{};
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));
        file.close();
        load(version);
        CHECK_FALSE(loaded);
    }
}
