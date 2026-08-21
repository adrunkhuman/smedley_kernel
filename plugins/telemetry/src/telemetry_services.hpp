#pragma once

#include <smedley/event_api.h>
#include <smedley/logging_api.h>
#include <smedley/telemetry_game_api.h>
#include <smedley/telemetry_observation_api.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace telemetry_plugin::services
{
    constexpr uint32_t max_world_countries = 512;
    constexpr uint32_t max_sample_destination_provinces = 4096;
    constexpr uint32_t max_sample_pops = SMEDLEY_TELEMETRY_OBSERVATION_MAX_POP_RECORDS;
    constexpr uint32_t max_sample_factories = SMEDLEY_TELEMETRY_OBSERVATION_MAX_FACTORY_RECORDS;
    constexpr uint32_t max_sample_factory_inputs = SMEDLEY_TELEMETRY_OBSERVATION_MAX_FACTORY_INPUTS;
    constexpr uint32_t max_pop_cash_flow_records = SMEDLEY_TELEMETRY_OBSERVATION_MAX_POP_RECORDS;
    constexpr uint32_t max_factory_flow_records = 2048;
    constexpr uint32_t max_factory_sales_records = 4096;
    constexpr uint32_t max_artisan_flow_records = 8192;
    constexpr uint32_t pop_cash_flow_component_count = 8;

    struct PopRef { uint64_t value = 0; uint64_t address() const { return value; } explicit operator bool() const { return value != 0; } };
    struct FactoryRef { uint64_t value = 0; uint64_t address() const { return value; } explicit operator bool() const { return value != 0; } };
    struct CountryRef { int32_t ordinal = -1; explicit operator bool() const { return ordinal >= 0; } };
    struct ProvinceRef { int32_t id = -1; explicit operator bool() const { return id >= 0; } };
    struct EmploymentRegistryRef { explicit operator bool() const { return true; } };

    struct Tag {
        char value[4]{};
        bool normalized_candidate() const { return value[3] == '\0' && value[0] != '\0' && value[1] != '\0' && value[2] != '\0'; }
        const char *str() const { return value; }
    };
    struct TelemetryCurrentState {
        struct GameStateRef { uintptr_t token = 0; explicit operator bool() const { return token != 0; } } game_state{1};
        int32_t date_raw = 0;
        uint32_t country_count_value = 0, country_ai_count_value = 0, province_count_value = 0;
        uint32_t availability_flags = 0, ongoing_war_count_value = 0;
        uint32_t country_count() const { return country_count_value; }
        uint32_t country_ai_count() const { return country_ai_count_value; }
        uint32_t province_count() const { return province_count_value; }
        bool has_human_controlled_country() const { return (availability_flags & 0x80000000u) != 0; }
        bool world_daily_available() const { return (availability_flags & SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_WORLD_DAILY) != 0; }
        bool military_available() const { return (availability_flags & SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_WORLD_MILITARY) != 0; }
        bool province_count_available() const { return province_count_value != 0; }
        bool ongoing_war_count_candidate(int *out) const { if (!military_available() || out == nullptr) return false; *out = static_cast<int>(ongoing_war_count_value); return true; }
        bool province_count_candidate(size_t *out) const { if (out == nullptr) return false; *out = province_count_value; return true; }
    };

    struct TelemetryCountrySnapshot {
        CountryRef country{}; Tag tag_value{}, overlord_value{}, sphere_value{};
        uint32_t availability_flags = 0, unit_count_value = 0, scheduled_mobilization_count_value = 0;
        uint32_t sphereling_count_value = 0, vassal_count_value = 0, ally_count_value = 0, guaranteed_count_value = 0, neighbor_count_value = 0;
        uint32_t mobilized_value = 0, substate_value = 0, vassal_value = 0;
        int32_t ranking_value = 0, military_ranking_value = 0, industrial_ranking_value = 0, prestige_ranking_value = 0;
        int64_t treasury_value = 0, prestige_value = 0, infamy_value = 0, plurality_value = 0, war_exhaustion_value = 0;
        int64_t diplomatic_points_value = 0, research_points_value = 0, leadership_value = 0;
        bool daily_available() const { return (availability_flags & SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_DAILY) != 0; }
        bool power_available() const { return (availability_flags & SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_POWER) != 0; }
        bool politics_available() const { return (availability_flags & SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_POLITICS) != 0; }
        bool military_available() const { return (availability_flags & SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_MILITARY) != 0; }
        bool diplomacy_status_available() const { return (availability_flags & SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_DIPLOMACY_STATUS) != 0; }
        bool diplomacy_relations_available() const { return (availability_flags & SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_DIPLOMACY_RELATIONS) != 0; }
        int64_t treasury_raw() const { return treasury_value; } int64_t prestige_candidate_raw() const { return prestige_value; }
        int64_t infamy_candidate_raw() const { return infamy_value; } int64_t plurality_candidate_raw() const { return plurality_value; }
        int64_t war_exhaustion_candidate_raw() const { return war_exhaustion_value; } int64_t diplomatic_points_candidate_raw() const { return diplomatic_points_value; }
        int64_t research_points_candidate_raw() const { return research_points_value; } int64_t leadership_candidate_raw() const { return leadership_value; }
        int32_t ranking_candidate() const { return ranking_value; } int32_t military_ranking_candidate() const { return military_ranking_value; }
        int32_t industrial_ranking_candidate() const { return industrial_ranking_value; } int32_t prestige_ranking_candidate() const { return prestige_ranking_value; }
        bool mobilized_candidate() const { return mobilized_value != 0; } bool substate_candidate() const { return substate_value != 0; } bool vassal_candidate() const { return vassal_value != 0; }
        bool unit_count_candidate(int *out) const { if (!military_available() || out == nullptr) return false; *out = static_cast<int>(unit_count_value); return true; }
        bool scheduled_mobilization_count_candidate(size_t *out) const { if (!military_available() || out == nullptr) return false; *out = scheduled_mobilization_count_value; return true; }
        bool sphereling_count_candidate(size_t *out) const { if (!diplomacy_relations_available() || out == nullptr) return false; *out = sphereling_count_value; return true; }
        bool vassal_count_candidate(size_t *out) const { if (!diplomacy_relations_available() || out == nullptr) return false; *out = vassal_count_value; return true; }
        bool ally_count_candidate(size_t *out) const { if (!diplomacy_relations_available() || out == nullptr) return false; *out = ally_count_value; return true; }
        bool guaranteed_count_candidate(size_t *out) const { if (!diplomacy_relations_available() || out == nullptr) return false; *out = guaranteed_count_value; return true; }
        bool neighbor_count_candidate(size_t *out) const { if (!diplomacy_relations_available() || out == nullptr) return false; *out = neighbor_count_value; return true; }
        const Tag &tag() const { return tag_value; } const Tag &overlord_candidate() const { return overlord_value; } const Tag &sphere_leader_candidate() const { return sphere_value; }
    };

    struct TelemetryProvinceSnapshot {
        ProvinceRef province{}; Tag owner_value{}, controller_value{}; int64_t infrastructure_value = 0;
        int32_t id_value = -1, colonial_level_value = 0, life_rating_value = 0, construction_count_value = 0;
        uint32_t availability_flags = 0, building_slot_count_value = 0;
        bool daily_available() const { return (availability_flags & SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_PROVINCE_DAILY) != 0; }
        bool production_available() const { return (availability_flags & SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_PROVINCE_PRODUCTION) != 0; }
        int32_t id_candidate() const { return id_value; } int32_t colonial_level_candidate() const { return colonial_level_value; }
        int32_t life_rating_candidate() const { return life_rating_value; } int64_t infrastructure_candidate() const { return infrastructure_value; }
        bool building_slot_count_candidate(size_t *out) const { if (!production_available() || out == nullptr) return false; *out = building_slot_count_value; return true; }
        bool construction_count_candidate(int *out) const { if (!production_available() || out == nullptr) return false; *out = construction_count_value; return true; }
        const Tag &owner_candidate() const { return owner_value; } const Tag &controller_candidate() const { return controller_value; }
    };

    struct PopMoneySnapshot { int64_t money_raw = 0, interest_cash_flow_raw = 0, total_cash_flow_raw = 0, savings_raw = 0; };
    struct CountryEconomySnapshot {
        int32_t date_raw = 0; char country_tag[4]{}; int32_t country_ordinal = -1, state_count_reported = 0;
        uint32_t states_walked = 0, province_element_candidates = 0, states_with_savings = 0, states_with_interest = 0, creditor_count = 0, creditor_destinations = 0, creditors_was_paid = 0;
        uint32_t invalid_creditor_key = 0, invalid_creditor_was_paid = 0, destination_provinces_resolved = 0,
            destination_province_attempts = 0, destination_pop_lists = 0, destination_pops = 0, destination_pop_attempts = 0;
        int32_t invalid_creditor_ordinal = 0;
        int64_t treasury_raw = 0, state_savings_raw = 0, state_interest_raw = 0, bank_interest_raw = 0, creditor_interest_raw = 0, creditor_debt_raw = 0, destination_bank_interest_raw = 0, destination_state_savings_raw = 0, destination_state_interest_raw = 0, destination_pop_savings_raw = 0, destination_pop_savings_state_scale_raw = 0;
        std::array<uint32_t, 512> destination_keys{}; std::array<int32_t, 512> destination_ordinals{}; std::array<int64_t, 512> destination_bank_interests_raw{}; uint32_t flags = 0;
    };
    struct PopCandidate { PopRef address{}; int64_t savings_raw = 0; };
    struct PopDetailSnapshot { int32_t pop_id = -1, province_id_candidate = -1, pop_type_id_candidate = -1, size_candidate = 0, employed_candidate = 0; int64_t consciousness_candidate_raw = 0, militancy_candidate_raw = 0, literacy_candidate_raw = 0; PopMoneySnapshot economy{}; };
    struct PopNeedsSnapshot { int64_t life_satisfaction_candidate_raw = 0, everyday_satisfaction_candidate_raw = 0, luxury_satisfaction_candidate_raw = 0; };
    struct PopIdentityDimensions { char pop_type_tag_candidate[64]{}, culture_tag_candidate[64]{}, religion_tag_candidate[64]{}; };
    struct ArtisanInputSnapshot { int32_t good_ordinal = -1; int64_t stockpile_raw = 0, need_raw = 0; };
    struct ArtisanSnapshot { PopRef address{}; int32_t pop_id = -1, output_good_ordinal = -1; char production_type[64]{}, output_good[64]{}; int64_t base_output_raw = 0, current_producing_raw = 0, gross_output_raw = 0, last_spending_raw = 0, percent_afforded_raw = 0, percent_sold_domestic_raw = 0, percent_sold_export_raw = 0, leftover_raw = 0, throttle_raw = 0, needs_cost_raw = 0, production_income_raw = 0; };
    struct ArtisanReadFailure { uint32_t reason = 0; int32_t pop_id = -1; int64_t offending_raw = 0; };
    struct FactorySnapshot { FactoryRef address{}; uint32_t state_index = 0, factory_index = 0; int32_t state_id = -1, anchor_province_id_candidate = -1, level = 0, employee_count = 0, craftsmen_count = 0, clerk_count = 0, output_raw = 0, output_good_ordinal = -1, base_output_raw = 0; char state_region_key[64]{}, factory_type[64]{}, output_good[64]{}; bool subsidized = false, closed = false; int64_t budget_raw = 0, market_spending_raw = 0, sales_income_raw = 0, paychecks_raw = 0, investment_raw = 0; };
    struct FactoryInputSnapshot { uint32_t factory_snapshot_index = 0; int32_t good_ordinal = -1; int64_t stockpile_raw = 0, requested_raw = 0; };
    struct WorldMarketSnapshot { int32_t good_ordinal = -1; int64_t price_raw = 0, last_price_raw = 0, supply_raw = 0, last_supply_raw = 0, worldmarket_stock_raw = 0, demand_raw = 0, real_demand_raw = 0, actual_sold_raw = 0, actual_sold_world_raw = 0; };
    struct RgoSnapshot { int32_t province_id = -1, output_good_ordinal = -1, employment_capacity = 0, employed = 0, owner_population = 0, state_rgo_employment_capacity = 0; char production_type[64]{}, output_good[64]{}; int64_t base_output_per_size_raw = 0, base_size_raw = 0, output_efficiency_raw = 0, throughput_raw = 0, gross_output_raw = 0, owner_output_modifier_raw = 0, income_raw = 0, percent_sold_domestic_raw = 0, percent_sold_export_raw = 0, leftover_raw = 0; };
    struct FactorySettlementHookRecord { FactoryRef factory{}; uint32_t pool = 0; std::array<int64_t, 64> quantity_raw{}; };
    struct FactorySalesHookRecord { FactoryRef factory{}; int64_t proceeds_raw = 0, produced_raw = 0, opening_inventory_raw = 0, closing_inventory_raw = 0; };
    struct ArtisanSettlementHookRecord { PopRef pop{}; uint32_t pool = 0; std::array<int64_t, 64> quantity_raw{}; };
    struct PopCashFlowHookRecord { PopRef pop{}; std::array<int64_t, 8> posted_raw{}, money_delta_raw{}; uint32_t call_count = 0, clamped_call_count = 0; };
    struct PopCashFlowHookStats {
        uint64_t calls = 0, invalid_index = 0, table_full = 0, overflow = 0, output_overflow = 0;
        bool complete() const { return invalid_index == 0 && table_full == 0 && overflow == 0 && output_overflow == 0; }
    };

    class Api final
    {
    public:
        bool Acquire(std::string *error);
        bool Open(std::string *error);
        bool EnsureOpen(uint32_t hooks, const uint32_t *artisan_keys, uint32_t artisan_count) noexcept;
        bool Close();
        bool Subscribe(uint32_t hooks, const uint32_t *artisan_keys, uint32_t artisan_count, std::string *error);
        bool Unsubscribe();
        void BeginHookDrain();
        bool ReadCurrent(TelemetryCurrentState *out) const;
        bool ReadCountry(CountryRef country, TelemetryCountrySnapshot *out) const;
        bool ReadProvince(ProvinceRef province, TelemetryProvinceSnapshot *out) const;
        bool ReadMarket(WorldMarketSnapshot *out, uint32_t capacity, uint32_t *count, uint32_t groups) const;
        bool ReadPops(CountryRef country, PopCandidate *candidates, PopDetailSnapshot *details, uint32_t capacity, uint32_t *count) const;
        bool ReadCountryEconomy(CountryRef country, CountryEconomySnapshot *out) const;
        bool ReadFactories(CountryRef country, FactorySnapshot *factories, uint32_t factory_capacity, uint32_t *factory_count, FactoryInputSnapshot *inputs, uint32_t input_capacity, uint32_t *input_count, uint32_t groups, uint32_t *flags) const;
        bool ReadRgo(ProvinceRef province, uint32_t groups, RgoSnapshot *out) const;
        bool ReadPopIdentity(PopRef pop, PopIdentityDimensions *out) const;
        bool ReadPopNeeds(PopRef pop, PopNeedsSnapshot *out) const;
        bool ReadArtisan(PopRef pop, ArtisanSnapshot *out, ArtisanInputSnapshot *inputs, uint32_t capacity, uint32_t *count, uint32_t groups, ArtisanReadFailure *failure) const;
        bool DrainHooks(SmedleyTelemetryHookRecordV1 *records, uint32_t capacity, uint32_t *count, uint64_t *dropped) const;
        bool DrainFactoryConsumption(FactorySettlementHookRecord *records, uint32_t capacity, uint32_t *count, uint64_t *dropped);
        bool DrainFactorySales(FactorySalesHookRecord *records, uint32_t capacity, uint32_t *count, uint64_t *dropped);
        bool DrainArtisanConsumption(ArtisanSettlementHookRecord *records, uint32_t capacity, uint32_t *count, uint64_t *dropped);
        bool DrainPopCashFlow(PopCashFlowHookRecord *records, uint32_t capacity, uint32_t *count, PopCashFlowHookStats *stats);
        bool ResolveDailyCountry(const SmedleyDailyEventV1 *event, CountryRef *out) const;
        void Log(SmedleyLogLevel level, const std::string &message) const;
        const SmedleyEventApiV1 &events() const { return event_api_; }
        bool usable() const { return session_ != 0 && observation_ != 0; }

    private:
        bool ObservationOk(SmedleyTelemetryObservationResult result) const;
        SmedleyEventApiV1 event_api_{}; SmedleyLoggingApiV1 logging_api_{}; SmedleyTelemetryGameApiV1 game_api_{}; SmedleyTelemetryObservationApiV1 observation_api_{};
        bool FillHookCache();
        SmedleyTelemetrySession session_ = 0; SmedleyTelemetryObservationSession observation_ = 0; SmedleyTelemetryHookSubscription hooks_ = 0;
        bool hooks_drained_ = false;
        uint64_t hook_dropped_ = 0;
        uint32_t hook_record_count_ = 0;
        std::vector<SmedleyTelemetryHookRecordV1> hook_records_;
        std::unique_ptr<std::array<SmedleyTelemetryPopObservationV1, max_sample_pops>> pop_values_;
        std::unique_ptr<std::array<SmedleyTelemetryFactoryObservationV1, max_sample_factories>> factory_values_;
        std::unique_ptr<std::array<SmedleyTelemetryFactoryInputObservationV1, max_sample_factory_inputs>> factory_input_values_;
    };

    inline Api *active_api = nullptr;
    inline void SetActiveApi(Api *api) { active_api = api; }
}

// The telemetry policy predates the service boundary. These local aliases preserve its
// value-only data model while every read below dispatches through the checked C tables.
namespace smedley::game_state
{
    using telemetry_plugin::services::Api;
    using telemetry_plugin::services::ArtisanInputSnapshot;
    using telemetry_plugin::services::ArtisanReadFailure;
    using telemetry_plugin::services::ArtisanSnapshot;
    using telemetry_plugin::services::CountryRef;
    using telemetry_plugin::services::CountryEconomySnapshot;
    using telemetry_plugin::services::EmploymentRegistryRef;
    using telemetry_plugin::services::FactoryInputSnapshot;
    using telemetry_plugin::services::FactoryRef;
    using telemetry_plugin::services::FactorySnapshot;
    using telemetry_plugin::services::PopDetailSnapshot;
    using telemetry_plugin::services::PopCandidate;
    using telemetry_plugin::services::PopMoneySnapshot;
    using telemetry_plugin::services::PopIdentityDimensions;
    using telemetry_plugin::services::PopNeedsSnapshot;
    using telemetry_plugin::services::PopRef;
    using telemetry_plugin::services::ProvinceRef;
    using telemetry_plugin::services::RgoSnapshot;
    using telemetry_plugin::services::TelemetryCountrySnapshot;
    using telemetry_plugin::services::TelemetryCurrentState;
    using telemetry_plugin::services::TelemetryProvinceSnapshot;
    using telemetry_plugin::services::WorldMarketSnapshot;
    constexpr uint32_t max_sample_pops = telemetry_plugin::services::max_sample_pops;
    constexpr uint32_t max_sample_factories = telemetry_plugin::services::max_sample_factories;
    constexpr uint32_t max_sample_factory_inputs = telemetry_plugin::services::max_sample_factory_inputs;
    constexpr uint32_t max_sample_destination_provinces = telemetry_plugin::services::max_sample_destination_provinces;
    constexpr uint32_t max_game_countries = telemetry_plugin::services::max_world_countries;
    enum CountryEconomySnapshotFlag : uint32_t {
        SAMPLE_COUNTRY_UNREADABLE = 1u << 0, SAMPLE_STATE_LIST_INVALID = 1u << 1,
        SAMPLE_STATE_UNREADABLE = 1u << 2, SAMPLE_STATE_VECTOR_INVALID = 1u << 3,
        SAMPLE_STATE_LIMIT = 1u << 4, SAMPLE_STATE_COUNT_MISMATCH = 1u << 5,
        SAMPLE_SUM_OVERFLOW = 1u << 6, SAMPLE_BANK_UNREADABLE = 1u << 7,
        SAMPLE_CREDITOR_VECTOR_INVALID = 1u << 8, SAMPLE_CREDITOR_UNREADABLE = 1u << 11,
        SAMPLE_CREDITOR_TAG_INVALID = 1u << 12, SAMPLE_CREDITOR_DESTINATION_INVALID = 1u << 13,
        SAMPLE_CREDITOR_DESTINATION_LIMIT = 1u << 14, SAMPLE_CREDITOR_DUPLICATE_DESTINATION = 1u << 15,
        SAMPLE_PROVINCE_INVALID = 1u << 16, SAMPLE_POP_VECTOR_INVALID = 1u << 17,
        SAMPLE_POP_LIST_INVALID = 1u << 18, SAMPLE_POP_UNREADABLE = 1u << 19,
        SAMPLE_POP_LIMIT = 1u << 20, SAMPLE_DUPLICATE_PROVINCE = 1u << 21,
        SAMPLE_DUPLICATE_POP = 1u << 22,
    };
    using GameStateRef = telemetry_plugin::services::TelemetryCurrentState::GameStateRef;
    inline Api *ActiveApi() { return telemetry_plugin::services::active_api; }
    inline bool ReadTelemetryCurrentState(TelemetryCurrentState *out) { return telemetry_plugin::services::active_api != nullptr && telemetry_plugin::services::active_api->ReadCurrent(out); }
    inline CountryRef ResolveCountry(GameStateRef, int32_t ordinal) { return {ordinal}; }
    inline ProvinceRef ResolveProvince(GameStateRef, int32_t id) { return {id}; }
    inline bool ReadTelemetryCountry(CountryRef country, TelemetryCountrySnapshot *out) { return telemetry_plugin::services::active_api != nullptr && telemetry_plugin::services::active_api->ReadCountry(country, out); }
    inline bool ReadTelemetryProvince(ProvinceRef province, TelemetryProvinceSnapshot *out) { return telemetry_plugin::services::active_api != nullptr && telemetry_plugin::services::active_api->ReadProvince(province, out); }
    inline bool CollectWorldMarket(GameStateRef, WorldMarketSnapshot *out, size_t capacity, uint32_t *count) { return telemetry_plugin::services::active_api != nullptr && telemetry_plugin::services::active_api->ReadMarket(out, static_cast<uint32_t>(capacity), count, 0); }
    inline bool CollectWorldMarketGroups(GameStateRef, WorldMarketSnapshot *out, size_t capacity, uint32_t *count, uint32_t groups) { return telemetry_plugin::services::active_api != nullptr && telemetry_plugin::services::active_api->ReadMarket(out, static_cast<uint32_t>(capacity), count, groups); }
    inline EmploymentRegistryRef ResolveStateEmploymentRegistry() { return {}; }
    inline constexpr uint32_t ARTISAN_IDENTITY = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_ARTISAN_IDENTITY;
    inline constexpr uint32_t ARTISAN_PRODUCTION = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_ARTISAN_PRODUCTION;
    inline constexpr uint32_t ARTISAN_INPUTS = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_ARTISAN_INPUTS;
    inline constexpr uint32_t ARTISAN_FINANCE = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_ARTISAN_FINANCE;
    inline constexpr uint32_t ARTISAN_FLOWS = 0;
    inline constexpr uint32_t ARTISAN_ALL = ARTISAN_IDENTITY | ARTISAN_PRODUCTION | ARTISAN_INPUTS | ARTISAN_FINANCE | ARTISAN_FLOWS;
    inline constexpr uint32_t FACTORY_IDENTITY = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_IDENTITY;
    inline constexpr uint32_t FACTORY_EMPLOYMENT = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_EMPLOYMENT;
    inline constexpr uint32_t FACTORY_PRODUCTION = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_PRODUCTION;
    inline constexpr uint32_t FACTORY_FINANCE = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_FINANCE;
    inline constexpr uint32_t FACTORY_INPUTS = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_INPUTS;
    inline constexpr uint32_t RGO_IDENTITY = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_IDENTITY;
    inline constexpr uint32_t RGO_EMPLOYMENT = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_EMPLOYMENT;
    inline constexpr uint32_t RGO_PRODUCTION = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_PRODUCTION;
    inline constexpr uint32_t RGO_FINANCE = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_FINANCE;
    inline constexpr uint32_t RGO_MODIFIERS = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_MODIFIERS;
    inline constexpr uint32_t RGO_SALES = SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_SALES;
    inline bool CollectCountryFactories(CountryRef country, FactorySnapshot *factories, size_t factory_capacity, uint32_t *factory_count,
        FactoryInputSnapshot *inputs, size_t input_capacity, uint32_t *input_count, uint32_t groups, uint32_t *flags)
    { return telemetry_plugin::services::active_api != nullptr && telemetry_plugin::services::active_api->ReadFactories(country, factories, static_cast<uint32_t>(factory_capacity), factory_count, inputs, static_cast<uint32_t>(input_capacity), input_count, groups, flags); }
    inline bool ReadProvinceRgo(EmploymentRegistryRef, ProvinceRef province, int32_t, size_t, uint32_t groups, RgoSnapshot *out)
    { return telemetry_plugin::services::active_api != nullptr && telemetry_plugin::services::active_api->ReadRgo(province, groups, out); }
    inline bool ReadPopIdentityDimensions(PopRef pop, PopIdentityDimensions *out) { return telemetry_plugin::services::active_api != nullptr && telemetry_plugin::services::active_api->ReadPopIdentity(pop, out); }
    inline bool ReadPopNeedsSnapshot(PopRef pop, PopNeedsSnapshot *out) { return telemetry_plugin::services::active_api != nullptr && telemetry_plugin::services::active_api->ReadPopNeeds(pop, out); }
    inline bool ReadArtisanSnapshot(PopRef pop, ArtisanSnapshot *out, ArtisanInputSnapshot *inputs, size_t capacity, uint32_t *count, uint32_t groups, ArtisanReadFailure *failure = nullptr)
    { return telemetry_plugin::services::active_api != nullptr && telemetry_plugin::services::active_api->ReadArtisan(pop, out, inputs, static_cast<uint32_t>(capacity), count, groups, failure); }
    inline bool ReadCountryCount(GameStateRef, uint32_t *count) { TelemetryCurrentState state{}; return telemetry_plugin::services::active_api != nullptr && telemetry_plugin::services::active_api->ReadCurrent(&state) && ((*count = state.country_count()), true); }
    inline CountryEconomySnapshot ReadCountryEconomy(CountryRef country, int32_t) {
        CountryEconomySnapshot result{};
        if (telemetry_plugin::services::active_api == nullptr || !telemetry_plugin::services::active_api->ReadCountryEconomy(country, &result)) result.flags = SAMPLE_COUNTRY_UNREADABLE;
        return result;
    }
    struct CachedPopDetail { PopRef pop{}; PopDetailSnapshot detail{}; };
    inline thread_local std::unique_ptr<std::array<CachedPopDetail, max_sample_pops>> cached_pop_details;
    inline thread_local uint32_t cached_pop_detail_count = 0;
    inline void ResetCachedPopDetails() { cached_pop_detail_count = 0; }
    inline void SortCachedPopDetails() {
        if (cached_pop_details == nullptr) return;
        std::sort(cached_pop_details->begin(), cached_pop_details->begin() + cached_pop_detail_count,
            [](const CachedPopDetail &left, const CachedPopDetail &right) { return left.pop.value < right.pop.value; });
    }
    inline bool CollectCountryPops(CountryRef country, GameStateRef, int32_t, PopCandidate *candidates, size_t capacity,
        uint32_t remaining_provinces, uint32_t *count, CountryEconomySnapshot *quality)
    {
        static thread_local std::unique_ptr<std::array<PopDetailSnapshot, max_sample_pops>> details;
        if (quality != nullptr && (telemetry_plugin::services::active_api == nullptr
            || !telemetry_plugin::services::active_api->ReadCountryEconomy(country, quality))) return false;
        if (quality != nullptr && quality->destination_province_attempts > remaining_provinces) {
            quality->flags |= SAMPLE_STATE_LIMIT;
            return false;
        }
        if (cached_pop_details == nullptr) cached_pop_details = std::make_unique<std::array<CachedPopDetail, max_sample_pops>>();
        if (details == nullptr) details = std::make_unique<std::array<PopDetailSnapshot, max_sample_pops>>();
        if (telemetry_plugin::services::active_api == nullptr || !telemetry_plugin::services::active_api->ReadPops(country, candidates, details->data(), static_cast<uint32_t>(capacity), count)) return false;
        if (*count > max_sample_pops - cached_pop_detail_count) return false;
        for (uint32_t index = 0; index < *count; ++index) (*cached_pop_details)[cached_pop_detail_count + index] = {candidates[index].address, (*details)[index]};
        cached_pop_detail_count += *count;
        return true;
    }
    inline const CachedPopDetail *CachedPop(PopRef pop) {
        if (cached_pop_details == nullptr) return nullptr;
        const auto end = cached_pop_details->begin() + cached_pop_detail_count;
        const auto found = std::lower_bound(cached_pop_details->begin(), end, pop.value, [](const CachedPopDetail &detail, uint64_t value) { return detail.pop.value < value; });
        return found != end && found->pop.value == pop.value ? &*found : nullptr;
    }
    inline bool ReadPopMoneySnapshot(PopRef pop, PopMoneySnapshot *out) { const auto *cached = CachedPop(pop); if (!cached || !out) return false; *out = cached->detail.economy; return true; }
    inline bool ReadPopDetailSnapshot(PopRef pop, PopDetailSnapshot *out) { const auto *cached = CachedPop(pop); if (!cached || !out) return false; *out = cached->detail; return true; }
    inline bool ReadInactiveArtisan(PopRef pop, int32_t *pop_id) { PopDetailSnapshot detail{}; if (!ReadPopDetailSnapshot(pop, &detail) || detail.pop_type_id_candidate != 2) return false; if (pop_id) *pop_id = detail.pop_id; return true; }
    inline const char *ArtisanReadFailureName(uint32_t) { return "observation_unavailable"; }
}

namespace smedley::events
{
    class DailyUpdateEvent final
    {
    public:
        explicit DailyUpdateEvent(const SmedleyDailyEventV1 &value) : value_(value) {}
        const SmedleyDailyEventV1 &value() const { return value_; }
    private:
        const SmedleyDailyEventV1 &value_;
    };
}

namespace smedley::game_state
{
    inline CountryRef DailyUpdateCountry(events::DailyUpdateEvent &event) {
        CountryRef country{};
        return telemetry_plugin::services::active_api != nullptr && telemetry_plugin::services::active_api->ResolveDailyCountry(&event.value(), &country) ? country : CountryRef{};
    }
}
