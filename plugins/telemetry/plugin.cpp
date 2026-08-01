#include "telemetry_core.hpp"
#include "economic_capture.hpp"

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
    std::shared_timed_mutex active_sink_mutex;
    std::mutex drain_call_mutex;
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
                handler_registered_ = true;
                std::unique_lock<std::shared_timed_mutex> lock(active_sink_mutex);
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
                std::unique_lock<std::shared_timed_mutex> lock(active_sink_mutex);
                if (active_sink == this) active_sink = nullptr;
            }
            (void)DrainUntil((std::chrono::steady_clock::time_point::max)());
            writer_.reset();
        }

        SmedleyTelemetryResult EmitExternal(const SmedleyTelemetryRecordV1 *record, bool reliable)
        {
            return EmitRecord(record, false, reliable);
        }

        SmedleyTelemetryDrainResult DrainUntil(std::chrono::steady_clock::time_point deadline)
        {
            std::unique_lock<std::timed_mutex> lock(drain_mutex_, std::defer_lock);
            const bool state_ready = deadline == (std::chrono::steady_clock::time_point::max)()
                ? (lock.lock(), true) : lock.try_lock_until(deadline);
            if (!state_ready) return SMEDLEY_TELEMETRY_DRAIN_TIMEOUT;
            if (!writer_) return SMEDLEY_TELEMETRY_DRAIN_UNAVAILABLE;
            if (!drain_started_) {
                draining_.store(true, std::memory_order_release);
                std::unique_lock<std::shared_timed_mutex> producer_lock(producer_mutex_, std::defer_lock);
                const bool producer_ready = deadline == (std::chrono::steady_clock::time_point::max)()
                    ? (producer_lock.lock(), true) : producer_lock.try_lock_until(deadline);
                if (!producer_ready) return SMEDLEY_TELEMETRY_DRAIN_TIMEOUT;
                if (handler_registered_) {
                    RemoveEventHandler<smedley::events::DailyUpdateEvent>("telemetry.daily");
                    handler_registered_ = false;
                }
                drain_started_ = true;
                try {
                    drain_thread_ = std::thread([this] {
                        SmedleyTelemetryDrainResult result = SMEDLEY_TELEMETRY_DRAIN_FAILED;
                        try {
                            const bool lifecycle = smedley::telemetry::HasCategory(config_, "lifecycle");
                            const bool stopped = writer_->Stop([this, lifecycle](const smedley::telemetry::QueueStats &stats) {
                                return lifecycle ? MakeSummaryEnvelope(stats) : std::string{};
                            });
                            result = stopped ? SMEDLEY_TELEMETRY_DRAIN_COMPLETED : SMEDLEY_TELEMETRY_DRAIN_FAILED;
                        } catch (...) {
                            result = SMEDLEY_TELEMETRY_DRAIN_FAILED;
                        }
                        {
                            std::lock_guard<std::timed_mutex> result_lock(drain_mutex_);
                            drain_result_ = result;
                            drain_complete_ = true;
                        }
                        drain_complete_cv_.notify_all();
                    });
                } catch (...) {
                    drain_started_ = false;
                    return SMEDLEY_TELEMETRY_DRAIN_FAILED;
                }
            }
            bool complete = false;
            if (deadline == (std::chrono::steady_clock::time_point::max)()) {
                drain_complete_cv_.wait(lock, [this] { return drain_complete_; });
                complete = true;
            } else {
                complete = drain_complete_cv_.wait_until(lock, deadline, [this] { return drain_complete_; });
            }
            if (!complete) return SMEDLEY_TELEMETRY_DRAIN_TIMEOUT;
            const auto result = drain_result_;
            if (drain_thread_.joinable()) drain_thread_.join();
            return result;
        }

    private:
        SmedleyTelemetryResult EmitRecord(const SmedleyTelemetryRecordV1 *record, bool initial, bool reliable = false)
        {
            if (draining_.load(std::memory_order_acquire)) return SMEDLEY_TELEMETRY_UNAVAILABLE;
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
            std::shared_lock<std::shared_timed_mutex> producer_lock(producer_mutex_);
            const uint64_t started = smedley::telemetry::MonotonicMicroseconds();
            uint64_t collection_us = 0;
            auto finish = [&] {
                const uint64_t elapsed = smedley::telemetry::MonotonicMicroseconds() - started;
                callback_overhead_us_.fetch_add(elapsed > collection_us ? elapsed - collection_us : 0, std::memory_order_relaxed);
                callback_count_.fetch_add(1, std::memory_order_relaxed);
            };
            if (draining_.load(std::memory_order_acquire) || !writer_) { finish(); return; }
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
                    try {
                        const EconomicSnapshot snapshot = economic_capture_.Collect(game_state, *raw_date);
                        collection_us = snapshot.collection_us;
                        EmitEconomicSnapshot(snapshot);
                    } catch (const std::exception &error) {
                        logger().Failure(std::string("world economic telemetry failed: ") + error.what());
                    } catch (...) {
                        logger().Failure("world economic telemetry failed with an unknown exception");
                    }
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
        EconomicCapture economic_capture_;
        std::unique_ptr<smedley::telemetry::Writer> writer_;
        std::atomic<uint64_t> sequence_{0};
        std::mutex emission_mutex_;
        std::atomic<uint64_t> callback_count_{0};
        std::atomic<uint64_t> callback_overhead_us_{0};
        std::atomic<uint64_t> skipped_unsampleable_{0};
        std::atomic<bool> write_failure_logged_{false};
        std::atomic<bool> draining_{false};
        bool handler_registered_ = false;
        std::shared_timed_mutex producer_mutex_;
        std::timed_mutex drain_mutex_;
        std::condition_variable_any drain_complete_cv_;
        std::thread drain_thread_;
        bool drain_started_ = false;
        bool drain_complete_ = false;
        SmedleyTelemetryDrainResult drain_result_ = SMEDLEY_TELEMETRY_DRAIN_FAILED;
        std::optional<int> sampled_date_;
        std::optional<int> last_world_date_;
        std::optional<int> last_progress_date_;
        std::optional<int> last_observed_date_;

        void EmitEconomicSnapshot(const EconomicSnapshot &snapshot)
        {
            using namespace interest_bug_fix;
            const SmedleyTelemetryFieldV1 health[] = {
                BoolField("complete", snapshot.complete()),
                IntField("snapshot_flags", snapshot.snapshot_flags),
                IntField("collection_flags", snapshot.collection_flags),
                IntField("credit_flags", snapshot.credit_flags),
                IntField("country_count", snapshot.country_count),
                IntField("state_count", snapshot.state_count),
                IntField("province_count", snapshot.province_count),
                IntField("pop_count", snapshot.pop_count),
            };
            EmitTyped("world.economy.health", "state", snapshot.date_raw, nullptr, 0, health, 8, false, true);
            if (!snapshot.complete()) return;

            const SmedleyTelemetryFieldV1 capacity[] = {
                IntField("country_limit", max_world_countries),
                IntField("province_limit", max_sample_destination_provinces),
                IntField("pop_limit", max_sample_pops),
                IntField("country_utilization_bp", UtilizationBasisPoints(snapshot.country_count, max_world_countries)),
                IntField("province_utilization_bp", UtilizationBasisPoints(snapshot.province_count, max_sample_destination_provinces)),
                IntField("pop_utilization_bp", UtilizationBasisPoints(snapshot.pop_count, max_sample_pops)),
                IntField("collection_us", static_cast<int64_t>(snapshot.collection_us)),
            };
            EmitTyped("world.economy.capacity", "state", snapshot.date_raw, nullptr, 0, capacity, 7, false, true);

            const SmedleyTelemetryFieldV1 holdings[] = {
                IntField("treasury_observed_raw", snapshot.treasury_observed_raw),
                IntField("pop_money_observed_raw", snapshot.pop_money_observed_raw),
                IntField("pop_savings_observed_raw", snapshot.pop_savings_observed_raw),
                IntField("bank_interest_accumulator_raw", snapshot.bank_interest_accumulator_raw),
                IntField("positive_money_pops", snapshot.positive_money_pops),
                IntField("positive_savings_pops", snapshot.positive_savings_pops),
                IntField("negative_treasury_countries", snapshot.countries_with_negative_treasury),
            };
            EmitTyped("world.economy.holdings", "state", snapshot.date_raw, nullptr, 0, holdings, 7, false, true);
            if (snapshot.credit_flags != 0) return;

            const SmedleyTelemetryFieldV1 credit[] = {
                IntField("creditor_count", snapshot.creditor_count),
                IntField("creditors_was_paid", snapshot.creditors_was_paid),
                IntField("countries_with_creditors", snapshot.countries_with_creditors),
                IntField("creditor_interest_candidate_raw", snapshot.creditor_interest_candidate_raw),
                IntField("creditor_debt_candidate_raw", snapshot.creditor_debt_candidate_raw),
                IntField("state_savings_candidate_raw", snapshot.state_savings_candidate_raw),
                IntField("state_interest_candidate_raw", snapshot.state_interest_candidate_raw),
            };
            EmitTyped("world.economy.credit", "state", snapshot.date_raw, nullptr, 0, credit, 7, false, true);
        }

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
        std::shared_lock<std::shared_timed_mutex> lock(telemetry_plugin::active_sink_mutex, std::try_to_lock);
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
        std::shared_lock<std::shared_timed_mutex> lock(telemetry_plugin::active_sink_mutex);
        return telemetry_plugin::active_sink == nullptr ? SMEDLEY_TELEMETRY_UNAVAILABLE
            : telemetry_plugin::active_sink->EmitExternal(record, true);
    } catch (...) {
        return SMEDLEY_TELEMETRY_DROPPED;
    }
}

extern "C" SMEDLEY_TELEMETRY_EXPORT SmedleyTelemetryDrainResult SMEDLEY_TELEMETRY_CALL
SmedleyTelemetryDrainV1(uint32_t timeout_ms)
{
    try {
        std::unique_lock<std::mutex> call_lock(telemetry_plugin::drain_call_mutex, std::try_to_lock);
        if (!call_lock.owns_lock()) return SMEDLEY_TELEMETRY_DRAIN_BUSY;
        const auto deadline = timeout_ms == UINT32_MAX ? (std::chrono::steady_clock::time_point::max)()
            : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        std::unique_lock<std::shared_timed_mutex> sink_lock(telemetry_plugin::active_sink_mutex, std::defer_lock);
        const bool sink_ready = deadline == (std::chrono::steady_clock::time_point::max)()
            ? (sink_lock.lock(), true) : sink_lock.try_lock_until(deadline);
        if (!sink_ready) return SMEDLEY_TELEMETRY_DRAIN_TIMEOUT;
        return telemetry_plugin::active_sink == nullptr ? SMEDLEY_TELEMETRY_DRAIN_UNAVAILABLE
            : telemetry_plugin::active_sink->DrainUntil(deadline);
    } catch (...) {
        return SMEDLEY_TELEMETRY_DRAIN_FAILED;
    }
}

PLUGIN_API smedley::Plugin *CreatePlugin()
{
    return new telemetry_plugin::Plugin();
}
