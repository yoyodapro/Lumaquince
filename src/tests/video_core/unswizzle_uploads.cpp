// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <latch>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/alignment.h"
#include "common/div_ceil.h"
#include "common/scope_exit.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "tests/core/system_test_context.h"
#include "video_core/memory_manager.h"
#include "video_core/texture_cache/util.h"
#include "video_core/textures/decoders.h"
#include "video_core/textures/unswizzle_worker.h"
#include "video_core/textures/workers.h"

namespace {

using namespace Tegra::Texture;

struct Layout {
    u32 width;
    u32 height;
    u32 bpp;
    u32 block_height;
    u32 alignment;

    size_t GuestBytes() const {
        const size_t stride = size_t{Common::AlignUpLog2(width, alignment)} * bpp;
        return Common::DivCeil(stride, size_t{GOB_SIZE_X}) *
               Common::DivCeil(height, GOB_SIZE_Y << block_height) *
               (size_t{GOB_SIZE} << block_height);
    }

    size_t HostBytes() const {
        return size_t{width} * height * bpp;
    }

    void Serial(std::span<u8> output, std::span<const u8> input, bool native) const {
        UnswizzleTexture(output, input, bpp, width, height, 1, block_height, 0, alignment, native);
    }

    bool Parallel(std::span<u8> output, std::span<const u8> input, bool native,
                  UnswizzleWorker& worker, Common::ThreadWorker& pool, bool verify = false) const {
        return worker.TryUnswizzle(output, input, bpp, width, height, 1, block_height, 0, alignment,
                                   native, verify, &pool);
    }
};

void Fill(std::span<u8> bytes) {
    u32 state = 0x12345678;
    for (u8& byte : bytes) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        byte = static_cast<u8>(state);
    }
}

} // namespace

TEST_CASE("Full upload keeps mip layer offsets and compressed edge blocks", "[unswizzle_workers]") {
    using namespace VideoCommon;
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    UnswizzleWorker worker;
    for (const bool native : {false, true}) {
        Tegra::MemoryManager memory{system, device_memory, 32, 1ULL << 31, 16};
        if (native) {
            memory.EnableNativeTiling();
        }
        for (const auto format : {PixelFormat::R8_UNORM, PixelFormat::A8B8G8R8_UNORM,
                                  PixelFormat::BC1_RGBA_UNORM, PixelFormat::ASTC_2D_5X4_UNORM}) {
            ImageInfo info;
            info.type = ImageType::e2D;
            info.format = format;
            info.size = {2049, 1025, 1};
            info.block = {0, 4, 0};
            info.resources = {6, 3};
            info.tile_width_spacing = 1;
            info.layer_stride = CalculateLayerStride(info);
            CAPTURE(native, format);
            std::vector<u8> input(CalculateGuestSizeInBytes(info));
            Fill(input);
            std::vector<u8> expected(CalculateUnswizzledSizeBytes(info) + 64, 0xCD);
            std::vector<u8> actual(expected.size(), 0xCD);
            const auto original = UnswizzleImage(
                memory, 0, info, input, std::span{expected}.subspan(32, expected.size() - 64));
            const auto parallel = UnswizzleImage(
                memory, 0, info, input, std::span{actual}.subspan(32, actual.size() - 64), &worker);
            REQUIRE(actual == expected);
            REQUIRE(parallel.size() == original.size());
            for (size_t i = 0; i < original.size(); ++i) {
                CHECK(parallel[i].buffer_offset == original[i].buffer_offset);
                CHECK(parallel[i].buffer_size == original[i].buffer_size);
                CHECK(parallel[i].buffer_row_length == original[i].buffer_row_length);
                CHECK(parallel[i].buffer_image_height == original[i].buffer_image_height);
                CHECK(parallel[i].image_subresource.base_level ==
                      original[i].image_subresource.base_level);
                CHECK(parallel[i].image_subresource.base_layer ==
                      original[i].image_subresource.base_layer);
                CHECK(parallel[i].image_subresource.num_layers ==
                      original[i].image_subresource.num_layers);
                CHECK(parallel[i].image_extent == original[i].image_extent);
                CHECK(parallel[i].image_offset == original[i].image_offset);
            }
        }
    }
}

TEST_CASE("Full upload does not wait behind a saturated shared pool", "[unswizzle_workers]") {
    using namespace VideoCommon;
    using namespace std::chrono_literals;
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    Tegra::MemoryManager memory{system, device_memory, 32, 1ULL << 31, 16};
    memory.EnableNativeTiling();
    ImageInfo info;
    info.type = ImageType::e2D;
    info.format = PixelFormat::R8_UNORM;
    info.size = {2049, 1025, 1};
    info.block = {0, 4, 0};
    info.resources = {1, 1};
    info.layer_stride = CalculateLayerStride(info);
    std::vector<u8> input(CalculateGuestSizeInBytes(info));
    Fill(input);
    std::vector<u8> expected(CalculateUnswizzledSizeBytes(info));
    [[maybe_unused]] const auto original = UnswizzleImage(memory, 0, info, input, expected);
    std::vector<u8> actual(expected.size(), 0xCD);
    auto& pool = GetThreadWorkers();
    const char* threads_env = std::getenv("EDEN_NX2_TRANSCODE_THREADS");
    const u32 threads =
        threads_env ? (std::max)(static_cast<u32>(std::strtoul(threads_env, nullptr, 10)), 1U)
                    : (std::max)(std::thread::hardware_concurrency() / 4, 2U);
    std::latch entered{threads};
    std::latch release{1};
    for (u32 i = 0; i < threads; ++i) {
        pool.QueueWork([&] {
            entered.count_down();
            release.wait();
        });
    }
    entered.wait();
    UnswizzleWorker worker;
    std::promise<void> started;
    auto result = std::async(std::launch::async, [&] {
        started.set_value();
        return UnswizzleImage(memory, 0, info, input, actual, &worker);
    });
    auto unblock = SCOPE_GUARD {
        release.count_down();
        pool.WaitForRequests();
    };
    started.get_future().wait();
    REQUIRE(result.wait_for(2s) == std::future_status::ready);
    release.count_down();
    unblock.Cancel();
    REQUIRE(result.get().size() == original.size());
    REQUIRE(actual == expected);
    pool.WaitForRequests();
}

TEST_CASE("Full upload end to end microbenchmark", "[.unswizzle_upload_benchmark]") {
    using namespace VideoCommon;
    using Clock = std::chrono::steady_clock;
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    Tegra::MemoryManager memory{system, device_memory, 32, 1ULL << 31, 16};
    memory.EnableNativeTiling();
    UnswizzleWorker worker;
    auto& pool = GetThreadWorkers();
    std::printf("format,competing_jobs,serial_upload_us,owned_upload_us,ratio\n");
    for (const auto format :
         {PixelFormat::R8_UNORM, PixelFormat::R16_UNORM, PixelFormat::A8B8G8R8_UNORM}) {
        ImageInfo info;
        info.type = ImageType::e2D;
        info.format = format;
        info.size = {2049, 1025, 1};
        info.block = {0, 4, 0};
        info.resources = {6, 3};
        info.layer_stride = CalculateLayerStride(info);
        std::vector<u8> input(CalculateGuestSizeInBytes(info));
        Fill(input);
        std::vector<u8> expected(CalculateUnswizzledSizeBytes(info));
        std::vector<u8> actual(expected.size());
        for (const u32 competing_jobs : {0U, 4U}) {
            std::vector<double> serial_times;
            std::vector<double> parallel_times;
            const auto measure = [&](UnswizzleWorker* selected, std::span<u8> output) {
                for (u32 job = 0; job < competing_jobs; ++job) {
                    pool.QueueWork([&] {
                        const Layout busy{2049, 1025, 1, 0, 8};
                        std::vector<u8> busy_input(busy.GuestBytes(), 0x5A);
                        std::vector<u8> busy_output(busy.HostBytes());
                        for (int iteration = 0; iteration < 4; ++iteration) {
                            busy.Serial(busy_output, busy_input, true);
                        }
                    });
                }
                const auto begin = Clock::now();
                [[maybe_unused]] const auto copies =
                    UnswizzleImage(memory, 0, info, input, output, selected);
                const double us =
                    std::chrono::duration<double, std::micro>(Clock::now() - begin).count();
                pool.WaitForRequests();
                return us;
            };
            for (int i = 0; i < 18; ++i) {
                double s, p;
                if (i % 2 == 0) {
                    s = measure(nullptr, expected);
                    p = measure(&worker, actual);
                } else {
                    p = measure(&worker, actual);
                    s = measure(nullptr, expected);
                }
                REQUIRE(actual == expected);
                if (i >= 2) {
                    serial_times.push_back(s);
                    parallel_times.push_back(p);
                }
            }
            std::ranges::sort(serial_times);
            std::ranges::sort(parallel_times);
            const double s = serial_times[serial_times.size() / 2];
            const double p = parallel_times[parallel_times.size() / 2];
            std::printf("%u,%u,%.2f,%.2f,%.3f\n", static_cast<unsigned>(format), competing_jobs, s,
                        p, p / s);
        }
    }
}
