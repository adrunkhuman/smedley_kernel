#pragma once

#include <cstdint>

namespace interest_probe
{
    enum SampleFlag : uint32_t
    {
        SAMPLE_COUNTRY_UNREADABLE = 1u << 0,
        SAMPLE_STATE_LIST_INVALID = 1u << 1,
        SAMPLE_STATE_UNREADABLE = 1u << 2,
        SAMPLE_STATE_VECTOR_INVALID = 1u << 3,
        SAMPLE_STATE_LIMIT = 1u << 4,
        SAMPLE_STATE_COUNT_MISMATCH = 1u << 5,
        SAMPLE_SUM_OVERFLOW = 1u << 6,
        SAMPLE_BANK_UNREADABLE = 1u << 7,
        SAMPLE_CREDITOR_VECTOR_INVALID = 1u << 8,
        SAMPLE_DATE_UNAVAILABLE = 1u << 9,
        SAMPLE_EVENT_CALLBACK_FAILURE = 1u << 10,
        SAMPLE_CREDITOR_UNREADABLE = 1u << 11,
        SAMPLE_CREDITOR_TAG_INVALID = 1u << 12,
        SAMPLE_CREDITOR_DESTINATION_INVALID = 1u << 13,
        SAMPLE_CREDITOR_DESTINATION_LIMIT = 1u << 14,
        SAMPLE_CREDITOR_DUPLICATE_DESTINATION = 1u << 15,
    };

    struct Sample
    {
        int32_t date_raw = 0;
        char country_tag[4]{};
        int32_t state_count_reported = 0;
        uint32_t states_walked = 0;
        uint32_t province_element_candidates = 0;
        uint32_t states_with_savings = 0;
        uint32_t states_with_interest = 0;
        uint32_t creditor_count = 0;
        uint32_t creditor_destinations = 0;
        uint32_t creditors_was_paid = 0;
        int64_t treasury_raw = 0;
        int64_t state_savings_raw = 0;
        int64_t state_interest_raw = 0;
        int64_t bank_interest_raw = 0;
        int64_t creditor_interest_raw = 0;
        int64_t creditor_debt_raw = 0;
        int64_t destination_bank_interest_raw = 0;
        int64_t destination_state_savings_raw = 0;
        int64_t destination_state_interest_raw = 0;
        uint32_t flags = 0;
        uint32_t collection_us = 0;
    };

    using CountryResolver = const void *(*)(const void *context, int32_t ordinal);

    Sample CollectSample(const void *country, int32_t date_raw,
                         CountryResolver resolver = nullptr, const void *resolver_context = nullptr);
}
