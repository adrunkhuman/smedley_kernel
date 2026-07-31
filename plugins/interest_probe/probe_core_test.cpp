#include "probe_core.hpp"
#include "pair_queue.hpp"
#include "interest_allocation.hpp"
#include "economic_telemetry_core.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace
{
    template <typename T, size_t Size>
    void Write(std::array<std::byte, Size> *bytes, size_t offset, const T &value)
    {
        ASSERT_LE(offset + sizeof(value), bytes->size());
        std::memcpy(bytes->data() + offset, &value, sizeof(value));
    }

    struct Node
    {
        const void *data;
        const Node *previous;
        const Node *next;
        uint8_t deleted;
        uint8_t padding[3];
    };

    struct CountryLookup
    {
        int32_t ordinal;
        const void *country;
        int32_t province_id = -1;
        const void *province = nullptr;
    };

    struct PopList
    {
        const void *first;
        const void *last;
        int32_t count;
        uint32_t unknown;
    };

    const void *ResolveCountry(const void *context, int32_t ordinal)
    {
        const auto *lookup = static_cast<const CountryLookup *>(context);
        return ordinal == lookup->ordinal ? lookup->country : nullptr;
    }

    const void *ResolveProvince(const void *context, int32_t id)
    {
        const auto *lookup = static_cast<const CountryLookup *>(context);
        return id == lookup->province_id ? lookup->province : nullptr;
    }
}

TEST(InterestProbeTest, CollectsBoundedStateAndBankCandidates)
{
    std::array<std::byte, 0x1608> country{};
    std::array<std::byte, 0x290> state{};
    std::array<std::byte, 0x28> bank{};
    std::array<std::byte, 0x28> creditor_a{};
    std::array<std::byte, 0x28> creditor_b{};
    std::array<int, 2> provinces{7, 11};
    std::array<void *, 2> creditors{creditor_a.data(), creditor_b.data()};
    Node node{state.data(), nullptr, nullptr, 0, {}};

    const char tag[4] = {'E', 'N', 'G', '\0'};
    const int state_count = 1;
    const int64_t treasury = 90;
    const int64_t savings = 120;
    const int64_t interest = 30;
    const int64_t bank_interest = 40;
    const char creditor_a_tag[4] = {'F', 'R', 'A', '\0'};
    const char creditor_b_tag[4] = {'P', 'R', 'U', '\0'};
    const int32_t creditor_a_ordinal = 2;
    const int32_t creditor_b_ordinal = 3;
    const int64_t creditor_a_interest = 5;
    const int64_t creditor_b_interest = 7;
    const int64_t creditor_a_debt = 20;
    const int64_t creditor_b_debt = 30;
    const uint8_t creditor_a_paid = 1;
    const uint8_t creditor_b_paid = 0;
    const void *state_head = &node;
    const void *state_tail = &node;
    const void *bank_pointer = bank.data();
    const void *province_begin = provinces.data();
    const void *province_end = provinces.data() + provinces.size();
    const void *creditor_begin = creditors.data();
    const void *creditor_end = creditors.data() + creditors.size();

    Write(&country, 0x1c, tag);
    Write(&country, 0xe44, state_head);
    Write(&country, 0xe48, state_tail);
    Write(&country, 0xe4c, state_count);
    Write(&country, 0xe78, treasury);
    Write(&country, 0xe88, bank_pointer);
    Write(&country, 0xe8c, creditor_begin);
    Write(&country, 0xe90, creditor_end);
    Write(&country, 0xe94, creditor_end);
    Write(&state, 0x48, province_begin);
    Write(&state, 0x4c, province_end);
    Write(&state, 0x50, province_end);
    Write(&state, 0x258, savings);
    Write(&state, 0x260, interest);
    Write(&bank, 0x20, bank_interest);
    Write(&creditor_a, 0x08, creditor_a_tag);
    Write(&creditor_a, 0x0c, creditor_a_ordinal);
    Write(&creditor_a, 0x10, creditor_a_interest);
    Write(&creditor_a, 0x18, creditor_a_debt);
    Write(&creditor_a, 0x20, creditor_a_paid);
    Write(&creditor_b, 0x08, creditor_b_tag);
    Write(&creditor_b, 0x0c, creditor_b_ordinal);
    Write(&creditor_b, 0x10, creditor_b_interest);
    Write(&creditor_b, 0x18, creditor_b_debt);
    Write(&creditor_b, 0x20, creditor_b_paid);

    const auto sample = interest_probe::CollectSample(country.data(), 1234);
    EXPECT_STREQ(sample.country_tag, "ENG");
    EXPECT_EQ(sample.date_raw, 1234);
    EXPECT_EQ(sample.state_count_reported, 1);
    EXPECT_EQ(sample.states_walked, 1u);
    EXPECT_EQ(sample.province_element_candidates, 2u);
    EXPECT_EQ(sample.states_with_savings, 1u);
    EXPECT_EQ(sample.states_with_interest, 1u);
    EXPECT_EQ(sample.creditor_count, 2u);
    EXPECT_EQ(sample.creditors_was_paid, 1u);
    EXPECT_EQ(sample.creditor_interest_raw, creditor_a_interest + creditor_b_interest);
    EXPECT_EQ(sample.creditor_debt_raw, creditor_a_debt + creditor_b_debt);
    EXPECT_EQ(sample.treasury_raw, treasury);
    EXPECT_EQ(sample.state_savings_raw, savings);
    EXPECT_EQ(sample.state_interest_raw, interest);
    EXPECT_EQ(sample.bank_interest_raw, bank_interest);
    EXPECT_EQ(sample.flags, 0u);
}

TEST(InterestProbeTest, RejectsSelfReferentialStateList)
{
    std::array<std::byte, 0x1608> country{};
    std::array<std::byte, 0x290> state{};
    Node node{state.data(), nullptr, nullptr, 0, {}};
    node.next = &node;
    const void *head = &node;
    const int state_count = 2;
    Write(&country, 0xe44, head);
    Write(&country, 0xe48, head);
    Write(&country, 0xe4c, state_count);

    const auto sample = interest_probe::CollectSample(country.data(), 0);
    EXPECT_EQ(sample.states_walked, 1u);
    EXPECT_NE(sample.flags & interest_probe::SAMPLE_STATE_LIST_INVALID, 0u);
    EXPECT_NE(sample.flags & interest_probe::SAMPLE_STATE_COUNT_MISMATCH, 0u);
}

TEST(InterestProbeTest, RejectsMalformedProvinceVector)
{
    std::array<std::byte, 0x1608> country{};
    std::array<std::byte, 0x290> state{};
    std::array<int, 1> provinces{};
    Node node{state.data(), nullptr, nullptr, 0, {}};
    const void *head = &node;
    const int state_count = 1;
    const void *begin = provinces.data() + 1;
    const void *end = provinces.data();
    Write(&country, 0xe44, head);
    Write(&country, 0xe48, head);
    Write(&country, 0xe4c, state_count);
    Write(&state, 0x48, begin);
    Write(&state, 0x4c, end);
    Write(&state, 0x50, begin);

    const auto sample = interest_probe::CollectSample(country.data(), 0);
    EXPECT_NE(sample.flags & interest_probe::SAMPLE_STATE_VECTOR_INVALID, 0u);
}

TEST(InterestProbeTest, AggregatesOpaqueCreditorsWithoutPollutingPopTraversal)
{
    std::array<std::byte, 0x1608> country{};
    std::array<std::byte, 0x28> bank{};
    std::array<std::byte, 0x28> creditor{};
    std::array<void *, 1> creditors{creditor.data()};
    const int64_t interest = 15;
    const int64_t debt = 90;
    const uint8_t was_paid = 1;
    const void *bank_pointer = bank.data();
    const void *creditor_begin = creditors.data();
    const void *creditor_end = creditors.data() + creditors.size();

    Write(&country, 0xe88, bank_pointer);
    Write(&country, 0xe8c, creditor_begin);
    Write(&country, 0xe90, creditor_end);
    Write(&country, 0xe94, creditor_end);
    Write(&creditor, 0x10, interest);
    Write(&creditor, 0x18, debt);
    Write(&creditor, 0x20, was_paid);

    const auto aggregate = interest_probe::CollectSample(country.data(), 1234);
    EXPECT_EQ(aggregate.creditor_count, 1u);
    EXPECT_EQ(aggregate.creditors_was_paid, 1u);
    EXPECT_EQ(aggregate.creditor_interest_raw, interest);
    EXPECT_EQ(aggregate.creditor_debt_raw, debt);
    EXPECT_EQ(aggregate.flags, 0u);

    const uint8_t invalid_paid = 2;
    Write(&creditor, 0x20, invalid_paid);
    const CountryLookup lookup{};
    uint32_t candidate_count = 0;
    interest_probe::Sample quality{};
    EXPECT_TRUE(interest_probe::CollectCountryPops(country.data(), 1234, ResolveProvince, &lookup,
        nullptr, 0, interest_probe::max_sample_destination_provinces, &candidate_count, &quality));
    EXPECT_EQ(candidate_count, 0u);
    EXPECT_EQ(quality.creditor_count, 1u);
    EXPECT_EQ(quality.flags, 0u);
}

TEST(InterestProbeTest, CollectsCreditorAndDestinationCandidates)
{
    std::array<std::byte, 0x1608> debtor{};
    std::array<std::byte, 0x1608> destination{};
    std::array<std::byte, 0x28> creditor{};
    std::array<std::byte, 0x28> debtor_bank{};
    std::array<std::byte, 0x28> destination_bank{};
    std::array<std::byte, 0x290> destination_state{};
    std::array<std::byte, 0x1a0> destination_province{};
    std::array<std::byte, 0x288> destination_pop{};
    std::array<void *, 1> creditors{creditor.data()};
    std::array<int32_t, 1> province_ids{3};
    std::array<PopList, 1> pop_lists{{{destination_pop.data(), destination_pop.data(), 1, 0}}};
    Node destination_node{destination_state.data(), nullptr, nullptr, 0, {}};
    const char debtor_tag[4] = {'S', 'W', 'E', '\0'};
    const char destination_tag[4] = {'E', 'N', 'G', '\0'};
    const int32_t destination_ordinal = 7;
    const int64_t creditor_interest = 15;
    const int64_t creditor_debt = 90;
    const uint8_t was_paid = 1;
    const int64_t destination_bank_interest = 40;
    const int64_t destination_state_savings = 120;
    const int64_t destination_state_interest = 30;
    const int64_t destination_pop_money = 5000;
    const int64_t destination_pop_interest_cash_flow = 40;
    const int64_t destination_pop_total_cash_flow = 100;
    const int64_t destination_pop_savings = 120000;
    const void *debtor_bank_pointer = debtor_bank.data();
    const void *destination_bank_pointer = destination_bank.data();
    const void *destination_state_head = &destination_node;
    const int destination_state_count = 1;
    const void *creditor_begin = creditors.data();
    const void *creditor_end = creditors.data() + creditors.size();
    const void *province_begin = province_ids.data();
    const void *province_end = province_ids.data() + province_ids.size();
    const void *pop_list_begin = pop_lists.data();
    const void *pop_list_end = pop_lists.data() + pop_lists.size();

    Write(&debtor, 0x1c, debtor_tag);
    Write(&debtor, 0xe88, debtor_bank_pointer);
    Write(&debtor, 0xe8c, creditor_begin);
    Write(&debtor, 0xe90, creditor_end);
    Write(&debtor, 0xe94, creditor_end);
    Write(&destination, 0x1c, destination_tag);
    Write(&destination, 0x20, destination_ordinal);
    Write(&destination, 0xe44, destination_state_head);
    Write(&destination, 0xe48, destination_state_head);
    Write(&destination, 0xe4c, destination_state_count);
    Write(&destination, 0xe88, destination_bank_pointer);
    Write(&destination_bank, 0x20, destination_bank_interest);
    Write(&destination_state, 0x258, destination_state_savings);
    Write(&destination_state, 0x260, destination_state_interest);
    Write(&destination_state, 0x48, province_begin);
    Write(&destination_state, 0x4c, province_end);
    Write(&destination_state, 0x50, province_end);
    Write(&destination_province, 0x194, pop_list_begin);
    Write(&destination_province, 0x198, pop_list_end);
    Write(&destination_province, 0x19c, pop_list_end);
    Write(&destination_pop, 0x180, destination_pop_money);
    Write(&destination_pop, 0x210, destination_pop_interest_cash_flow);
    Write(&destination_pop, 0x218, destination_pop_total_cash_flow);
    Write(&destination_pop, 0x250, destination_pop_savings);
    Write(&creditor, 0x08, destination_tag);
    Write(&creditor, 0x0c, destination_ordinal);
    Write(&creditor, 0x10, creditor_interest);
    Write(&creditor, 0x18, creditor_debt);
    Write(&creditor, 0x20, was_paid);
    const CountryLookup lookup{destination_ordinal, destination.data(), province_ids[0], destination_province.data()};

    const void *immediate_pop = nullptr;
    const auto sample = interest_probe::CollectSample(
        debtor.data(), 1234, ResolveCountry, ResolveProvince, &lookup, &immediate_pop);
    EXPECT_EQ(sample.creditor_count, 1u);
    EXPECT_EQ(sample.creditor_destinations, 1u);
    EXPECT_EQ(sample.creditors_was_paid, 1u);
    EXPECT_EQ(sample.creditor_interest_raw, creditor_interest);
    EXPECT_EQ(sample.creditor_debt_raw, creditor_debt);
    EXPECT_EQ(sample.destination_bank_interest_raw, destination_bank_interest);
    EXPECT_EQ(sample.destination_state_savings_raw, destination_state_savings);
    EXPECT_EQ(sample.destination_state_interest_raw, destination_state_interest);
    EXPECT_EQ(sample.destination_provinces_resolved, 1u);
    EXPECT_EQ(sample.destination_province_attempts, 1u);
    EXPECT_EQ(sample.destination_pop_lists, 1u);
    EXPECT_EQ(sample.destination_pops, 1u);
    EXPECT_EQ(sample.destination_pop_attempts, 1u);
    EXPECT_EQ(sample.destination_pop_savings_raw, destination_pop_savings);
    EXPECT_EQ(sample.destination_pop_savings_state_scale_raw, destination_state_savings);
    EXPECT_EQ(sample.flags, 0u);
    EXPECT_EQ(immediate_pop, destination_pop.data());

    const auto aggregate_only = interest_probe::CollectSample(debtor.data(), 1234);
    EXPECT_EQ(aggregate_only.creditor_count, 1u);
    EXPECT_EQ(aggregate_only.creditors_was_paid, 1u);
    EXPECT_EQ(aggregate_only.creditor_interest_raw, creditor_interest);
    EXPECT_EQ(aggregate_only.creditor_debt_raw, creditor_debt);
    EXPECT_EQ(aggregate_only.creditor_destinations, 0u);
    EXPECT_EQ(aggregate_only.flags, 0u);

    interest_probe::PopMoneySnapshot snapshot{};
    ASSERT_TRUE(interest_probe::ReadPopMoneySnapshot(immediate_pop, &snapshot));
    EXPECT_EQ(snapshot.money_raw, destination_pop_money);
    EXPECT_EQ(snapshot.interest_cash_flow_raw, destination_pop_interest_cash_flow);
    EXPECT_EQ(snapshot.total_cash_flow_raw, destination_pop_total_cash_flow);
    EXPECT_EQ(snapshot.savings_raw, destination_pop_savings);
    EXPECT_FALSE(interest_probe::ReadPopMoneySnapshot(nullptr, &snapshot));
    EXPECT_TRUE(interest_probe::CanWritePopMoney(destination_pop.data()));
    EXPECT_FALSE(interest_probe::CanWritePopMoney(nullptr));

    std::array<interest_probe::PopCandidate, 1> candidates{};
    uint32_t candidate_count = 0;
    interest_probe::Sample pop_quality{};
    ASSERT_TRUE(interest_probe::CollectCountryPops(destination.data(), 1234, ResolveProvince, &lookup,
        candidates.data(), candidates.size(), interest_probe::max_sample_destination_provinces,
        &candidate_count, &pop_quality));
    EXPECT_EQ(candidate_count, 1u);
    EXPECT_EQ(candidates[0].address, destination_pop.data());
    EXPECT_EQ(candidates[0].savings_raw, destination_pop_savings);
    EXPECT_EQ(pop_quality.flags, 0u);

    pop_lists[0].count = 2;
    const auto mismatched = interest_probe::CollectSample(
        debtor.data(), 1234, ResolveCountry, ResolveProvince, &lookup);
    EXPECT_NE(mismatched.flags & interest_probe::SAMPLE_POP_LIST_INVALID, 0u);

    pop_lists[0].count = 100001;
    const auto limited = interest_probe::CollectSample(
        debtor.data(), 1234, ResolveCountry, ResolveProvince, &lookup);
    EXPECT_NE(limited.flags & interest_probe::SAMPLE_POP_LIMIT, 0u);

    pop_lists[0].count = 1;
    std::array<int32_t, 2> duplicate_province_ids{province_ids[0], province_ids[0]};
    const void *duplicate_begin = duplicate_province_ids.data();
    const void *duplicate_end = duplicate_province_ids.data() + duplicate_province_ids.size();
    Write(&destination_state, 0x48, duplicate_begin);
    Write(&destination_state, 0x4c, duplicate_end);
    Write(&destination_state, 0x50, duplicate_end);
    const auto duplicate = interest_probe::CollectSample(
        debtor.data(), 1234, ResolveCountry, ResolveProvince, &lookup);
    EXPECT_NE(duplicate.flags & interest_probe::SAMPLE_DUPLICATE_PROVINCE, 0u);
    EXPECT_NE(duplicate.flags & interest_probe::SAMPLE_DUPLICATE_POP, 0u);
}

TEST(InterestProbeTest, RejectsMismatchedCreditorDestination)
{
    std::array<std::byte, 0x1608> debtor{};
    std::array<std::byte, 0x1608> destination{};
    std::array<std::byte, 0x28> creditor{};
    std::array<std::byte, 0x28> debtor_bank{};
    std::array<void *, 1> creditors{creditor.data()};
    const char debtor_tag[4] = {'S', 'W', 'E', '\0'};
    const char creditor_tag[4] = {'E', 'N', 'G', '\0'};
    const char destination_tag[4] = {'F', 'R', 'A', '\0'};
    const int32_t destination_ordinal = 7;
    const void *debtor_bank_pointer = debtor_bank.data();
    const void *creditor_begin = creditors.data();
    const void *creditor_end = creditors.data() + creditors.size();

    Write(&debtor, 0x1c, debtor_tag);
    Write(&debtor, 0xe88, debtor_bank_pointer);
    Write(&debtor, 0xe8c, creditor_begin);
    Write(&debtor, 0xe90, creditor_end);
    Write(&debtor, 0xe94, creditor_end);
    Write(&destination, 0x1c, destination_tag);
    Write(&destination, 0x20, destination_ordinal);
    Write(&creditor, 0x08, creditor_tag);
    Write(&creditor, 0x0c, destination_ordinal);
    const CountryLookup lookup{destination_ordinal, destination.data()};

    const auto sample = interest_probe::CollectSample(debtor.data(), 1234, ResolveCountry, nullptr, &lookup);
    EXPECT_EQ(sample.creditor_destinations, 0u);
    EXPECT_NE(sample.flags & interest_probe::SAMPLE_CREDITOR_DESTINATION_INVALID, 0u);
}

TEST(InterestProbeTest, PairQueueRejectsOnlyCompletePairsAtCapacity)
{
    interest_probe::PairQueue<4> queue;
    for (int date = 1; date <= 3; ++date) {
        interest_probe::SamplePair pair{};
        pair.before.date_raw = date;
        pair.after.date_raw = date;
        ASSERT_TRUE(queue.TryPush(pair));
    }
    interest_probe::SamplePair rejected{};
    EXPECT_FALSE(queue.TryPush(rejected));

    for (int date = 1; date <= 3; ++date) {
        interest_probe::SamplePair pair{};
        ASSERT_TRUE(queue.TryPop(&pair));
        EXPECT_EQ(pair.before.date_raw, date);
        EXPECT_EQ(pair.after.date_raw, date);
    }
    interest_probe::SamplePair empty{};
    EXPECT_FALSE(queue.TryPop(&empty));
}

TEST(InterestProbeTest, ComputesExactPerDestinationTransfers)
{
    interest_probe::Sample before{};
    before.creditor_destinations = 2;
    before.destination_bank_interest_raw = 400;
    before.destination_ordinals[0] = 7;
    before.destination_ordinals[1] = 9;
    before.destination_bank_interests_raw[0] = 100;
    before.destination_bank_interests_raw[1] = 300;

    interest_probe::Sample after = before;
    after.destination_bank_interest_raw = 475;
    after.destination_bank_interests_raw[0] = 125;
    after.destination_bank_interests_raw[1] = 350;

    EXPECT_TRUE(interest_probe::ComputeDestinationTransfers(before, &after));
    EXPECT_EQ(after.destination_transfer_count, 2u);
    EXPECT_EQ(after.destination_transfer_raw, 75);
    EXPECT_EQ(after.destination_transfers_raw[0], 25);
    EXPECT_EQ(after.destination_transfers_raw[1], 50);
    EXPECT_EQ(after.flags, 0u);
}

TEST(InterestProbeTest, RejectsChangedDestinationOrder)
{
    interest_probe::Sample before{};
    before.creditor_destinations = 1;
    before.destination_ordinals[0] = 7;

    interest_probe::Sample after = before;
    after.destination_ordinals[0] = 8;

    EXPECT_FALSE(interest_probe::ComputeDestinationTransfers(before, &after));
    EXPECT_NE(after.flags & interest_probe::SAMPLE_DESTINATION_TRANSFER_INVALID, 0u);
}

TEST(InterestProbeTest, RejectsFlaggedBeforeSample)
{
    interest_probe::Sample before{};
    before.flags = interest_probe::SAMPLE_BANK_UNREADABLE;
    interest_probe::Sample after{};

    EXPECT_FALSE(interest_probe::ComputeDestinationTransfers(before, &after));
    EXPECT_NE(after.flags & interest_probe::SAMPLE_DESTINATION_TRANSFER_INVALID, 0u);
}

TEST(InterestAllocationTest, ConservesPayoutWithDeterministicRemainders)
{
    std::array<interest_probe::AllocationEntry, 3> entries{{{1}, {1}, {1}}};
    std::array<uint32_t, 3> scratch{};

    EXPECT_EQ(interest_probe::AllocateInterest(1, entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_probe::AllocationStatus::success);
    EXPECT_EQ(entries[0].payout_raw, 334);
    EXPECT_EQ(entries[1].payout_raw, 333);
    EXPECT_EQ(entries[2].payout_raw, 333);
}

TEST(InterestAllocationTest, UsesLargestFractionalRemainder)
{
    std::array<interest_probe::AllocationEntry, 3> entries{{{3}, {2}, {1}}};
    std::array<uint32_t, 3> scratch{};

    EXPECT_EQ(interest_probe::AllocateInterest(1, entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_probe::AllocationStatus::success);
    EXPECT_EQ(entries[0].payout_raw, 500);
    EXPECT_EQ(entries[1].payout_raw, 333);
    EXPECT_EQ(entries[2].payout_raw, 167);
}

TEST(InterestAllocationTest, IgnoresNonpositiveSavings)
{
    std::array<interest_probe::AllocationEntry, 3> entries{{{10}, {0}, {-5}}};
    std::array<uint32_t, 1> scratch{};

    EXPECT_EQ(interest_probe::AllocateInterest(2, entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_probe::AllocationStatus::success);
    EXPECT_EQ(entries[0].payout_raw, 2000);
    EXPECT_EQ(entries[1].payout_raw, 0);
    EXPECT_EQ(entries[2].payout_raw, 0);
}

TEST(InterestAllocationTest, RejectsNoEligibleSavingsAndOverflow)
{
    std::array<interest_probe::AllocationEntry, 2> entries{{{0}, {-1}}};
    std::array<uint32_t, 2> scratch{};
    EXPECT_EQ(interest_probe::AllocateInterest(1, entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_probe::AllocationStatus::no_eligible_savings);

    entries = {{{2}, {1}}};
    EXPECT_EQ(interest_probe::AllocateInterest((std::numeric_limits<int64_t>::max)() / 1000,
        entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_probe::AllocationStatus::overflow);
    EXPECT_EQ(entries[0].payout_raw, 0);
    EXPECT_EQ(entries[1].payout_raw, 0);
}

TEST(InterestAllocationTest, ClearsReusedOutputsForNoPayment)
{
    std::array<interest_probe::AllocationEntry, 1> entries{{{1}}};
    std::array<uint32_t, 1> scratch{};
    ASSERT_EQ(interest_probe::AllocateInterest(1, entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_probe::AllocationStatus::success);
    ASSERT_EQ(entries[0].payout_raw, 1000);

    EXPECT_EQ(interest_probe::AllocateInterest(0, entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_probe::AllocationStatus::no_payment);
    EXPECT_EQ(entries[0].payout_raw, 0);
    EXPECT_EQ(entries[0].remainder, 0u);
}

TEST(InterestAllocationTest, DistinguishesInvalidEmptyAndShortScratchInputs)
{
    std::array<interest_probe::AllocationEntry, 1> entries{{{1}}};
    EXPECT_EQ(interest_probe::AllocateInterest(1, nullptr, 1, nullptr, 0),
        interest_probe::AllocationStatus::invalid_input);
    EXPECT_EQ(interest_probe::AllocateInterest(1, nullptr, 0, nullptr, 0),
        interest_probe::AllocationStatus::no_eligible_savings);
    EXPECT_EQ(interest_probe::AllocateInterest(1, entries.data(), entries.size(), nullptr, 0),
        interest_probe::AllocationStatus::scratch_too_small);
    EXPECT_EQ(entries[0].payout_raw, 0);
}

TEST(InterestAllocationTest, RejectsSavingsSumAndPayoutScaleOverflow)
{
    std::array<interest_probe::AllocationEntry, 2> entries{{
        {(std::numeric_limits<int64_t>::max)()}, {1}}};
    std::array<uint32_t, 2> scratch{};
    EXPECT_EQ(interest_probe::AllocateInterest(1, entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_probe::AllocationStatus::overflow);
    EXPECT_EQ(entries[0].payout_raw, 0);
    EXPECT_EQ(entries[1].payout_raw, 0);

    entries = {{{1}, {1}}};
    EXPECT_EQ(interest_probe::AllocateInterest((std::numeric_limits<int64_t>::max)() / 1000 + 1,
        entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_probe::AllocationStatus::overflow);
    EXPECT_EQ(entries[0].payout_raw, 0);
    EXPECT_EQ(entries[1].payout_raw, 0);
}

TEST(EconomicTelemetryTest, ParsesExactStateCategoryAndBounds)
{
    const auto config = interest_probe::ParseEconomicTelemetryArguments({
        L"-smedley-telemetry-categories=lifecycle,state",
        L"-smedley-telemetry-sample-days=30",
        L"-smedley-telemetry-start-date-raw=-2147483648",
        L"-smedley-telemetry-end-date-raw=2147483647"});
    EXPECT_TRUE(config.enabled);
    EXPECT_EQ(config.sample_days, 30);
    EXPECT_EQ(config.start_date_raw, (std::numeric_limits<int32_t>::min)());
    EXPECT_EQ(config.end_date_raw, (std::numeric_limits<int32_t>::max)());

    const auto disabled = interest_probe::ParseEconomicTelemetryArguments({
        L"-smedley-telemetry-categories=lifecycle,estate",
        L"-smedley-telemetry-sample-days=366",
        L"-smedley-telemetry-start-date-raw=1x"});
    EXPECT_FALSE(disabled.enabled);
    EXPECT_EQ(disabled.sample_days, 1);
    EXPECT_FALSE(disabled.start_date_raw.has_value());
}

TEST(EconomicTelemetryTest, SamplesExactIntervalsAndResetsOnRegression)
{
    interest_probe::CaptureConfig config{};
    config.enabled = true;
    config.sample_days = 30;
    config.start_date_raw = 100;
    config.end_date_raw = 2000;
    std::optional<int32_t> observed;
    std::optional<int32_t> sampled;

    EXPECT_FALSE(interest_probe::ShouldCaptureEconomicDate(99, config, &observed, &sampled));
    EXPECT_TRUE(interest_probe::ShouldCaptureEconomicDate(100, config, &observed, &sampled));
    EXPECT_FALSE(interest_probe::ShouldCaptureEconomicDate(100, config, &observed, &sampled));
    EXPECT_FALSE(interest_probe::ShouldCaptureEconomicDate(819, config, &observed, &sampled));
    EXPECT_TRUE(interest_probe::ShouldCaptureEconomicDate(820, config, &observed, &sampled));
    EXPECT_TRUE(interest_probe::ShouldCaptureEconomicDate(200, config, &observed, &sampled));
    EXPECT_FALSE(interest_probe::ShouldCaptureEconomicDate(2001, config, &observed, &sampled));
}

TEST(EconomicTelemetryTest, DetectsSignedAggregationOverflow)
{
    uint32_t flags = 0;
    int64_t total = (std::numeric_limits<int64_t>::max)();
    interest_probe::AddEconomicValue(1, &total, &flags);
    EXPECT_EQ(total, (std::numeric_limits<int64_t>::max)());
    EXPECT_NE(flags & interest_probe::SNAPSHOT_SUM_OVERFLOW, 0u);

    flags = 0;
    total = (std::numeric_limits<int64_t>::min)();
    interest_probe::AddEconomicValue(-1, &total, &flags);
    EXPECT_EQ(total, (std::numeric_limits<int64_t>::min)());
    EXPECT_NE(flags & interest_probe::SNAPSHOT_SUM_OVERFLOW, 0u);
    EXPECT_EQ(interest_probe::UtilizationBasisPoints(20000, 100000), 2000);
}
