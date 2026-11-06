// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include "common/common_funcs.h"
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/builtin_adaptive_timing.h"
#include "adaptive_timing_fixture.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/nso.h"
#include "core/memory/cheat_engine.h"

namespace {

constexpr u64 OcarinaTitleId = 0x0400486024DEC000;
constexpr u64 OtherTitleId = 0x0100000000010000;
constexpr std::string_view MaxResolution = "Max Resolution (built-in)";
constexpr std::string_view OcarinaBuildIdHex = "E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348";
constexpr FileSys::PatchManager::BuildID OcarinaBuildId{
    0xE0, 0xF3, 0xCC, 0xFA, 0xF5, 0x00, 0x65, 0xAE, 0x4E, 0xA0,
    0x1E, 0xB5, 0xAE, 0xB1, 0x3B, 0x1A, 0x45, 0xDA, 0x33, 0x48,
};
constexpr size_t PatchOffset = sizeof(Loader::NSOHeader) + 0x805BD0;
constexpr u32 TbzInstruction = 0x360041E8;
constexpr u32 BranchInstruction = 0x1400020F;
constexpr u32 NopInstruction = 0xD503201F;

constexpr std::array<std::pair<Common::FS::EdenPath, const char*>, 4> TestDirectories{{
    {Common::FS::EdenPath::NANDDir, "nand"},
    {Common::FS::EdenPath::SDMCDir, "sdmc"},
    {Common::FS::EdenPath::LoadDir, "load"},
    {Common::FS::EdenPath::DumpDir, "dump"},
}};

void WriteIps(const std::filesystem::path& path, u32 word) {
    std::vector<u8> ips{'P', 'A', 'T', 'C', 'H'};
    ips.push_back(static_cast<u8>(PatchOffset >> 16));
    ips.push_back(static_cast<u8>(PatchOffset >> 8));
    ips.push_back(static_cast<u8>(PatchOffset));
    ips.push_back(0);
    ips.push_back(sizeof(word));
    for (size_t i = 0; i < sizeof(word); ++i) {
        ips.push_back(static_cast<u8>(word >> (i * 8)));
    }
    ips.insert(ips.end(), {'E', 'O', 'F'});
    std::ofstream file{path, std::ios::binary};
    file.write(reinterpret_cast<const char*>(ips.data()), static_cast<std::streamsize>(ips.size()));
}

class PatchManagerTestContext {
public:
    explicit PatchManagerTestContext(std::optional<u32> user_mod_word = std::nullopt) {
        Settings::values.program_args.SetValue("");
        for (size_t i = 0; i < TestDirectories.size(); ++i) {
            const auto& [path, name] = TestDirectories[i];
            previous[i] = Common::FS::GetEdenPath(path);
            std::filesystem::create_directories(root / name);
            Common::FS::SetEdenPath(path, root / name);
        }
        const auto title_dir = root / "load" / fmt::format("{:016X}", OcarinaTitleId);
        std::filesystem::create_directories(title_dir);
        if (user_mod_word) {
            const auto exefs = title_dir / "UserMod" / "exefs";
            std::filesystem::create_directories(exefs);
            WriteIps(exefs / fmt::format("{}.ips", OcarinaBuildIdHex), *user_mod_word);
        }
        system.SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
        system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
        system.GetFileSystemController().CreateFactories(*system.GetFilesystem());
    }

    ~PatchManagerTestContext() {
        Settings::values.program_args.SetValue(previous_program_args);
        for (size_t i = 0; i < TestDirectories.size(); ++i) {
            Common::FS::SetEdenPath(TestDirectories[i].first, previous[i]);
        }
        Settings::values.disabled_addons.erase(OcarinaTitleId);
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    PatchManagerTestContext(const PatchManagerTestContext&) = delete;
    PatchManagerTestContext& operator=(const PatchManagerTestContext&) = delete;

    FileSys::PatchManager Manager(u64 title_id = OcarinaTitleId) {
        return FileSys::PatchManager{title_id, system.GetFileSystemController(),
                                     system.GetContentProvider()};
    }

    void WriteCheat(std::string_view filename, std::string_view name = "Fixture",
                    std::string_view mod = "CheatMod") {
        const auto dir = root / "load" / fmt::format("{:016X}", OcarinaTitleId) / mod / "cheats";
        std::filesystem::create_directories(dir);
        std::ofstream file{dir / filename, std::ios::binary};
        file << '{' << name << "}\n04000000 00000000 00000001\n";
    }

    void RemoveTitleModDirectory() {
        std::filesystem::remove(root / "load" / fmt::format("{:016X}", OcarinaTitleId));
    }

private:
    const std::string previous_program_args = Settings::values.program_args.GetValue();
    std::array<std::filesystem::path, TestDirectories.size()> previous;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("builtin-patch-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Core::System system;
};

std::vector<u8> MakeMain(const FileSys::PatchManager::BuildID& build_id, u32 word) {
    std::vector<u8> nso(PatchOffset + 0x100);
    Loader::NSOHeader header{};
    header.magic = Common::MakeMagic('N', 'S', 'O', '0');
    header.build_id = build_id;
    std::memcpy(nso.data(), &header, sizeof(header));
    std::memcpy(nso.data() + PatchOffset, &word, sizeof(word));
    return nso;
}

u32 WordAt(const std::vector<u8>& nso) {
    REQUIRE(nso.size() >= PatchOffset + sizeof(u32));
    u32 word;
    std::memcpy(&word, nso.data() + PatchOffset, sizeof(word));
    return word;
}

u32 PatchMain(PatchManagerTestContext& context, const FileSys::PatchManager::BuildID& build_id,
              u32 word, const std::string& name = "main", u64 title_id = OcarinaTitleId) {
    return WordAt(context.Manager(title_id).PatchNSO(MakeMain(build_id, word), name));
}

} // Anonymous namespace

TEST_CASE("Each build-id cheat file is parsed once", "[patch_manager][cheat_files]") {
    PatchManagerTestContext context;
    auto build_id = OcarinaBuildId;
    SECTION("Uppercase filename") {
        context.WriteCheat("E0F3CCFAF50065AE.txt");
    }
    SECTION("Lowercase filename") {
        context.WriteCheat("e0f3ccfaf50065ae.txt");
    }
    SECTION("Numeric build id has identical upper and lower names") {
        build_id = {};
        context.WriteCheat("0000000000000000.txt");
    }
#ifdef _WIN32
    SECTION("Windows accepts a mixed-case filename and extension once") {
        context.WriteCheat("e0F3CcFAf50065Ae.TXT");
    }
#endif
    const auto cheats = context.Manager().CreateCheatList(build_id);
    REQUIRE(cheats.size() == 1);
    CHECK(std::string(cheats[0].definition.readable_name.data()) == "Fixture");
    CHECK(cheats[0].definition.num_opcodes == 3);
    CHECK(cheats[0].definition.opcodes[2] == 1);
}

TEST_CASE("Cheat loading retains mod boundaries and excludes disabled or unrelated files",
          "[patch_manager][cheat_files]") {
    PatchManagerTestContext context;
    context.WriteCheat("E0F3CCFAF50065AE.txt", "First", "A");
    context.WriteCheat("e0f3ccfaf50065ae.txt", "Second", "B");
    context.WriteCheat("FFFFFFFFFFFFFFFF.txt", "Other build", "A");
    context.WriteCheat("E0F3CCFAF50065AE.txt.bak", "Backup", "A");
    auto cheats = context.Manager().CreateCheatList(OcarinaBuildId);
    REQUIRE(cheats.size() == 2);
    CHECK(std::string(cheats[0].definition.readable_name.data()) == "First");
    CHECK(std::string(cheats[1].definition.readable_name.data()) == "Second");
    Settings::values.disabled_addons[OcarinaTitleId] = {"A"};
    cheats = context.Manager().CreateCheatList(OcarinaBuildId);
    REQUIRE(cheats.size() == 1);
    CHECK(std::string(cheats[0].definition.readable_name.data()) == "Second");
}

#ifndef _WIN32
TEST_CASE("Case-sensitive cheat files remain separate and uppercase is loaded first",
          "[patch_manager][cheat_files]") {
    PatchManagerTestContext context;
    context.WriteCheat("E0F3CCFAF50065AE.txt", "Upper");
    context.WriteCheat("e0f3ccfaf50065ae.txt", "Lower");
    const auto cheats = context.Manager().CreateCheatList(OcarinaBuildId);
    REQUIRE(cheats.size() == 2);
    CHECK(std::string(cheats[0].definition.readable_name.data()) == "Upper");
    CHECK(std::string(cheats[1].definition.readable_name.data()) == "Lower");
}
#endif

TEST_CASE("Built-in Max Resolution patch is gated on build id and original word",
          "[patch_manager]") {
    PatchManagerTestContext context;

    SECTION("Matching build id and original word applies") {
        REQUIRE(PatchMain(context, OcarinaBuildId, TbzInstruction) == BranchInstruction);
    }

    SECTION("Wrong original word skips") {
        REQUIRE(PatchMain(context, OcarinaBuildId, NopInstruction) == NopInstruction);
    }

    SECTION("Other build id skips") {
        auto other = OcarinaBuildId;
        other[0] ^= 1;
        REQUIRE(PatchMain(context, other, TbzInstruction) == TbzInstruction);
    }

    SECTION("Other module skips") {
        REQUIRE(PatchMain(context, OcarinaBuildId, TbzInstruction, "sdk") == TbzInstruction);
    }

    SECTION("Other title skips") {
        REQUIRE(PatchMain(context, OcarinaBuildId, TbzInstruction, "main", OtherTitleId) ==
                TbzInstruction);
    }

    SECTION("Disabled in Add-Ons skips") {
        Settings::values.disabled_addons[OcarinaTitleId] = {std::string{MaxResolution}};
        REQUIRE(PatchMain(context, OcarinaBuildId, TbzInstruction) == TbzInstruction);
    }
}

TEST_CASE("Built-in Max Resolution patch defers to a user mod at the same offset",
          "[patch_manager]") {
    SECTION("A different word from the mod is kept") {
        PatchManagerTestContext context{NopInstruction};
        REQUIRE(PatchMain(context, OcarinaBuildId, TbzInstruction) == NopInstruction);
    }

    SECTION("The same word from the mod is applied once") {
        PatchManagerTestContext context{BranchInstruction};
        REQUIRE(PatchMain(context, OcarinaBuildId, TbzInstruction) == BranchInstruction);
    }

    SECTION("Disabling the built-in patch keeps the mod") {
        PatchManagerTestContext context{BranchInstruction};
        Settings::values.disabled_addons[OcarinaTitleId] = {std::string{MaxResolution}};
        REQUIRE(PatchMain(context, OcarinaBuildId, TbzInstruction) == BranchInstruction);
    }
}

TEST_CASE("Built-in Max Resolution patch is listed and loaded for Ocarina's main",
          "[patch_manager]") {
    PatchManagerTestContext context;
    const auto manager = context.Manager();

    REQUIRE(manager.HasNSOPatch(OcarinaBuildId, "main"));
    REQUIRE_FALSE(manager.HasNSOPatch(OcarinaBuildId, "sdk"));
    REQUIRE_FALSE(context.Manager(OtherTitleId).HasNSOPatch(OcarinaBuildId, "main"));

    const auto builtin_enabled = [&] {
        const auto patches = manager.GetPatches();
        const auto it = std::ranges::find(patches, MaxResolution, &FileSys::Patch::name);
        REQUIRE(it != patches.end());
        REQUIRE(it->type == FileSys::PatchType::Mod);
        return it->enabled;
    };
    REQUIRE(builtin_enabled());

    Settings::values.disabled_addons[OcarinaTitleId] = {std::string{MaxResolution}};
    REQUIRE_FALSE(builtin_enabled());
}

TEST_CASE("Adaptive timing is a separately switchable default-on Add-On",
          "[patch_manager][adaptive_timing]") {
    PatchManagerTestContext context;
    const auto manager = context.Manager();
    const auto find_timing = [&] {
        const auto patches = manager.GetPatches();
        const auto it = std::ranges::find(patches, FileSys::BuiltinAdaptiveTiming::Name,
                                          &FileSys::Patch::name);
        REQUIRE(it != patches.end());
        CHECK(it->version == "Built-in / exact build");
        CHECK(it->type == FileSys::PatchType::Mod);
        return it->enabled;
    };
    CHECK(find_timing());
    Settings::values.disabled_addons[OcarinaTitleId] = {
        std::string{FileSys::BuiltinAdaptiveTiming::Name}};
    CHECK_FALSE(find_timing());
    const auto patches = manager.GetPatches();
    const auto max_resolution = std::ranges::find(patches, MaxResolution, &FileSys::Patch::name);
    REQUIRE(max_resolution != patches.end());
    CHECK(max_resolution->enabled);
    const auto other = context.Manager(OtherTitleId).GetPatches();
    CHECK(std::ranges::find(other, FileSys::BuiltinAdaptiveTiming::Name,
                           &FileSys::Patch::name) == other.end());
}

TEST_CASE("PatchNSO wires adaptive timing guards and external patch precedence",
          "[patch_manager][adaptive_timing]") {
    using namespace AdaptiveTimingTest;
    auto input = Fixture();
    Put32(input, PatchOffset, TbzInstruction);
    constexpr auto hook = Timing::HeaderSize + Timing::HookOffset;
    constexpr auto cave = Timing::HeaderSize + Timing::CaveOffset;
    const auto check_skipped = [&](const std::vector<u8>& result) {
        CHECK(Get32(result, hook) == 0x1E201828);
        CHECK(std::equal(input.begin() + cave, input.begin() + cave + Timing::CaveSize,
                         result.begin() + cave));
    };
    const auto check_applied = [&](const std::vector<u8>& result) {
        CHECK(Get32(result, hook) == 0x14D57D4F);
        CHECK(Get32(result, cave) == 0x1E201828);
        CHECK(Get32(result, cave + 0xA8) == 0x172A8288);
    };

    SECTION("Default application coexists with Max Resolution") {
        PatchManagerTestContext context;
        const auto result = context.Manager().PatchNSO(input, "main");
        check_applied(result);
        CHECK(Get32(result, PatchOffset) == BranchInstruction);
    }
    SECTION("Missing title mod directory still permits built-ins") {
        PatchManagerTestContext context;
        context.RemoveTitleModDirectory();
        check_applied(context.Manager().PatchNSO(input, "main"));
    }
    SECTION("Disabled timing preserves Max Resolution") {
        PatchManagerTestContext context;
        Settings::values.disabled_addons[OcarinaTitleId] = {std::string{Timing::Name}};
        const auto result = context.Manager().PatchNSO(input, "main");
        check_skipped(result);
        CHECK(Get32(result, PatchOffset) == BranchInstruction);
    }
    SECTION("Program arguments refuse timing without affecting Max Resolution") {
        PatchManagerTestContext context;
        Settings::values.program_args.SetValue("synthetic-test-argument");
        check_skipped(context.Manager().PatchNSO(input, "main"));
    }
    SECTION("Enabled non-overlapping external IPS conservatively takes precedence") {
        PatchManagerTestContext context{NopInstruction};
        const auto result = context.Manager().PatchNSO(input, "main");
        check_skipped(result);
        CHECK(Get32(result, PatchOffset) == NopInstruction);
    }
    SECTION("Disabled external IPS does not suppress timing") {
        PatchManagerTestContext context{NopInstruction};
        Settings::values.disabled_addons[OcarinaTitleId] = {"UserMod"};
        const auto result = context.Manager().PatchNSO(input, "main");
        check_applied(result);
        CHECK(Get32(result, PatchOffset) == BranchInstruction);
    }
}
