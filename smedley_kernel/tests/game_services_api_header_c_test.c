#include <smedley/campaign_runtime_api.h>
#include <smedley/campaign_automation_api.h>
#include <smedley/interest_pool_api.h>
#include <smedley/telemetry_game_api.h>

typedef char campaign_runtime_snapshot_size[sizeof(SmedleyCampaignRuntimeSnapshotV1) == 32 ? 1 : -1];
typedef char frontend_save_snapshot_size[sizeof(SmedleyFrontendSaveSnapshotV1) == 288 ? 1 : -1];
typedef char observer_country_snapshot_size[sizeof(SmedleyObserverCountrySnapshotV1) == 44 ? 1 : -1];
typedef char interest_state_snapshot_size[sizeof(SmedleyInterestStateSnapshotV1) == 64 ? 1 : -1];
typedef char interest_pop_snapshot_size[sizeof(SmedleyInterestPopSnapshotV1) == 40 ? 1 : -1];
typedef char telemetry_hook_options_size[sizeof(SmedleyTelemetryHookOptionsV1) == 92 ? 1 : -1];
typedef char telemetry_hook_record_size[sizeof(SmedleyTelemetryHookRecordV1) == 560 ? 1 : -1];
typedef char campaign_runtime_api_size[sizeof(SmedleyCampaignRuntimeApiV1) == 88 ? 1 : -1];
typedef char campaign_automation_api_size[sizeof(SmedleyCampaignAutomationApiV1) == 52 ? 1 : -1];
typedef char campaign_automation_options_size[sizeof(SmedleyCampaignAutomationOptionsV1) == 48 ? 1 : -1];
typedef char campaign_automation_frontend_capture_size[sizeof(SmedleyCampaignFrontendCaptureV1) == 32 ? 1 : -1];
typedef char campaign_automation_annexation_size[sizeof(SmedleyCampaignAnnexationV1) == 32 ? 1 : -1];
typedef char campaign_automation_console_capture_size[sizeof(SmedleyCampaignConsoleCaptureV1) == 40 ? 1 : -1];
typedef char campaign_automation_tag_size[sizeof(SmedleyCampaignTagV1) == 28 ? 1 : -1];
typedef char campaign_automation_popup_size[sizeof(SmedleyCampaignPopupSnapshotV1) == 32 ? 1 : -1];
typedef char campaign_automation_metrics_size[sizeof(SmedleyCampaignProcessMetricsV1) == 56 ? 1 : -1];
typedef char interest_pool_api_size[sizeof(SmedleyInterestPoolApiV1) == 32 ? 1 : -1];
typedef char telemetry_game_api_size[sizeof(SmedleyTelemetryGameApiV1) == 60 ? 1 : -1];
typedef char telemetry_world_snapshot_size[sizeof(SmedleyTelemetryWorldSnapshotV1) == 44 ? 1 : -1];
typedef char telemetry_market_snapshot_size[sizeof(SmedleyTelemetryMarketSnapshotV1) == 72 ? 1 : -1];
typedef char telemetry_country_snapshot_size[sizeof(SmedleyTelemetryCountrySnapshotV1) == 48 ? 1 : -1];
typedef char telemetry_province_snapshot_size[sizeof(SmedleyTelemetryProvinceSnapshotV1) == 64 ? 1 : -1];
typedef char telemetry_pop_snapshot_size[sizeof(SmedleyTelemetryPopSnapshotV1) == 64 ? 1 : -1];
typedef char telemetry_factory_snapshot_size[sizeof(SmedleyTelemetryFactorySnapshotV1) == 64 ? 1 : -1];

void compile_game_services_api_headers_as_c(void)
{
    SmedleyCampaignRuntimeApiV1 campaign = {0};
    SmedleyCampaignAutomationApiV1 automation = {0};
    SmedleyInterestPoolApiV1 interest = {0};
    SmedleyTelemetryGameApiV1 telemetry = {0};
    campaign.struct_size = sizeof(campaign); campaign.version = SMEDLEY_CAMPAIGN_RUNTIME_API_VERSION_V1;
    automation.struct_size = sizeof(automation); automation.version = SMEDLEY_CAMPAIGN_AUTOMATION_API_VERSION_V1;
    interest.struct_size = sizeof(interest); interest.version = SMEDLEY_INTEREST_POOL_API_VERSION_V1;
    telemetry.struct_size = sizeof(telemetry); telemetry.version = SMEDLEY_TELEMETRY_GAME_API_VERSION_V1;
    (void)telemetry.subscribe_hooks;
}
