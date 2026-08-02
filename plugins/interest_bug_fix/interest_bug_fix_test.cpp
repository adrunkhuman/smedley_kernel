#include "economic_state.hpp"
#include "interest_allocation.hpp"
#include "interest_batch.hpp"
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace interest_probe = interest_bug_fix;

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

    struct FactoryNode
    {
        std::array<std::byte, 0x220> data{};
        const FactoryNode *previous = nullptr;
        const FactoryNode *next = nullptr;
        uint8_t deleted = 0;
        uint8_t padding[3]{};
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
TEST(InterestBugFixTest, CollectsBoundedStateAndBankCandidates)
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

TEST(InterestBugFixTest, RejectsSelfReferentialStateList)
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

TEST(InterestBugFixTest, RejectsMalformedProvinceVector)
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

TEST(InterestBugFixTest, AggregatesOpaqueCreditorsWithoutPollutingPopTraversal)
{
    std::array<std::byte, 0x1608> country{};
    std::array<std::byte, 0x28> bank{};
    std::array<std::byte, 0x28> creditor{};
    std::array<void *, 1> creditors{creditor.data()};
    const int64_t interest = 15;
    const int64_t debt = 90;
    const uint8_t was_paid = 1;
    const char no_country_tag[4] = {'-', '-', '-', '\0'};
    const void *bank_pointer = bank.data();
    const void *creditor_begin = creditors.data();
    const void *creditor_end = creditors.data() + creditors.size();

    Write(&country, 0xe88, bank_pointer);
    Write(&country, 0xe8c, creditor_begin);
    Write(&country, 0xe90, creditor_end);
    Write(&country, 0xe94, creditor_end);
    Write(&creditor, 0x08, no_country_tag);
    Write(&creditor, 0x10, interest);
    Write(&creditor, 0x18, debt);
    Write(&creditor, 0x20, was_paid);

    const auto aggregate = interest_probe::CollectSample(country.data(), 1234);
    EXPECT_EQ(aggregate.creditor_count, 1u);
    EXPECT_EQ(aggregate.creditors_was_paid, 1u);
    EXPECT_EQ(aggregate.creditor_interest_raw, interest);
    EXPECT_EQ(aggregate.creditor_debt_raw, debt);
    EXPECT_EQ(aggregate.flags, 0u);

    const CountryLookup lookup{};
    const auto resolved = interest_probe::CollectSample(
        country.data(), 1234, ResolveCountry, nullptr, &lookup);
    EXPECT_EQ(resolved.creditor_count, 1u);
    EXPECT_EQ(resolved.creditor_destinations, 0u);
    EXPECT_EQ(resolved.creditors_was_paid, 1u);
    EXPECT_EQ(resolved.creditor_interest_raw, interest);
    EXPECT_EQ(resolved.creditor_debt_raw, debt);
    EXPECT_EQ(resolved.flags, 0u);

    const uint8_t invalid_paid = 2;
    Write(&creditor, 0x20, invalid_paid);
    const void *malformed_creditor_begin = creditors.data() + creditors.size();
    const void *malformed_creditor_end = creditors.data();
    Write(&country, 0xe8c, malformed_creditor_begin);
    Write(&country, 0xe90, malformed_creditor_end);
    uint32_t candidate_count = 0;
    interest_probe::Sample quality{};
    EXPECT_TRUE(interest_probe::CollectCountryPops(country.data(), 1234, ResolveProvince, &lookup,
        nullptr, 0, interest_probe::max_sample_destination_provinces, &candidate_count, &quality));
    EXPECT_EQ(candidate_count, 0u);
    EXPECT_EQ(quality.creditor_count, 0u);
    EXPECT_EQ(quality.flags, 0u);
}

TEST(InterestBugFixTest, CollectsCreditorAndDestinationCandidates)
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

TEST(InterestBugFixTest, RejectsMismatchedCreditorDestination)
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

TEST(InterestBugFixTest, ComputesExactPerDestinationTransfers)
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

TEST(InterestBugFixTest, RejectsChangedDestinationOrder)
{
    interest_probe::Sample before{};
    before.creditor_destinations = 1;
    before.destination_ordinals[0] = 7;

    interest_probe::Sample after = before;
    after.destination_ordinals[0] = 8;

    EXPECT_FALSE(interest_probe::ComputeDestinationTransfers(before, &after));
    EXPECT_NE(after.flags & interest_probe::SAMPLE_DESTINATION_TRANSFER_INVALID, 0u);
}

TEST(InterestBugFixTest, RejectsChangedDestinationIdentity)
{
    interest_probe::Sample before{};
    before.creditor_destinations = 1;
    before.destination_keys[0] = 0x00474e45;
    before.destination_ordinals[0] = 7;

    interest_probe::Sample after = before;
    after.destination_keys[0] = 0x00415246;

    EXPECT_FALSE(interest_probe::ComputeDestinationTransfers(before, &after));
    EXPECT_NE(after.flags & interest_probe::SAMPLE_DESTINATION_TRANSFER_INVALID, 0u);
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

    interest_probe::Sample before{};
    before.creditor_count = 1;
    before.creditor_destinations = 1;
    std::memcpy(&before.destination_keys[0], destination_tag, 4);
    before.destination_ordinals[0] = destination_ordinal;
    before.destination_bank_interests_raw[0] = 100;
    before.destination_bank_interest_raw = 100;

    auto after = interest_probe::CollectInterestAfter(
        before, debtor.data(), 1234, ResolveCountry, &lookup);
    ASSERT_EQ(after.flags, 0u);
    ASSERT_EQ(after.creditor_destinations, 1u);
    EXPECT_TRUE(interest_probe::ComputeDestinationTransfers(before, &after));
    EXPECT_EQ(after.destination_transfer_raw, 25);
}

TEST(InterestBugFixTest, AcceptsResidualTreasurySinkBeyondDestinationTransfer)
{
    EXPECT_TRUE(interest_probe::TreasuryLossCoversTransfer(100, 75, 25));
    EXPECT_TRUE(interest_probe::TreasuryLossCoversTransfer(100, 70, 25));
    EXPECT_FALSE(interest_probe::TreasuryLossCoversTransfer(100, 80, 25));
    EXPECT_FALSE(interest_probe::TreasuryLossCoversTransfer(100, 101, 0));
    EXPECT_FALSE(interest_probe::TreasuryLossCoversTransfer(100, 75, -1));
    EXPECT_FALSE(interest_probe::TreasuryLossCoversTransfer(
        (std::numeric_limits<int64_t>::min)(), (std::numeric_limits<int64_t>::min)(), 1));
    int64_t residual = -1;
    EXPECT_TRUE(interest_probe::ComputeTreasuryResidual(100, 70, 25, &residual));
    EXPECT_EQ(residual, 5);
    EXPECT_FALSE(interest_probe::ComputeTreasuryResidual(100, 80, 25, &residual));
    EXPECT_FALSE(interest_probe::ComputeTreasuryResidual(
        (std::numeric_limits<int64_t>::max)(), (std::numeric_limits<int64_t>::min)(), 0, &residual));
}

TEST(InterestBatchTest, AggregatesDomesticForeignAndPrivateAmounts)
{
    interest_probe::DailyInterestBatch batch;
    ASSERT_TRUE(batch.Begin(2400, 4));

    std::array<interest_probe::InterestTransfer, 2> first{{
        {1, {'A', 'A', 'A', '\0'}, 10},
        {2, {'B', 'B', 'B', '\0'}, 20},
    }};
    std::array<interest_probe::InterestTransfer, 1> second{{
        {2, {'B', 'B', 'B', '\0'}, 5},
    }};
    EXPECT_EQ(batch.AddDebtor(1, first.data(), first.size(), 3),
        interest_probe::BatchAddStatus::success);
    EXPECT_EQ(batch.AddDebtor(2, second.data(), second.size(), 0),
        interest_probe::BatchAddStatus::success);
    EXPECT_EQ(batch.AddDebtor(3, nullptr, 0, 7), interest_probe::BatchAddStatus::success);

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
    interest_probe::DailyInterestBatch batch;
    ASSERT_TRUE(batch.Begin(2400, 3));
    const interest_probe::InterestTransfer maximum{2, {'B', 'B', 'B', '\0'},
        (std::numeric_limits<int64_t>::max)()};
    const interest_probe::InterestTransfer overflow{2, {'B', 'B', 'B', '\0'}, 1};

    EXPECT_EQ(batch.AddDebtor(1, &maximum, 1, 0), interest_probe::BatchAddStatus::success);
    EXPECT_EQ(batch.AddDebtor(1, nullptr, 0, 0), interest_probe::BatchAddStatus::duplicate_debtor);
    EXPECT_EQ(batch.AddDebtor(2, &overflow, 1, 0), interest_probe::BatchAddStatus::overflow);
    EXPECT_TRUE(batch.RejectDebtor(2));
    EXPECT_TRUE(batch.complete());
    EXPECT_EQ(batch.rejected_debtors(), 1u);
    EXPECT_EQ(batch.recipient(2).transfer_raw, (std::numeric_limits<int64_t>::max)());
}

TEST(InterestBatchTest, TracksDailyPopIdentitiesWithoutAllocation)
{
    static interest_probe::DailyPopSet pops;
    pops.Reset();
    EXPECT_EQ(pops.Insert(0x1000), interest_probe::PointerInsertStatus::inserted);
    EXPECT_EQ(pops.Insert(0x2000), interest_probe::PointerInsertStatus::inserted);
    EXPECT_TRUE(pops.Contains(0x1000));
    EXPECT_FALSE(pops.Contains(0x3000));
    EXPECT_EQ(pops.Insert(0x1000), interest_probe::PointerInsertStatus::duplicate);
    EXPECT_EQ(pops.Insert(0), interest_probe::PointerInsertStatus::duplicate);
    EXPECT_EQ(pops.size(), 2u);
    pops.Reset();
    EXPECT_EQ(pops.size(), 0u);
    EXPECT_EQ(pops.Insert(0x1000), interest_probe::PointerInsertStatus::inserted);
}

TEST(InterestBugFixTest, RejectsFlaggedBeforeSample)
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
    const int64_t large_transfer = (std::numeric_limits<int64_t>::max)() / 1000;
    EXPECT_EQ(interest_probe::AllocateInterest(
        large_transfer, entries.data(), entries.size(), scratch.data(), scratch.size()),
        interest_probe::AllocationStatus::success);
    EXPECT_EQ(entries[0].payout_raw + entries[1].payout_raw, large_transfer * 1000);
    EXPECT_GT(entries[0].payout_raw, entries[1].payout_raw);
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

TEST(InterestBugFixTest, CollectsBoundedFactoryFields)
{
    std::array<std::byte, 0x1608> country{};
    std::array<std::byte, 0x290> state{};
    std::array<std::byte, 0x108> region{};
    std::array<std::byte, 0x140> definition{};
    std::array<std::byte, 0x140> production_type{};
    std::array<std::byte, 0x40> output_good{};
    std::array<std::byte, 0x70> pop{};
    std::array<std::byte, 0x40> pop_type{};
    std::array<std::byte, 0x10> employment{};
    std::array<int32_t, 1> provinces{549};
    std::array<int64_t, 2> stockpile_values{0, 12357};
    Node state_node{state.data(), nullptr, nullptr, 0, {}};
    FactoryNode factory_node{};
    const void *state_head = &state_node;
    const int state_count = 1;
    const void *province_begin = provinces.data();
    const void *province_end = provinces.data() + provinces.size();
    const void *factory_head = &factory_node;
    const int factory_count = 1;
    const void *definition_pointer = definition.data();
    const void *region_pointer = region.data();
    const void *production_type_pointer = production_type.data();
    const void *output_good_pointer = output_good.data();
    const void *pop_pointer = pop.data();
    const void *pop_type_pointer = pop_type.data();
    const void *employment_begin = employment.data();
    const void *employment_end = employment.data() + employment.size();
    const void *stockpile_begin = stockpile_values.data();
    const void *stockpile_end = stockpile_values.data() + stockpile_values.size();
    const char factory_type[] = "glass_factory";
    const uint32_t factory_type_size = sizeof(factory_type) - 1;
    const uint32_t factory_type_capacity = 15;
    const char output_good_type[] = "small_arms";
    const uint32_t output_good_type_size = sizeof(output_good_type) - 1;
    const uint32_t output_good_type_capacity = 15;
    const char pop_type_name[] = "craftsmen";
    const uint32_t pop_type_name_size = sizeof(pop_type_name) - 1;
    const uint32_t pop_type_name_capacity = 15;
    const char state_region_key[] = "PRU_549";
    const uint32_t state_region_key_size = sizeof(state_region_key) - 1;
    const int32_t state_id = 750;
    const int32_t level = 1;
    const int32_t employees = 1998;
    const int32_t output = 13437;
    const int32_t output_good_ordinal = 1;
    const int32_t base_output = 65536;
    const int64_t budget = 8244550872;
    const int64_t spending = 362208000;
    const int64_t income = 497291000;
    const int64_t paychecks = 8504523;
    const int64_t investment = 804455000;

    Write(&country, 0xe44, state_head);
    Write(&country, 0xe48, state_head);
    Write(&country, 0xe4c, state_count);
    Write(&state, 0x48, province_begin);
    Write(&state, 0x4c, province_end);
    Write(&state, 0x50, province_end);
    Write(&state, 0x60, factory_head);
    Write(&state, 0x64, factory_head);
    Write(&state, 0x68, factory_count);
    Write(&state, 0x0c, state_id);
    Write(&state, 0x250, region_pointer);
    Write(&factory_node.data, 0x18, definition_pointer);
    Write(&factory_node.data, 0x20, level);
    Write(&factory_node.data, 0xd8, output);
    Write(&factory_node.data, 0x128, employees);
    Write(&factory_node.data, 0x150, budget);
    Write(&factory_node.data, 0x158, spending);
    Write(&factory_node.data, 0x160, income);
    Write(&factory_node.data, 0x168, paychecks);
    Write(&factory_node.data, 0x170, investment);
    factory_node.data[0x30] = std::byte{1};
    Write(&factory_node.data, 0x70, stockpile_begin);
    Write(&factory_node.data, 0x74, stockpile_end);
    Write(&factory_node.data, 0x78, stockpile_end);
    Write(&factory_node.data, 0xf0, employment_begin);
    Write(&factory_node.data, 0xf4, employment_end);
    Write(&factory_node.data, 0xf8, employment_end);
    std::memcpy(definition.data() + 0x20, factory_type, sizeof(factory_type));
    Write(&definition, 0x30, factory_type_size);
    Write(&definition, 0x34, factory_type_capacity);
    Write(&definition, 0x12c, production_type_pointer);
    Write(&production_type, 0x80, output_good_pointer);
    Write(&production_type, 0x88, base_output);
    Write(&output_good, 0x08, output_good_ordinal);
    std::memcpy(output_good.data() + 0x0c, output_good_type, sizeof(output_good_type));
    Write(&output_good, 0x1c, output_good_type_size);
    Write(&output_good, 0x20, output_good_type_capacity);
    Write(&pop, 0x68, pop_type_pointer);
    std::memcpy(pop_type.data() + 0x08, pop_type_name, sizeof(pop_type_name));
    Write(&pop_type, 0x18, pop_type_name_size);
    Write(&pop_type, 0x1c, pop_type_name_capacity);
    Write(&employment, 0x08, pop_pointer);
    Write(&employment, 0x0c, employees);
    std::memcpy(region.data() + 0x18, state_region_key, sizeof(state_region_key));
    Write(&region, 0x28, state_region_key_size);
    Write(&region, 0x2c, factory_type_capacity);

    std::array<interest_probe::FactorySnapshot, 2> snapshots{};
    std::array<interest_probe::FactoryInputSnapshot, 2> inputs{};
    uint32_t captured = 0;
    uint32_t input_count = 0;
    uint32_t flags = 0;
    ASSERT_TRUE(interest_probe::CollectCountryFactories(
        country.data(), snapshots.data(), snapshots.size(), &captured,
        inputs.data(), inputs.size(), &input_count,
        interest_probe::FACTORY_IDENTITY | interest_probe::FACTORY_EMPLOYMENT
            | interest_probe::FACTORY_PRODUCTION | interest_probe::FACTORY_FINANCE
            | interest_probe::FACTORY_INPUTS,
        &flags));
    ASSERT_EQ(flags, 0u);
    ASSERT_EQ(captured, 1u);
    const auto &snapshot = snapshots[0];
    EXPECT_EQ(snapshot.state_index, 0u);
    EXPECT_EQ(snapshot.factory_index, 0u);
    EXPECT_EQ(snapshot.state_id, state_id);
    EXPECT_STREQ(snapshot.state_region_key, state_region_key);
    EXPECT_EQ(snapshot.anchor_province_id_candidate, 549);
    EXPECT_STREQ(snapshot.factory_type, factory_type);
    EXPECT_EQ(snapshot.level, level);
    EXPECT_EQ(snapshot.employee_count, employees);
    EXPECT_EQ(snapshot.craftsmen_count, employees);
    EXPECT_EQ(snapshot.clerk_count, 0);
    EXPECT_EQ(snapshot.output_raw, output);
    EXPECT_EQ(snapshot.output_good_ordinal, output_good_ordinal);
    EXPECT_STREQ(snapshot.output_good, output_good_type);
    EXPECT_EQ(snapshot.base_output_raw, base_output);
    EXPECT_FALSE(snapshot.subsidized);
    EXPECT_FALSE(snapshot.closed);
    EXPECT_EQ(snapshot.budget_raw, budget);
    EXPECT_EQ(snapshot.market_spending_raw, spending);
    EXPECT_EQ(snapshot.sales_income_raw, income);
    EXPECT_EQ(snapshot.paychecks_raw, paychecks);
    EXPECT_EQ(snapshot.investment_raw, investment);
    ASSERT_EQ(input_count, 1u);
    EXPECT_EQ(inputs[0].factory_snapshot_index, 0u);
    EXPECT_EQ(inputs[0].good_ordinal, 0);
    EXPECT_EQ(inputs[0].stockpile_raw, stockpile_values[1]);

    const void *null_pointer = nullptr;
    Write(&definition, 0x12c, null_pointer);
    ASSERT_TRUE(interest_probe::CollectCountryFactories(
        country.data(), snapshots.data(), snapshots.size(), &captured,
        inputs.data(), inputs.size(), &input_count, interest_probe::FACTORY_IDENTITY, &flags));
    EXPECT_EQ(captured, 1u);
    Write(&definition, 0x12c, production_type_pointer);

    Write(&state, 0x48, null_pointer);
    Write(&state, 0x4c, null_pointer);
    Write(&state, 0x50, null_pointer);
    ASSERT_TRUE(interest_probe::CollectCountryFactories(
        country.data(), snapshots.data(), snapshots.size(), &captured,
        inputs.data(), inputs.size(), &input_count, interest_probe::FACTORY_FINANCE, &flags));
    EXPECT_EQ(captured, 1u);
    Write(&state, 0x48, province_begin);
    Write(&state, 0x4c, province_end);
    Write(&state, 0x50, province_end);

    factory_node.data[0x31] = std::byte{1};
    EXPECT_FALSE(interest_probe::CollectCountryFactories(
        country.data(), snapshots.data(), snapshots.size(), &captured,
        inputs.data(), inputs.size(), &input_count, interest_probe::FACTORY_INPUTS, &flags));
    EXPECT_NE(flags & interest_probe::FACTORY_UNREADABLE, 0u);
    factory_node.data[0x31] = std::byte{0};

    factory_node.next = &factory_node;
    const int malformed_factory_count = 2;
    Write(&state, 0x68, malformed_factory_count);
    EXPECT_FALSE(interest_probe::CollectCountryFactories(
        country.data(), snapshots.data(), snapshots.size(), &captured,
        inputs.data(), inputs.size(), &input_count, interest_probe::FACTORY_IDENTITY, &flags));
    EXPECT_NE(flags & interest_probe::FACTORY_LIST_INVALID, 0u);

    factory_node.next = nullptr;
    Write(&state, 0x68, factory_count);
    EXPECT_FALSE(interest_probe::CollectCountryFactories(
        country.data(), snapshots.data(), 0, &captured,
        inputs.data(), inputs.size(), &input_count, interest_probe::FACTORY_IDENTITY, &flags));
    EXPECT_NE(flags & interest_probe::FACTORY_LIMIT, 0u);
}

TEST(InterestBugFixTest, CollectsAndValidatesWorldMarketPools)
{
    std::array<std::byte, 0xd08> game_state{};
    std::array<std::byte, 0x54c> world_market{};
    std::array<std::array<int64_t, 2>, 9> values{};
    constexpr std::array<size_t, 9> offsets{0x08, 0x60, 0x120, 0x178, 0x1d0, 0x280, 0x2d8, 0x434, 0x4f4};
    const void *world_market_pointer = world_market.data();
    Write(&game_state, 0xbcc, world_market_pointer);
    for (size_t index = 0; index < offsets.size(); ++index) {
        values[index] = {0, static_cast<int64_t>((index + 1) * 100)};
        world_market[offsets[index] + 0x08] = std::byte{1};
        const void *begin = values[index].data();
        const void *end = values[index].data() + values[index].size();
        Write(&world_market, offsets[index] + 0x48, begin);
        Write(&world_market, offsets[index] + 0x4c, end);
        Write(&world_market, offsets[index] + 0x50, end);
    }

    std::array<interest_probe::WorldMarketSnapshot, 4> snapshots{};
    uint32_t captured = 0;
    ASSERT_TRUE(interest_probe::CollectWorldMarket(
        game_state.data(), snapshots.data(), snapshots.size(), &captured));
    ASSERT_EQ(captured, 1u);
    EXPECT_EQ(snapshots[0].good_ordinal, 0);
    EXPECT_EQ(snapshots[0].supply_raw, 100);
    EXPECT_EQ(snapshots[0].last_supply_raw, 200);
    EXPECT_EQ(snapshots[0].worldmarket_stock_raw, 300);
    EXPECT_EQ(snapshots[0].demand_raw, 400);
    EXPECT_EQ(snapshots[0].real_demand_raw, 500);
    EXPECT_EQ(snapshots[0].price_raw, 600);
    EXPECT_EQ(snapshots[0].last_price_raw, 700);
    EXPECT_EQ(snapshots[0].actual_sold_raw, 800);
    EXPECT_EQ(snapshots[0].actual_sold_world_raw, 900);

    world_market[offsets[5] + 0x09] = std::byte{1};
    EXPECT_FALSE(interest_probe::CollectWorldMarket(
        game_state.data(), snapshots.data(), snapshots.size(), &captured));

    std::array<std::array<int64_t, 65>, 9> dense_values{};
    for (size_t pool = 0; pool < offsets.size(); ++pool) {
        for (size_t ordinal = 0; ordinal < 64; ++ordinal) {
            dense_values[pool][ordinal + 1] = static_cast<int64_t>(ordinal + 1);
            world_market[offsets[pool] + 0x08 + ordinal] = static_cast<std::byte>(ordinal + 1);
        }
        const void *begin = dense_values[pool].data();
        const void *end = dense_values[pool].data() + dense_values[pool].size();
        Write(&world_market, offsets[pool] + 0x48, begin);
        Write(&world_market, offsets[pool] + 0x4c, end);
        Write(&world_market, offsets[pool] + 0x50, end);
    }
    std::array<interest_probe::WorldMarketSnapshot, 64> dense_snapshots{};
    ASSERT_TRUE(interest_probe::CollectWorldMarket(
        game_state.data(), dense_snapshots.data(), dense_snapshots.size(), &captured));
    EXPECT_EQ(captured, 64u);
    EXPECT_EQ(dense_snapshots.back().good_ordinal, 63);
}

TEST(InterestBugFixTest, ReadsValidatedPopDetailCandidates)
{
    std::array<std::byte, 0x280> pop{};
    std::array<std::byte, 0x60> province{};
    std::array<std::byte, 0x2c> pop_type{};
    const int32_t type_id = 4;
    const int32_t province_id = 549;
    const int64_t money = 123456;
    const int32_t size = 4275;
    const int64_t savings = 789;
    const int32_t employed = 1000;
    const int32_t consciousness = 98337;
    const int32_t militancy = 32768;
    const int32_t literacy = 22938;
    const int64_t interest = 31;
    const int64_t cash_flow = -12;
    const void *province_pointer = province.data();
    const void *type_pointer = pop_type.data();
    Write(&province, 0x58, province_id);
    Write(&pop_type, 0x28, type_id);
    Write(&pop, 0x58, size);
    Write(&pop, 0x60, employed);
    Write(&pop, 0x64, province_pointer);
    Write(&pop, 0x68, type_pointer);
    Write(&pop, 0x118, consciousness);
    Write(&pop, 0x120, militancy);
    Write(&pop, 0x128, literacy);
    Write(&pop, 0x180, money);
    Write(&pop, 0x210, interest);
    Write(&pop, 0x218, cash_flow);
    Write(&pop, 0x250, savings);

    interest_probe::PopDetailSnapshot detail{};
    ASSERT_TRUE(interest_probe::ReadPopDetailSnapshot(pop.data(), &detail));
    EXPECT_EQ(detail.pop_type_id_candidate, type_id);
    EXPECT_EQ(detail.province_id_candidate, province_id);
    EXPECT_EQ(detail.size_candidate, size);
    EXPECT_EQ(detail.employed_candidate, employed);
    EXPECT_EQ(detail.consciousness_candidate_raw, consciousness);
    EXPECT_EQ(detail.militancy_candidate_raw, militancy);
    EXPECT_EQ(detail.literacy_candidate_raw, literacy);
    EXPECT_EQ(detail.economy.money_raw, money);
    EXPECT_EQ(detail.economy.savings_raw, savings);
    EXPECT_EQ(detail.economy.interest_cash_flow_raw, interest);
    EXPECT_EQ(detail.economy.total_cash_flow_raw, cash_flow);

    Write(&pop, 0x60, size + 1);
    EXPECT_FALSE(interest_probe::ReadPopDetailSnapshot(pop.data(), &detail));
}
