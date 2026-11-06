// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <latch>
#include <limits>
#include <vector>

#include "common/alignment.h"
#include "common/div_ceil.h"
#include "common/logging.h"
#include "common/scope_exit.h"
#include "video_core/textures/decoders.h"
#include "video_core/textures/unswizzle_worker.h"
#include "video_core/textures/workers.h"

namespace Tegra::Texture {

bool UnswizzleWorker::TryUnswizzle(std::span<u8> output, std::span<const u8> input,
                                   u32 bytes_per_pixel, u32 width, u32 height, u32 depth,
                                   u32 block_height, u32 block_depth, u32 stride_alignment,
                                   bool native_tiling, bool verify, Common::ThreadWorker* workers) {
    // The serial decoder uses u32 offsets and shifts. Validate before calculating
    // storage sizes, so malformed layouts cannot wrap into an eligible allocation.
    if (!native_tiling || depth != 1 || block_depth != 0 || block_height > 5 ||
        stride_alignment > 31 || width == 0 || height == 0 ||
        (bytes_per_pixel != 1 && bytes_per_pixel != 2)) {
        return false;
    }
    const u64 pitch = u64{width} * bytes_per_pixel;
    constexpr u64 MaxGuestBytes = 32 * 1024 * 1024;
    if (pitch > MaxGuestBytes || height > MaxGuestBytes / pitch || (pitch & 3) == 0) {
        return false;
    }
    const u64 host_bytes = pitch * height;
    if (host_bytes < ParallelUnswizzleMinBytes) {
        return false;
    }
    const u32 band_rows = GOB_SIZE_Y << block_height;
    const u32 first_rows = height / 2 / band_rows * band_rows;
    if (first_rows == 0) {
        return false;
    }
    const u64 aligned_width = Common::AlignUpLog2(u64{width}, stride_alignment);
    const u64 stride = aligned_width * bytes_per_pixel;
    const u64 band_bytes =
        Common::DivCeil(stride, u64{GOB_SIZE_X}) * (u64{GOB_SIZE} << block_height);
    const u64 guest_bytes = Common::DivCeil(u64{height}, u64{band_rows}) * band_bytes;
    if (stride > std::numeric_limits<u32>::max() || guest_bytes > MaxGuestBytes ||
        guest_bytes > 2 * host_bytes ||
        input.size() < guest_bytes || output.size() < host_bytes) {
        return false;
    }
    owned.resize_destructive(guest_bytes);
    const std::span<const u8> snapshot{owned};
    const size_t guest_split = first_rows / band_rows * band_bytes;
    const size_t host_split = first_rows * pitch;
    bool snapshot_valid{};
    std::latch ready{1};
    std::latch completed{1};
    auto& pool = workers ? *workers : GetThreadWorkers();
    if (!pool.TryQueueWork([=, &ready, &completed, &snapshot_valid] {
            SCOPE_EXIT {
                completed.count_down();
            };
            ready.wait();
            if (!snapshot_valid) {
                return;
            }
            UnswizzleTexture(output.first(host_split), snapshot.first(guest_split), bytes_per_pixel,
                             width, first_rows, 1, block_height, 0, stride_alignment,
                             native_tiling);
        })) {
        return false;
    }
    {
        SCOPE_EXIT {
            if (!snapshot_valid) {
                ready.count_down();
            }
            completed.wait();
        };
        // Rejected admission leaves the source unread. Storage allocation happens before
        // admission, and the worker cannot read it until the complete snapshot is published.
        std::memcpy(owned.data(), input.data(), guest_bytes);
        snapshot_valid = true;
        ready.count_down();
        UnswizzleTexture(output.subspan(host_split, host_bytes - host_split),
                         snapshot.subspan(guest_split), bytes_per_pixel, width, height - first_rows,
                         1, block_height, 0, stride_alignment, native_tiling);
    }
    if (verify) {
        std::vector<u8> reference(host_bytes);
        UnswizzleTexture(reference, snapshot, bytes_per_pixel, width, height, depth, block_height,
                         block_depth, stride_alignment, native_tiling);
        if (std::memcmp(reference.data(), output.data(), host_bytes) != 0) {
            LOG_ERROR(HW_GPU,
                      "Parallel unswizzle output differs for {}x{} bpp={} native={}", width,
                      height, bytes_per_pixel, native_tiling);
        }
        std::memcpy(output.data(), reference.data(), host_bytes);
    }
    return true;
}

} // namespace Tegra::Texture
