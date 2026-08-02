#pragma once

#include <array>
#include <cstdint>

namespace interest_bug_fix
{
    constexpr uint32_t max_sample_creditor_destinations = 64;
    constexpr uint32_t max_sample_destination_provinces = 4096;
    constexpr uint32_t max_sample_pops = 100000;
    constexpr uint32_t max_sample_factories = 4096;
    constexpr uint32_t max_sample_factory_inputs = 16384;

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
        SAMPLE_CREDITOR_UNREADABLE = 1u << 11,
        SAMPLE_CREDITOR_TAG_INVALID = 1u << 12,
        SAMPLE_CREDITOR_DESTINATION_INVALID = 1u << 13,
        SAMPLE_CREDITOR_DESTINATION_LIMIT = 1u << 14,
        SAMPLE_CREDITOR_DUPLICATE_DESTINATION = 1u << 15,
        SAMPLE_PROVINCE_INVALID = 1u << 16,
        SAMPLE_POP_VECTOR_INVALID = 1u << 17,
        SAMPLE_POP_LIST_INVALID = 1u << 18,
        SAMPLE_POP_UNREADABLE = 1u << 19,
        SAMPLE_POP_LIMIT = 1u << 20,
        SAMPLE_DUPLICATE_PROVINCE = 1u << 21,
        SAMPLE_DUPLICATE_POP = 1u << 22,
        SAMPLE_DESTINATION_TRANSFER_INVALID = 1u << 24,
    };

    struct Sample
    {
        int32_t date_raw = 0;
        char country_tag[4]{};
        int32_t country_ordinal = -1;
        int32_t state_count_reported = 0;
        uint32_t states_walked = 0;
        uint32_t province_element_candidates = 0;
        uint32_t states_with_savings = 0;
        uint32_t states_with_interest = 0;
        uint32_t creditor_count = 0;
        uint32_t creditor_destinations = 0;
        uint32_t creditors_was_paid = 0;
        uint32_t invalid_creditor_key = 0;
        int32_t invalid_creditor_ordinal = 0;
        uint8_t invalid_creditor_was_paid = 0;
        uint32_t destination_provinces_resolved = 0;
        uint32_t destination_province_attempts = 0;
        uint32_t destination_pop_lists = 0;
        uint32_t destination_pops = 0;
        uint32_t destination_pop_attempts = 0;
        int64_t treasury_raw = 0;
        int64_t state_savings_raw = 0;
        int64_t state_interest_raw = 0;
        int64_t bank_interest_raw = 0;
        int64_t creditor_interest_raw = 0;
        int64_t creditor_debt_raw = 0;
        int64_t destination_bank_interest_raw = 0;
        int64_t destination_state_savings_raw = 0;
        int64_t destination_state_interest_raw = 0;
        int64_t destination_pop_savings_raw = 0;
        int64_t destination_pop_savings_state_scale_raw = 0;
        std::array<uint32_t, max_sample_creditor_destinations> destination_keys{};
        std::array<int32_t, max_sample_creditor_destinations> destination_ordinals{};
        std::array<int64_t, max_sample_creditor_destinations> destination_bank_interests_raw{};
        std::array<int64_t, max_sample_creditor_destinations> destination_transfers_raw{};
        uint32_t destination_transfer_count = 0;
        int64_t destination_transfer_raw = 0;
        uint32_t flags = 0;
    };

    struct PopMoneySnapshot
    {
        int64_t money_raw = 0;
        int64_t interest_cash_flow_raw = 0;
        int64_t total_cash_flow_raw = 0;
        int64_t savings_raw = 0;
    };

    struct PopCandidate
    {
        const void *address = nullptr;
        int64_t savings_raw = 0;
    };

    struct PopDetailSnapshot
    {
        int32_t province_id_candidate = -1;
        int32_t pop_type_id_candidate = -1;
        int32_t size_candidate = 0;
        int32_t employed_candidate = 0;
        int64_t consciousness_candidate_raw = 0;
        int64_t militancy_candidate_raw = 0;
        int64_t literacy_candidate_raw = 0;
        PopMoneySnapshot economy;
    };

    enum FactoryCaptureFlag : uint32_t
    {
        FACTORY_COUNTRY_UNREADABLE = 1u << 0,
        FACTORY_STATE_LIST_INVALID = 1u << 1,
        FACTORY_STATE_UNREADABLE = 1u << 2,
        FACTORY_LIST_INVALID = 1u << 3,
        FACTORY_UNREADABLE = 1u << 4,
        FACTORY_DEFINITION_INVALID = 1u << 5,
        FACTORY_LIMIT = 1u << 6,
    };

    enum FactoryCaptureGroup : uint32_t
    {
        FACTORY_IDENTITY = 1u << 0,
        FACTORY_EMPLOYMENT = 1u << 1,
        FACTORY_PRODUCTION = 1u << 2,
        FACTORY_FINANCE = 1u << 3,
        FACTORY_INPUTS = 1u << 4,
    };

    struct FactorySnapshot
    {
        uint32_t state_index = 0;
        uint32_t factory_index = 0;
        int32_t state_id = -1;
        int32_t anchor_province_id_candidate = -1;
        char state_region_key[64]{};
        char factory_type[64]{};
        int32_t level = 0;
        int32_t employee_count = 0;
        int32_t craftsmen_count = 0;
        int32_t clerk_count = 0;
        int32_t output_raw = 0;
        int32_t output_good_ordinal = -1;
        char output_good[64]{};
        int32_t base_output_raw = 0;
        bool subsidized = false;
        bool closed = false;
        int64_t budget_raw = 0;
        int64_t market_spending_raw = 0;
        int64_t sales_income_raw = 0;
        int64_t paychecks_raw = 0;
        int64_t investment_raw = 0;
    };

    struct FactoryInputSnapshot
    {
        uint32_t factory_snapshot_index = 0;
        int32_t good_ordinal = -1;
        int64_t stockpile_raw = 0;
    };

    struct WorldMarketSnapshot
    {
        int32_t good_ordinal = -1;
        int64_t price_raw = 0;
        int64_t last_price_raw = 0;
        int64_t supply_raw = 0;
        int64_t last_supply_raw = 0;
        int64_t worldmarket_stock_raw = 0;
        int64_t demand_raw = 0;
        int64_t real_demand_raw = 0;
        int64_t actual_sold_raw = 0;
        int64_t actual_sold_world_raw = 0;
    };

    using CountryResolver = const void *(*)(const void *context, int32_t ordinal);
    using ProvinceResolver = const void *(*)(const void *context, int32_t id);

    Sample CollectSample(const void *country, int32_t date_raw,
                         CountryResolver country_resolver = nullptr, ProvinceResolver province_resolver = nullptr,
                         const void *resolver_context = nullptr, const void **immediate_pop = nullptr);
    Sample CollectInterestSample(const void *country, int32_t date_raw,
                                 CountryResolver country_resolver, const void *resolver_context);
    Sample CollectInterestAfter(const Sample &before, const void *country, int32_t date_raw,
                                CountryResolver country_resolver, const void *resolver_context);
    bool ComputeDestinationTransfers(const Sample &before, Sample *after);
    bool TreasuryLossCoversTransfer(int64_t before_treasury, int64_t after_treasury, int64_t transfer);
    bool ComputeTreasuryResidual(int64_t before_treasury, int64_t after_treasury,
                                 int64_t transfer, int64_t *residual);
    bool CollectCountryPops(const void *country, int32_t date_raw,
                            ProvinceResolver province_resolver, const void *resolver_context,
                            PopCandidate *candidates, size_t candidate_capacity,
                            uint32_t province_attempt_capacity, uint32_t *candidate_count,
                            Sample *quality);
    bool ReadPopMoneySnapshot(const void *pop, PopMoneySnapshot *snapshot);
    bool ReadPopDetailSnapshot(const void *pop, PopDetailSnapshot *snapshot);
    bool CanWritePopMoney(const void *pop);
    bool CollectCountryFactories(const void *country, FactorySnapshot *snapshots,
                                 size_t snapshot_capacity, uint32_t *snapshot_count,
                                 FactoryInputSnapshot *inputs, size_t input_capacity,
                                 uint32_t *input_count, uint32_t groups, uint32_t *flags);
    bool CollectWorldMarket(const void *game_state, WorldMarketSnapshot *snapshots,
                            size_t snapshot_capacity, uint32_t *snapshot_count);
}
