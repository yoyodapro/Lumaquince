// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <array>
#include <map>
#include <random>
#include <vector>

#include "common/assert.h"
#include "video_core/query_cache/bank_base.h"
#include "video_core/query_cache/bank_bookkeeping.h"

namespace {

// GPU completion is controlled by the test; allocation and reference tracking are production code.
class QueryBank : public VideoCommon::BankBase {
public:
    QueryBank() : BankBase{256} {}

    bool IsDead() const {
        return BankBase::IsDead() && gpu_complete;
    }

    bool gpu_complete{};
};

size_t AllocateBank(VideoCommon::BankPool<QueryBank>& pool) {
    return pool.ReserveBank([](std::deque<QueryBank>& banks, size_t) { banks.emplace_back(); });
}

} // namespace

TEST_CASE("Query bank rejects a slot past its capacity", "[video_core][query_bank]") {
    QueryBank bank;
    for (size_t slot = 0; slot < 256; ++slot) {
        const auto [allocated, index] = bank.Reserve();
        REQUIRE(allocated);
        REQUIRE(index == slot);
    }
    REQUIRE(bank.IsClosed());
    REQUIRE_FALSE(bank.Reserve().first);
}

TEST_CASE("Query bank pool grows beyond 64 retained banks", "[video_core][query_bank]") {
    VideoCommon::BankPool<QueryBank> pool;
    for (size_t index = 0; index < 65; ++index) {
        REQUIRE(AllocateBank(pool) == index);
        auto& bank = pool.GetBank(index);
        for (size_t slot = 0; slot < 256; ++slot) {
            REQUIRE(bank.Reserve().first);
        }
        bank.AddReference(256);
        bank.gpu_complete = true;
    }
    REQUIRE(pool.BankCount() == 65);
    for (size_t index = 0; index < 65; ++index) {
        REQUIRE(pool.GetBank(index).IsClosed());
        REQUIRE_FALSE(pool.GetBank(index).IsDead());
    }
}

TEST_CASE("Query bank reuse requires released references and GPU completion",
          "[video_core][query_bank]") {
    VideoCommon::BankPool<QueryBank> pool;
    REQUIRE(AllocateBank(pool) == 0);
    auto& first = pool.GetBank(0);
    REQUIRE(first.Reserve().first);
    first.AddReference();
    first.Close();

    first.gpu_complete = true;
    REQUIRE(AllocateBank(pool) == 1);
    first.gpu_complete = false;
    first.CloseReference();
    REQUIRE(AllocateBank(pool) == 2);
    first.gpu_complete = true;
    REQUIRE(AllocateBank(pool) == 0);
    REQUIRE(pool.BankCount() == 3);
    REQUIRE_FALSE(first.IsClosed());
    REQUIRE(first.Size() == 256);
    const auto [allocated, slot] = first.Reserve();
    REQUIRE(allocated);
    REQUIRE(slot == 0);
}

TEST_CASE("Query bank tail matches retained-chain traversal through growth and recycling",
          "[video_core][query_bank]") {
    VideoCommon::BankPool<QueryBank> pool;
    VideoCommon::QueryBankTail tail;
    std::vector<size_t> next;
    std::mt19937 random{0x709};
    QueryBank* current = nullptr;
    size_t current_id = 0;
    for (size_t window = 0; window < 400; ++window) {
        std::map<size_t, size_t> references;
        size_t start_bank = 0;
        size_t bank_count = 0;
        const size_t slots = 1 + random() % 600;
        for (size_t slot = 0; slot < slots; ++slot) {
            if (current && random() % 19 == 0) {
                current->Close();
            }
            if (!current || current->IsClosed()) {
                const size_t bank = AllocateBank(pool);
                next.resize(pool.BankCount());
                if (current) {
                    next[current_id] = bank;
                }
                current_id = bank;
                current = &pool.GetBank(bank);
                current->gpu_complete = true;
            }
            REQUIRE(current->Reserve().first);
            current->AddReference();
            ++references[current_id];
            if (slot == 0) {
                start_bank = current_id;
                bank_count = 1;
                tail.Reset(current_id);
            } else {
                size_t bank = start_bank;
                bool found = bank == current_id;
                for (size_t i = 1; !found && i < bank_count; ++i) {
                    bank = next[bank];
                    found = bank == current_id;
                }
                const bool appended = tail.Append(current_id);
                REQUIRE(appended == !found);
                bank_count += appended;
            }
            REQUIRE(bank_count == references.size());
        }
        for (const auto& [bank, count] : references) {
            pool.GetBank(bank).CloseReference(count);
        }
    }
}

TEST_CASE("Query bank inline ranges match the original map unions", "[video_core][query_bank]") {
    std::mt19937 random{0x70a};
    for (size_t batch = 0; batch < 1500; ++batch) {
        VideoCommon::QueryBankRanges<true> ordered;
        VideoCommon::QueryBankRanges<false> unordered;
        std::map<size_t, std::pair<size_t, size_t>> reference;
        const size_t inputs = random() % 200;
        for (size_t i = 0; i < inputs; ++i) {
            const size_t bank = batch % 3 == 0 ? 71 : random() % 70;
            const size_t start = random() % 256;
            const size_t amount = random() % (257 - start);
            auto [it, inserted] = reference.try_emplace(bank, start, start + amount);
            if (!inserted) {
                it->second.first = (std::min)(it->second.first, start);
                it->second.second = (std::max)(it->second.second, start + amount);
            }
            ordered.Add(bank, start, amount);
            unordered.Add(bank, start, amount);
        }
        using Entry = std::array<size_t, 3>;
        std::vector<Entry> expected, actual_ordered, actual_unordered;
        for (const auto& [bank, range] : reference) {
            expected.push_back({bank, range.first, range.second - range.first});
        }
        ordered.ForEach([&](size_t bank, size_t start, size_t amount) {
            actual_ordered.push_back({bank, start, amount});
        });
        unordered.ForEach([&](size_t bank, size_t start, size_t amount) {
            actual_unordered.push_back({bank, start, amount});
        });
        std::ranges::sort(actual_unordered);
        REQUIRE(actual_ordered == expected);
        REQUIRE(actual_unordered == expected);
    }
}
