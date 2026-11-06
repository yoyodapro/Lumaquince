// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <type_traits>
#include <utility>

#include "common/container/unordered_map.h"

namespace VideoCommon {

// A live query holds references to every bank in its chain, so none can be recycled while
// extending it. The next reserved slot is in its tail bank or in a newly appended bank.
class QueryBankTail {
public:
    void Reset(size_t bank) noexcept {
        last = bank;
    }

    [[nodiscard]] bool Append(size_t bank) noexcept {
        return std::exchange(last, bank) != bank;
    }

private:
    size_t last{};
};

// Most batches touch one bank. Keep that bank inline and retain the previous map algorithm
// for additional banks; this avoids allocating a map node/table for the single-bank case.
template <bool Ordered>
class QueryBankRanges {
public:
    void Add(size_t bank, size_t start, size_t amount) {
        if (!has_first) {
            first_bank = bank;
            first = {start, start + amount};
            has_first = true;
        } else if (bank == first_bank) {
            Extend(first, start, amount);
        } else {
            if (!others) {
                others.emplace();
            }
            auto [it, inserted] = others->try_emplace(bank, start, start + amount);
            if (!inserted) {
                Extend(it->second, start, amount);
            }
        }
    }

    template <class Func>
    void ForEach(Func&& func) const {
        if (!others) {
            if (has_first) {
                func(first_bank, first.first, first.second - first.first);
            }
            return;
        }
        bool pending = has_first;
        for (const auto& [bank, range] : *others) {
            if (pending && (!Ordered || first_bank < bank)) {
                func(first_bank, first.first, first.second - first.first);
                pending = false;
            }
            func(bank, range.first, range.second - range.first);
        }
        if (pending) {
            func(first_bank, first.first, first.second - first.first);
        }
    }

private:
    using Range = std::pair<size_t, size_t>;
    static void Extend(Range& range, size_t start, size_t amount) {
        range.first = (std::min)(range.first, start);
        range.second = (std::max)(range.second, start + amount);
    }

    using Map = std::conditional_t<Ordered, std::map<size_t, Range>,
                                   Common::unordered_map<size_t, Range>>;
    std::optional<Map> others;
    Range first{};
    size_t first_bank{};
    bool has_first{};
};

} // namespace VideoCommon
