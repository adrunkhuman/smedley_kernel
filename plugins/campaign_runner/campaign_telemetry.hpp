#pragma once

#include <smedley/telemetry.h>

#include <string_view>
#include <string>
#include <optional>

namespace campaign_runner
{
    bool IsSiblingTelemetryPath(const std::wstring &campaign_runner_path, const std::wstring &candidate_path);
    bool TelemetryDrainAllowsQuit(SmedleyTelemetryDrainResult result);

    class CampaignTelemetry
    {
    public:
        CampaignTelemetry() = default;
        explicit CampaignTelemetry(SmedleyTelemetryEmitV1Fn emit) : emit_(emit), reliable_emit_(emit) {}
        CampaignTelemetry(SmedleyTelemetryEmitV1Fn emit, SmedleyTelemetryEmitV1Fn reliable_emit,
                          SmedleyTelemetryDrainV1Fn drain = nullptr)
            : emit_(emit), reliable_emit_(reliable_emit), drain_(drain) {}

        SmedleyTelemetryResult SaveSelectionRequested();
        SmedleyTelemetryResult SaveLoadCompleted();
        SmedleyTelemetryResult Entered(bool observer_requested, int requested_speed, bool requested_paused);
        SmedleyTelemetryResult ObserverConfigured(std::string_view viewing_country);
        SmedleyTelemetryResult SpeedConfigured(int previous_speed, int current_speed, int requested_speed);
        SmedleyTelemetryResult PauseConfigured(bool previous_paused, bool current_paused, bool requested_paused);
        SmedleyTelemetryResult BenchmarkStarted(int start_date_raw, int target_date_raw, int requested_days, int timeout_seconds);
        SmedleyTelemetryResult BenchmarkResources(std::optional<int> game_date_raw,
                                                   std::optional<int64_t> process_cpu_us,
                                                   std::optional<int64_t> working_set_start_bytes,
                                                   std::optional<int64_t> working_set_end_bytes,
                                                   std::optional<int64_t> private_bytes_start,
                                                   std::optional<int64_t> private_bytes_end,
                                                   std::optional<int64_t> process_peak_working_set_bytes);
        SmedleyTelemetryResult BenchmarkCompleted(int start_date_raw, int target_date_raw, int actual_date_raw,
                                                  int game_days, int64_t elapsed_us);
        SmedleyTelemetryResult BenchmarkFailed(int start_date_raw, int target_date_raw, std::optional<int> actual_date_raw,
                                               int64_t elapsed_us, std::string_view reason, std::optional<bool> paused);
        SmedleyTelemetryDrainResult Drain(uint32_t timeout_ms);

    private:
        SmedleyTelemetryResult Emit(const char *event_type, const SmedleyTelemetryFieldV1 *entities, uint32_t entity_count,
                                     const SmedleyTelemetryFieldV1 *payload, uint32_t payload_count, bool *emitted,
                                     const char *quality = "verified-runtime", std::optional<int> game_date_raw = std::nullopt);
        SmedleyTelemetryEmitV1Fn Resolve();

        SmedleyTelemetryEmitV1Fn emit_ = nullptr;
        SmedleyTelemetryEmitV1Fn reliable_emit_ = nullptr;
        SmedleyTelemetryDrainV1Fn drain_ = nullptr;
        bool resolution_attempted_ = false;
        bool save_selection_emitted_ = false;
        bool save_load_emitted_ = false;
        bool entered_emitted_ = false;
        bool observer_emitted_ = false;
        bool speed_emitted_ = false;
        bool pause_emitted_ = false;
        bool benchmark_started_emitted_ = false;
        bool benchmark_resources_emitted_ = false;
        bool benchmark_terminal_emitted_ = false;
    };
}
