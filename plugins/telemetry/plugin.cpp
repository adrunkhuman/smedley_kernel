#include "telemetry_core.hpp"

#include <smedley/events/dailyupdate.hpp>
#include <smedley/plugin.hpp>
#include <smedley/v2/country.hpp>
#include <smedley/v2/gamestate.hpp>

#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <memory>

namespace telemetry_plugin
{
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

        std::string CountryEntities(const char *tag)
        {
            return std::string("{\"country_tag\":\"") + smedley::telemetry::EscapeJson(tag ? tag : "") + "\"}";
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
            if (smedley::telemetry::HasCategory(config_, "lifecycle")
                && !writer_->WriteInitial(MakeEnvelope("session.started", "lifecycle", std::nullopt, "{}", "{\"plugin\":\"telemetry\"}"))) {
                throw std::runtime_error("could not queue telemetry session start");
            }
            AddEventHandler<smedley::events::DailyUpdateEvent>(
                "telemetry.daily", [this](smedley::events::DailyUpdateEvent &event) { OnDailyUpdate(event); });
            logger().Info("writing bounded JSON Lines telemetry to " + config_.output_path.string());
        }

        void OnUnload() override
        {
            RemoveEventHandler<smedley::events::DailyUpdateEvent>("telemetry.daily");
            if (!writer_) return;
            const bool lifecycle = smedley::telemetry::HasCategory(config_, "lifecycle");
            writer_->Stop([this, lifecycle](const smedley::telemetry::QueueStats &stats) {
                return lifecycle ? MakeEnvelope("telemetry.summary", "lifecycle", std::nullopt, "{}", StatsPayload(stats)) : std::string{};
            });
            writer_.reset();
        }

    private:
        void OnDailyUpdate(smedley::events::DailyUpdateEvent &event)
        {
            const uint64_t started = smedley::telemetry::MonotonicMicroseconds();
            auto finish = [&] {
                callback_overhead_us_.fetch_add(smedley::telemetry::MonotonicMicroseconds() - started, std::memory_order_relaxed);
                callback_count_.fetch_add(1, std::memory_order_relaxed);
            };
            if (!writer_) {
                finish();
                return;
            }
            if (writer_->stats().write_failed && !write_failure_logged_.exchange(true, std::memory_order_relaxed)) {
                logger().Failure("telemetry output failed; subsequent records are being dropped");
            }
            const bool lifecycle_enabled = smedley::telemetry::HasCategory(config_, "lifecycle");
            const bool state_enabled = smedley::telemetry::HasCategory(config_, "state");
            const auto *game_state = smedley::v2::CCurrentGameState::instance();
            const std::optional<int> raw_date = game_state ? std::optional<int>(game_state->current_date_raw()) : std::nullopt;
            if (!raw_date) {
                if (state_enabled) skipped_unsampleable_.fetch_add(1, std::memory_order_relaxed);
                finish();
                return;
            }
            const bool progress = lifecycle_enabled
                && (!last_progress_date_ || *raw_date != *last_progress_date_);
            if (state_enabled && smedley::telemetry::IsDateInRange(config_, raw_date)
                && smedley::telemetry::ShouldSampleDate(raw_date, config_.sample_days, &sampled_date_)) {
                const auto *country = event.GetCountry();
                if (country != nullptr && smedley::telemetry::HasCountryTag(config_, country->tag().str())) {
                    const int64_t treasury_raw = country->treasury_raw();
                    const std::string payload = "{\"treasury_raw\":" + std::to_string(treasury_raw)
                        + ",\"treasury\":" + std::to_string(static_cast<double>(treasury_raw) / 32768.0) + "}";
                    writer_->TryWrite(MakeEnvelope("country.daily", "state", raw_date, CountryEntities(country->tag().str()), payload));
                }
            }
            if (progress && writer_->TryWrite(MakeEnvelope("telemetry.progress", "lifecycle", raw_date, "{}", StatsPayload(writer_->stats())))) {
                last_progress_date_ = *raw_date;
            }
            finish();
        }

        std::string MakeEnvelope(const char *event_type, const char *category, std::optional<int> game_date_raw,
                                 const std::string &entities, const std::string &payload)
        {
            smedley::telemetry::Envelope envelope;
            envelope.run_id = config_.run_id;
            envelope.sequence = sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
            envelope.wall_time_utc = smedley::telemetry::UtcNow();
            envelope.monotonic_us = smedley::telemetry::MonotonicMicroseconds();
            envelope.game_date_raw = game_date_raw;
            envelope.event_type = event_type;
            envelope.category = category;
            envelope.mapping_id = "v2game-3.04";
            envelope.quality = game_date_raw || category == std::string("state") ? "provisional" : "verified-current";
            envelope.entities_json = entities;
            envelope.payload_json = payload;
            return smedley::telemetry::FormatEnvelope(envelope);
        }

        smedley::telemetry::Config config_;
        std::unique_ptr<smedley::telemetry::Writer> writer_;
        std::atomic<uint64_t> sequence_{0};
        std::atomic<uint64_t> callback_count_{0};
        std::atomic<uint64_t> callback_overhead_us_{0};
        std::atomic<uint64_t> skipped_unsampleable_{0};
        std::atomic<bool> write_failure_logged_{false};
        std::optional<int> sampled_date_;
        std::optional<int> last_progress_date_;

        std::string StatsPayload(const smedley::telemetry::QueueStats &stats) const
        {
            const uint64_t callbacks = callback_count_.load(std::memory_order_relaxed);
            const uint64_t overhead = callback_overhead_us_.load(std::memory_order_relaxed);
            return "{\"accepted\":" + std::to_string(stats.accepted)
                + ",\"written\":" + std::to_string(stats.written)
                + ",\"dropped\":" + std::to_string(stats.dropped)
                + ",\"high_water\":" + std::to_string(stats.high_water)
                + ",\"write_failed\":" + (stats.write_failed ? "true" : "false")
                + ",\"callback_enqueue_format_us_total\":" + std::to_string(overhead)
                + ",\"callback_enqueue_format_us_mean\":"
                    + std::to_string(callbacks == 0 ? 0.0 : static_cast<double>(overhead) / callbacks)
                + ",\"callback_count\":" + std::to_string(callbacks)
                + ",\"skipped_unsampleable\":" + std::to_string(skipped_unsampleable_.load(std::memory_order_relaxed)) + "}";
        }
    };
}

PLUGIN_API smedley::Plugin *CreatePlugin()
{
    return new telemetry_plugin::Plugin();
}
