#include "probe_core.hpp"

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
