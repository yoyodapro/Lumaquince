// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <thread>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "video_core/nx2.h"

namespace {
struct WrittenPagesSession {
    WrittenPagesSession() { Tegra::Nx2ClearGpuWrittenPages(); }
    ~WrittenPagesSession() { Tegra::Nx2ClearGpuWrittenPages(); }
};
} // namespace

TEST_CASE("GPU-written word masks match the union of intersected device pages",
          "[video_core][nx2_gpu_written_pages]") {
    WrittenPagesSession session;
    std::array<bool, 512> expected{};
    for (const auto [address, size] : {
             std::pair<u64, u64>{4095, 2}, {63 * 4096 + 11, 64 * 4096},
             {127 * 4096, 65 * 4096}, {256 * 4096, 63 * 4096},
             {320 * 4096, 64 * 4096}, {384 * 4096, 65 * 4096}, {0, 0}}) {
        Tegra::Nx2MarkGpuWritten(address, size);
        Tegra::Nx2MarkGpuWritten(address, size);
        if (size != 0) {
            for (u64 page = address / 4096; page <= (address + size - 1) / 4096; ++page) {
                expected.at(page) = true;
            }
        }
        for (size_t page = 0; page < expected.size(); ++page) {
            CHECK(Tegra::Nx2IsGpuWritten(page * 4096) == expected[page]);
        }
    }
}

TEST_CASE("GPU-written masks clamp at the device-space end and reset between sessions",
          "[video_core][nx2_gpu_written_pages]") {
    WrittenPagesSession session;
    constexpr u64 end = u64{1} << 36;
    Tegra::Nx2MarkGpuWritten(end - 1, 8192);
    CHECK(Tegra::Nx2IsGpuWritten(end - 1));
    CHECK_FALSE(Tegra::Nx2IsGpuWritten(end - 4097));
    CHECK_FALSE(Tegra::Nx2IsGpuWritten(end));
    Tegra::Nx2MarkGpuWritten(end, 4096);
    CHECK_FALSE(Tegra::Nx2IsGpuWritten(0));
    Tegra::Nx2ClearGpuWrittenPages();
    CHECK_FALSE(Tegra::Nx2IsGpuWritten(end - 1));
}

TEST_CASE("Concurrent overlapping GPU-written word marks retain every bit",
          "[video_core][nx2_gpu_written_pages]") {
    WrittenPagesSession session;
    std::vector<std::jthread> writers;
    for (u64 worker = 0; worker < 8; ++worker) {
        writers.emplace_back([worker] {
            for (u64 repeat = 0; repeat < 64; ++repeat) {
                Tegra::Nx2MarkGpuWritten((worker * 7 + 1) * 4096, 65 * 4096);
            }
        });
    }
    writers.clear();
    CHECK_FALSE(Tegra::Nx2IsGpuWritten(0));
    for (u64 page = 1; page <= 114; ++page) {
        CHECK(Tegra::Nx2IsGpuWritten(page * 4096));
    }
    CHECK_FALSE(Tegra::Nx2IsGpuWritten(115 * 4096));
}
