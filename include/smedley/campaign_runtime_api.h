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

#define SMEDLEY_CAMPAIGN_RUNTIME_API_VERSION_V1 UINT32_C(1)
#define SMEDLEY_CAMPAIGN_RUNTIME_GET_API_V1_SYMBOL "SmedleyGetCampaignRuntimeApiV1"
#define SMEDLEY_CAMPAIGN_SAVE_BASENAME_BYTES UINT32_C(260)

typedef uint32_t SmedleyCampaignRuntimeResult;
enum { SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS = 0, SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT = 1,
    SMEDLEY_CAMPAIGN_RUNTIME_UNAVAILABLE = 2, SMEDLEY_CAMPAIGN_RUNTIME_STALE_HANDLE = 3,
    SMEDLEY_CAMPAIGN_RUNTIME_PRECONDITION_FAILED = 4, SMEDLEY_CAMPAIGN_RUNTIME_READBACK_FAILED = 5,
    SMEDLEY_CAMPAIGN_RUNTIME_WRONG_THREAD = 6 };
typedef uint64_t SmedleyCampaignSession;
typedef uint64_t SmedleyFrontendController;

typedef struct SmedleyCampaignRuntimeSnapshotV1 {
    uint32_t struct_size, version;
    int32_t game_date_raw, speed_index;
    uint32_t paused, reserved[3];
} SmedleyCampaignRuntimeSnapshotV1;
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

typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyOpenCampaignSessionV1Fn)(SmedleyCampaignSession *session);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyCloseCampaignSessionV1Fn)(SmedleyCampaignSession session);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyReadCampaignRuntimeV1Fn)(SmedleyCampaignSession session, SmedleyCampaignRuntimeSnapshotV1 *snapshot);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleySetCampaignPausedRuntimeV1Fn)(SmedleyCampaignSession session, uint32_t paused);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleySetCampaignSpeedRuntimeV1Fn)(SmedleyCampaignSession session, int32_t speed_index);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyRequestCampaignQuitRuntimeV1Fn)(SmedleyCampaignSession session);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyAcquireFrontendControllerV1Fn)(SmedleyCampaignSession session, uint32_t kind, SmedleyFrontendController *controller);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyReleaseFrontendControllerV1Fn)(SmedleyFrontendController controller);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyRequestFrontendSaveV1Fn)(SmedleyFrontendController controller, const char *basename, uint32_t basename_bytes);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyReadFrontendSaveV1Fn)(SmedleyFrontendController controller, SmedleyFrontendSaveSnapshotV1 *snapshot);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyDispatchFrontendControlV1Fn)(SmedleyFrontendController controller, const char *name, uint32_t name_bytes);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyDispatchMainMenuSinglePlayerV1Fn)(SmedleyFrontendController controller);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyReadObserverStateV1Fn)(SmedleyCampaignSession session, SmedleyObserverStateSnapshotV1 *snapshot);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyReadObserverCountryV1Fn)(SmedleyCampaignSession session, int32_t ordinal, SmedleyObserverCountrySnapshotV1 *snapshot);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyFindObserverCountryV1Fn)(SmedleyCampaignSession session, int32_t excluded_ordinal, SmedleyObserverCountrySnapshotV1 *snapshot);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleySetObserverViewCountryV1Fn)(SmedleyCampaignSession session, const SmedleyObserverCountrySnapshotV1 *country, SmedleyObserverStateSnapshotV1 *after);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyReturnObserverCountryV1Fn)(SmedleyCampaignSession session, const SmedleyObserverCountrySnapshotV1 *country, SmedleyObserverStateSnapshotV1 *after);
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyEnableObserverFowV1Fn)(SmedleyCampaignSession session);

typedef struct SmedleyCampaignRuntimeApiV1 {
    uint32_t struct_size, version, reserved[2];
    SmedleyOpenCampaignSessionV1Fn open_session;
    SmedleyCloseCampaignSessionV1Fn close_session;
    SmedleyReadCampaignRuntimeV1Fn read_campaign;
    SmedleySetCampaignPausedRuntimeV1Fn set_paused;
    SmedleySetCampaignSpeedRuntimeV1Fn set_speed_index;
    SmedleyRequestCampaignQuitRuntimeV1Fn request_quit;
    SmedleyAcquireFrontendControllerV1Fn acquire_frontend;
    SmedleyReleaseFrontendControllerV1Fn release_frontend;
    SmedleyRequestFrontendSaveV1Fn request_save;
    SmedleyReadFrontendSaveV1Fn read_save;
    SmedleyDispatchFrontendControlV1Fn dispatch_frontend_control;
    SmedleyDispatchMainMenuSinglePlayerV1Fn dispatch_main_menu_single_player;
    SmedleyReadObserverStateV1Fn read_observer_state;
    SmedleyReadObserverCountryV1Fn read_observer_country;
    SmedleyFindObserverCountryV1Fn find_observer_country;
    SmedleySetObserverViewCountryV1Fn set_observer_view_country;
    SmedleyReturnObserverCountryV1Fn return_observer_country;
    SmedleyEnableObserverFowV1Fn enable_observer_fow;
} SmedleyCampaignRuntimeApiV1;
typedef SmedleyCampaignRuntimeResult (SMEDLEY_CAMPAIGN_RUNTIME_CALL *SmedleyGetCampaignRuntimeApiV1Fn)(SmedleyCampaignRuntimeApiV1 *api);
SMEDLEY_CAMPAIGN_RUNTIME_EXPORT SmedleyCampaignRuntimeResult SMEDLEY_CAMPAIGN_RUNTIME_CALL SmedleyGetCampaignRuntimeApiV1(SmedleyCampaignRuntimeApiV1 *api);

#ifdef __cplusplus
}
#endif
#endif
