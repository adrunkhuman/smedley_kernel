#include <stddef.h>

#include <smedley/telemetry_observation_api.h>

#define SMEDLEY_ASSERT_LAYOUT(type, member, offset) typedef char type##_##member##_offset[(offsetof(type, member) == offset) ? 1 : -1]
typedef char smedley_observation_result_is_u32[(sizeof(SmedleyTelemetryObservationResult) == sizeof(uint32_t)) ? 1 : -1];
typedef char smedley_observation_session_is_u64[(sizeof(SmedleyTelemetryObservationSession) == sizeof(uint64_t)) ? 1 : -1];
#define SMEDLEY_ASSERT_SIZE(type, size) typedef char type##_size[(sizeof(type) == size) ? 1 : -1]
SMEDLEY_ASSERT_SIZE(SmedleyTelemetryWorldObservationV1, 48); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryWorldObservationV1, date_raw, 8); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryWorldObservationV1, availability_flags, 32);
SMEDLEY_ASSERT_SIZE(SmedleyTelemetryMarketObservationV1, 112); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryMarketObservationV1, group_flags, 16); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryMarketObservationV1, price_raw, 24); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryMarketObservationV1, reserved, 96);
SMEDLEY_ASSERT_SIZE(SmedleyTelemetryCountryObservationV1, 168); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryCountryObservationV1, tag, 12); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryCountryObservationV1, availability_flags, 24); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryCountryObservationV1, treasury_raw, 88); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryCountryObservationV1, reserved, 152);
SMEDLEY_ASSERT_SIZE(SmedleyTelemetryProvinceObservationV1, 64); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryProvinceObservationV1, province_id, 8); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryProvinceObservationV1, infrastructure_raw, 24); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryProvinceObservationV1, availability_flags, 40);
SMEDLEY_ASSERT_SIZE(SmedleyTelemetryCountryEconomyObservationV1, 160); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryCountryEconomyObservationV1, country_tag, 20); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryCountryEconomyObservationV1, source_flags, 56); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryCountryEconomyObservationV1, treasury_raw, 72);
SMEDLEY_ASSERT_SIZE(SmedleyTelemetryCreditorDestinationObservationV1, 40); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryCreditorDestinationObservationV1, country_ordinal, 12); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryCreditorDestinationObservationV1, bank_interest_raw, 16);
SMEDLEY_ASSERT_SIZE(SmedleyTelemetryPopObservationV1, 112); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryPopObservationV1, pop, 8); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryPopObservationV1, availability_flags, 36); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryPopObservationV1, money_raw, 56);
SMEDLEY_ASSERT_SIZE(SmedleyTelemetryPopIdentityObservationV1, 224); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryPopIdentityObservationV1, pop_type_tag, 16); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryPopIdentityObservationV1, availability_flags, 208);
SMEDLEY_ASSERT_SIZE(SmedleyTelemetryPopNeedsObservationV1, 56); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryPopNeedsObservationV1, life_satisfaction_raw, 16); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryPopNeedsObservationV1, availability_flags, 40);
SMEDLEY_ASSERT_SIZE(SmedleyTelemetryArtisanObservationV1, 264); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryArtisanObservationV1, production_type, 24); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryArtisanObservationV1, availability_flags, 152); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryArtisanObservationV1, base_output_raw, 176);
SMEDLEY_ASSERT_SIZE(SmedleyTelemetryArtisanInputObservationV1, 48); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryArtisanInputObservationV1, stockpile_raw, 16); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryArtisanInputObservationV1, reserved, 32);
SMEDLEY_ASSERT_SIZE(SmedleyTelemetryArtisanFailureV1, 40); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryArtisanFailureV1, pop_id, 12); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryArtisanFailureV1, offending_raw, 16);
SMEDLEY_ASSERT_SIZE(SmedleyTelemetryFactoryObservationV1, 328); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryFactoryObservationV1, factory, 8); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryFactoryObservationV1, observation_index, 16); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryFactoryObservationV1, state_region_key, 80); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryFactoryObservationV1, budget_raw, 272);
SMEDLEY_ASSERT_SIZE(SmedleyTelemetryFactoryInputObservationV1, 56); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryFactoryInputObservationV1, factory, 8); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryFactoryInputObservationV1, stockpile_raw, 24); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryFactoryInputObservationV1, reserved, 40);
SMEDLEY_ASSERT_SIZE(SmedleyTelemetryRgoObservationV1, 264); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRgoObservationV1, production_type, 32); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRgoObservationV1, availability_flags, 160); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRgoObservationV1, base_output_per_size_raw, 184);
SMEDLEY_ASSERT_SIZE(SmedleyTelemetryObservationApiV1, 72); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryObservationApiV1, open_session, 16); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryObservationApiV1, read_province, 40); SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryObservationApiV1, read_rgo, 68);

void compile_telemetry_observation_api_header_as_c(void)
{
    SmedleyTelemetryObservationApiV1 api = {0};
    SmedleyTelemetryWorldObservationV1 world = {0};
    SmedleyTelemetryProvinceObservationV1 province = {0};
    api.struct_size = sizeof(api);
    world.struct_size = sizeof(world);
    province.struct_size = sizeof(province);
}
