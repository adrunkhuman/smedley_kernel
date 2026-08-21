#ifndef SMEDLEY_CAMPAIGN_RUNTIME_API_H
#define SMEDLEY_CAMPAIGN_RUNTIME_API_H

#include <stdint.h>

#ifdef _WIN32
#define SMEDLEY_CAMPAIGN_RUNTIME_CALL __cdecl
#ifdef SMEDLEY_CAMPAIGN_RUNTIME_BUILD
#define SMEDLEY_CAMPAIGN_RUNTIME_EXPORT __declspec(dllexport)
#else
#define SMEDLEY_CAMPAIGN_RUNTIME_EXPORT
#endif
#else
#define SMEDLEY_CAMPAIGN_RUNTIME_CALL
#define SMEDLEY_CAMPAIGN_RUNTIME_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SMEDLEY_CAMPAIGN_RUNTIME_API_VERSION_V2 UINT32_C(2)
#define SMEDLEY_CAMPAIGN_RUNTIME_GET_API_V2_SYMBOL "SmedleyGetCampaignRuntimeApiV2"
#define SMEDLEY_CAMPAIGN_RUNTIME_RECORD_VERSION_V1 UINT32_C(1)
#define SMEDLEY_CAMPAIGN_SAVE_BASENAME_BYTES UINT32_C(260)

typedef uint32_t SmedleyCampaignRuntimeResult;
enum { SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS = 0, SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT = 1,
    SMEDLEY_CAMPAIGN_RUNTIME_UNAVAILABLE = 2, SMEDLEY_CAMPAIGN_RUNTIME_STALE_HANDLE = 3,
    SMEDLEY_CAMPAIGN_RUNTIME_PRECONDITION_FAILED = 4, SMEDLEY_CAMPAIGN_RUNTIME_READBACK_FAILED = 5,
    SMEDLEY_CAMPAIGN_RUNTIME_WRONG_THREAD = 6 };
typedef uint64_t SmedleyCampaignSession;
typedef uint64_t SmedleyFrontendController;

typedef struct SmedleyCampaignRuntimeSnapshotV2 {
    uint32_t struct_size, version;
    int32_t game_date_raw, speed_index;
    uint32_t paused, game_over, reserved[2];
} SmedleyCampaignRuntimeSnapshotV2;
typedef struct SmedleyFrontendSaveSnapshotV1 {
    uint32_t struct_size, version, request_pending, completed;
    char selected_basename[SMEDLEY_CAMPAIGN_SAVE_BASENAME_BYTES];
    uint32_t reserved[3];
} SmedleyFrontendSaveSnapshotV1;
typedef struct SmedleyObserverCountrySnapshotV1 {
    uint32_t struct_size, version;
    char tag[4];
    int32_t ordinal;
    uint32_t exists, human_controlled, has_ai, ai_scheduled, reserved[3];
} SmedleyObserverCountrySnapshotV1;
typedef struct SmedleyObserverStateSnapshotV1 {
    uint32_t struct_size, version;
    SmedleyObserverCountrySnapshotV1 view_country;
    uint32_t country_count, country_ai_count, human_control_present, full_map_visibility_enabled, reserved[3];
} SmedleyObserverStateSnapshotV1;

typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyOpenCampaignSessionV2Fn)(SmedleyCampaignSession *session);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyCloseCampaignSessionV2Fn)(SmedleyCampaignSession session);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyReadCampaignRuntimeV2Fn)(SmedleyCampaignSession session, SmedleyCampaignRuntimeSnapshotV2 *snapshot);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleySetCampaignPausedRuntimeV2Fn)(SmedleyCampaignSession session, uint32_t paused);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleySetCampaignSpeedRuntimeV2Fn)(SmedleyCampaignSession session, int32_t speed_index);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyRequestCampaignQuitRuntimeV2Fn)(SmedleyCampaignSession session);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyAcquireFrontendControllerV2Fn)(SmedleyCampaignSession session, uint32_t kind, SmedleyFrontendController *controller);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyReleaseFrontendControllerV2Fn)(SmedleyFrontendController controller);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyRequestFrontendSaveV2Fn)(SmedleyFrontendController controller, const char *basename, uint32_t basename_bytes);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyReadFrontendSaveV2Fn)(SmedleyFrontendController controller, SmedleyFrontendSaveSnapshotV1 *snapshot);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyDispatchFrontendControlV2Fn)(SmedleyFrontendController controller, const char *name, uint32_t name_bytes);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyDispatchMainMenuSinglePlayerV2Fn)(SmedleyFrontendController controller);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyReadObserverStateV2Fn)(SmedleyCampaignSession session, SmedleyObserverStateSnapshotV1 *snapshot);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyReadObserverCountryV2Fn)(SmedleyCampaignSession session, int32_t ordinal, SmedleyObserverCountrySnapshotV1 *snapshot);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyFindObserverCountryV2Fn)(SmedleyCampaignSession session, int32_t excluded_ordinal, SmedleyObserverCountrySnapshotV1 *snapshot);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleySetObserverViewCountryV2Fn)(SmedleyCampaignSession session, const SmedleyObserverCountrySnapshotV1 *country, SmedleyObserverStateSnapshotV1 *after);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyReturnObserverCountryV2Fn)(SmedleyCampaignSession session, const SmedleyObserverCountrySnapshotV1 *country, SmedleyObserverStateSnapshotV1 *after);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyEnableObserverFowV2Fn)(SmedleyCampaignSession session);

typedef struct SmedleyCampaignRuntimeApiV2 {
    uint32_t struct_size, version, reserved[2];
    SmedleyOpenCampaignSessionV2Fn open_session;
    SmedleyCloseCampaignSessionV2Fn close_session;
    SmedleyReadCampaignRuntimeV2Fn read_campaign;
    SmedleySetCampaignPausedRuntimeV2Fn set_paused;
    SmedleySetCampaignSpeedRuntimeV2Fn set_speed_index;
    SmedleyRequestCampaignQuitRuntimeV2Fn request_quit;
    SmedleyAcquireFrontendControllerV2Fn acquire_frontend;
    SmedleyReleaseFrontendControllerV2Fn release_frontend;
    SmedleyRequestFrontendSaveV2Fn request_save;
    SmedleyReadFrontendSaveV2Fn read_save;
    SmedleyDispatchFrontendControlV2Fn dispatch_frontend_control;
    SmedleyDispatchMainMenuSinglePlayerV2Fn dispatch_main_menu_single_player;
    SmedleyReadObserverStateV2Fn read_observer_state;
    SmedleyReadObserverCountryV2Fn read_observer_country;
    SmedleyFindObserverCountryV2Fn find_observer_country;
    SmedleySetObserverViewCountryV2Fn set_observer_view_country;
    SmedleyReturnObserverCountryV2Fn return_observer_country;
    SmedleyEnableObserverFowV2Fn enable_observer_fow;
} SmedleyCampaignRuntimeApiV2;
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyGetCampaignRuntimeApiV2Fn)(SmedleyCampaignRuntimeApiV2 *api);
SMEDLEY_CAMPAIGN_RUNTIME_EXPORT SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL SmedleyGetCampaignRuntimeApiV2(SmedleyCampaignRuntimeApiV2 *api);

#ifdef __cplusplus
}
#endif
#endif
