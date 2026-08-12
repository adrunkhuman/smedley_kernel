#include <smedley/campaign_control_api.h>
#include <smedley/game_state/runtime.hpp>

namespace
{
    SmedleyCampaignControlResult CampaignResult(smedley::game_state::CampaignOperationStatus status)
    {
        using smedley::game_state::CampaignOperationStatus;
        switch (status) {
        case CampaignOperationStatus::completed: return SMEDLEY_CAMPAIGN_CONTROL_SUCCESS;
        case CampaignOperationStatus::outside_campaign: return SMEDLEY_CAMPAIGN_CONTROL_OUTSIDE_CAMPAIGN;
        case CampaignOperationStatus::invalid_state: return SMEDLEY_CAMPAIGN_CONTROL_INVALID_STATE;
        case CampaignOperationStatus::signature_mismatch: return SMEDLEY_CAMPAIGN_CONTROL_SIGNATURE_MISMATCH;
        case CampaignOperationStatus::readback_failed: return SMEDLEY_CAMPAIGN_CONTROL_READBACK_FAILED;
        }
        return SMEDLEY_CAMPAIGN_CONTROL_INVALID_STATE;
    }

    SmedleyCampaignControlResult SMEDLEY_CAMPAIGN_CONTROL_CALL ReadCampaign(
        SmedleyCampaignSnapshotV1 *snapshot)
    {
        if (snapshot == nullptr || snapshot->struct_size != sizeof(SmedleyCampaignSnapshotV1)
            || snapshot->version != SMEDLEY_CAMPAIGN_SNAPSHOT_VERSION_V1) {
            return SMEDLEY_CAMPAIGN_CONTROL_INVALID_ARGUMENT;
        }
        for (const auto value : snapshot->reserved) {
            if (value != 0) return SMEDLEY_CAMPAIGN_CONTROL_INVALID_ARGUMENT;
        }
        smedley::game_state::CampaignRuntimeSnapshot value{};
        using smedley::game_state::CampaignRuntimeObservationStatus;
        switch (smedley::game_state::ReadCampaignRuntime(&value)) {
        case CampaignRuntimeObservationStatus::completed: break;
        case CampaignRuntimeObservationStatus::outside_campaign:
            return SMEDLEY_CAMPAIGN_CONTROL_OUTSIDE_CAMPAIGN;
        case CampaignRuntimeObservationStatus::invalid_state:
            return SMEDLEY_CAMPAIGN_CONTROL_INVALID_STATE;
        case CampaignRuntimeObservationStatus::signature_mismatch:
            return SMEDLEY_CAMPAIGN_CONTROL_SIGNATURE_MISMATCH;
        }
        snapshot->game_date_raw = value.date_raw;
        snapshot->speed_index = value.speed_index;
        snapshot->paused = value.paused ? 1 : 0;
        return SMEDLEY_CAMPAIGN_CONTROL_SUCCESS;
    }

    SmedleyCampaignControlResult SMEDLEY_CAMPAIGN_CONTROL_CALL SetPaused(uint32_t paused)
    {
        if (paused > 1) return SMEDLEY_CAMPAIGN_CONTROL_INVALID_ARGUMENT;
        return CampaignResult(smedley::game_state::SetCampaignPaused(paused != 0));
    }

    SmedleyCampaignControlResult SMEDLEY_CAMPAIGN_CONTROL_CALL SetSpeedIndex(int32_t speed_index)
    {
        if (speed_index < 0 || speed_index > 4) return SMEDLEY_CAMPAIGN_CONTROL_INVALID_ARGUMENT;
        return CampaignResult(smedley::game_state::SetCampaignSpeedIndex(speed_index));
    }

    SmedleyCampaignControlResult SMEDLEY_CAMPAIGN_CONTROL_CALL RequestQuit()
    {
        return CampaignResult(smedley::game_state::RequestCampaignQuit());
    }
}

SMEDLEY_CAMPAIGN_CONTROL_EXPORT SmedleyCampaignControlResult SMEDLEY_CAMPAIGN_CONTROL_CALL
SmedleyGetCampaignControlApiV1(SmedleyCampaignControlApiV1 *api)
{
    if (api == nullptr || api->struct_size != sizeof(SmedleyCampaignControlApiV1)
        || api->version != SMEDLEY_CAMPAIGN_CONTROL_API_VERSION_V1) {
        return SMEDLEY_CAMPAIGN_CONTROL_INVALID_ARGUMENT;
    }
    for (const auto value : api->reserved) {
        if (value != 0) return SMEDLEY_CAMPAIGN_CONTROL_INVALID_ARGUMENT;
    }
    api->read_campaign = &ReadCampaign;
    api->set_paused = &SetPaused;
    api->set_speed_index = &SetSpeedIndex;
    api->request_quit = &RequestQuit;
    return SMEDLEY_CAMPAIGN_CONTROL_SUCCESS;
}

static_assert(sizeof(SmedleyCampaignSnapshotV1) == 32, "campaign snapshot ABI v1 layout changed");
static_assert(sizeof(SmedleyCampaignControlApiV1) == 32, "campaign control API v1 layout changed");
