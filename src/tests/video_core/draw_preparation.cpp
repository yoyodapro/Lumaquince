// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <sstream>
#include <vector>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "common/page_table.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/memory.h"
#include "tests/core/system_test_context.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/buffer_cache/memory_tracker_base.h"
#include "video_core/control/channel_state.h"
#include "video_core/gpu.h"
#include "video_core/renderer_null/null_rasterizer.h"

namespace {

struct Runtime;

struct Buffer : VideoCommon::BufferBase {
    Buffer(Runtime&, VideoCommon::NullBufferParams params) : BufferBase{params} {}
    Buffer(Runtime&, DAddr address, u32 size, bool mapped)
        : BufferBase{address, size}, bytes(size), host_mapped{mapped} {}

    void MarkUsage(u64 offset, u64 size) {
        REQUIRE(offset + size <= bytes.size());
    }
    void ImmediateUpload(u64 offset, std::span<const u8> data) {
        REQUIRE(offset + data.size() <= bytes.size());
        std::memcpy(bytes.data() + offset, data.data(), data.size());
    }
    void ImmediateDownload(u64 offset, std::span<u8> data) {
        REQUIRE(offset + data.size() <= bytes.size());
        std::memcpy(data.data(), bytes.data() + offset, data.size());
        ++immediate_downloads;
    }
    std::span<u8> Mapped() {
        return host_mapped ? std::span{bytes} : std::span<u8>{};
    }
    u64 LastUsageTick() const {
        return tick;
    }

    std::vector<u8> bytes;
    bool host_mapped{};
    u64 tick{};
    u32 immediate_downloads{};
};

struct Staging {
    Buffer* buffer;
    u64 offset;
    std::span<u8> mapped_span;
};

struct Runtime {
    bool CanReportMemoryUsage() const {
        return false;
    }
    u64 GetDeviceLocalMemory() const {
        return 0;
    }
    u32 GetStorageBufferAlignment() const {
        return alignment;
    }
    u32 GetUniformBufferAlignment() const {
        return uniform_alignment;
    }
    bool IsFree(u64) const {
        return true;
    }
    void WaitSubmittedTick(u64) {}
    bool CanReorderUpload(const Buffer&, std::span<VideoCommon::BufferCopy>) const {
        return false;
    }
    Staging DownloadStagingBuffer(u64 size, bool = false) {
        REQUIRE(size <= 0xffff'ffffULL - 128);
        staging = std::make_unique<Buffer>(*this, 0, static_cast<u32>(size + 128), false);
        ++allocations;
        return {staging.get(), 128, std::span{staging->bytes}.subspan(128)};
    }
    Staging UploadStagingBuffer(u64 size) {
        return DownloadStagingBuffer(size);
    }
    void ClearBuffer(Buffer& buffer, u32 offset, u64 size, u32 value) {
        REQUIRE(offset + size <= buffer.bytes.size());
        REQUIRE(size % sizeof(value) == 0);
        for (u64 index = 0; index < size; index += sizeof(value)) {
            std::memcpy(buffer.bytes.data() + offset + index, &value, sizeof(value));
        }
    }
    void CopyBuffer(Buffer* dst, Buffer& src, std::span<const VideoCommon::BufferCopy> copies,
                    bool) {
        for (const auto& copy : copies) {
            REQUIRE(copy.src_offset + copy.size <= src.bytes.size());
            REQUIRE(copy.dst_offset + copy.size <= dst->bytes.size());
            pending.push_back([dst, &src, copy] {
                std::memcpy(dst->bytes.data() + copy.dst_offset, src.bytes.data() + copy.src_offset,
                            copy.size);
            });
            ++downloads;
            download_bytes += copy.size;
        }
    }
    void Finish() {
        if (before_completion) {
            before_completion();
        }
        for (auto& copy : pending) {
            copy();
        }
        pending.clear();
        ++finishes;
    }
    void Wait(u64 tick) {
        if (before_completion) {
            before_completion();
        }
        waited_tick = tick;
        ++waits;
    }
    void CopyBuffer(Buffer& dst, Buffer* src, std::span<const VideoCommon::BufferCopy> copies, bool,
                    bool) {
        for (const auto& copy : copies) {
            REQUIRE(copy.src_offset + copy.size <= src->bytes.size());
            REQUIRE(copy.dst_offset + copy.size <= dst.bytes.size());
            std::memcpy(dst.bytes.data() + copy.dst_offset, src->bytes.data() + copy.src_offset,
                        copy.size);
            ++uploads;
            upload_bytes += copy.size;
        }
    }
    void CopyBuffer(Buffer& dst, Buffer& src, std::span<const VideoCommon::BufferCopy> copies,
                    bool barrier) {
        CopyBuffer(dst, &src, copies, barrier, false);
    }
    void BindUniformBuffer(Buffer& buffer, u32 offset, u32 size) {
        REQUIRE(offset + size <= buffer.bytes.size());
        uniform_bindings.push_back({std::span{buffer.bytes}.subspan(offset, size), false});
    }
    std::span<u8> BindMappedUniformBuffer(size_t, u32, u32 size) {
        auto& stream = uniform_streams.emplace_back(size);
        uniform_bindings.push_back({stream, true});
        return stream;
    }

    struct UniformBinding {
        std::span<const u8> bytes;
        bool streamed;
    };
    u32 alignment{256};
    u32 uniform_alignment{1};
    u32 allocations{}, uploads{};
    u64 upload_bytes{};
    u32 downloads{}, finishes{}, waits{};
    u64 download_bytes{}, waited_tick{};
    std::unique_ptr<Buffer> staging;
    std::vector<std::vector<u8>> uniform_streams;
    std::vector<UniformBinding> uniform_bindings;
    std::vector<std::function<void()>> pending;
    std::function<void()> before_completion;
};

struct Tracker : VideoCommon::MemoryTrackerBase<Tegra::MaxwellDeviceMemoryManager> {
    using MemoryTrackerBase::MemoryTrackerBase;
    void MarkRegionAsCpuModified(DAddr address, u64 size) {
        ++cpu_marks;
        MemoryTrackerBase::MarkRegionAsCpuModified(address, size);
    }
    u32 cpu_marks{};
};

struct Policy {
    using Runtime = ::Runtime;
    using Buffer = ::Buffer;
    using Async_Buffer = Staging;
    using MemoryTracker = Tracker;
    static constexpr bool IS_OPENGL = false;
    static constexpr bool HAS_PERSISTENT_UNIFORM_BUFFER_BINDINGS = false;
    static constexpr bool HAS_FULL_INDEX_AND_PRIMITIVE_SUPPORT = false;
    static constexpr bool NEEDS_BIND_UNIFORM_INDEX = false;
    static constexpr bool NEEDS_BIND_STORAGE_INDEX = false;
    static constexpr bool USE_MEMORY_MAPS = true;
    static constexpr bool SEPARATE_IMAGE_BUFFER_BINDINGS = false;
    static constexpr bool USE_MEMORY_MAPS_FOR_UPLOADS = true;
};

using Cache = VideoCommon::BufferCache<Policy>;
struct ImmediatePolicy : Policy {
    static constexpr bool USE_MEMORY_MAPS_FOR_UPLOADS = false;
};
using ImmediateCache = VideoCommon::BufferCache<ImmediatePolicy>;
struct ImmediateDownloadPolicy : Policy {
    static constexpr bool USE_MEMORY_MAPS = false;
};
using ImmediateDownloadCache = VideoCommon::BufferCache<ImmediateDownloadPolicy>;

} // namespace

namespace VideoCommon {

struct BufferCacheTestAccess {
    static Binding Bind(Cache& cache, Tegra::MemoryManager& memory, GPUVAddr descriptor,
                        bool written = false, bool native = false, bool table = false,
                        bool mapping_size = true) {
        cache.gpu_memory = &memory;
        return cache.StorageBufferBinding(descriptor, 0, written, native, table, mapping_size);
    }
    template <class TestCache>
    static BufferId Add(TestCache& cache, DAddr address, u32 size, bool mapped = false) {
        const auto id = cache.slot_buffers.insert(cache.runtime, address, size, mapped);
        cache.Register(id);
        cache.memory_tracker.ForEachUploadRange(address, size, [](auto, auto) {});
        return id;
    }
    template <class TestCache>
    static Buffer& Get(TestCache& cache, BufferId id) {
        return cache.slot_buffers[id];
    }
    template <class TestCache>
    static void Dirty(TestCache& cache, DAddr address, u64 size) {
        cache.memory_tracker.MarkRegionAsGpuModified(address, size);
        cache.gpu_modified_ranges.Add(address, size);
        cache.uncommitted_gpu_modified_ranges.Add(address, size);
        cache.committed_gpu_modified_ranges.emplace_back().Add(address, size);
        cache.pending_download_ranges.emplace_back().Add(address, size);
    }
    template <class TestCache>
    static bool Pending(TestCache& cache, DAddr address, u64 size) {
        if (cache.gpu_modified_ranges.Intersects(address, size) ||
            cache.uncommitted_gpu_modified_ranges.Intersects(address, size)) {
            return true;
        }
        for (const auto& ranges : cache.committed_gpu_modified_ranges) {
            if (ranges.Intersects(address, size)) {
                return true;
            }
        }
        for (const auto& ranges : cache.pending_download_ranges) {
            if (ranges.Intersects(address, size)) {
                return true;
            }
        }
        return false;
    }
    static void Download(Cache& cache, std::span<const std::pair<DAddr, u64>> ranges) {
        cache.DownloadRangesMemory(ranges);
    }
    static void EvictionDownload(Cache& cache, std::span<const VideoCommon::BufferId> ids) {
        cache.DownloadBuffersMemory(ids);
    }
    static void Synchronize(Cache& cache, BufferId id, DAddr address, u32 size) {
        cache.SynchronizeBuffer(cache.slot_buffers[id], address, size);
    }
    template <class TestCache>
    static void Uniform(TestCache& cache, BufferCacheChannelInfo& channel, BufferId id, DAddr address,
                        u32 size) {
        cache.channel_state = &channel;
        cache.maxwell3d = &channel.maxwell3d;
        cache.gpu_memory = &channel.gpu_memory;
        channel.uniform_buffers[0][0] = {.device_addr = address, .size = size, .buffer_id = id};
        channel.enabled_uniform_buffer_masks[0] = 1;
        cache.UpdateUniformBuffers(0);
        cache.BindHostGraphicsUniformBuffer(0, 0, 0, true);
    }
    static void ComputeUniforms(Cache& cache, BufferCacheChannelInfo& channel) {
        cache.channel_state = &channel;
        cache.maxwell3d = &channel.maxwell3d;
        cache.kepler_compute = &channel.kepler_compute;
        cache.gpu_memory = &channel.gpu_memory;
        auto& launch = channel.kepler_compute.launch_description;
        launch.const_buffer_enable_mask.Assign(channel.enabled_compute_uniform_buffer_mask);
        for (u32 index = 0; index < NUM_COMPUTE_UNIFORM_BUFFERS; ++index) {
            if ((channel.enabled_compute_uniform_buffer_mask & (1U << index)) == 0) {
                continue;
            }
            const Binding& binding = channel.compute_uniform_buffers[index];
            auto& config = launch.const_buffer_config[index];
            config.address_low = static_cast<u32>(binding.device_addr);
            config.address_high.Assign(static_cast<u32>(binding.device_addr >> 32));
            config.size.Assign(binding.size);
        }
        cache.UpdateComputeUniformBuffers();
        cache.BindHostComputeUniformBuffers();
    }
    static void GraphicsUniforms(Cache& cache, BufferCacheChannelInfo& channel) {
        cache.channel_state = &channel;
        cache.maxwell3d = &channel.maxwell3d;
        cache.gpu_memory = &channel.gpu_memory;
        cache.UpdateUniformBuffers(0);
        cache.BindHostGraphicsUniformBuffers(0);
    }
    static void CpuDirty(Cache& cache, DAddr address, u64 size) {
        cache.memory_tracker.MarkRegionAsCpuModified(address, size);
    }
    static bool TrackerGpuDirty(Cache& cache, DAddr address, u64 size) {
        return cache.memory_tracker.IsRegionGpuModified(address, size);
    }
    template <class TestCache>
    static bool UploadBarrierPending(const TestCache& cache) {
        return cache.any_buffer_uploaded;
    }
    static void NextFrame(Cache& cache) {
        cache.native_indirect_uniforms_this_frame.clear();
    }
    static u32 CpuMarks(Cache& cache) {
        return cache.memory_tracker.cpu_marks;
    }
};

} // namespace VideoCommon

namespace {

using Access = VideoCommon::BufferCacheTestAccess;

struct Fixture {
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device{physical};
    Tegra::GPU gpu{system, false, false};
    Null::RasterizerNull rasterizer{gpu};
    std::optional<Tegra::MemoryManager> memory;
    Runtime runtime;
    std::unique_ptr<Cache> cache;
    std::unique_ptr<Kernel::KProcess> process;
    Core::Asid asid{};
    u8* bytes{};

    Fixture() {
        system.Initialize();
        process = std::make_unique<Kernel::KProcess>(system.Kernel());
        auto& table = process->GetPageTable().GetImpl();
        table.Resize(24, 12);
        bytes = physical.GetPointerFromRaw<u8>(0x10000);
        std::memset(bytes, 0, 0x40000);
        for (u64 page = 0; page < 64; ++page) {
            table.entries.GetAndFault(0x10 + page)
                .Store(false, Common::PageType::Memory, 1,
                       reinterpret_cast<uintptr_t>(bytes) - 0x10000);
        }
        process->GetMemory().SetCurrentPageTable(*process);
        asid = device.RegisterProcess(&process->GetMemory());
        device.BindInterface(&rasterizer);
        device.Map(0x10000, 0x10000, 0x40000, asid);
        ResetMemory();
        cache = std::make_unique<Cache>(device, runtime);
    }
    ~Fixture() {
        cache.reset();
        memory.reset();
        device.Unmap(0x10000, 0x40000);
        device.UnregisterProcess(asid);
        system.Kernel().UnregisterKernelObject(process.get());
    }
    void ResetMemory() {
        memory.reset();
        memory.emplace(system, device, 32, 1ULL << 31, 16);
        memory->BindRasterizer(&rasterizer);
        memory->Map(0x100000, 0x10000, 0x10000, Tegra::PTEKind::PITCH, true);
    }
    void Descriptor(GPUVAddr address, u64 size) {
        std::memcpy(bytes, &address, sizeof(address));
        std::memcpy(bytes + 8, &size, sizeof(size));
    }
    VideoCommon::Binding Bind(bool written = false, bool native = false, bool table = false,
                              bool mapping_size = true) {
        return Access::Bind(*cache, *memory, 0x100000, written, native, table, mapping_size);
    }
};

struct UniformFixture : Fixture {
    Tegra::Control::ChannelState state{0};
    std::optional<VideoCommon::BufferCacheChannelInfo> channel;
    VideoCommon::UniformBufferSizes graphics_sizes{};
    VideoCommon::ComputeUniformBufferSizes compute_sizes{};

    UniformFixture() {
        memory->Map(0x20000, 0x20000, 0x10000, Tegra::PTEKind::PITCH, true);
        state.memory_manager = &*memory;
        state.payload.emplace(system, *memory, state);
        channel.emplace(state);
        channel->uniform_buffer_sizes = &graphics_sizes;
        channel->compute_uniform_buffer_sizes = &compute_sizes;
    }

    void Bind(bool compute, VideoCommon::BufferId id, DAddr address, u32 size) {
        if (compute) {
            compute_sizes[0] = size;
            channel->enabled_compute_uniform_buffer_mask = 1;
            channel->compute_uniform_buffers[0] = {
                .device_addr = address, .size = size, .buffer_id = id};
            Access::ComputeUniforms(*cache, *channel);
        } else {
            graphics_sizes[0][0] = size;
            Access::Uniform(*cache, *channel, id, address, size);
        }
    }
};

} // namespace

TEST_CASE("Storage binding cache tracks map changes and descriptor dependencies",
          "[video_core][draw_preparation]") {
    Fixture f;
    constexpr GPUVAddr base = 0x200000;
    f.memory->Map(base, 0x20000, 0x10000, Tegra::PTEKind::PITCH, true);
    f.Descriptor(base + 0x123, 0x456);
    for (int i = 0; i < 3; ++i) {
        const auto binding = f.Bind(true);
        REQUIRE(binding.device_addr == 0x20100);
        REQUIRE(binding.gpu_addr == base + 0x100);
        REQUIRE(binding.size == 0x479);
        REQUIRE(binding.buffer_id == VideoCommon::BufferId{});
        REQUIRE(binding.segment_count == 0);
    }
    REQUIRE(f.Bind().size == 0xf00);
    f.Descriptor(base + 0x123, 0x1456);
    REQUIRE(f.Bind(true).size == 0x1479);
    f.runtime.alignment = 4096;
    REQUIRE(f.Bind(true).device_addr == 0x20000);
    REQUIRE(f.Bind(true).size == 0x1579);
    REQUIRE(f.Bind(true, true).size == 0x10000);
    f.memory->Map(base, 0x30000, 0x10000, Tegra::PTEKind::PITCH, true);
    REQUIRE(f.Bind(true).device_addr == 0x30000);
    f.memory->MapSparse(base, 0x10000, true);
    REQUIRE(f.Bind().size == 0);
    f.memory->Unmap(base, 0x10000);
    REQUIRE(f.Bind().size == 0);
    f.memory->Map(base, 0x20000, 0x2000, Tegra::PTEKind::PITCH, false);
    REQUIRE(f.Bind(true, true).size == 0x2000);
    f.memory->Map(base + 0x1000, 0x40000, 0x1000, Tegra::PTEKind::PITCH, false);
    REQUIRE(f.Bind(true, true).size == 0x1000);
    const auto old_owner = f.memory->GetID();
    f.ResetMemory();
    f.memory->Map(base, 0x40000, 0x10000, Tegra::PTEKind::PITCH, true);
    REQUIRE(f.memory->GetID() != old_owner);
    REQUIRE(f.Bind(true).device_addr == 0x40000);
    f.Descriptor(0, 0x123);
    REQUIRE(f.Bind().size == 0);
}

TEST_CASE("Storage binding cache preserves native and table size policies", "[video_core][draw_preparation]") {
    Fixture f;
    constexpr GPUVAddr base = 0x4000000;
    f.memory->Map(base, 0x10000000, 80 << 20, Tegra::PTEKind::PITCH, true);
    f.Descriptor(base, 256);
    REQUIRE(f.Bind().size == 4096);
    REQUIRE(f.Bind(false, true).size == 8 << 20);
    REQUIRE(f.Bind(false, true, false, false).size == 4096);
    REQUIRE(f.Bind(true, true, false, false).size == 256);
    REQUIRE(f.Bind(false, true).size == 8 << 20);
    REQUIRE(f.Bind(false, false, true).size == 64 << 20);
    REQUIRE(f.Bind(true, false, true).size == 256);
    f.Descriptor(base, (1ULL << 32) | 256);
    REQUIRE(f.Bind().size == 8 << 20);
    REQUIRE(f.Bind(false, false, true).size == 64 << 20);
    f.Descriptor(base + (79 << 20), 0);
    REQUIRE(f.Bind(false, false, true).size == 1 << 20);
}

TEST_CASE("Storage binding cache does not reuse a destroyed address space", "[video_core][draw_preparation]") {
    Fixture f;
    f.memory->Map(0x200000, 0x20000, 0x10000, Tegra::PTEKind::PITCH, true);
    f.Descriptor(0x200000, 0x1000);
    REQUIRE(f.Bind(true).device_addr == 0x20000);
    const auto serial = f.memory->MapSerial();
    const auto owner = f.memory->GetID();
    const auto* address = &*f.memory;
    f.ResetMemory();
    f.memory->Map(0x200000, 0x30000, 0x10000, Tegra::PTEKind::PITCH, true);
    REQUIRE(&*f.memory == address);
    REQUIRE(f.memory->MapSerial() == serial);
    REQUIRE(f.memory->GetID() != owner);
    REQUIRE(f.Bind(true).device_addr == 0x30000);
}

TEST_CASE("Native uniform banks upload directly without overwriting GPU-owned bytes",
          "[video_core][draw_preparation]") {
    Fixture f;
    f.memory->EnableNativeTiling();
    Tegra::Control::ChannelState state{0};
    state.memory_manager = &*f.memory;
    state.payload.emplace(f.system, *f.memory, state);
    VideoCommon::BufferCacheChannelInfo channel{state};
    VideoCommon::UniformBufferSizes sizes{};
    sizes[0][0] = 0x10000;
    channel.uniform_buffer_sizes = &sizes;
    const auto id = Access::Add(*f.cache, 0x20000, 0x10000);
    std::memset(f.bytes + 0x10000, 0x5a, 0x10000);
    Access::Uniform(*f.cache, channel, id, 0x20000, 0x10000);
    REQUIRE(f.runtime.upload_bytes == 0x10000);
    REQUIRE(Access::CpuMarks(*f.cache) == 0);
    REQUIRE(Access::Get(*f.cache, id).bytes[0xffff] == 0x5a);
    Access::Uniform(*f.cache, channel, id, 0x20000, 0x10000);
    REQUIRE(f.runtime.uploads == 1);
    // A legitimate CPU invalidation still refreshes bytes within the same frame.
    std::memset(f.bytes + 0x11000, 0x3c, 0x1000);
    Access::CpuDirty(*f.cache, 0x21000, 0x1000);
    Access::Uniform(*f.cache, channel, id, 0x20000, 0x10000);
    REQUIRE(f.runtime.uploads == 2);
    REQUIRE(Access::Get(*f.cache, id).bytes[0x1000] == 0x3c);
    Access::NextFrame(*f.cache);
    Access::Dirty(*f.cache, 0x21000, 0x1000);
    std::memset(f.bytes + 0x10000, 0xa5, 0x10000);
    Access::Uniform(*f.cache, channel, id, 0x20000, 0x10000);
    REQUIRE(f.runtime.uploads == 2);
    REQUIRE(Access::Get(*f.cache, id).bytes[0x1000] == 0x3c);
}

TEST_CASE("Native uniform uploads stop at holes and keep unaligned bank boundaries",
          "[video_core][draw_preparation]") {
    Fixture f;
    f.memory->EnableNativeTiling();
    Tegra::Control::ChannelState state{0};
    state.memory_manager = &*f.memory;
    state.payload.emplace(f.system, *f.memory, state);
    VideoCommon::BufferCacheChannelInfo channel{state};
    VideoCommon::UniformBufferSizes sizes{};
    sizes[0][0] = 0x10000;
    channel.uniform_buffer_sizes = &sizes;
    const auto id = Access::Add(*f.cache, 0x20000, 0x20000);
    std::memset(f.bytes + 0x10000, 0x6b, 0x20000);
    f.device.Unmap(0x22000, 0x1000);
    Access::Uniform(*f.cache, channel, id, 0x20123, 0x10000);
    REQUIRE(f.runtime.upload_bytes == 0x1edd);
    const auto& data = Access::Get(*f.cache, id).bytes;
    REQUIRE(data[0x122] == 0);
    REQUIRE(data[0x123] == 0x6b);
    REQUIRE(data[0x1fff] == 0x6b);
    REQUIRE(data[0x2000] == 0);
    REQUIRE(data[0x3000] == 0);
    REQUIRE(Access::CpuMarks(*f.cache) == 0);
    f.device.Map(0x22000, 0x22000, 0x1000, f.asid);
    Access::NextFrame(*f.cache);
    Access::Uniform(*f.cache, channel, id, 0x20123, 0x10000);
    REQUIRE(f.runtime.upload_bytes == 0x11edd);
    REQUIRE(data[0x10122] == 0x6b);
    REQUIRE(data[0x10123] == 0);
}

TEST_CASE("Native uniform refresh preserves GPU spans on a CPU-untracked page",
          "[video_core][draw_preparation]") {
    Fixture f;
    f.memory->EnableNativeTiling();
    Tegra::Control::ChannelState state{0};
    state.memory_manager = &*f.memory;
    state.payload.emplace(f.system, *f.memory, state);
    VideoCommon::BufferCacheChannelInfo channel{state};
    VideoCommon::UniformBufferSizes sizes{};
    sizes[0][0] = 0x10000;
    channel.uniform_buffer_sizes = &sizes;
    const auto id = Access::Add(*f.cache, 0x20000, 0x10000);
    auto& host = Access::Get(*f.cache, id).bytes;
    std::memset(f.bytes + 0x10000, 0x11, 0x10000);
    std::memset(host.data() + 0x40, 0xc7, 16);
    Access::Dirty(*f.cache, 0x20040, 16);
    REQUIRE(Access::TrackerGpuDirty(*f.cache, 0x20000, 0x1000));

    // A CPU/service invalidation elsewhere on this page masks its coarse GPU bit, but
    // does not transfer ownership of the GPU's untouched bytes back to guest memory.
    std::memset(f.bytes + 0x10080, 0x22, 16);
    Access::CpuDirty(*f.cache, 0x20080, 16);
    // Native writes can bypass tracking. The forced refresh must still cover a second
    // clean-tracked page, even though the first page prevents a direct bank upload.
    std::memset(f.bytes + 0x11080, 0x33, 16);
    REQUIRE_FALSE(Access::TrackerGpuDirty(*f.cache, 0x20000, 0x1000));
    REQUIRE(f.cache->IsRegionGpuModified(0x20040, 16));
    REQUIRE(host[0x1080] == 0);
    Access::Uniform(*f.cache, channel, id, 0x20000, 0x10000);

    REQUIRE(f.runtime.upload_bytes == 0x10000 - 16);
    for (size_t i = 0; i < 16; ++i) {
        CHECK(host[0x40 + i] == 0xc7);
        CHECK(host[0x80 + i] == 0x22);
        CHECK(host[0x1080 + i] == 0x33);
        CHECK(f.bytes[0x10040 + i] == 0x11);
    }
    REQUIRE(f.cache->IsRegionGpuModified(0x20040, 16));
}

TEST_CASE("Direct native uniform refresh retains immediate-upload barrier bookkeeping",
          "[video_core][draw_preparation]") {
    Fixture f;
    f.memory->EnableNativeTiling();
    Tegra::Control::ChannelState state{0};
    state.memory_manager = &*f.memory;
    state.payload.emplace(f.system, *f.memory, state);
    VideoCommon::BufferCacheChannelInfo channel{state};
    VideoCommon::UniformBufferSizes sizes{};
    sizes[0][0] = 0x10000;
    channel.uniform_buffer_sizes = &sizes;
    auto cache = std::make_unique<ImmediateCache>(f.device, f.runtime);
    const auto id = Access::Add(*cache, 0x20000, 0x10000);
    std::memset(f.bytes + 0x10000, 0x42, 0x10000);
    REQUIRE_FALSE(Access::UploadBarrierPending(*cache));
    Access::Uniform(*cache, channel, id, 0x20000, 0x10000);
    REQUIRE(Access::UploadBarrierPending(*cache));
    REQUIRE(Access::Get(*cache, id).bytes[0xffff] == 0x42);
}

TEST_CASE("Small uniform bindings preserve GPU spans on a CPU-untracked page",
          "[video_core][draw_preparation][uniform_coherency]") {
    for (const bool compute : {false, true}) {
        CAPTURE(compute);
        UniformFixture f;
        f.runtime.uniform_alignment = 256;
        const auto id = Access::Add(*f.cache, 0x20000, 0x10000);
        auto& host = Access::Get(*f.cache, id).bytes;
        std::memset(f.bytes + 0x10000, 0x11, 0x10000);
        std::fill_n(host.begin() + 0x40, 16, 0xc7);
        Access::Dirty(*f.cache, 0x20040, 16);
        std::memset(f.bytes + 0x10080, 0x22, 16);
        Access::CpuDirty(*f.cache, 0x20080, 16);
        REQUIRE_FALSE(Access::TrackerGpuDirty(*f.cache, 0x20000, 256));
        REQUIRE(f.cache->IsRegionGpuModified(0x20040, 16));

        std::vector<u8> expected(256, 0x11);
        std::fill_n(expected.begin() + 0x40, 16, 0xc7);
        std::fill_n(expected.begin() + 0x80, 16, 0x22);
        f.Bind(compute, id, 0x20000, 256);
        REQUIRE(f.runtime.uniform_bindings.size() == 1);
        CHECK(std::ranges::equal(f.runtime.uniform_bindings.back().bytes, expected));
        CHECK_FALSE(f.runtime.uniform_bindings.back().streamed);
        CHECK(f.cache->IsRegionGpuModified(0x20040, 16));
        CHECK(f.bytes[0x10040] == 0x11); // The aligned host binding needs no readback.
        CHECK(f.runtime.downloads == 0);
        CHECK(f.runtime.finishes == 0);

        // A later partial CPU update must preserve the same GPU-owned bytes again.
        std::memset(f.bytes + 0x10080, 0x33, 16);
        Access::CpuDirty(*f.cache, 0x20080, 16);
        std::fill_n(expected.begin() + 0x80, 16, 0x33);
        f.Bind(compute, id, 0x20000, 256);
        REQUIRE(f.runtime.uniform_bindings.size() == 2);
        CHECK(std::ranges::equal(f.runtime.uniform_bindings.back().bytes, expected));
        CHECK(f.runtime.downloads == 0);
    }
}

TEST_CASE("Unaligned uniform streams reconcile GPU bytes without overwriting CPU writes",
          "[video_core][draw_preparation][uniform_coherency]") {
    for (const bool compute : {false, true}) {
        for (const bool cpu_untracked : {false, true}) {
            CAPTURE(compute, cpu_untracked);
            UniformFixture f;
            f.runtime.uniform_alignment = 256;
            const auto id = Access::Add(*f.cache, 0x20000, 0x10000);
            auto& host = Access::Get(*f.cache, id).bytes;
            std::memset(f.bytes + 0x10000, 0x11, 0x10000);
            std::ranges::fill(host, 0x11);
            std::fill_n(host.begin() + 0x50, 16, 0xc7);
            Access::Dirty(*f.cache, 0x20050, 16);
            std::vector<u8> expected(256, 0x11);
            std::fill_n(expected.begin() + 0x40, 16, 0xc7);
            if (cpu_untracked) {
                std::memset(f.bytes + 0x10090, 0x22, 16);
                Access::CpuDirty(*f.cache, 0x20090, 16);
                std::fill_n(expected.begin() + 0x80, 16, 0x22);
            }
            REQUIRE(Access::TrackerGpuDirty(*f.cache, 0x20010, 256) == !cpu_untracked);
            REQUIRE(f.cache->IsRegionGpuModified(0x20050, 16));
            f.runtime.before_completion = [&] {
                CHECK(f.bytes[0x10050] == 0x11); // Do not publish before completion.
                if (cpu_untracked) {
                    CHECK(f.bytes[0x10090] == 0x22);
                }
            };
            const auto describe_mismatch = [&] {
                std::ostringstream message;
                const auto bound = f.runtime.uniform_bindings.back().bytes;
                const auto* current_device = f.device.GetPointer<u8>(0x20010);
                std::array<u8, 256> cached_read{};
                f.device.ReadBlockUnsafe(0x20010, cached_read.data(), cached_read.size());
                message << "bound_size=" << bound.size() << " expected_size=" << expected.size()
                        << " device_pointer_matches_cpu="
                        << (current_device == f.bytes + 0x10010);
                size_t mismatches = 0;
                for (size_t index = 0; index < (std::min)(bound.size(), expected.size()); ++index) {
                    if (bound[index] == expected[index]) {
                        continue;
                    }
                    if (mismatches++ < 8) {
                        message << " [index=" << index << " bound=" << u32{bound[index]}
                                << " expected=" << u32{expected[index]}
                                << " cpu=" << u32{f.bytes[0x10010 + index]}
                                << " device=" << (current_device ? int{current_device[index]} : -1)
                                << " cached=" << u32{cached_read[index]} << ']';
                    }
                }
                message << " mismatches=" << mismatches;
                return message.str();
            };

            f.Bind(compute, id, 0x20010, 256);
            REQUIRE(f.runtime.uniform_bindings.size() == 1);
            INFO("first bind: " << describe_mismatch());
            CHECK(std::ranges::equal(f.runtime.uniform_bindings.back().bytes, expected));
            CHECK(f.runtime.uniform_bindings.back().streamed);
            CHECK(f.runtime.download_bytes == 16);
            CHECK(f.runtime.finishes == 1);
            CHECK_FALSE(Access::Pending(*f.cache, 0x20050, 16));
            CHECK(f.bytes[0x1000f] == 0x11);
            CHECK(f.bytes[0x10110] == 0x11);

            // Rebinding coherent bytes keeps the stream path without another readback.
            f.runtime.before_completion = {};
            f.Bind(compute, id, 0x20010, 256);
            REQUIRE(f.runtime.uniform_bindings.size() == 2);
            INFO("repeated bind: " << describe_mismatch());
            CHECK(std::ranges::equal(f.runtime.uniform_bindings.back().bytes, expected));
            CHECK(f.runtime.download_bytes == 16);
            CHECK(f.runtime.finishes == 1);

            // A new GPU write after that bind must not reuse the previous CPU contents.
            std::fill_n(host.begin() + 0x50, 16, 0xd3);
            Access::Dirty(*f.cache, 0x20050, 16);
            std::fill_n(expected.begin() + 0x40, 16, 0xd3);
            f.Bind(compute, id, 0x20010, 256);
            INFO("new GPU write: " << describe_mismatch());
            CHECK(std::ranges::equal(f.runtime.uniform_bindings.back().bytes, expected));
            CHECK(f.runtime.download_bytes == 32);
            CHECK(f.runtime.finishes == 2);
        }
    }
}

TEST_CASE("CPU-owned uniforms retain streaming without uploads or GPU waits",
          "[video_core][draw_preparation][uniform_coherency]") {
    for (const bool compute : {false, true}) {
        for (const u32 offset : {0U, 16U}) {
            CAPTURE(compute, offset);
            UniformFixture f;
            f.runtime.uniform_alignment = 256;
            const auto id = Access::Add(*f.cache, 0x20000, 0x10000);
            std::memset(f.bytes + 0x10000, 0x42, 0x10000);
            std::vector<u8> expected(256, 0x42);
            f.Bind(compute, id, 0x20000 + offset, 256);
            REQUIRE(f.runtime.uniform_bindings.size() == 1);
            CHECK(f.runtime.uniform_bindings.back().streamed);
            CHECK(std::ranges::equal(f.runtime.uniform_bindings.back().bytes, expected));
            // Native CPU writes may bypass tracking; the stream must still read current bytes.
            f.bytes[0x10000 + offset + 3] = 0x91;
            expected[3] = 0x91;
            f.Bind(compute, id, 0x20000 + offset, 256);
            REQUIRE(f.runtime.uniform_bindings.size() == 2);
            CHECK(std::ranges::equal(f.runtime.uniform_bindings.back().bytes, expected));
            CHECK(f.runtime.uploads == 0);
            CHECK(f.runtime.downloads == 0);
            CHECK(f.runtime.finishes == 0);
            CHECK(f.runtime.waits == 0);
        }
    }
}

TEST_CASE("Unaligned uniform readback preserves a newer CPU overwrite of GPU bytes",
          "[video_core][draw_preparation][uniform_coherency]") {
    for (const bool compute : {false, true}) {
        CAPTURE(compute);
        UniformFixture f;
        f.runtime.uniform_alignment = 256;
        const auto id = Access::Add(*f.cache, 0x20000, 0x10000);
        auto& host = Access::Get(*f.cache, id).bytes;
        std::memset(f.bytes + 0x10000, 0x11, 0x10000);
        std::ranges::fill(host, 0x11);
        std::fill_n(host.begin() + 0x50, 32, 0xc7);
        Access::Dirty(*f.cache, 0x20050, 32);

        // A CPU/service write cancels only its exact bytes, leaving GPU ownership on both sides.
        std::memset(f.bytes + 0x10058, 0x92, 16);
        f.cache->WriteMemory(0x20058, 16, false);
        REQUIRE_FALSE(f.cache->IsRegionGpuModified(0x20058, 16));
        REQUIRE(f.cache->IsRegionGpuModified(0x20050, 8));
        REQUIRE(f.cache->IsRegionGpuModified(0x20068, 8));
        REQUIRE_FALSE(Access::TrackerGpuDirty(*f.cache, 0x20010, 256));

        std::vector<u8> expected(256, 0x11);
        std::fill_n(expected.begin() + 0x40, 32, 0xc7);
        std::fill_n(expected.begin() + 0x48, 16, 0x92);
        f.Bind(compute, id, 0x20010, 256);
        REQUIRE(f.runtime.uniform_bindings.size() == 1);
        CHECK(std::ranges::equal(f.runtime.uniform_bindings.back().bytes, expected));
        CHECK(f.runtime.uniform_bindings.back().streamed);
        CHECK(f.runtime.download_bytes == 16);
        CHECK(f.runtime.finishes == 1);
        CHECK_FALSE(Access::Pending(*f.cache, 0x20050, 32));
        CHECK(std::ranges::all_of(std::span{f.bytes + 0x10058, 16},
                                  [](u8 value) { return value == 0x92; }));
    }
}

TEST_CASE("Compute uniforms append mixed streamed and cached bindings in enabled-slot order",
          "[video_core][draw_preparation][uniform_coherency]") {
    UniformFixture f;
    f.runtime.uniform_alignment = 256;
    const auto id = Access::Add(*f.cache, 0x20000, 0x10000);
    auto& host = Access::Get(*f.cache, id).bytes;
    constexpr std::array<u32, 4> slots{0, 2, 5, 7};
    constexpr std::array<u32, 4> offsets{0, 0x1000, 0x3000, 0x4000};
    constexpr std::array<u32, 4> sizes{256, 8192, 256, 256};
    constexpr std::array<u8, 4> values{0x11, 0x22, 0x33, 0x44};
    constexpr std::array<bool, 4> streamed{true, false, true, false};
    for (size_t index = 0; index < slots.size(); ++index) {
        const u32 slot = slots[index];
        f.compute_sizes[slot] = sizes[index];
        f.channel->enabled_compute_uniform_buffer_mask |= 1U << slot;
        f.channel->compute_uniform_buffers[slot] = {
            .device_addr = 0x20000 + offsets[index], .size = sizes[index], .buffer_id = id};
        if (streamed[index]) {
            std::memset(f.bytes + 0x10000 + offsets[index], values[index], sizes[index]);
        } else {
            std::fill_n(host.begin() + offsets[index], sizes[index], values[index]);
            Access::Dirty(*f.cache, 0x20000 + offsets[index], sizes[index]);
        }
    }
    Access::ComputeUniforms(*f.cache, *f.channel);
    REQUIRE(f.runtime.uniform_bindings.size() == slots.size());
    for (size_t index = 0; index < slots.size(); ++index) {
        CAPTURE(index);
        const auto& bound = f.runtime.uniform_bindings[index];
        CHECK(bound.streamed == streamed[index]);
        REQUIRE(bound.bytes.size() == sizes[index]);
        CHECK(std::ranges::all_of(bound.bytes, [&](u8 value) { return value == values[index]; }));
    }
    CHECK(f.runtime.uploads == 0);
    CHECK(f.runtime.downloads == 0);
    CHECK(f.runtime.finishes == 0);
}

TEST_CASE("Uniform readback completes before the first descriptor of a draw or dispatch",
          "[video_core][draw_preparation][uniform_coherency]") {
    for (const bool compute : {false, true}) {
        CAPTURE(compute);
        UniformFixture f;
        f.runtime.uniform_alignment = 256;
        const auto id = Access::Add(*f.cache, 0x20000, 0x10000);
        std::memset(f.bytes + 0x10000, 0x31, 256);
        std::fill_n(Access::Get(*f.cache, id).bytes.begin() + 0x1010, 256, 0x73);
        Access::Dirty(*f.cache, 0x21010, 256);
        const VideoCommon::Binding clean{
            .device_addr = 0x20000, .size = 256, .buffer_id = id};
        const VideoCommon::Binding gpu_written{
            .device_addr = 0x21010, .size = 256, .buffer_id = id};
        f.runtime.before_completion = [&] {
            // A submission here must not retire an earlier descriptor's stream allocation.
            CHECK(f.runtime.uniform_bindings.empty());
        };
        if (compute) {
            f.compute_sizes[0] = f.compute_sizes[2] = 256;
            f.channel->enabled_compute_uniform_buffer_mask = (1U << 0) | (1U << 2);
            f.channel->compute_uniform_buffers[0] = clean;
            f.channel->compute_uniform_buffers[2] = gpu_written;
            Access::ComputeUniforms(*f.cache, *f.channel);
        } else {
            f.graphics_sizes[0][0] = f.graphics_sizes[0][2] = 256;
            f.channel->enabled_uniform_buffer_masks[0] = (1U << 0) | (1U << 2);
            f.channel->uniform_buffers[0][0] = clean;
            f.channel->uniform_buffers[0][2] = gpu_written;
            Access::GraphicsUniforms(*f.cache, *f.channel);
        }
        REQUIRE(f.runtime.uniform_bindings.size() == 2);
        CHECK(f.runtime.finishes == 1);
        CHECK(std::ranges::all_of(f.runtime.uniform_bindings[0].bytes,
                                  [](u8 value) { return value == 0x31; }));
        CHECK(std::ranges::all_of(f.runtime.uniform_bindings[1].bytes,
                                  [](u8 value) { return value == 0x73; }));
    }
}

TEST_CASE("Buffer downloads clip cross-buffer requests before clearing dirty pages",
          "[video_core][draw_preparation][buffer_download]") {
    Fixture f;
    const auto check = [&](auto& cache, bool mapped) {
        const auto first = Access::Add(cache, 0x10000, 0x10000, mapped);
        const auto second = Access::Add(cache, 0x30000, 0x10000, mapped);
        std::ranges::fill(Access::Get(cache, first).bytes, 0x51);
        std::ranges::fill(Access::Get(cache, second).bytes, 0x62);
        Access::Dirty(cache, 0x11020, 16); // Outside the request's first page.
        Access::Dirty(cache, 0x12030, 32);
        Access::Dirty(cache, 0x30020, 48);
        Access::Dirty(cache, 0x31010, 16); // Outside the request's last page.
        cache.DownloadMemory(0x12010, 0x1e040);
        for (size_t i = 0; i < 32; ++i) {
            REQUIRE(f.bytes[0x2030 + i] == 0x51);
        }
        for (size_t i = 0; i < 48; ++i) {
            REQUIRE(f.bytes[0x20020 + i] == 0x62);
        }
        CHECK(f.bytes[0x2030 - 1] == 0);
        CHECK(f.bytes[0x2050] == 0);
        CHECK(f.bytes[0x10000] == 0); // No cached buffer covers this gap.
        CHECK(f.bytes[0x20020 - 1] == 0);
        CHECK(f.bytes[0x20050] == 0);
        CHECK(f.bytes[0x1020] == 0);
        CHECK(f.bytes[0x21010] == 0);
        CHECK(Access::Pending(cache, 0x11020, 16));
        CHECK(Access::Pending(cache, 0x31010, 16));
        CHECK_FALSE(Access::Pending(cache, 0x12030, 32));
        CHECK_FALSE(Access::Pending(cache, 0x30020, 48));
    };
    SECTION("Staging backend completes both buffers with one wait") {
        check(*f.cache, false);
        CHECK(f.runtime.allocations == 1);
        CHECK(f.runtime.finishes == 1);
        CHECK(f.runtime.download_bytes == 80);
    }
    SECTION("Host-coherent buffers need no staging allocation") {
        check(*f.cache, true);
        CHECK(f.runtime.allocations == 0);
        CHECK(f.runtime.waits == 1);
    }
    SECTION("Immediate-download backend also clips each buffer") {
        auto cache = std::make_unique<ImmediateDownloadCache>(f.device, f.runtime);
        check(*cache, false);
        CHECK(f.runtime.allocations == 0);
        CHECK(f.runtime.finishes == 0);
    }
}

TEST_CASE("Mapped buffer eviction waits for the latest usage before publishing guest bytes",
          "[video_core][draw_preparation][buffer_download]") {
    Fixture f;
    const auto first = Access::Add(*f.cache, 0x10000, 0x10000, true);
    const auto second = Access::Add(*f.cache, 0x30000, 0x10000, true);
    auto& a = Access::Get(*f.cache, first);
    auto& b = Access::Get(*f.cache, second);
    a.tick = 67;
    b.tick = 41;
    Access::Dirty(*f.cache, 0x10040, 16);
    Access::Dirty(*f.cache, 0x30080, 16);
    f.runtime.before_completion = [&] {
        CHECK(f.bytes[0x40] == 0);
        CHECK(f.bytes[0x20080] == 0);
        std::fill_n(a.bytes.begin() + 0x40, 16, 0x13);
        std::fill_n(b.bytes.begin() + 0x80, 16, 0x24);
    };
    const std::array ids{first, second};
    Access::EvictionDownload(*f.cache, ids);
    CHECK(f.runtime.waited_tick == 67);
    CHECK(f.runtime.waits == 1);
    CHECK(f.runtime.allocations == 0);
    CHECK(f.runtime.downloads == 0);
    CHECK(f.bytes[0x40] == 0x13);
    CHECK(f.bytes[0x20080] == 0x24);
    CHECK_FALSE(Access::Pending(*f.cache, 0x10000, 0x30000));
}

TEST_CASE("Mixed staged and coherent readback publishes only after batch completion",
          "[video_core][draw_preparation][buffer_download]") {
    Fixture f;
    bool first_mapped{};
    SECTION("Mapped range first") { first_mapped = true; }
    SECTION("Mapped range last") { first_mapped = false; }
    const auto first = Access::Add(*f.cache, 0x10000, 0x10000, first_mapped);
    const auto second = Access::Add(*f.cache, 0x30000, 0x10000, !first_mapped);
    auto& a = Access::Get(*f.cache, first);
    auto& b = Access::Get(*f.cache, second);
    Access::Dirty(*f.cache, 0x10040, 16);
    Access::Dirty(*f.cache, 0x30080, 16);
    f.runtime.before_completion = [&] {
        CHECK(f.bytes[0x40] == 0);
        CHECK(f.bytes[0x20080] == 0);
        std::fill_n(a.bytes.begin() + 0x40, 16, 0x35);
        std::fill_n(b.bytes.begin() + 0x80, 16, 0x46);
    };
    f.cache->DownloadMemory(0x10000, 0x30000);
    CHECK(f.runtime.finishes == 1);
    CHECK(f.runtime.waits == 0);
    CHECK(f.runtime.allocations == 1);
    CHECK(f.runtime.download_bytes == 16);
    CHECK(f.bytes[0x40] == 0x35);
    CHECK(f.bytes[0x20080] == 0x46);
}

TEST_CASE("Repeated and empty buffer readback ranges do not duplicate transfers",
          "[video_core][draw_preparation][buffer_download]") {
    Fixture f;
    const auto id = Access::Add(*f.cache, 0x20000, 0x10000);
    std::ranges::fill(Access::Get(*f.cache, id).bytes, 0x57);
    Access::Dirty(*f.cache, 0x20040, 16);
    const std::array<std::pair<DAddr, u64>, 5> ranges{{
        {0, 0}, {0x20040, 0}, {0x20000, 0x1000}, {0x20000, 0x1000}, {0x10000, 0x1000},
    }};
    Access::Download(*f.cache, ranges);
    CHECK(f.runtime.downloads == 1);
    CHECK(f.runtime.download_bytes == 16);
    CHECK(f.runtime.finishes == 1);
    CHECK(f.bytes[0x10040] == 0x57);
    Access::Download(*f.cache, ranges);
    CHECK(f.runtime.allocations == 1);
    CHECK(f.runtime.finishes == 1);
    CHECK_FALSE(Access::Pending(*f.cache, 0x20000, 0x1000));
}

TEST_CASE("Batched downloads preserve newer CPU bytes alongside pending GPU data",
          "[video_core][draw_preparation][buffer_download]") {
    Fixture f;
    const auto id = Access::Add(*f.cache, 0x20000, 0x10000);
    auto& host = Access::Get(*f.cache, id).bytes;
    std::fill_n(host.begin() + 0x40, 64, 0x68);
    Access::Dirty(*f.cache, 0x20040, 64);
    std::memset(f.bytes + 0x10050, 0x79, 16);
    f.cache->WriteMemory(0x20050, 16, false);
    Access::Synchronize(*f.cache, id, 0x20000, 0x1000);
    CHECK(f.cache->IsRegionGpuModified(0x20040, 16));
    CHECK_FALSE(f.cache->IsRegionGpuModified(0x20050, 16));
    f.cache->DownloadMemory(0x20000, 0x1000);
    CHECK(f.runtime.download_bytes == 48);
    CHECK(f.runtime.finishes == 1);
    for (size_t i = 0x40; i < 0x80; ++i) {
        CHECK(f.bytes[0x10000 + i] == (i >= 0x50 && i < 0x60 ? 0x79 : 0x68));
    }
    CHECK_FALSE(Access::Pending(*f.cache, 0x20000, 0x1000));
}
