#ifndef SMEDLEY_TELEMETRY_GAME_API_H
#define SMEDLEY_TELEMETRY_GAME_API_H

#include <stdint.h>

#ifdef _WIN32
#define SMEDLEY_TELEMETRY_GAME_CALL __cdecl
#ifdef SMEDLEY_TELEMETRY_GAME_BUILD
#define SMEDLEY_TELEMETRY_GAME_EXPORT __declspec(dllexport)
#else
#define SMEDLEY_TELEMETRY_GAME_EXPORT
#endif
#else
#define SMEDLEY_TELEMETRY_GAME_CALL
#define SMEDLEY_TELEMETRY_GAME_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SMEDLEY_TELEMETRY_GAME_API_VERSION_V1 UINT32_C(1)
#define SMEDLEY_TELEMETRY_GAME_GET_API_V1_SYMBOL "SmedleyGetTelemetryGameApiV1"
#define SMEDLEY_TELEMETRY_HOOK_VALUES UINT32_C(64)
typedef uint32_t SmedleyTelemetryGameResult;
enum { SMEDLEY_TELEMETRY_GAME_SUCCESS = 0, SMEDLEY_TELEMETRY_GAME_INVALID_ARGUMENT = 1,
    SMEDLEY_TELEMETRY_GAME_UNAVAILABLE = 2, SMEDLEY_TELEMETRY_GAME_STALE_HANDLE = 3,
    SMEDLEY_TELEMETRY_GAME_CAPACITY = 4, SMEDLEY_TELEMETRY_GAME_WRONG_THREAD = 5 };
typedef uint64_t SmedleyTelemetrySession;
typedef uint64_t SmedleyTelemetryHookSubscription;
#define SMEDLEY_TELEMETRY_MAX_ARTISAN_COUNTRY_KEYS UINT32_C(16)
enum { SMEDLEY_TELEMETRY_HOOK_POP_CASH_FLOW = 1, SMEDLEY_TELEMETRY_HOOK_FACTORY_CONSUMPTION = 2,
    SMEDLEY_TELEMETRY_HOOK_FACTORY_SALES = 4, SMEDLEY_TELEMETRY_HOOK_ARTISAN_CONSUMPTION = 8 };
typedef struct SmedleyTelemetryWorldSnapshotV1 { uint32_t struct_size, version; int32_t date_raw; uint32_t country_count, country_ai_count, human_control_present, province_count, availability_flags, reserved[3]; } SmedleyTelemetryWorldSnapshotV1;
typedef struct SmedleyTelemetryMarketSnapshotV1 { uint32_t struct_size, version; int32_t good_ordinal; int64_t price_raw, last_price_raw, supply_raw, demand_raw, stock_raw; uint32_t reserved[3]; } SmedleyTelemetryMarketSnapshotV1;
typedef struct SmedleyTelemetryCountrySnapshotV1 { uint32_t struct_size, version; int32_t ordinal; char tag[4]; int64_t treasury_raw; uint32_t availability_flags, unit_count, reserved[3]; } SmedleyTelemetryCountrySnapshotV1;
typedef struct SmedleyTelemetryProvinceSnapshotV1 { uint32_t struct_size, version; int32_t id; char owner_tag[4], controller_tag[4]; int64_t infrastructure_raw; int32_t colonial_level, life_rating; uint32_t availability_flags, building_slots, constructions, reserved[3]; } SmedleyTelemetryProvinceSnapshotV1;
typedef struct SmedleyTelemetryPopSnapshotV1 { uint32_t struct_size, version; int32_t pop_id, province_id, pop_type_id, size, employed; int64_t money_raw, savings_raw; uint32_t reserved[3]; } SmedleyTelemetryPopSnapshotV1;
typedef struct SmedleyTelemetryFactorySnapshotV1 { uint32_t struct_size, version; int32_t state_id, anchor_province_id, level, employee_count, output_good_ordinal; int64_t budget_raw, sales_income_raw; uint32_t flags, reserved[3]; } SmedleyTelemetryFactorySnapshotV1;
typedef struct SmedleyTelemetryHookOptionsV1 { uint32_t struct_size, version, hooks, artisan_country_count; uint32_t artisan_country_keys[SMEDLEY_TELEMETRY_MAX_ARTISAN_COUNTRY_KEYS], reserved[3]; } SmedleyTelemetryHookOptionsV1;
typedef struct SmedleyTelemetryHookRecordV1 { uint32_t struct_size, version, kind, pool, value_count, call_count, auxiliary_count, reserved[3]; uint64_t entity_id; int64_t values[SMEDLEY_TELEMETRY_HOOK_VALUES]; } SmedleyTelemetryHookRecordV1;

typedef SmedleyTelemetryGameResult (SMEDLEY_TELEMETRY_GAME_CALL *SmedleyOpenTelemetrySessionV1Fn)(SmedleyTelemetrySession *session);
typedef SmedleyTelemetryGameResult (SMEDLEY_TELEMETRY_GAME_CALL *SmedleyCloseTelemetrySessionV1Fn)(SmedleyTelemetrySession session);
typedef SmedleyTelemetryGameResult (SMEDLEY_TELEMETRY_GAME_CALL *SmedleyReadTelemetryWorldV1Fn)(SmedleyTelemetrySession session, SmedleyTelemetryWorldSnapshotV1 *snapshot);
typedef SmedleyTelemetryGameResult (SMEDLEY_TELEMETRY_GAME_CALL *SmedleyReadTelemetryMarketV1Fn)(SmedleyTelemetrySession session, SmedleyTelemetryMarketSnapshotV1 *market, uint32_t capacity, uint32_t *count);
typedef SmedleyTelemetryGameResult (SMEDLEY_TELEMETRY_GAME_CALL *SmedleyReadTelemetryCountryV1Fn)(SmedleyTelemetrySession session, int32_t ordinal, SmedleyTelemetryCountrySnapshotV1 *snapshot);
typedef SmedleyTelemetryGameResult (SMEDLEY_TELEMETRY_GAME_CALL *SmedleyReadTelemetryProvinceV1Fn)(SmedleyTelemetrySession session, int32_t id, SmedleyTelemetryProvinceSnapshotV1 *snapshot);
typedef SmedleyTelemetryGameResult (SMEDLEY_TELEMETRY_GAME_CALL *SmedleyReadTelemetryPopsV1Fn)(SmedleyTelemetrySession session, int32_t country_ordinal, SmedleyTelemetryPopSnapshotV1 *pops, uint32_t capacity, uint32_t *count, uint32_t *flags);
typedef SmedleyTelemetryGameResult (SMEDLEY_TELEMETRY_GAME_CALL *SmedleyReadTelemetryFactoriesV1Fn)(SmedleyTelemetrySession session, int32_t country_ordinal, SmedleyTelemetryFactorySnapshotV1 *factories, uint32_t capacity, uint32_t *count, uint32_t *flags);
typedef SmedleyTelemetryGameResult (SMEDLEY_TELEMETRY_GAME_CALL *SmedleySubscribeTelemetryHooksV1Fn)(SmedleyTelemetrySession session, const SmedleyTelemetryHookOptionsV1 *options, SmedleyTelemetryHookSubscription *subscription);
typedef SmedleyTelemetryGameResult (SMEDLEY_TELEMETRY_GAME_CALL *SmedleyDrainTelemetryHooksV1Fn)(SmedleyTelemetryHookSubscription subscription, SmedleyTelemetryHookRecordV1 *records, uint32_t capacity, uint32_t *count, uint64_t *dropped);
typedef SmedleyTelemetryGameResult (SMEDLEY_TELEMETRY_GAME_CALL *SmedleyUnsubscribeTelemetryHooksV1Fn)(SmedleyTelemetryHookSubscription subscription);
typedef struct SmedleyTelemetryGameApiV1 { uint32_t struct_size, version, reserved[2]; SmedleyOpenTelemetrySessionV1Fn open_session; SmedleyCloseTelemetrySessionV1Fn close_session; SmedleyReadTelemetryWorldV1Fn read_world; SmedleyReadTelemetryMarketV1Fn read_market; SmedleyReadTelemetryCountryV1Fn read_country; SmedleyReadTelemetryProvinceV1Fn read_province; SmedleyReadTelemetryPopsV1Fn read_pops; SmedleyReadTelemetryFactoriesV1Fn read_factories; SmedleySubscribeTelemetryHooksV1Fn subscribe_hooks; SmedleyDrainTelemetryHooksV1Fn drain_hooks; SmedleyUnsubscribeTelemetryHooksV1Fn unsubscribe_hooks; } SmedleyTelemetryGameApiV1;
typedef SmedleyTelemetryGameResult (SMEDLEY_TELEMETRY_GAME_CALL *SmedleyGetTelemetryGameApiV1Fn)(SmedleyTelemetryGameApiV1 *api);
SMEDLEY_TELEMETRY_GAME_EXPORT SmedleyTelemetryGameResult SMEDLEY_TELEMETRY_GAME_CALL SmedleyGetTelemetryGameApiV1(SmedleyTelemetryGameApiV1 *api);

#ifdef __cplusplus
}
#endif
#endif
