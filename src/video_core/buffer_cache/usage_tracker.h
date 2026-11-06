// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <vector>

#include "common/alignment.h"
#include "common/common_types.h"

namespace VideoCommon {

class UsageTracker {
    // PAGE_SHIFT is a macro on FreeBSD
    static constexpr size_t BUFFER_BYTES_PER_BITSHIFT = 6;
    static constexpr size_t BUFFER_PAGE_SHIFT = 6 + BUFFER_BYTES_PER_BITSHIFT;
    static constexpr size_t BUFFER_PAGE_BYTES = 1 << BUFFER_PAGE_SHIFT;
public:
    explicit UsageTracker(size_t size) {
        const size_t num_pages = (size >> BUFFER_PAGE_SHIFT) + 1;
        pages.resize(num_pages, 0ULL);
        full_blocks.resize((num_pages >> BLOCK_SHIFT) / 64 + 1, 0ULL);
    }

    void Reset() noexcept {
        if (!touched) {
            return;
        }
        std::ranges::fill(pages, 0ULL);
        std::ranges::fill(full_blocks, 0ULL);
        recent.fill({~u64{0}, 0});
        touched = false;
    }

    void Track(u64 offset, u64 size) noexcept {
        if (size == 0 || size > std::numeric_limits<u64>::max() - offset) {
            return;
        }
        // Tracking is idempotent until Reset: skip ranges tracked just before. Draws bind the
        // same (often multi-megabyte) storage buffers again and again, and each Track wrote a
        // word per 4 KiB of the range.
        for (const auto& [recent_offset, recent_size] : recent) {
            if (recent_offset == offset && recent_size == size) {
                return;
            }
        }
        recent[recent_cursor++ % recent.size()] = {offset, size};
        touched = true;
        const size_t page = offset >> BUFFER_PAGE_SHIFT;
        const size_t page_end = (offset + size) >> BUFFER_PAGE_SHIFT;
        if (page_end < page || page_end >= pages.size()) {
            return;
        }
        TrackPage(page, offset, size);
        if (page == page_end) {
            return;
        }
        SetFullPages(page + 1, page_end);
        const size_t offset_end = offset + size;
        const size_t offset_end_page_aligned = Common::AlignDown(offset_end, BUFFER_PAGE_BYTES);
        TrackPage(page_end, offset_end_page_aligned, offset_end - offset_end_page_aligned);
    }

    [[nodiscard]] bool IsUsed(u64 offset, u64 size) const noexcept {
        if (size == 0 || size > std::numeric_limits<u64>::max() - offset) {
            return false;
        }
        const size_t page = offset >> BUFFER_PAGE_SHIFT;
        const size_t page_end = (offset + size) >> BUFFER_PAGE_SHIFT;
        if (page_end < page || page_end >= pages.size()) {
            return false;
        }
        if (IsPageUsed(page, offset, size)) {
            return true;
        }
        if (page == page_end) {
            return false;
        }
        for (size_t i = page + 1; i < page_end; i++) {
            if (pages[i] != 0 || IsBlockFull(i >> BLOCK_SHIFT)) {
                return true;
            }
        }
        const size_t offset_end = offset + size;
        const size_t offset_end_page_aligned = Common::AlignDown(offset_end, BUFFER_PAGE_BYTES);
        return IsPageUsed(page_end, offset_end_page_aligned, offset_end - offset_end_page_aligned);
    }

private:
    // A bit represents 64 wholly used pages (256 KiB). PR #178, 0f2af4fc6048.
    static constexpr size_t BLOCK_SHIFT = 6;

    [[nodiscard]] bool IsBlockFull(size_t block) const noexcept {
        return (full_blocks[block / 64] >> (block % 64)) & 1;
    }

    void SetFullPages(size_t first, size_t last) noexcept {
        constexpr size_t block_pages = size_t{1} << BLOCK_SHIFT;
        size_t i = first;
        for (; i < last && i % block_pages != 0; ++i) {
            pages[i] = ~u64{0};
        }
        for (; i + block_pages <= last; i += block_pages) {
            const size_t block = i >> BLOCK_SHIFT;
            full_blocks[block / 64] |= u64{1} << (block % 64);
        }
        for (; i < last; ++i) {
            pages[i] = ~u64{0};
        }
    }

    static u64 PageMask(u64 offset, u64 size) noexcept {
        const size_t first_byte = offset % BUFFER_PAGE_BYTES;
        const size_t bytes = std::min<u64>(size, BUFFER_PAGE_BYTES - first_byte);
        if (bytes == 0) {
            return 0;
        }
        // Upload reordering relies on this answer: include every intersected 64-byte granule.
        const size_t first_bit = first_byte >> BUFFER_BYTES_PER_BITSHIFT;
        const size_t last_bit = (first_byte + bytes - 1) >> BUFFER_BYTES_PER_BITSHIFT;
        return (~u64{0} << first_bit) & (~u64{0} >> (63 - last_bit));
    }

    void TrackPage(u64 page, u64 offset, u64 size) noexcept {
        pages[page] |= PageMask(offset, size);
    }

    bool IsPageUsed(u64 page, u64 offset, u64 size) const noexcept {
        const u64 mask2 = PageMask(offset, size);
        return (pages[page] & mask2) != 0 || (mask2 != 0 && IsBlockFull(page >> BLOCK_SHIFT));
    }

private:
    std::vector<u64> pages;
    std::vector<u64> full_blocks;
    bool touched{};
    /// Ranges tracked last, (offset, size).
    std::array<std::pair<u64, u64>, 4> recent{{{~u64{0}, 0}, {~u64{0}, 0}, {~u64{0}, 0}, {~u64{0}, 0}}};
    size_t recent_cursor{};
};

} // namespace VideoCommon
