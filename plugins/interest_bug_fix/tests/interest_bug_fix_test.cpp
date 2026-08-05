#include <smedley/game_state/readers.hpp>
#include "interest_allocation.hpp"
#include "interest_batch.hpp"
#include "interest_mutation_status.hpp"
#include "interest_reconciliation.hpp"
#include <gtest/gtest.h>

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

    game_state::CountryRef ResolveCountry(const void *context, int32_t ordinal)
    {
        const auto *lookup = static_cast<const CountryLookup *>(context);
        return ordinal == lookup->ordinal ? game_state::CountryRef{lookup->country} : game_state::CountryRef{};
    }

}
TEST(InterestBugFixTest, ComputesNamedTransfersDespiteArbitraryTreasuryChanges)
{
    game_state::CountryEconomySnapshot before{};
    before.treasury_raw = -500;
    before.creditor_destinations = 2;
    before.destination_bank_interest_raw = 400;
    before.destination_ordinals[0] = 7;
    before.destination_ordinals[1] = 9;
    before.destination_bank_interests_raw[0] = 100;
    before.destination_bank_interests_raw[1] = 300;

    game_state::CountryEconomySnapshot after = before;
    after.treasury_raw = (std::numeric_limits<int64_t>::max)();
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
    interest_bug_fix::ReconciliationFailure failure = interest_bug_fix::ReconciliationFailure::none;
    EXPECT_FALSE(interest_bug_fix::ComputeDestinationTransfers(before, after, &summary, &failure));
    EXPECT_EQ(failure, interest_bug_fix::ReconciliationFailure::destination_identity_changed);
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
    interest_bug_fix::ReconciliationFailure failure = interest_bug_fix::ReconciliationFailure::none;
    EXPECT_FALSE(interest_bug_fix::ComputeDestinationTransfers(before, after, &summary, &failure));
    EXPECT_EQ(failure, interest_bug_fix::ReconciliationFailure::destination_identity_changed);
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
        before, game_state::CountryRef{debtor.data()}, 1234, ResolveCountry, &lookup);
    ASSERT_EQ(after.flags, 0u);
    ASSERT_EQ(after.creditor_destinations, 1u);
    interest_bug_fix::DestinationTransferSummary summary{};
    EXPECT_TRUE(interest_bug_fix::ComputeDestinationTransfers(before, after, &summary));
    EXPECT_EQ(summary.transfer_raw, 25);
}

TEST(InterestBatchTest, AggregatesNamedDomesticAndForeignAmounts)
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
    EXPECT_EQ(batch.AddDebtor(1, first.data(), first.size()),
        interest_bug_fix::BatchAddStatus::success);
    EXPECT_EQ(batch.AddDebtor(2, second.data(), second.size()),
        interest_bug_fix::BatchAddStatus::success);
    EXPECT_EQ(batch.AddDebtor(3, nullptr, 0), interest_bug_fix::BatchAddStatus::success);

    EXPECT_TRUE(batch.complete());
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

    EXPECT_EQ(batch.AddDebtor(1, &maximum, 1), interest_bug_fix::BatchAddStatus::success);
    EXPECT_EQ(batch.AddDebtor(1, nullptr, 0), interest_bug_fix::BatchAddStatus::duplicate_debtor);
    EXPECT_EQ(batch.AddDebtor(2, &overflow, 1), interest_bug_fix::BatchAddStatus::overflow);
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
    interest_bug_fix::ReconciliationFailure failure = interest_bug_fix::ReconciliationFailure::none;
    EXPECT_FALSE(interest_bug_fix::ComputeDestinationTransfers(before, after, &summary, &failure));
    EXPECT_EQ(failure, interest_bug_fix::ReconciliationFailure::before_flags);
    EXPECT_FALSE(interest_bug_fix::ComputeDestinationTransfers(before, after, nullptr));
    EXPECT_EQ(after.flags, 0u);
    EXPECT_EQ(summary.transfers_raw[0], 0);
    EXPECT_EQ(summary.transfer_count, 0u);
    EXPECT_EQ(summary.transfer_raw, 0);
}

TEST(InterestBugFixTest, ClassifiesMutationFailuresWithoutOverstatingPostconditions)
{
    using interest_bug_fix::PopInterestFailureClass;
    using game_state::PopInterestMutationStatus;

    struct ExpectedFailure
    {
        PopInterestMutationStatus status;
        PopInterestFailureClass failure;
        bool unsafe;
    };
    constexpr ExpectedFailure failures[] = {
        {PopInterestMutationStatus::invalid_context, PopInterestFailureClass::unavailable, true},
        {PopInterestMutationStatus::invalid_phase, PopInterestFailureClass::unavailable, true},
        {PopInterestMutationStatus::invalid_thread, PopInterestFailureClass::unavailable, true},
        {PopInterestMutationStatus::invalid_amount, PopInterestFailureClass::precondition_changed, false},
        {PopInterestMutationStatus::balance_unreadable, PopInterestFailureClass::balance, false},
        {PopInterestMutationStatus::balance_overflow, PopInterestFailureClass::balance, false},
        {PopInterestMutationStatus::not_writable, PopInterestFailureClass::not_writable, false},
        {PopInterestMutationStatus::signature_mismatch, PopInterestFailureClass::unavailable, true},
        {PopInterestMutationStatus::unavailable, PopInterestFailureClass::unavailable, true},
        {PopInterestMutationStatus::state_changed, PopInterestFailureClass::precondition_changed, false},
        {PopInterestMutationStatus::postcondition_failed, PopInterestFailureClass::postcondition_failed, true},
    };
    for (const auto &failure : failures) {
        EXPECT_EQ(interest_bug_fix::ClassifyPopInterestFailure(failure.status), failure.failure);
        EXPECT_EQ(interest_bug_fix::IsUnsafePopInterestFailure(failure.status), failure.unsafe);
    }
    EXPECT_EQ(interest_bug_fix::ClassifyAppliedPopInterestFailure(PopInterestMutationStatus::state_changed, true),
        PopInterestFailureClass::partial_mutation);
    EXPECT_EQ(interest_bug_fix::ClassifyAppliedPopInterestFailure(PopInterestMutationStatus::state_changed, false),
        PopInterestFailureClass::precondition_changed);
    EXPECT_FALSE(interest_bug_fix::IsUnsafeAppliedPopInterestFailure(PopInterestMutationStatus::not_writable, false));
    EXPECT_TRUE(interest_bug_fix::IsUnsafeAppliedPopInterestFailure(PopInterestMutationStatus::state_changed, true));
    EXPECT_TRUE(interest_bug_fix::IsUnsafeAppliedPopInterestFailure(PopInterestMutationStatus::postcondition_failed, false));
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
