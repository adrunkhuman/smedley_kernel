#include "probe_core.hpp"
#include "pair_queue.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

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
    };

    const void *ResolveCountry(const void *context, int32_t ordinal)
    {
        const auto *lookup = static_cast<const CountryLookup *>(context);
        return ordinal == lookup->ordinal ? lookup->country : nullptr;
    }
}

TEST(InterestProbeTest, CollectsBoundedStateAndBankCandidates)
{
    std::array<std::byte, 0x1608> country{};
    std::array<std::byte, 0x290> state{};
    std::array<std::byte, 0x28> bank{};
    std::array<int, 2> provinces{7, 11};
    std::array<void *, 2> creditors{reinterpret_cast<void *>(1), reinterpret_cast<void *>(2)};
    Node node{state.data(), nullptr, nullptr, 0, {}};

    const char tag[4] = {'E', 'N', 'G', '\0'};
    const int state_count = 1;
    const int64_t treasury = 90;
    const int64_t savings = 120;
    const int64_t interest = 30;
    const int64_t bank_interest = 40;
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

    const auto sample = interest_probe::CollectSample(country.data(), 1234);
    EXPECT_STREQ(sample.country_tag, "ENG");
    EXPECT_EQ(sample.date_raw, 1234);
    EXPECT_EQ(sample.state_count_reported, 1);
    EXPECT_EQ(sample.states_walked, 1u);
    EXPECT_EQ(sample.province_element_candidates, 2u);
    EXPECT_EQ(sample.states_with_savings, 1u);
    EXPECT_EQ(sample.states_with_interest, 1u);
    EXPECT_EQ(sample.creditor_count, 2u);
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

TEST(InterestProbeTest, CollectsCreditorAndDestinationCandidates)
{
    std::array<std::byte, 0x1608> debtor{};
    std::array<std::byte, 0x1608> destination{};
    std::array<std::byte, 0x28> creditor{};
    std::array<std::byte, 0x28> debtor_bank{};
    std::array<std::byte, 0x28> destination_bank{};
    std::array<std::byte, 0x290> destination_state{};
    std::array<void *, 1> creditors{creditor.data()};
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
    const void *debtor_bank_pointer = debtor_bank.data();
    const void *destination_bank_pointer = destination_bank.data();
    const void *destination_state_head = &destination_node;
    const int destination_state_count = 1;
    const void *creditor_begin = creditors.data();
    const void *creditor_end = creditors.data() + creditors.size();

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
    Write(&creditor, 0x08, destination_tag);
    Write(&creditor, 0x0c, destination_ordinal);
    Write(&creditor, 0x10, creditor_interest);
    Write(&creditor, 0x18, creditor_debt);
    Write(&creditor, 0x20, was_paid);
    const CountryLookup lookup{destination_ordinal, destination.data()};

    const auto sample = interest_probe::CollectSample(debtor.data(), 1234, ResolveCountry, &lookup);
    EXPECT_EQ(sample.creditor_count, 1u);
    EXPECT_EQ(sample.creditor_destinations, 1u);
    EXPECT_EQ(sample.creditors_was_paid, 1u);
    EXPECT_EQ(sample.creditor_interest_raw, creditor_interest);
    EXPECT_EQ(sample.creditor_debt_raw, creditor_debt);
    EXPECT_EQ(sample.destination_bank_interest_raw, destination_bank_interest);
    EXPECT_EQ(sample.destination_state_savings_raw, destination_state_savings);
    EXPECT_EQ(sample.destination_state_interest_raw, destination_state_interest);
    EXPECT_EQ(sample.flags, 0u);
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

    const auto sample = interest_probe::CollectSample(debtor.data(), 1234, ResolveCountry, &lookup);
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
