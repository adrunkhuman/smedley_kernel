#include <smedley/game_state/readers.hpp>
#include "interest_allocation.hpp"
#include "interest_batch.hpp"
#include "interest_reconciliation.hpp"
#include "pop_money_write.hpp"
#include <gtest/gtest.h>

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace game_state = smedley::game_state;

namespace
{
    template <typename T, size_t Size>
    void Write(std::array<std::byte, Size> *bytes, size_t offset, const T &value)
    {
        ASSERT_LE(offset + sizeof(value), bytes->size());
        std::memcpy(bytes->data() + offset, &value, sizeof(value));
    }

    struct CountryLookup
    {
        int32_t ordinal;
        const void *country;
        int32_t province_id = -1;
        const void *province = nullptr;
    };

    const void *ResolveCountry(const void *context, int32_t ordinal)
    {
        const auto *lookup = static_cast<const CountryLookup *>(context);
        return ordinal == lookup->ordinal ? lookup->country : nullptr;
    }

}
TEST(InterestBugFixTest, ComputesExactPerDestinationTransfers)
{
    game_state::CountryEconomySnapshot before{};
    before.creditor_destinations = 2;
    before.destination_bank_interest_raw = 400;
    before.destination_ordinals[0] = 7;
    before.destination_ordinals[1] = 9;
    before.destination_bank_interests_raw[0] = 100;
    before.destination_bank_interests_raw[1] = 300;

    game_state::CountryEconomySnapshot after = before;
    after.destination_bank_interest_raw = 475;
    after.destination_bank_interests_raw[0] = 125;
    after.destination_bank_interests_raw[1] = 350;

    interest_bug_fix::DestinationTransferSummary summary{};
    EXPECT_TRUE(interest_bug_fix::ComputeDestinationTransfers(before, after, &summary));
    EXPECT_EQ(summary.transfer_count, 2u);
    EXPECT_EQ(summary.transfer_raw, 75);
    EXPECT_EQ(summary.transfers_raw[0], 25);
    EXPECT_EQ(summary.transfers_raw[1], 50);
    EXPECT_EQ(after.flags, 0u);
}

TEST(InterestBugFixTest, RejectsChangedDestinationOrder)
{
    game_state::CountryEconomySnapshot before{};
    before.creditor_destinations = 1;
    before.destination_ordinals[0] = 7;

    game_state::CountryEconomySnapshot after = before;
    after.destination_ordinals[0] = 8;

    interest_bug_fix::DestinationTransferSummary summary{};
    summary.transfers_raw[0] = 1;
    summary.transfer_count = 1;
    summary.transfer_raw = 1;
    EXPECT_FALSE(interest_bug_fix::ComputeDestinationTransfers(before, after, &summary));
    EXPECT_EQ(after.flags, 0u);
    EXPECT_EQ(summary.transfers_raw[0], 0);
    EXPECT_EQ(summary.transfer_count, 0u);
    EXPECT_EQ(summary.transfer_raw, 0);
}

TEST(InterestBugFixTest, RejectsChangedDestinationIdentity)
{
    game_state::CountryEconomySnapshot before{};
    before.creditor_destinations = 1;
    before.destination_keys[0] = 0x00474e45;
    before.destination_ordinals[0] = 7;

    game_state::CountryEconomySnapshot after = before;
    after.destination_keys[0] = 0x00415246;

    interest_bug_fix::DestinationTransferSummary summary{};
    summary.transfers_raw[0] = 1;
    summary.transfer_count = 1;
    summary.transfer_raw = 1;
    EXPECT_FALSE(interest_bug_fix::ComputeDestinationTransfers(before, after, &summary));
    EXPECT_EQ(after.flags, 0u);
    EXPECT_EQ(summary.transfers_raw[0], 0);
    EXPECT_EQ(summary.transfer_count, 0u);
    EXPECT_EQ(summary.transfer_raw, 0);
}

TEST(InterestBugFixTest, CompletesTransferAfterCreditorEntryDisappears)
{
    std::array<std::byte, 0x1608> debtor{};
    std::array<std::byte, 0x1608> destination{};
    std::array<std::byte, 0x28> debtor_bank{};
    std::array<std::byte, 0x28> destination_bank{};
    const char debtor_tag[4] = {'S', 'W', 'E', '\0'};
    const char destination_tag[4] = {'E', 'N', 'G', '\0'};
    const int32_t debtor_ordinal = 2;
    const int32_t destination_ordinal = 7;
    const int64_t treasury_after = 975;
    const int64_t bank_after = 125;
    const void *debtor_bank_pointer = debtor_bank.data();
    const void *destination_bank_pointer = destination_bank.data();
    Write(&debtor, 0x1c, debtor_tag);
    Write(&debtor, 0x20, debtor_ordinal);
    Write(&debtor, 0xe78, treasury_after);
    Write(&debtor, 0xe88, debtor_bank_pointer);
    Write(&destination, 0x1c, destination_tag);
    Write(&destination, 0x20, destination_ordinal);
    Write(&destination, 0xe88, destination_bank_pointer);
    Write(&destination_bank, 0x20, bank_after);
    const CountryLookup lookup{destination_ordinal, destination.data()};

    game_state::CountryEconomySnapshot before{};
    before.creditor_count = 1;
    before.creditor_destinations = 1;
    std::memcpy(&before.destination_keys[0], destination_tag, 4);
    before.destination_ordinals[0] = destination_ordinal;
    before.destination_bank_interests_raw[0] = 100;
    before.destination_bank_interest_raw = 100;

    auto after = game_state::ReadCountryCreditorBalances(
        before, debtor.data(), 1234, ResolveCountry, &lookup);
    ASSERT_EQ(after.flags, 0u);
    ASSERT_EQ(after.creditor_destinations, 1u);
    interest_bug_fix::DestinationTransferSummary summary{};
    EXPECT_TRUE(interest_bug_fix::ComputeDestinationTransfers(before, after, &summary));
    EXPECT_EQ(summary.transfer_raw, 25);
}

TEST(InterestBugFixTest, AcceptsResidualTreasurySinkBeyondDestinationTransfer)
{
    EXPECT_TRUE(interest_bug_fix::TreasuryLossCoversTransfer(100, 75, 25));
    EXPECT_TRUE(interest_bug_fix::TreasuryLossCoversTransfer(100, 70, 25));
    EXPECT_FALSE(interest_bug_fix::TreasuryLossCoversTransfer(100, 80, 25));
    EXPECT_FALSE(interest_bug_fix::TreasuryLossCoversTransfer(100, 101, 0));
    EXPECT_FALSE(interest_bug_fix::TreasuryLossCoversTransfer(100, 75, -1));
    EXPECT_FALSE(interest_bug_fix::TreasuryLossCoversTransfer(
        (std::numeric_limits<int64_t>::min)(), (std::numeric_limits<int64_t>::min)(), 1));
    int64_t residual = -1;
    EXPECT_TRUE(interest_bug_fix::ComputeTreasuryResidual(100, 70, 25, &residual));
    EXPECT_EQ(residual, 5);
    EXPECT_FALSE(interest_bug_fix::ComputeTreasuryResidual(100, 80, 25, &residual));
    EXPECT_FALSE(interest_bug_fix::ComputeTreasuryResidual(
        (std::numeric_limits<int64_t>::max)(), (std::numeric_limits<int64_t>::min)(), 0, &residual));
}

TEST(InterestBatchTest, AggregatesDomesticForeignAndPrivateAmounts)
{
    interest_bug_fix::DailyInterestBatch batch;
    ASSERT_TRUE(batch.Begin(2400, 4));

    std::array<interest_bug_fix::InterestTransfer, 2> first{{
        {1, {'A', 'A', 'A', '\0'}, 10},
        {2, {'B', 'B', 'B', '\0'}, 20},
    }};
    std::array<interest_bug_fix::InterestTransfer, 1> second{{
        {2, {'B', 'B', 'B', '\0'}, 5},
    }};
    EXPECT_EQ(batch.AddDebtor(1, first.data(), first.size(), 3),
        interest_bug_fix::BatchAddStatus::success);
    EXPECT_EQ(batch.AddDebtor(2, second.data(), second.size(), 0),
        interest_bug_fix::BatchAddStatus::success);
    EXPECT_EQ(batch.AddDebtor(3, nullptr, 0, 7), interest_bug_fix::BatchAddStatus::success);

    EXPECT_TRUE(batch.complete());
    EXPECT_EQ(batch.private_sink_raw(), 10);
    EXPECT_EQ(batch.recipient(1).transfer_raw, 10);
    EXPECT_EQ(batch.recipient(1).domestic_transfer_raw, 10);
    EXPECT_EQ(batch.recipient(1).foreign_transfer_raw, 0);
    EXPECT_EQ(batch.recipient(2).transfer_raw, 25);
    EXPECT_EQ(batch.recipient(2).domestic_transfer_raw, 5);
    EXPECT_EQ(batch.recipient(2).foreign_transfer_raw, 20);
    EXPECT_EQ(batch.recipient(2).source_count, 2u);
}

TEST(InterestBatchTest, RejectsInvalidDebtorsAtomically)
{
    interest_bug_fix::DailyInterestBatch batch;
    ASSERT_TRUE(batch.Begin(2400, 3));
    const interest_bug_fix::InterestTransfer maximum{2, {'B', 'B', 'B', '\0'},
        (std::numeric_limits<int64_t>::max)()};
    const interest_bug_fix::InterestTransfer overflow{2, {'B', 'B', 'B', '\0'}, 1};

    EXPECT_EQ(batch.AddDebtor(1, &maximum, 1, 0), interest_bug_fix::BatchAddStatus::success);
    EXPECT_EQ(batch.AddDebtor(1, nullptr, 0, 0), interest_bug_fix::BatchAddStatus::duplicate_debtor);
    EXPECT_EQ(batch.AddDebtor(2, &overflow, 1, 0), interest_bug_fix::BatchAddStatus::overflow);
    EXPECT_TRUE(batch.RejectDebtor(2));
    EXPECT_TRUE(batch.complete());
    EXPECT_EQ(batch.rejected_debtors(), 1u);
    EXPECT_EQ(batch.recipient(2).transfer_raw, (std::numeric_limits<int64_t>::max)());
}

TEST(InterestBatchTest, TracksDailyPopIdentitiesWithoutAllocation)
{
    static interest_bug_fix::DailyPopSet pops;
    pops.Reset();
    EXPECT_EQ(pops.Insert(0x1000), interest_bug_fix::PointerInsertStatus::inserted);
    EXPECT_EQ(pops.Insert(0x2000), interest_bug_fix::PointerInsertStatus::inserted);
    EXPECT_TRUE(pops.Contains(0x1000));
    EXPECT_FALSE(pops.Contains(0x3000));
    EXPECT_EQ(pops.Insert(0x1000), interest_bug_fix::PointerInsertStatus::duplicate);
    EXPECT_EQ(pops.Insert(0), interest_bug_fix::PointerInsertStatus::duplicate);
    EXPECT_EQ(pops.size(), 2u);
    pops.Reset();
    EXPECT_EQ(pops.size(), 0u);
    EXPECT_EQ(pops.Insert(0x1000), interest_bug_fix::PointerInsertStatus::inserted);
}

TEST(InterestBugFixTest, RejectsFlaggedBeforeSample)
{
    game_state::CountryEconomySnapshot before{};
    before.flags = game_state::SAMPLE_BANK_UNREADABLE;
    game_state::CountryEconomySnapshot after{};

    interest_bug_fix::DestinationTransferSummary summary{};
    summary.transfers_raw[0] = 1;
    summary.transfer_count = 1;
    summary.transfer_raw = 1;
    EXPECT_FALSE(interest_bug_fix::ComputeDestinationTransfers(before, after, &summary));
    EXPECT_FALSE(interest_bug_fix::ComputeDestinationTransfers(before, after, nullptr));
    EXPECT_EQ(after.flags, 0u);
    EXPECT_EQ(summary.transfers_raw[0], 0);
    EXPECT_EQ(summary.transfer_count, 0u);
    EXPECT_EQ(summary.transfer_raw, 0);
}

TEST(InterestBugFixTest, ValidatesPopMoneyWriteSpan)
{
    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    const size_t page_size = system_info.dwPageSize;
    ASSERT_NE(page_size, 0u);
    auto *pages = static_cast<std::byte *>(VirtualAlloc(
        nullptr, page_size * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    ASSERT_NE(pages, nullptr);

    const void *pop = pages + page_size - 0x180 - 1;
    EXPECT_TRUE(interest_bug_fix::CanWritePopMoney(pop));

    DWORD writable_protection = 0;
    const BOOL made_readonly = VirtualProtect(
        pages + page_size, page_size, PAGE_READONLY, &writable_protection);
    EXPECT_NE(made_readonly, FALSE);
    if (made_readonly != FALSE) {
        EXPECT_FALSE(interest_bug_fix::CanWritePopMoney(pop));
        DWORD restored_protection = 0;
        EXPECT_NE(VirtualProtect(pages + page_size, page_size, writable_protection, &restored_protection), FALSE);
    }
    EXPECT_FALSE(interest_bug_fix::CanWritePopMoney(nullptr));
    EXPECT_NE(VirtualFree(pages, 0, MEM_RELEASE), FALSE);
}

TEST(InterestAllocationTest, ConservesPayoutWithDeterministicRemainders)
{
    std::array<interest_bug_fix::AllocationEntry, 3> entries{{{1}, {1}, {1}}};
    std::array<uint32_t, 3> scratch{};

    EXPECT_EQ(interest_bug_fix::AllocateInterest(1, entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_bug_fix::AllocationStatus::success);
    EXPECT_EQ(entries[0].payout_raw, 334);
    EXPECT_EQ(entries[1].payout_raw, 333);
    EXPECT_EQ(entries[2].payout_raw, 333);
}

TEST(InterestAllocationTest, UsesLargestFractionalRemainder)
{
    std::array<interest_bug_fix::AllocationEntry, 3> entries{{{3}, {2}, {1}}};
    std::array<uint32_t, 3> scratch{};

    EXPECT_EQ(interest_bug_fix::AllocateInterest(1, entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_bug_fix::AllocationStatus::success);
    EXPECT_EQ(entries[0].payout_raw, 500);
    EXPECT_EQ(entries[1].payout_raw, 333);
    EXPECT_EQ(entries[2].payout_raw, 167);
}

TEST(InterestAllocationTest, IgnoresNonpositiveSavings)
{
    std::array<interest_bug_fix::AllocationEntry, 3> entries{{{10}, {0}, {-5}}};
    std::array<uint32_t, 1> scratch{};

    EXPECT_EQ(interest_bug_fix::AllocateInterest(2, entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_bug_fix::AllocationStatus::success);
    EXPECT_EQ(entries[0].payout_raw, 2000);
    EXPECT_EQ(entries[1].payout_raw, 0);
    EXPECT_EQ(entries[2].payout_raw, 0);
}

TEST(InterestAllocationTest, RejectsNoEligibleSavingsAndOverflow)
{
    std::array<interest_bug_fix::AllocationEntry, 2> entries{{{0}, {-1}}};
    std::array<uint32_t, 2> scratch{};
    EXPECT_EQ(interest_bug_fix::AllocateInterest(1, entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_bug_fix::AllocationStatus::no_eligible_savings);

    entries = {{{2}, {1}}};
    const int64_t large_transfer = (std::numeric_limits<int64_t>::max)() / 1000;
    EXPECT_EQ(interest_bug_fix::AllocateInterest(
        large_transfer, entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_bug_fix::AllocationStatus::success);
    EXPECT_EQ(entries[0].payout_raw + entries[1].payout_raw, large_transfer * 1000);
    EXPECT_GT(entries[0].payout_raw, entries[1].payout_raw);
}

TEST(InterestAllocationTest, ClearsReusedOutputsForNoPayment)
{
    std::array<interest_bug_fix::AllocationEntry, 1> entries{{{1}}};
    std::array<uint32_t, 1> scratch{};
    ASSERT_EQ(interest_bug_fix::AllocateInterest(1, entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_bug_fix::AllocationStatus::success);
    ASSERT_EQ(entries[0].payout_raw, 1000);

    EXPECT_EQ(interest_bug_fix::AllocateInterest(0, entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_bug_fix::AllocationStatus::no_payment);
    EXPECT_EQ(entries[0].payout_raw, 0);
    EXPECT_EQ(entries[0].remainder, 0u);
}

TEST(InterestAllocationTest, DistinguishesInvalidEmptyAndShortScratchInputs)
{
    std::array<interest_bug_fix::AllocationEntry, 1> entries{{{1}}};
    EXPECT_EQ(interest_bug_fix::AllocateInterest(1, nullptr, 1, nullptr, 0),
        interest_bug_fix::AllocationStatus::invalid_input);
    EXPECT_EQ(interest_bug_fix::AllocateInterest(1, nullptr, 0, nullptr, 0),
        interest_bug_fix::AllocationStatus::no_eligible_savings);
    EXPECT_EQ(interest_bug_fix::AllocateInterest(1, entries.data(), entries.size(), nullptr, 0),
        interest_bug_fix::AllocationStatus::scratch_too_small);
    EXPECT_EQ(entries[0].payout_raw, 0);
}

TEST(InterestAllocationTest, RejectsSavingsSumAndPayoutScaleOverflow)
{
    std::array<interest_bug_fix::AllocationEntry, 2> entries{{
        {(std::numeric_limits<int64_t>::max)()}, {1}}};
    std::array<uint32_t, 2> scratch{};
    EXPECT_EQ(interest_bug_fix::AllocateInterest(1, entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_bug_fix::AllocationStatus::overflow);
    EXPECT_EQ(entries[0].payout_raw, 0);
    EXPECT_EQ(entries[1].payout_raw, 0);

    entries = {{{1}, {1}}};
    EXPECT_EQ(interest_bug_fix::AllocateInterest((std::numeric_limits<int64_t>::max)() / 1000 + 1,
        entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_bug_fix::AllocationStatus::overflow);
    EXPECT_EQ(entries[0].payout_raw, 0);
    EXPECT_EQ(entries[1].payout_raw, 0);
}
