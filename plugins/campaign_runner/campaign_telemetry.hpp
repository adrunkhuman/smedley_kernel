#pragma once

#include <smedley/telemetry.h>

#include <string_view>
#include <string>

namespace campaign_runner
{
    bool IsSiblingTelemetryPath(const std::wstring &campaign_runner_path, const std::wstring &candidate_path);

    class CampaignTelemetry
    {
    public:
        CampaignTelemetry() = default;
        explicit CampaignTelemetry(SmedleyTelemetryEmitV1Fn emit) : emit_(emit) {}

        SmedleyTelemetryResult SaveSelectionRequested();
        SmedleyTelemetryResult SaveLoadCompleted();
        SmedleyTelemetryResult Entered(bool observer_requested, int requested_speed, bool requested_paused);
        SmedleyTelemetryResult ObserverConfigured(std::string_view viewing_country);
        SmedleyTelemetryResult SpeedConfigured(int previous_speed, int current_speed, int requested_speed);
        SmedleyTelemetryResult PauseConfigured(bool previous_paused, bool current_paused, bool requested_paused);

    private:
        SmedleyTelemetryResult Emit(const char *event_type, const SmedleyTelemetryFieldV1 *entities, uint32_t entity_count,
                                    const SmedleyTelemetryFieldV1 *payload, uint32_t payload_count, bool *emitted);
        SmedleyTelemetryEmitV1Fn Resolve();

        SmedleyTelemetryEmitV1Fn emit_ = nullptr;
        bool resolution_attempted_ = false;
        bool save_selection_emitted_ = false;
        bool save_load_emitted_ = false;
        bool entered_emitted_ = false;
        bool observer_emitted_ = false;
        bool speed_emitted_ = false;
        bool pause_emitted_ = false;
    };
}
