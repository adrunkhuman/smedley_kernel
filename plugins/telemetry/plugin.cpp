#include "telemetry_core.hpp"

#include <smedley/events/dailyupdate.hpp>
#include <smedley/plugin.hpp>
#include <smedley/v2/country.hpp>
#include <smedley/v2/gamestate.hpp>

#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>

namespace telemetry_plugin
{
    class Plugin;
    std::shared_mutex active_sink_mutex;
    Plugin *active_sink = nullptr;

    namespace
    {
        std::vector<std::wstring> CommandLineArguments()
        {
            int argc = 0;
            wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            if (argv == nullptr) return {};
            std::vector<std::wstring> arguments(argv + 1, argv + argc);
            LocalFree(argv);
            return arguments;
        }

        SmedleyTelemetryFieldV1 IntField(const char *key, int64_t value)
        {
            SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key,
                static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_INT64, 0, {}};
            field.value.int64_value = value;
            return field;
        }

        SmedleyTelemetryFieldV1 DoubleField(const char *key, double value)
        {
            SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key,
                static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_DOUBLE, 0, {}};
            field.value.double_value = value;
            return field;
        }

        SmedleyTelemetryFieldV1 BoolField(const char *key, bool value)
        {
            SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key,
                static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_BOOL, 0, {}};
            field.value.bool_value = value ? 1u : 0u;
            return field;
        }

        SmedleyTelemetryFieldV1 StringField(const char *key, const char *value)
        {
            SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key,
                static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_UTF8_STRING, 0, {}};
            field.value.string_value = {value, static_cast<uint32_t>(std::strlen(value)), 0};
            return field;
        }

    }

    class Plugin final : public smedley::Plugin
    {
    public:
        void OnLoad() override
        {
            std::string error;
            const auto arguments = CommandLineArguments();
            const bool configured = std::any_of(arguments.begin(), arguments.end(), [](const std::wstring &argument) {
                return argument.rfind(L"-smedley-telemetry", 0) == 0;
            });
            if (!configured) return;
            if (!smedley::telemetry::ParseLaunchArguments(arguments, &config_, &error)) {
                logger().Failure("telemetry arguments are invalid: " + error);
                throw std::runtime_error(error);
            }
            writer_ = std::make_unique<smedley::telemetry::Writer>(config_);
            if (!writer_->Start(&error)) {
                logger().Failure("telemetry did not start: " + error);
                writer_.reset();
                throw std::runtime_error(error);
            }
            const auto plugin = StringField("plugin", "telemetry");
            const auto start_result = EmitTyped("session.started", "lifecycle", std::nullopt, nullptr, 0, &plugin, 1, true);
            if (start_result != SMEDLEY_TELEMETRY_ACCEPTED && start_result != SMEDLEY_TELEMETRY_FILTERED) {
                writer_->Stop();
                writer_.reset();
                throw std::runtime_error("could not queue telemetry session start");
            }
            bool handler_registered = false;
            try {
                logger().Info("writing bounded JSON Lines telemetry to " + config_.output_path.string());
                handler_registered = true;
                AddEventHandler<smedley::events::DailyUpdateEvent>(
                    "telemetry.daily", [this](smedley::events::DailyUpdateEvent &event) { OnDailyUpdate(event); });
                std::unique_lock<std::shared_mutex> lock(active_sink_mutex);
                active_sink = this;
            } catch (...) {
                if (handler_registered) RemoveEventHandler<smedley::events::DailyUpdateEvent>("telemetry.daily");
                writer_->Stop();
                writer_.reset();
                throw;
            }
        }

        void OnUnload() override
        {
            {
                std::unique_lock<std::shared_mutex> lock(active_sink_mutex);
                if (active_sink == this) active_sink = nullptr;
            }
            RemoveEventHandler<smedley::events::DailyUpdateEvent>("telemetry.daily");
            if (!writer_) return;
            const bool lifecycle = smedley::telemetry::HasCategory(config_, "lifecycle");
            writer_->Stop([this, lifecycle](const smedley::telemetry::QueueStats &stats) {
                return lifecycle ? MakeSummaryEnvelope(stats) : std::string{};
            });
            writer_.reset();
        }

        SmedleyTelemetryResult EmitExternal(const SmedleyTelemetryRecordV1 *record, bool reliable)
        {
            return EmitRecord(record, false, reliable);
        }

    private:
        SmedleyTelemetryResult EmitRecord(const SmedleyTelemetryRecordV1 *record, bool initial, bool reliable = false)
        {
            if (!writer_) return SMEDLEY_TELEMETRY_UNAVAILABLE;
            std::string error;
            if (!smedley::telemetry::ValidateRecordV1(record, &error)) return SMEDLEY_TELEMETRY_INVALID;
            if (!smedley::telemetry::HasCategory(config_, std::string_view(record->category, record->category_length))) {
                return SMEDLEY_TELEMETRY_FILTERED;
            }
            smedley::telemetry::PreparedRecordV1 prepared;
            if (!smedley::telemetry::PrepareRecordV1(record, config_.run_id, smedley::telemetry::UtcNow(),
                                                     smedley::telemetry::MonotonicMicroseconds(), &prepared, &error)) {
                return SMEDLEY_TELEMETRY_INVALID;
            }
            return smedley::telemetry::PublishPreparedRecord(
                prepared, &sequence_, &emission_mutex_, initial || reliable,
                [this, initial, reliable](std::string_view line) {
                    return initial ? writer_->WriteInitial(line)
                        : reliable ? writer_->WriteReliable(line) : writer_->TryWrite(line);
                },
                [this] { writer_->MarkDropped(); });
        }

        SmedleyTelemetryResult EmitTyped(const char *event_type, const char *category, std::optional<int> game_date_raw,
                                         const SmedleyTelemetryFieldV1 *entities, uint32_t entity_count,
                                         const SmedleyTelemetryFieldV1 *payload, uint32_t payload_count,
                                         bool initial, bool reliable = false)
        {
            const char *quality = game_date_raw ? "provisional" : "verified-current";
            SmedleyTelemetryRecordV1 record{sizeof(record), SMEDLEY_TELEMETRY_ABI_VERSION_V1,
                game_date_raw ? SMEDLEY_TELEMETRY_RECORD_HAS_GAME_DATE : 0, 0,
                event_type, static_cast<uint32_t>(std::strlen(event_type)), category, static_cast<uint32_t>(std::strlen(category)),
                "v2game-3.04", 11, quality, static_cast<uint32_t>(std::strlen(quality)), game_date_raw.value_or(0), 0,
                entities, entity_count, payload, payload_count, {0, 0, 0, 0}};
            return EmitRecord(&record, initial, reliable);
        }

        void OnDailyUpdate(smedley::events::DailyUpdateEvent &event)
        {
            const uint64_t started = smedley::telemetry::MonotonicMicroseconds();
            auto finish = [&] {
                callback_overhead_us_.fetch_add(smedley::telemetry::MonotonicMicroseconds() - started, std::memory_order_relaxed);
                callback_count_.fetch_add(1, std::memory_order_relaxed);
            };
            if (!writer_) { finish(); return; }
            if (writer_->stats().write_failed && !write_failure_logged_.exchange(true, std::memory_order_relaxed)) {
                logger().Failure("telemetry output failed; subsequent records are being dropped");
            }
            const bool lifecycle_enabled = smedley::telemetry::HasCategory(config_, "lifecycle");
            const bool state_enabled = smedley::telemetry::HasCategory(config_, "state");
            const auto *game_state = smedley::v2::CCurrentGameState::instance();
            const std::optional<int> raw_date = game_state ? std::optional<int>(game_state->current_date_raw()) : std::nullopt;
            if (!raw_date) { if (state_enabled) skipped_unsampleable_.fetch_add(1, std::memory_order_relaxed); finish(); return; }
            const std::optional<int> previous_date = last_observed_date_;
            int64_t delta = 0;
            const bool regressed = smedley::telemetry::ObserveDateRegression(*raw_date, &last_observed_date_, &delta);
            if (lifecycle_enabled && regressed) {
                const SmedleyTelemetryFieldV1 payload[] = {
                    IntField("previous_date_raw", *previous_date), IntField("current_date_raw", *raw_date), IntField("delta_raw", delta)};
                EmitTyped("date.regressed", "lifecycle", raw_date, nullptr, 0, payload, 3, false, true);
            }
            const bool progress = lifecycle_enabled && (!last_progress_date_ || *raw_date != *last_progress_date_);
            const bool sampled = state_enabled && smedley::telemetry::IsDateInRange(config_, raw_date)
                && smedley::telemetry::ShouldSampleDate(raw_date, config_.sample_days, &sampled_date_);
            if (sampled) {
                if (last_world_date_ != raw_date) {
                    last_world_date_ = raw_date;
                    const SmedleyTelemetryFieldV1 payload[] = {
                        IntField("country_slot_count", static_cast<int64_t>(game_state->country_count())),
                        IntField("ai_scheduler_entry_count", static_cast<int64_t>(game_state->country_ai_count())),
                        BoolField("human_control_present", game_state->has_human_controlled_country())};
                    EmitTyped("world.daily", "state", raw_date, nullptr, 0, payload, 3, false, true);
                }
                const auto *country = event.GetCountry();
                if (country != nullptr && smedley::telemetry::HasCountryTag(config_, country->tag().str())) {
                    const auto country_tag = StringField("country_tag", country->tag().str());
                    const int64_t treasury_raw = country->treasury_raw();
                    const SmedleyTelemetryFieldV1 payload[] = {
                        IntField("treasury_raw", treasury_raw), DoubleField("treasury", static_cast<double>(treasury_raw) / 32768.0)};
                    EmitTyped("country.daily", "state", raw_date, &country_tag, 1, payload, 2, false);
                }
            }
            smedley::telemetry::PreparedRecordV1 progress_record;
            if (progress && PrepareEnvelope("telemetry.progress", "lifecycle", raw_date, "{}", StatsPayload(writer_->stats()), &progress_record)
                && smedley::telemetry::PublishPreparedRecord(
                    progress_record, &sequence_, &emission_mutex_, true,
                    [this](std::string_view line) { return writer_->WriteReliable(line); },
                    [this] { writer_->MarkDropped(); }) == SMEDLEY_TELEMETRY_ACCEPTED) {
                last_progress_date_ = *raw_date;
            }
            finish();
        }

        bool PrepareEnvelope(const char *event_type, const char *category, std::optional<int> game_date_raw,
                             const std::string &entities, const std::string &payload,
                             smedley::telemetry::PreparedRecordV1 *prepared)
        {
            smedley::telemetry::Envelope envelope;
            envelope.run_id = config_.run_id;
            envelope.wall_time_utc = smedley::telemetry::UtcNow();
            envelope.monotonic_us = smedley::telemetry::MonotonicMicroseconds();
            envelope.game_date_raw = game_date_raw;
            envelope.event_type = event_type;
            envelope.category = category;
            envelope.mapping_id = "v2game-3.04";
            envelope.quality = game_date_raw || std::string_view(category) == "state" ? "provisional" : "verified-current";
            envelope.entities_json = entities;
            envelope.payload_json = payload;
            return smedley::telemetry::PrepareEnvelope(envelope, prepared);
        }

        std::string MakeSummaryEnvelope(const smedley::telemetry::QueueStats &stats)
        {
            smedley::telemetry::PreparedRecordV1 prepared;
            if (!PrepareEnvelope("telemetry.summary", "lifecycle", std::nullopt, "{}", StatsPayload(stats), &prepared)) return {};
            std::string line;
            const auto result = smedley::telemetry::PublishPreparedRecord(
                prepared, &sequence_, &emission_mutex_, true,
                [&line](std::string_view published) { line.assign(published); return true; },
                [this] { writer_->MarkDropped(); });
            return result == SMEDLEY_TELEMETRY_ACCEPTED ? line : std::string{};
        }

        smedley::telemetry::Config config_;
        std::unique_ptr<smedley::telemetry::Writer> writer_;
        std::atomic<uint64_t> sequence_{0};
        std::mutex emission_mutex_;
        std::atomic<uint64_t> callback_count_{0};
        std::atomic<uint64_t> callback_overhead_us_{0};
        std::atomic<uint64_t> skipped_unsampleable_{0};
        std::atomic<bool> write_failure_logged_{false};
        std::optional<int> sampled_date_;
        std::optional<int> last_world_date_;
        std::optional<int> last_progress_date_;
        std::optional<int> last_observed_date_;

        std::string StatsPayload(const smedley::telemetry::QueueStats &stats) const
        {
            const uint64_t callbacks = callback_count_.load(std::memory_order_relaxed);
            const uint64_t overhead = callback_overhead_us_.load(std::memory_order_relaxed);
            return "{\"accepted\":" + std::to_string(stats.accepted) + ",\"written\":" + std::to_string(stats.written)
                + ",\"dropped\":" + std::to_string(stats.dropped) + ",\"high_water\":" + std::to_string(stats.high_water)
                + ",\"write_failed\":" + (stats.write_failed ? "true" : "false")
                + ",\"callback_enqueue_format_us_total\":" + std::to_string(overhead)
                + ",\"callback_enqueue_format_us_mean\":" + std::to_string(callbacks == 0 ? 0.0 : static_cast<double>(overhead) / callbacks)
                + ",\"callback_count\":" + std::to_string(callbacks)
                + ",\"skipped_unsampleable\":" + std::to_string(skipped_unsampleable_.load(std::memory_order_relaxed)) + "}";
        }
    };
}

extern "C" SMEDLEY_TELEMETRY_EXPORT SmedleyTelemetryResult SMEDLEY_TELEMETRY_CALL
SmedleyTelemetryEmitV1(const SmedleyTelemetryRecordV1 *record)
{
    try {
        std::shared_lock<std::shared_mutex> lock(telemetry_plugin::active_sink_mutex, std::try_to_lock);
        if (!lock.owns_lock()) return SMEDLEY_TELEMETRY_DROPPED;
        return telemetry_plugin::active_sink == nullptr ? SMEDLEY_TELEMETRY_UNAVAILABLE
            : telemetry_plugin::active_sink->EmitExternal(record, false);
    } catch (...) {
        return SMEDLEY_TELEMETRY_DROPPED;
    }
}

extern "C" SMEDLEY_TELEMETRY_EXPORT SmedleyTelemetryResult SMEDLEY_TELEMETRY_CALL
SmedleyTelemetryEmitReliableV1(const SmedleyTelemetryRecordV1 *record)
{
    try {
        std::shared_lock<std::shared_mutex> lock(telemetry_plugin::active_sink_mutex);
        return telemetry_plugin::active_sink == nullptr ? SMEDLEY_TELEMETRY_UNAVAILABLE
            : telemetry_plugin::active_sink->EmitExternal(record, true);
    } catch (...) {
        return SMEDLEY_TELEMETRY_DROPPED;
    }
}

PLUGIN_API smedley::Plugin *CreatePlugin()
{
    return new telemetry_plugin::Plugin();
}
