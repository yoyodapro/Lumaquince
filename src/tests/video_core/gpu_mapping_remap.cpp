// SPDX-License-Identifier: GPL-3.0-or-later

#include <initializer_list>
#include <stdexcept>
#include <tuple>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "core/core.h"
#include "core/device_memory.h"
#include "tests/core/system_test_context.h"
#include "video_core/buffer_cache/virtual_range_cache.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"

namespace {

using Notification = std::tuple<size_t, GPUVAddr, u64>;

class MappingRecorder final : public VideoCore::RasterizerInterface {
public:
    VideoCommon::VirtualRangeCache ranges;
    std::vector<Notification> notifications;

    void ModifyGPUMemory(size_t owner, GPUVAddr address, u64 size) override {
        notifications.emplace_back(owner, address, size);
        // The Vulkan rasterizer forwards this notification through BufferCache::UnmapGPUMemory.
        ranges.Unmap(owner, address, size);
    }

    void Draw(bool, u32) override {}
    void DrawTexture() override {}
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
    void FlushAndInvalidateRegion(DAddr, u64, VideoCommon::CacheType) override {}
    void WaitForIdle() override {}
    void FragmentBarrier() override {}
    void TiledCacheBarrier() override {}
    void FlushCommands() override {}
    void TickFrame() override {}
    Tegra::Engines::AccelerateDMAInterface& AccessAccelerateDMA() override {
        throw std::logic_error("Unexpected DMA in GPU mapping test");
    }
    void AccelerateInlineToMemory(GPUVAddr, size_t, std::span<const u8>) override {}
};

struct Fixture {
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    MappingRecorder recorder;
    Tegra::MemoryManager memory{system, device_memory, 32, 1ULL << 31, 16};

    Fixture() {
        memory.BindRasterizer(&recorder);
    }

    void CheckNotifications(std::initializer_list<Notification> expected) const {
        const std::vector<Notification> values{expected};
        CHECK(recorder.notifications == values);
    }

    void CheckSegments(GPUVAddr address, u32 size,
                       std::initializer_list<VideoCommon::VirtualSegment> expected) {
        const auto* segments = recorder.ranges.Query(memory, address, size);
        REQUIRE(segments != nullptr);
        REQUIRE(segments->size() == expected.size());
        size_t index = 0;
        for (const auto& value : expected) {
            const auto& actual = (*segments)[index++];
            REQUIRE(actual.gpu_addr == value.gpu_addr);
            REQUIRE(actual.device_addr == value.device_addr);
            REQUIRE(actual.size == value.size);
            REQUIRE(memory.GpuToCpuAddress(actual.gpu_addr) == value.device_addr);
            REQUIRE(memory.GpuToCpuAddress(actual.gpu_addr + actual.size - 1) ==
                    value.device_addr + value.size - 1);
        }
    }
};

} // namespace

TEST_CASE("Physical GPU remaps invalidate cached virtual segments without an unmap",
          "[video_core][gpu_mapping_remap]") {
    const bool big = GENERATE(false, true);
    CAPTURE(big);
    Fixture f;
    const u32 page = big ? 0x10000 : 0x1000;
    constexpr GPUVAddr gpu = 0x200000;
    constexpr DAddr a = 0x10000;
    constexpr DAddr b = 0x30000;
    constexpr DAddr c = 0x50000;
    const auto owner = f.memory.GetID();
    const auto map = [&](GPUVAddr address, DAddr device, u32 size) {
        f.memory.Map(address, device, size, Tegra::PTEKind::PITCH, big);
    };

    // Translation-only test: no Vulkan device or host backing bytes are needed.
    map(gpu, a, page);
    map(gpu + page, b, page);
    f.CheckSegments(gpu, 2 * page, {{gpu, a, page}, {gpu + page, b, page}});
    f.recorder.notifications.clear();

    map(gpu + page, c, page);
    REQUIRE(f.memory.GpuToCpuAddress(gpu + page) == c);
    f.CheckNotifications({{owner, gpu + page, page}});
    f.CheckSegments(gpu, 2 * page, {{gpu, a, page}, {gpu + page, c, page}});
    f.recorder.notifications.clear();

    map(gpu + page, c, page);
    f.CheckNotifications({});
    f.CheckSegments(gpu, 2 * page, {{gpu, a, page}, {gpu + page, c, page}});

    map(gpu + page, a + page, page);
    f.CheckNotifications({{owner, gpu + page, page}});
    f.CheckSegments(gpu, 2 * page, {{gpu, a, 2 * page}});
    f.recorder.notifications.clear();

    f.memory.MapSparse(gpu + page, page, big);
    f.CheckNotifications({{owner, gpu + page, page}});
    REQUIRE_FALSE(f.memory.GpuToCpuAddress(gpu + page));
    f.CheckSegments(gpu, 2 * page, {});
    f.recorder.notifications.clear();

    f.memory.MapSparse(gpu + page, page, big);
    f.CheckNotifications({});
    f.CheckSegments(gpu, 2 * page, {});

    map(gpu + page, c, page);
    f.CheckNotifications({{owner, gpu + page, page}});
    f.CheckSegments(gpu, 2 * page, {{gpu, a, page}, {gpu + page, c, page}});
}

TEST_CASE("Physical GPU remap notifications coalesce changed pages across unchanged gaps",
          "[video_core][gpu_mapping_remap]") {
    const bool big = GENERATE(false, true);
    CAPTURE(big);
    Fixture f;
    const u32 page = big ? 0x10000 : 0x1000;
    constexpr GPUVAddr gpu = 0x200000;
    constexpr DAddr target = 0x100000;
    constexpr DAddr old_first = 0x400000;
    constexpr DAddr old_middle = 0x600000;
    const auto owner = f.memory.GetID();
    const auto map = [&](GPUVAddr address, DAddr device, u32 size) {
        f.memory.Map(address, device, size, Tegra::PTEKind::PITCH, big);
    };

    map(gpu, old_first, page);
    map(gpu + page, target + page, page);
    map(gpu + 2 * page, old_middle, 2 * page);
    map(gpu + 4 * page, target + 4 * page, page);
    f.CheckSegments(gpu, 5 * page,
                    {{gpu, old_first, page}, {gpu + page, target + page, page},
                     {gpu + 2 * page, old_middle, 2 * page},
                     {gpu + 4 * page, target + 4 * page, page}});
    f.recorder.notifications.clear();

    map(gpu, target, 5 * page);
    f.CheckNotifications({{owner, gpu, page}, {owner, gpu + 2 * page, 2 * page}});
    f.CheckSegments(gpu, 5 * page, {{gpu, target, 5 * page}});
    f.recorder.notifications.clear();

    map(gpu, target, 5 * page);
    f.CheckNotifications({});
    f.CheckSegments(gpu, 5 * page, {{gpu, target, 5 * page}});
}
