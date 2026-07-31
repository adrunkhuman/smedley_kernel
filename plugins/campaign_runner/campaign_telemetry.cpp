#include "campaign_telemetry.hpp"

#include <cstring>
#include <array>
#include <filesystem>

#include <windows.h>
#include <psapi.h>

namespace campaign_runner
{
    namespace
    {
        SmedleyTelemetryFieldV1 BoolField(const char *key, bool value)
        {
            SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key,
                static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_BOOL, 0, {}};
            field.value.bool_value = value ? 1u : 0u;
            return field;
        }

        SmedleyTelemetryFieldV1 IntField(const char *key, int64_t value)
        {
            SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key,
                static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_INT64, 0, {}};
            field.value.int64_value = value;
            return field;
        }

        SmedleyTelemetryFieldV1 StringField(const char *key, std::string_view value)
        {
            SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key,
                static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_UTF8_STRING, 0, {}};
            field.value.string_value = {value.data(), static_cast<uint32_t>(value.size()), 0};
            return field;
        }

        bool ModulePath(HMODULE module, std::filesystem::path *path)
        {
            std::array<wchar_t, 32768> buffer{};
            const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0 || length >= buffer.size()) return false;
            *path = std::filesystem::path(std::wstring(buffer.data(), length)).lexically_normal();
            return true;
        }

        bool SamePath(const std::filesystem::path &left, const std::filesystem::path &right)
        {
            const std::wstring left_value = left.native();
            const std::wstring right_value = right.native();
            return CompareStringOrdinal(left_value.data(), static_cast<int>(left_value.size()),
                                        right_value.data(), static_cast<int>(right_value.size()), TRUE) == CSTR_EQUAL;
        }
    }

    bool IsSiblingTelemetryPath(const std::wstring &campaign_runner_path, const std::wstring &candidate_path)
    {
        if (campaign_runner_path.empty() || candidate_path.empty()) return false;
        const auto expected = (std::filesystem::path(campaign_runner_path).lexically_normal().parent_path() / L"telemetry.dll").lexically_normal();
        return SamePath(expected, std::filesystem::path(candidate_path).lexically_normal());
    }

    SmedleyTelemetryEmitV1Fn CampaignTelemetry::Resolve()
    {
        if (emit_ != nullptr) return emit_;
        if (resolution_attempted_) return nullptr;
        resolution_attempted_ = true;
        // Deliberately do not load a module or derive a path from mutable launch metadata.
        HMODULE runner_module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&BoolField), &runner_module)) return nullptr;
        std::filesystem::path runner_path;
        if (!ModulePath(runner_module, &runner_path)) return nullptr;
        std::array<HMODULE, 1024> modules{};
        DWORD required = 0;
        if (!EnumProcessModules(GetCurrentProcess(), modules.data(), static_cast<DWORD>(sizeof(modules)), &required)
            || required > sizeof(modules)) return nullptr;
        const size_t module_count = required / sizeof(HMODULE);
        for (size_t index = 0; index < module_count; ++index) {
            const HMODULE module = modules[index];
            if (module == nullptr) continue;
            std::filesystem::path candidate_path;
            if (!ModulePath(module, &candidate_path)
                || !IsSiblingTelemetryPath(runner_path.native(), candidate_path.native())) continue;
            emit_ = reinterpret_cast<SmedleyTelemetryEmitV1Fn>(GetProcAddress(module, SMEDLEY_TELEMETRY_EMIT_V1_SYMBOL));
            return emit_;
        }
        return emit_;
    }

    SmedleyTelemetryResult CampaignTelemetry::Emit(const char *event_type, const SmedleyTelemetryFieldV1 *entities,
                                                     uint32_t entity_count, const SmedleyTelemetryFieldV1 *payload,
                                                     uint32_t payload_count, bool *emitted, const char *quality,
                                                     std::optional<int> game_date_raw)
    {
        if (*emitted) return SMEDLEY_TELEMETRY_FILTERED;
        SmedleyTelemetryEmitV1Fn emit = nullptr;
        try {
            emit = Resolve();
        } catch (...) {
            return SMEDLEY_TELEMETRY_UNAVAILABLE;
        }
        if (emit == nullptr) return SMEDLEY_TELEMETRY_UNAVAILABLE;
        SmedleyTelemetryRecordV1 record{sizeof(record), SMEDLEY_TELEMETRY_ABI_VERSION_V1,
            game_date_raw ? SMEDLEY_TELEMETRY_RECORD_HAS_GAME_DATE : 0, 0,
            event_type, static_cast<uint32_t>(std::strlen(event_type)), "lifecycle", 9,
            "v2game-3.04", 11, quality, static_cast<uint32_t>(std::strlen(quality)), game_date_raw.value_or(0), 0,
            entities, entity_count, payload, payload_count, {0, 0, 0, 0}};
        SmedleyTelemetryResult result = SMEDLEY_TELEMETRY_UNAVAILABLE;
        try {
            result = emit(&record);
        } catch (...) {
            return SMEDLEY_TELEMETRY_UNAVAILABLE;
        }
        if (result != SMEDLEY_TELEMETRY_UNAVAILABLE) *emitted = true;
        return result;
    }

    SmedleyTelemetryResult CampaignTelemetry::SaveSelectionRequested()
    {
        const auto source = StringField("source", "campaign_runner");
        return Emit("campaign.save_selection_requested", nullptr, 0, &source, 1, &save_selection_emitted_);
    }

    SmedleyTelemetryResult CampaignTelemetry::SaveLoadCompleted()
    {
        return Emit("campaign.save_load_completed", nullptr, 0, nullptr, 0, &save_load_emitted_);
    }

    SmedleyTelemetryResult CampaignTelemetry::Entered(bool observer_requested, int requested_speed, bool requested_paused)
    {
        const SmedleyTelemetryFieldV1 payload[] = {BoolField("observer_requested", observer_requested),
            IntField("requested_speed", requested_speed), BoolField("requested_paused", requested_paused)};
        return Emit("campaign.entered", nullptr, 0, payload, 3, &entered_emitted_);
    }

    SmedleyTelemetryResult CampaignTelemetry::ObserverConfigured(std::string_view viewing_country)
    {
        const auto entity = StringField("viewing_country", viewing_country);
        const SmedleyTelemetryFieldV1 payload[] = {BoolField("full_ai_control", true), BoolField("full_map_visibility", true)};
        return Emit("observer.configured", &entity, 1, payload, 2, &observer_emitted_);
    }

    SmedleyTelemetryResult CampaignTelemetry::SpeedConfigured(int previous_speed, int current_speed, int requested_speed)
    {
        const SmedleyTelemetryFieldV1 payload[] = {IntField("previous_speed", previous_speed),
            IntField("current_speed", current_speed), IntField("requested_speed", requested_speed)};
        return Emit("speed.configured", nullptr, 0, payload, 3, &speed_emitted_);
    }

    SmedleyTelemetryResult CampaignTelemetry::PauseConfigured(bool previous_paused, bool current_paused, bool requested_paused)
    {
        const SmedleyTelemetryFieldV1 payload[] = {BoolField("previous_paused", previous_paused),
            BoolField("current_paused", current_paused), BoolField("requested_paused", requested_paused)};
        return Emit("pause.configured", nullptr, 0, payload, 3, &pause_emitted_);
    }

    SmedleyTelemetryResult CampaignTelemetry::BenchmarkStarted(int start_date_raw, int target_date_raw, int requested_days,
                                                                 int timeout_seconds)
    {
        const SmedleyTelemetryFieldV1 payload[] = {IntField("start_date_raw", start_date_raw), IntField("target_date_raw", target_date_raw),
            IntField("requested_days", requested_days), IntField("timeout_seconds", timeout_seconds)};
        return Emit("benchmark.started", nullptr, 0, payload, 4, &benchmark_started_emitted_, "provisional", start_date_raw);
    }

    SmedleyTelemetryResult CampaignTelemetry::BenchmarkResources(std::optional<int> game_date_raw,
                                                                   std::optional<int64_t> process_cpu_us,
                                                                   std::optional<int64_t> working_set_start_bytes,
                                                                   std::optional<int64_t> working_set_end_bytes,
                                                                   std::optional<int64_t> private_bytes_start,
                                                                   std::optional<int64_t> private_bytes_end,
                                                                   std::optional<int64_t> process_peak_working_set_bytes)
    {
        SmedleyTelemetryFieldV1 payload[6]{};
        uint32_t count = 0;
        const auto append = [&](const char *key, std::optional<int64_t> value) {
            if (value) payload[count++] = IntField(key, *value);
        };
        append("process_cpu_us", process_cpu_us);
        append("working_set_start_bytes", working_set_start_bytes);
        append("working_set_end_bytes", working_set_end_bytes);
        append("private_bytes_start", private_bytes_start);
        append("private_bytes_end", private_bytes_end);
        append("process_peak_working_set_bytes", process_peak_working_set_bytes);
        if (count == 0) return SMEDLEY_TELEMETRY_UNAVAILABLE;
        return Emit("benchmark.resources", nullptr, 0, payload, count, &benchmark_resources_emitted_,
                    "verified-current", game_date_raw);
    }

    SmedleyTelemetryResult CampaignTelemetry::BenchmarkCompleted(int start_date_raw, int target_date_raw, int actual_date_raw,
                                                                   int game_days, int64_t elapsed_us)
    {
        const SmedleyTelemetryFieldV1 payload[] = {IntField("start_date_raw", start_date_raw), IntField("target_date_raw", target_date_raw),
            IntField("actual_date_raw", actual_date_raw), IntField("game_days", game_days), IntField("elapsed_us", elapsed_us),
            IntField("overshoot_raw", 0), BoolField("paused", true)};
        return Emit("benchmark.completed", nullptr, 0, payload, 7, &benchmark_terminal_emitted_, "provisional", actual_date_raw);
    }

    SmedleyTelemetryResult CampaignTelemetry::BenchmarkFailed(int start_date_raw, int target_date_raw,
                                                                std::optional<int> actual_date_raw, int64_t elapsed_us,
                                                                std::string_view reason, std::optional<bool> paused)
    {
        SmedleyTelemetryFieldV1 payload[6] = {IntField("start_date_raw", start_date_raw), IntField("target_date_raw", target_date_raw),
            IntField("elapsed_us", elapsed_us), StringField("reason", reason)};
        uint32_t count = 4;
        if (actual_date_raw) payload[count++] = IntField("actual_date_raw", *actual_date_raw);
        if (paused) payload[count++] = BoolField("paused", *paused);
        return Emit("benchmark.failed", nullptr, 0, payload, count, &benchmark_terminal_emitted_, "provisional", actual_date_raw);
    }
}
