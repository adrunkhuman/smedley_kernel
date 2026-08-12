#ifndef SMEDLEY_CAMPAIGN_CONTROL_API_H
#define SMEDLEY_CAMPAIGN_CONTROL_API_H

#include <stdint.h>

#ifdef _WIN32
#define SMEDLEY_CAMPAIGN_CONTROL_CALL __cdecl
#ifdef SMEDLEY_CAMPAIGN_CONTROL_BUILD
#define SMEDLEY_CAMPAIGN_CONTROL_EXPORT __declspec(dllexport)
#else
#define SMEDLEY_CAMPAIGN_CONTROL_EXPORT
#endif
#else
#define SMEDLEY_CAMPAIGN_CONTROL_CALL
#define SMEDLEY_CAMPAIGN_CONTROL_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SMEDLEY_CAMPAIGN_CONTROL_API_VERSION_V1 UINT32_C(1)
#define SMEDLEY_CAMPAIGN_SNAPSHOT_VERSION_V1 UINT32_C(1)
#define SMEDLEY_CAMPAIGN_CONTROL_GET_API_V1_SYMBOL "SmedleyGetCampaignControlApiV1"

typedef uint32_t SmedleyCampaignControlResult;
enum {
    SMEDLEY_CAMPAIGN_CONTROL_SUCCESS = 0,
    SMEDLEY_CAMPAIGN_CONTROL_INVALID_ARGUMENT = 1,
    SMEDLEY_CAMPAIGN_CONTROL_OUTSIDE_CAMPAIGN = 2,
    SMEDLEY_CAMPAIGN_CONTROL_INVALID_STATE = 3,
    SMEDLEY_CAMPAIGN_CONTROL_SIGNATURE_MISMATCH = 4,
    SMEDLEY_CAMPAIGN_CONTROL_READBACK_FAILED = 5
};

typedef struct SmedleyCampaignSnapshotV1 {
    uint32_t struct_size;
    uint32_t version;
    int32_t game_date_raw;
    int32_t speed_index;
    uint32_t paused;
    uint32_t reserved[3];
} SmedleyCampaignSnapshotV1;

typedef SmedleyCampaignControlResult (SMEDLEY_CAMPAIGN_CONTROL_CALL *SmedleyReadCampaignV1Fn)(
    SmedleyCampaignSnapshotV1 *snapshot);
typedef SmedleyCampaignControlResult (SMEDLEY_CAMPAIGN_CONTROL_CALL *SmedleySetCampaignPausedV1Fn)(
    uint32_t paused);
typedef SmedleyCampaignControlResult (SMEDLEY_CAMPAIGN_CONTROL_CALL *SmedleySetCampaignSpeedV1Fn)(
    int32_t speed_index);
typedef SmedleyCampaignControlResult (SMEDLEY_CAMPAIGN_CONTROL_CALL *SmedleyRequestCampaignQuitV1Fn)(void);

typedef struct SmedleyCampaignControlApiV1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t reserved[2];
    SmedleyReadCampaignV1Fn read_campaign;
    SmedleySetCampaignPausedV1Fn set_paused;
    SmedleySetCampaignSpeedV1Fn set_speed_index;
    SmedleyRequestCampaignQuitV1Fn request_quit;
} SmedleyCampaignControlApiV1;

typedef SmedleyCampaignControlResult (SMEDLEY_CAMPAIGN_CONTROL_CALL *SmedleyGetCampaignControlApiV1Fn)(
    SmedleyCampaignControlApiV1 *api);

SMEDLEY_CAMPAIGN_CONTROL_EXPORT SmedleyCampaignControlResult SMEDLEY_CAMPAIGN_CONTROL_CALL
SmedleyGetCampaignControlApiV1(SmedleyCampaignControlApiV1 *api);

#ifdef __cplusplus
}
#endif

#endif
