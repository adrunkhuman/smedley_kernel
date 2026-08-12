#ifndef SMEDLEY_TELEMETRY_OBSERVATION_API_H
#define SMEDLEY_TELEMETRY_OBSERVATION_API_H

#include <stdint.h>

#include <smedley/event_api.h>
#include <smedley/telemetry_game_api.h>

#ifdef _WIN32
#define SMEDLEY_TELEMETRY_OBSERVATION_CALL __cdecl
#ifdef SMEDLEY_TELEMETRY_OBSERVATION_BUILD
#define SMEDLEY_TELEMETRY_OBSERVATION_EXPORT __declspec(dllexport)
#else
#define SMEDLEY_TELEMETRY_OBSERVATION_EXPORT
#endif
#else
#define SMEDLEY_TELEMETRY_OBSERVATION_CALL
#define SMEDLEY_TELEMETRY_OBSERVATION_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SMEDLEY_TELEMETRY_OBSERVATION_API_VERSION_V1 UINT32_C(1)
#define SMEDLEY_TELEMETRY_OBSERVATION_GET_API_V1_SYMBOL "SmedleyGetTelemetryObservationApiV1"
#define SMEDLEY_TELEMETRY_OBSERVATION_KEY_CAPACITY UINT32_C(64)
#define SMEDLEY_TELEMETRY_OBSERVATION_MAX_GOODS UINT32_C(64)
#define SMEDLEY_TELEMETRY_OBSERVATION_MAX_POP_RECORDS UINT32_C(100000)
#define SMEDLEY_TELEMETRY_OBSERVATION_MAX_FACTORY_RECORDS UINT32_C(4096)
#define SMEDLEY_TELEMETRY_OBSERVATION_MAX_FACTORY_INPUTS UINT32_C(16384)
#define SMEDLEY_TELEMETRY_OBSERVATION_MAX_CREDITOR_DESTINATIONS UINT32_C(512)

typedef uint32_t SmedleyTelemetryObservationResult;
enum {
    SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS = 0,
    SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT = 1,
    SMEDLEY_TELEMETRY_OBSERVATION_UNAVAILABLE = 2,
    SMEDLEY_TELEMETRY_OBSERVATION_STALE_HANDLE = 3,
    SMEDLEY_TELEMETRY_OBSERVATION_CAPACITY = 4,
    SMEDLEY_TELEMETRY_OBSERVATION_WRONG_THREAD = 5,
    SMEDLEY_TELEMETRY_OBSERVATION_TRUNCATED = 6,
    SMEDLEY_TELEMETRY_OBSERVATION_INVALID_SOURCE = 7
};

typedef uint64_t SmedleyTelemetryObservationSession;
typedef uint64_t SmedleyTelemetryOpaquePop;
typedef uint64_t SmedleyTelemetryOpaqueFactory;

enum {
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_WORLD_DAILY = 1u << 0,
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_WORLD_MILITARY = 1u << 1,
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_DAILY = 1u << 2,
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_POWER = 1u << 3,
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_POLITICS = 1u << 4,
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_MILITARY = 1u << 5,
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_DIPLOMACY_STATUS = 1u << 6,
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_COUNTRY_DIPLOMACY_RELATIONS = 1u << 7,
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_PROVINCE_DAILY = 1u << 8,
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_PROVINCE_PRODUCTION = 1u << 9,
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_ECONOMY = 1u << 10,
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_MARKET = 1u << 11,
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_POP_DETAIL = 1u << 12,
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_POP_IDENTITY = 1u << 13,
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_POP_NEEDS = 1u << 14,
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_ARTISAN = 1u << 15,
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_FACTORY = 1u << 16,
    SMEDLEY_TELEMETRY_OBSERVATION_AVAILABLE_RGO = 1u << 17
};

enum {
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_MARKET_PRICE = 1u << 0,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_MARKET_SUPPLY = 1u << 1,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_MARKET_DEMAND = 1u << 2,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_MARKET_SALES = 1u << 3,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_ARTISAN_IDENTITY = 1u << 4,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_ARTISAN_PRODUCTION = 1u << 5,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_ARTISAN_INPUTS = 1u << 6,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_ARTISAN_FINANCE = 1u << 7,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_IDENTITY = 1u << 8,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_EMPLOYMENT = 1u << 9,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_PRODUCTION = 1u << 10,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_FINANCE = 1u << 11,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_FACTORY_INPUTS = 1u << 12,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_IDENTITY = 1u << 13,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_EMPLOYMENT = 1u << 14,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_PRODUCTION = 1u << 15,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_FINANCE = 1u << 16,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_MODIFIERS = 1u << 17,
    SMEDLEY_TELEMETRY_OBSERVATION_GROUP_RGO_SALES = 1u << 18
};

typedef struct SmedleyTelemetryWorldObservationV1 {
    uint32_t struct_size, version;
    int32_t date_raw;
    uint32_t country_count, country_ai_count, human_control_present, province_count, ongoing_war_count;
    uint32_t availability_flags, reserved[3];
} SmedleyTelemetryWorldObservationV1;

typedef struct SmedleyTelemetryMarketObservationV1 {
    uint32_t struct_size, version;
    int32_t good_ordinal;
    uint32_t availability_flags, group_flags;
    int64_t price_raw, last_price_raw, supply_raw, last_supply_raw, worldmarket_stock_raw;
    int64_t demand_raw, real_demand_raw, actual_sold_raw, actual_sold_world_raw;
    uint32_t reserved[3];
} SmedleyTelemetryMarketObservationV1;

typedef struct SmedleyTelemetryCountryObservationV1 {
    uint32_t struct_size, version;
    int32_t ordinal;
    char tag[4], overlord_tag[4], sphere_leader_tag[4];
    uint32_t availability_flags, mobilized, substate, vassal;
    uint32_t unit_count, scheduled_mobilization_count, sphereling_count, vassal_count, ally_count, guarantee_count, neighbor_count;
    int32_t ranking, military_ranking, industrial_ranking, prestige_ranking;
    int64_t treasury_raw, prestige_raw, infamy_raw, plurality_raw, war_exhaustion_raw;
    int64_t diplomatic_points_raw, research_points_raw, leadership_raw;
    uint32_t reserved[3];
} SmedleyTelemetryCountryObservationV1;

typedef struct SmedleyTelemetryProvinceObservationV1 {
    uint32_t struct_size, version;
    int32_t province_id;
    char owner_tag[4], controller_tag[4];
    int64_t infrastructure_raw;
    int32_t colonial_level, life_rating;
    uint32_t availability_flags, building_slot_count, construction_count, reserved[3];
} SmedleyTelemetryProvinceObservationV1;

typedef struct SmedleyTelemetryCountryEconomyObservationV1 {
    uint32_t struct_size, version;
    int32_t country_ordinal, date_raw, state_count_reported;
    char country_tag[4];
    uint32_t states_walked, province_element_candidates, states_with_savings, states_with_interest;
    uint32_t creditor_count, creditor_destinations, creditors_was_paid, availability_flags, source_flags, reserved[3];
    int64_t treasury_raw, state_savings_raw, state_interest_raw, bank_interest_raw, creditor_interest_raw, creditor_debt_raw;
    int64_t destination_bank_interest_raw, destination_state_savings_raw, destination_state_interest_raw;
    int64_t destination_pop_savings_raw, destination_pop_savings_state_scale_raw;
} SmedleyTelemetryCountryEconomyObservationV1;

typedef struct SmedleyTelemetryCreditorDestinationObservationV1 {
    uint32_t struct_size, version;
    char tag[4];
    int32_t country_ordinal;
    int64_t bank_interest_raw;
    uint32_t availability_flags, reserved[3];
} SmedleyTelemetryCreditorDestinationObservationV1;

typedef struct SmedleyTelemetryPopObservationV1 {
    uint32_t struct_size, version;
    SmedleyTelemetryOpaquePop pop;
    int32_t pop_id, province_id_candidate, pop_type_id_candidate, size_candidate, employed_candidate;
    uint32_t availability_flags, source_flags, reserved[3];
    int64_t money_raw, savings_raw, interest_cash_flow_raw, total_cash_flow_raw;
    int64_t consciousness_candidate_raw, militancy_candidate_raw, literacy_candidate_raw;
} SmedleyTelemetryPopObservationV1;

typedef struct SmedleyTelemetryPopIdentityObservationV1 {
    uint32_t struct_size, version;
    SmedleyTelemetryOpaquePop pop;
    char pop_type_tag[SMEDLEY_TELEMETRY_OBSERVATION_KEY_CAPACITY];
    char culture_tag[SMEDLEY_TELEMETRY_OBSERVATION_KEY_CAPACITY];
    char religion_tag[SMEDLEY_TELEMETRY_OBSERVATION_KEY_CAPACITY];
    uint32_t availability_flags, reserved[3];
} SmedleyTelemetryPopIdentityObservationV1;

typedef struct SmedleyTelemetryPopNeedsObservationV1 {
    uint32_t struct_size, version;
    SmedleyTelemetryOpaquePop pop;
    int64_t life_satisfaction_raw, everyday_satisfaction_raw, luxury_satisfaction_raw;
    uint32_t availability_flags, reserved[3];
} SmedleyTelemetryPopNeedsObservationV1;

typedef struct SmedleyTelemetryArtisanObservationV1 {
    uint32_t struct_size, version;
    SmedleyTelemetryOpaquePop pop;
    int32_t pop_id, output_good_ordinal;
    char production_type[SMEDLEY_TELEMETRY_OBSERVATION_KEY_CAPACITY];
    char output_good[SMEDLEY_TELEMETRY_OBSERVATION_KEY_CAPACITY];
    uint32_t availability_flags, group_flags, inactive, reserved[2];
    int64_t base_output_raw, current_producing_raw, gross_output_raw, last_spending_raw, percent_afforded_raw;
    int64_t percent_sold_domestic_raw, percent_sold_export_raw, leftover_raw, throttle_raw, needs_cost_raw, production_income_raw;
} SmedleyTelemetryArtisanObservationV1;

typedef struct SmedleyTelemetryArtisanInputObservationV1 {
    uint32_t struct_size, version;
    int32_t good_ordinal;
    int64_t stockpile_raw, need_raw;
    uint32_t reserved[3];
} SmedleyTelemetryArtisanInputObservationV1;

typedef struct SmedleyTelemetryArtisanFailureV1 {
    uint32_t struct_size, version, reason;
    int32_t pop_id;
    int64_t offending_raw;
    uint32_t reserved[3];
} SmedleyTelemetryArtisanFailureV1;

typedef struct SmedleyTelemetryFactoryObservationV1 {
    uint32_t struct_size, version;
    SmedleyTelemetryOpaqueFactory factory;
    uint32_t observation_index, state_index, factory_index, availability_flags, group_flags;
    int32_t state_id, anchor_province_id_candidate, level, employee_count, craftsmen_count, clerk_count;
    int32_t output_raw, output_good_ordinal, base_output_raw, subsidized, closed;
    char state_region_key[SMEDLEY_TELEMETRY_OBSERVATION_KEY_CAPACITY];
    char factory_type[SMEDLEY_TELEMETRY_OBSERVATION_KEY_CAPACITY];
    char output_good[SMEDLEY_TELEMETRY_OBSERVATION_KEY_CAPACITY];
    int64_t budget_raw, market_spending_raw, sales_income_raw, paychecks_raw, investment_raw;
    uint32_t reserved[3];
} SmedleyTelemetryFactoryObservationV1;

typedef struct SmedleyTelemetryFactoryInputObservationV1 {
    uint32_t struct_size, version;
    SmedleyTelemetryOpaqueFactory factory;
    uint32_t factory_observation_index;
    int32_t good_ordinal;
    int64_t stockpile_raw, requested_raw;
    uint32_t reserved[3];
} SmedleyTelemetryFactoryInputObservationV1;

typedef struct SmedleyTelemetryRgoObservationV1 {
    uint32_t struct_size, version;
    int32_t province_id, output_good_ordinal, employment_capacity, employed, owner_population, state_rgo_employment_capacity;
    char production_type[SMEDLEY_TELEMETRY_OBSERVATION_KEY_CAPACITY];
    char output_good[SMEDLEY_TELEMETRY_OBSERVATION_KEY_CAPACITY];
    uint32_t availability_flags, group_flags, reserved[3];
    int64_t base_output_per_size_raw, base_size_raw, output_efficiency_raw, throughput_raw, gross_output_raw;
    int64_t owner_output_modifier_raw, income_raw, percent_sold_domestic_raw, percent_sold_export_raw, leftover_raw;
} SmedleyTelemetryRgoObservationV1;

typedef SmedleyTelemetryObservationResult (SMEDLEY_TELEMETRY_OBSERVATION_CALL *SmedleyOpenTelemetryObservationSessionV1Fn)(SmedleyTelemetrySession parent_session, SmedleyTelemetryObservationSession *session);
typedef SmedleyTelemetryObservationResult (SMEDLEY_TELEMETRY_OBSERVATION_CALL *SmedleyCloseTelemetryObservationSessionV1Fn)(SmedleyTelemetryObservationSession session);
typedef SmedleyTelemetryObservationResult (SMEDLEY_TELEMETRY_OBSERVATION_CALL *SmedleyReadTelemetryWorldObservationV1Fn)(SmedleyTelemetryObservationSession session, SmedleyTelemetryWorldObservationV1 *world);
typedef SmedleyTelemetryObservationResult (SMEDLEY_TELEMETRY_OBSERVATION_CALL *SmedleyReadTelemetryMarketObservationsV1Fn)(SmedleyTelemetryObservationSession session, uint32_t groups, SmedleyTelemetryMarketObservationV1 *markets, uint32_t capacity, uint32_t *count);
typedef SmedleyTelemetryObservationResult (SMEDLEY_TELEMETRY_OBSERVATION_CALL *SmedleyResolveTelemetryDailyCountryV1Fn)(SmedleyTelemetryObservationSession session, const SmedleyDailyEventV1 *event, int32_t *country_ordinal);
typedef SmedleyTelemetryObservationResult (SMEDLEY_TELEMETRY_OBSERVATION_CALL *SmedleyReadTelemetryCountryObservationV1Fn)(SmedleyTelemetryObservationSession session, int32_t country_ordinal, SmedleyTelemetryCountryObservationV1 *country);
typedef SmedleyTelemetryObservationResult (SMEDLEY_TELEMETRY_OBSERVATION_CALL *SmedleyReadTelemetryProvinceObservationV1Fn)(SmedleyTelemetryObservationSession session, int32_t province_id, SmedleyTelemetryProvinceObservationV1 *province);
typedef SmedleyTelemetryObservationResult (SMEDLEY_TELEMETRY_OBSERVATION_CALL *SmedleyReadTelemetryCountryEconomyV1Fn)(SmedleyTelemetryObservationSession session, int32_t country_ordinal, SmedleyTelemetryCountryEconomyObservationV1 *economy, SmedleyTelemetryCreditorDestinationObservationV1 *destinations, uint32_t capacity, uint32_t *count);
typedef SmedleyTelemetryObservationResult (SMEDLEY_TELEMETRY_OBSERVATION_CALL *SmedleyReadTelemetryPopObservationsV1Fn)(SmedleyTelemetryObservationSession session, int32_t country_ordinal, SmedleyTelemetryPopObservationV1 *pops, uint32_t capacity, uint32_t *count, uint32_t *source_flags);
typedef SmedleyTelemetryObservationResult (SMEDLEY_TELEMETRY_OBSERVATION_CALL *SmedleyReadTelemetryPopIdentityV1Fn)(SmedleyTelemetryObservationSession session, SmedleyTelemetryOpaquePop pop, SmedleyTelemetryPopIdentityObservationV1 *identity);
typedef SmedleyTelemetryObservationResult (SMEDLEY_TELEMETRY_OBSERVATION_CALL *SmedleyReadTelemetryPopNeedsV1Fn)(SmedleyTelemetryObservationSession session, SmedleyTelemetryOpaquePop pop, SmedleyTelemetryPopNeedsObservationV1 *needs);
typedef SmedleyTelemetryObservationResult (SMEDLEY_TELEMETRY_OBSERVATION_CALL *SmedleyReadTelemetryArtisanV1Fn)(SmedleyTelemetryObservationSession session, SmedleyTelemetryOpaquePop pop, uint32_t groups, SmedleyTelemetryArtisanObservationV1 *artisan, SmedleyTelemetryArtisanInputObservationV1 *inputs, uint32_t capacity, uint32_t *count, SmedleyTelemetryArtisanFailureV1 *failure);
typedef SmedleyTelemetryObservationResult (SMEDLEY_TELEMETRY_OBSERVATION_CALL *SmedleyReadTelemetryFactoryObservationsV1Fn)(SmedleyTelemetryObservationSession session, int32_t country_ordinal, uint32_t groups, SmedleyTelemetryFactoryObservationV1 *factories, uint32_t factory_capacity, uint32_t *factory_count, SmedleyTelemetryFactoryInputObservationV1 *inputs, uint32_t input_capacity, uint32_t *input_count, uint32_t *source_flags);
typedef SmedleyTelemetryObservationResult (SMEDLEY_TELEMETRY_OBSERVATION_CALL *SmedleyReadTelemetryRgoObservationV1Fn)(SmedleyTelemetryObservationSession session, int32_t province_id, uint32_t groups, SmedleyTelemetryRgoObservationV1 *rgo);

typedef struct SmedleyTelemetryObservationApiV1 {
    uint32_t struct_size, version, reserved[2];
    SmedleyOpenTelemetryObservationSessionV1Fn open_session;
    SmedleyCloseTelemetryObservationSessionV1Fn close_session;
    SmedleyReadTelemetryWorldObservationV1Fn read_world;
    SmedleyReadTelemetryMarketObservationsV1Fn read_market;
    SmedleyResolveTelemetryDailyCountryV1Fn resolve_daily_country;
    SmedleyReadTelemetryCountryObservationV1Fn read_country;
    SmedleyReadTelemetryProvinceObservationV1Fn read_province;
    SmedleyReadTelemetryCountryEconomyV1Fn read_country_economy;
    SmedleyReadTelemetryPopObservationsV1Fn read_pops;
    SmedleyReadTelemetryPopIdentityV1Fn read_pop_identity;
    SmedleyReadTelemetryPopNeedsV1Fn read_pop_needs;
    SmedleyReadTelemetryArtisanV1Fn read_artisan;
    SmedleyReadTelemetryFactoryObservationsV1Fn read_factories;
    SmedleyReadTelemetryRgoObservationV1Fn read_rgo;
} SmedleyTelemetryObservationApiV1;

typedef SmedleyTelemetryObservationResult (SMEDLEY_TELEMETRY_OBSERVATION_CALL *SmedleyGetTelemetryObservationApiV1Fn)(SmedleyTelemetryObservationApiV1 *api);
SMEDLEY_TELEMETRY_OBSERVATION_EXPORT SmedleyTelemetryObservationResult SMEDLEY_TELEMETRY_OBSERVATION_CALL SmedleyGetTelemetryObservationApiV1(SmedleyTelemetryObservationApiV1 *api);

#ifdef __cplusplus
}
#endif

#endif
