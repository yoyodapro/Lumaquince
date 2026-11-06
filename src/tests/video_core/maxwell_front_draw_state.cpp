// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <stdexcept>
#include <tuple>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "core/core.h"
#include "core/device_memory.h"
#include "tests/core/system_test_context.h"
#include "video_core/dirty_flags.h"
#include "video_core/engines/maxwell_3d_front_state.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"

namespace {
using Tegra::Engines::Maxwell3D;
using Tegra::Engines::Maxwell3DFrontDrawState;
using Mode = Maxwell3D::DrawManager::DrawMode;
using Shadow = Maxwell3D::Regs::ShadowRamControl;
constexpr u32 Begin = MAXWELL3D_REG_INDEX(draw.begin);
constexpr u32 End = MAXWELL3D_REG_INDEX(draw.end);
constexpr u32 First = MAXWELL3D_REG_INDEX(vertex_buffer.first);
constexpr u32 Count = MAXWELL3D_REG_INDEX(vertex_buffer.count);
constexpr u32 Control = MAXWELL3D_REG_INDEX(shadow_ram_control);

class DrawRecorder final : public VideoCore::RasterizerInterface {
public:
    using Event = std::tuple<bool, u32, u32, u32, u32, Mode>;
    explicit DrawRecorder(Maxwell3D& engine_) : engine{engine_} {}
    std::vector<Event> events;
    void Draw(bool indexed, u32 instances) override {
        const auto& s = engine.draw_manager.draw_state;
        events.emplace_back(indexed, instances, s.base_instance,
                            indexed ? s.index_buffer.first : s.vertex_buffer.first,
                            s.draw_element_count, s.draw_mode);
    }
    void DrawTexture() override { throw std::logic_error("Unexpected DrawTexture"); }
    void Clear(u32) override {}
    void DispatchCompute() override {}
    void ResetCounter(VideoCommon::QueryType) override {}
    void Query(GPUVAddr, VideoCommon::QueryType, VideoCommon::QueryPropertiesFlags, u32, u32) override {}
    void BindGraphicsUniformBuffer(size_t, u32, GPUVAddr, u32) override {}
    void DisableGraphicsUniformBuffer(size_t, u32) override {}
    void SignalFence(std::function<void()>&&) override {}
    void SyncOperation(std::function<void()>&&) override {}
    void SignalSyncPoint(u32) override {}
    void SignalReference() override {}
    void ReleaseFences(bool) override {}
    void FlushAll() override {}
    void FlushRegion(DAddr, u64, VideoCommon::CacheType) override {}
    bool MustFlushRegion(DAddr, u64, VideoCommon::CacheType) override { return false; }
    VideoCore::RasterizerDownloadArea GetFlushArea(DAddr, u64) override { return {}; }
    void InvalidateRegion(DAddr, u64, VideoCommon::CacheType) override {}
    void OnCacheInvalidation(PAddr, u64) override {}
    bool OnCPUWrite(PAddr, u64) override { return false; }
    void InvalidateGPUCache() override {}
    void UnmapMemory(DAddr, u64) override {}
    void ModifyGPUMemory(size_t, GPUVAddr, u64) override {}
    void FlushAndInvalidateRegion(DAddr, u64, VideoCommon::CacheType) override {}
    void WaitForIdle() override {}
    void FragmentBarrier() override {}
    void TiledCacheBarrier() override {}
    void FlushCommands() override {}
    void TickFrame() override {}
    Tegra::Engines::AccelerateDMAInterface& AccessAccelerateDMA() override {
        throw std::logic_error("Unexpected DMA");
    }
    void AccelerateInlineToMemory(GPUVAddr, size_t, std::span<const u8>) override {}
private:
    Maxwell3D& engine;
};

struct Fixture {
    explicit Fixture(Tegra::MemoryManager& memory, Mode mode)
        : engine{memory}, recorder{engine} {
        engine.BindRasterizer(&recorder);
        VideoCommon::Dirty::SetupDirtyFlags(engine.dirty.tables);
        engine.draw_manager.draw_state.draw_mode = mode;
        front.mode = engine.draw_manager.draw_state.draw_mode;
        registers = engine.regs.reg_array;
        shadow = engine.shadow_state.reg_array;
        control = engine.shadow_state.shadow_ram_control;
    }

    // The production helper consumes effective values, as FrontWrite/FrontCallMultiMethod do.
    // Forwarded words run the real CallMethod/CallMultiMethod and DrawManager, with a recorder.
    bool Observe(u32 method, u32 argument, bool filter) {
        u32 value = argument;
        bool shadow_kept = true;
        if (control == Shadow::Track || control == Shadow::TrackWithFilter) {
            shadow_kept = shadow[method] == argument;
            shadow[method] = argument;
        } else if (control == Shadow::Replay) {
            value = shadow[method];
        }
        front.Observe(method, value);
        const bool skip = filter && front.CanSkip(method, registers[method], value, shadow_kept, true);
        registers[method] = value;
        if (method == Control) {
            control = static_cast<Shadow>(argument);
            shadow[method] = argument;
        }
        skipped += skip;
        return skip;
    }
    void Scalar(Core::System& system, u32 method, u32 argument, bool filter) {
        if (!Observe(method, argument, filter)) {
            engine.CallMethod(system, method, argument, true);
        }
        REQUIRE(front.mode == engine.draw_manager.draw_state.draw_mode);
    }
    void Batch(Core::System& system, u32 method, std::span<const u32> words) {
        for (u32 word : words) {
            Observe(method, word, false);
        }
        // Empty inline packets have no mode effect in ProcessInlineIndexMultiData.
        const u32 dummy{};
        engine.CallMultiMethod(system, method, words.empty() ? &dummy : words.data(),
                               static_cast<u32>(words.size()), static_cast<u32>(words.size()));
        REQUIRE(front.mode == engine.draw_manager.draw_state.draw_mode);
    }
    void Boundary() {
        engine.draw_manager.DrawDeferred(engine);
        REQUIRE(front.mode == engine.draw_manager.draw_state.draw_mode);
    }
    Maxwell3D engine;
    DrawRecorder recorder;
    Maxwell3DFrontDrawState front;
    std::array<u32, Maxwell3D::Regs::NUM_REGS> registers;
    std::array<u32, Maxwell3D::Regs::NUM_REGS> shadow;
    Shadow control;
    u32 skipped{};
};

void Compare(const Fixture& baseline, const Fixture& filtered) {
    REQUIRE(baseline.recorder.events == filtered.recorder.events);
    REQUIRE(baseline.engine.regs.reg_array == filtered.engine.regs.reg_array);
    REQUIRE(baseline.engine.shadow_state.reg_array == filtered.engine.shadow_state.reg_array);
    const auto& a = baseline.engine.draw_manager.draw_state;
    const auto& b = filtered.engine.draw_manager.draw_state;
    REQUIRE(a.draw_mode == b.draw_mode);
    REQUIRE(a.instance_count == b.instance_count);
    REQUIRE(a.drawn_instances == b.drawn_instances);
    REQUIRE(a.draw_indexed == b.draw_indexed);
    REQUIRE(a.inline_index_draw_indexes == b.inline_index_draw_indexes);
}
} // namespace

TEST_CASE("Front draw filter preserves real draw events through scalar and batched transitions",
          "[maxwell_front_draw_state]") {
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    Tegra::MemoryManager memory{system, device_memory, 32, 1ULL << 31};
    for (Mode initial : {Mode::General, Mode::Instance, Mode::InlineIndex}) {
        for (Shadow tracking : {Shadow::Track, Shadow::TrackWithFilter, Shadow::Passthrough}) {
            CAPTURE(initial, tracking);
            Fixture baseline{memory, initial}, filtered{memory, initial};
            const auto scalar = [&](u32 method, u32 value) {
                baseline.Scalar(system, method, value, false);
                filtered.Scalar(system, method, value, true);
                Compare(baseline, filtered);
            };
            const auto batch = [&](u32 method, std::initializer_list<u32> values) {
                baseline.Batch(system, method, values);
                filtered.Batch(system, method, values);
                Compare(baseline, filtered);
            };
            scalar(Control, static_cast<u32>(tracking));
            scalar(End, 0); // Includes taking over a previously active instance loop.
            scalar(Control, static_cast<u32>(Shadow::Passthrough));
            scalar(Count, 17);
            scalar(Control, static_cast<u32>(Shadow::Track));
            const u32 skipped_before_shadow_update = filtered.skipped;
            scalar(Count, 17); // Same register, different shadow: must still reach the back.
            REQUIRE(filtered.skipped == skipped_before_shadow_update);
            scalar(Control, static_cast<u32>(tracking));
            scalar(Begin, 0);
            scalar(First, 4);
            scalar(Count, 6);
            scalar(End, 0);
            scalar(Begin, 1U << 26);
            scalar(Count, 6);
            scalar(End, 0);
            scalar(End, 1); // Changed values must be forwarded even in Instance.
            scalar(End, 1);
            batch(Begin, {1U << 26, 2U << 26, 3U << 26});
            scalar(End, 1);
            baseline.Boundary();
            filtered.Boundary();
            Compare(baseline, filtered);
            batch(Begin, {0}); // Regression: batched First must leave Instance.
            scalar(End, 1);
            for (u32 inline_method : {u32(MAXWELL3D_REG_INDEX(draw_inline_index)),
                                      u32(MAXWELL3D_REG_INDEX(inline_index_2x16.even)),
                                      u32(MAXWELL3D_REG_INDEX(inline_index_4x8.index0))}) {
                scalar(Begin, 1U << 26);
                batch(inline_method, {});
                scalar(End, 1);
                batch(inline_method, {0x01020304});
                batch(Begin, {2U << 26, 3U << 26});
                scalar(End, 1); // Inline mode draws after Unchanged/reserved Begin too.
                scalar(Begin, 1U << 26);
                scalar(inline_method, 0x05060708);
                scalar(End, 1);
            }
            for (u32 method : {u32(MAXWELL3D_REG_INDEX(vertex_array_instance_first)),
                                u32(MAXWELL3D_REG_INDEX(vertex_array_instance_subsequent))}) {
                scalar(method, 0);
                scalar(End, 1);
                batch(method, {0, 0});
                scalar(End, 1);
            }
            scalar(Control, static_cast<u32>(Shadow::Track));
            scalar(Begin, 0);
            scalar(Control, static_cast<u32>(Shadow::Replay));
            batch(Begin, {1U << 26}); // Effective First, despite Subsequent argument.
            scalar(End, 99);
            scalar(Control, static_cast<u32>(Shadow::TrackWithFilter));
            scalar(Begin, 1U << 26);
            scalar(Control, static_cast<u32>(Shadow::Replay));
            scalar(Begin, 0); // Effective Subsequent, despite First argument.
            scalar(End, 99);
            baseline.Boundary();
            filtered.Boundary();
            Compare(baseline, filtered);
            baseline.engine.dirty.flags.reset();
            filtered.engine.dirty.flags.reset();
            scalar(MAXWELL3D_REG_INDEX(index_buffer.first), 0);
            REQUIRE(baseline.engine.dirty.flags[VideoCommon::Dirty::IndexBuffer]);
            REQUIRE(filtered.engine.dirty.flags[VideoCommon::Dirty::IndexBuffer]);
            REQUIRE(filtered.skipped > 0);
        }
    }
}

TEST_CASE("Scalar and batched draw packets produce the same recorded draws",
          "[maxwell_front_draw_state]") {
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    Tegra::MemoryManager memory{system, device_memory, 32, 1ULL << 31};
    Fixture scalar{memory, Mode::General}, batched{memory, Mode::General};
    const auto packet = [&](u32 method, std::initializer_list<u32> words) {
        for (u32 word : words) {
            scalar.Scalar(system, method, word, true);
        }
        batched.Batch(system, method, words);
        Compare(scalar, batched);
    };
    packet(Begin, {0});
    packet(Count, {6, 6});
    packet(End, {0});
    packet(Begin, {1U << 26, 1U << 26, 2U << 26, 3U << 26});
    packet(End, {0, 0});
    scalar.Boundary();
    batched.Boundary();
    Compare(scalar, batched);
    packet(Begin, {0});
    packet(End, {0});
    for (u32 method : {u32(MAXWELL3D_REG_INDEX(draw_inline_index)),
                        u32(MAXWELL3D_REG_INDEX(inline_index_2x16.even)),
                        u32(MAXWELL3D_REG_INDEX(inline_index_4x8.index0))}) {
        packet(Begin, {1U << 26});
        packet(method, {0x01020304, 0x05060708});
        packet(End, {0});
    }
    for (u32 method : {u32(MAXWELL3D_REG_INDEX(vertex_array_instance_first)),
                        u32(MAXWELL3D_REG_INDEX(vertex_array_instance_subsequent))}) {
        packet(method, {0, 0});
        packet(End, {0});
    }
    packet(Control, {static_cast<u32>(Shadow::Track)});
    packet(Begin, {0});
    packet(Control, {static_cast<u32>(Shadow::Replay)});
    packet(Begin, {1U << 26, 2U << 26});
    packet(End, {0});
    scalar.Boundary();
    batched.Boundary();
    Compare(scalar, batched);
}

TEST_CASE("Front filter keeps shadow updates and index-buffer dirty effects",
          "[maxwell_front_draw_state]") {
    Maxwell3DFrontDrawState front{Mode::Instance};
    for (u32 method : {End, First, Count}) {
        REQUIRE(front.CanSkip(method, 7, 7, true, true));
        REQUIRE_FALSE(front.CanSkip(method, 7, 7, false, true));
        REQUIRE_FALSE(front.CanSkip(method, 7, 8, true, true));
        REQUIRE_FALSE(front.CanSkip(method, 7, 7, true, false));
    }
    REQUIRE_FALSE(front.CanSkip(MAXWELL3D_REG_INDEX(index_buffer.first), 7, 7, true, true));
    for (Mode mode : {Mode::General, Mode::InlineIndex}) {
        front.mode = mode;
        REQUIRE_FALSE(front.CanSkip(End, 7, 7, true, true));
    }
}

TEST_CASE("Front filtering preserves deferred instance counts and base instances",
          "[maxwell_front_draw_state]") {
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    Tegra::MemoryManager memory{system, device_memory, 32, 1ULL << 31};
    Fixture baseline{memory, Mode::General}, filtered{memory, Mode::General};
    for (Fixture* fixture : {&baseline, &filtered}) {
        const bool filter = fixture == &filtered;
        fixture->Scalar(system, Begin, 0, filter);
        fixture->Scalar(system, Count, 6, filter);
        fixture->Scalar(system, End, 0, filter);
        fixture->Scalar(system, Begin, 1U << 26, filter);
        fixture->Scalar(system, End, 0, filter);
        fixture->Scalar(system, Begin, 1U << 26, filter);
        fixture->Scalar(system, End, 0, filter);
        // An ordinary state write flushes a pending loop while retaining its mode.
        fixture->Scalar(system, MAXWELL3D_REG_INDEX(global_base_vertex_index), 8, filter);
        fixture->Scalar(system, Begin, 1U << 26, filter);
        fixture->Scalar(system, End, 0, filter);
        fixture->Boundary();
        fixture->Boundary(); // No extra instances at an empty macro boundary.
    }
    Compare(baseline, filtered);
    const auto& events = filtered.recorder.events;
    REQUIRE(events.size() == 3);
    REQUIRE(std::get<1>(events[0]) == 1);
    REQUIRE(std::get<1>(events[1]) == 3);
    REQUIRE(std::get<1>(events[2]) == 1);
    REQUIRE(std::get<2>(events[2]) == 3);
    REQUIRE(filtered.engine.draw_manager.draw_state.instance_count == 0);
}
