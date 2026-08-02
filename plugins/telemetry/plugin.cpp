#include "telemetry_core.hpp"
#include "economic_capture.hpp"

#include <smedley/events/dailyupdate.hpp>
#include <smedley/plugin.hpp>
#include <smedley/v2/country.hpp>
#include <smedley/v2/gamestate.hpp>
#include <smedley/v2/province.hpp>

#include <shellapi.h>

#include <algorithm>
#include <array>
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

        SmedleyTelemetryFieldV1 StringField(const char *key, std::string_view value)
        {
            SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key,
                static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_UTF8_STRING, 0, {}};
            field.value.string_value = {value.data(), static_cast<uint32_t>(value.size()), 0};
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
                EmitFamilySummaries();
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
        struct FamilyStats
        {
            uint64_t polls_due = 0;
            uint64_t collection_attempts = 0;
            uint64_t accepted = 0;
            uint64_t filtered = 0;
            uint64_t dropped = 0;
            uint64_t invalid = 0;
            uint64_t collection_us = 0;
        };

        struct PopAggregate
        {
            int32_t province_id = -1;
            int32_t pop_type_id = -1;
            int64_t pop_count = 0;
            int64_t size = 0;
            int64_t employed = 0;
            int64_t money_raw = 0;
            int64_t savings_raw = 0;
        };

        void AccountResult(size_t rule_index, SmedleyTelemetryResult result)
        {
            auto &stats = family_stats_[rule_index];
            if (result == SMEDLEY_TELEMETRY_ACCEPTED) ++stats.accepted;
            else if (result == SMEDLEY_TELEMETRY_FILTERED) ++stats.filtered;
            else if (result == SMEDLEY_TELEMETRY_INVALID) ++stats.invalid;
            else ++stats.dropped;
        }

        void AccountPoll(size_t rule_index, int32_t date_raw)
        {
            if (last_family_poll_dates_[rule_index] == date_raw) return;
            last_family_poll_dates_[rule_index] = date_raw;
            ++family_stats_[rule_index].polls_due;
        }

        void EmitFamilySummaries()
        {
            if (!smedley::telemetry::HasCategory(config_, "lifecycle")) return;
            for (size_t index = 0; index < config_.capture_rules.size(); ++index) {
                const auto family = StringField("family", config_.capture_rules[index].family.c_str());
                const auto &stats = family_stats_[index];
                const SmedleyTelemetryFieldV1 payload[] = {
                    IntField("polls_due", static_cast<int64_t>(stats.polls_due)),
                    IntField("collection_attempts", static_cast<int64_t>(stats.collection_attempts)),
                    IntField("accepted", static_cast<int64_t>(stats.accepted)),
                    IntField("filtered", static_cast<int64_t>(stats.filtered)),
                    IntField("dropped", static_cast<int64_t>(stats.dropped)),
                    IntField("invalid", static_cast<int64_t>(stats.invalid)),
                    IntField("collection_us", static_cast<int64_t>(stats.collection_us)),
                };
                (void)EmitTyped("telemetry.family.summary", "lifecycle", std::nullopt,
                                &family, 1, payload, 7, true, true);
            }
        }

        SmedleyTelemetryResult EmitRecord(const SmedleyTelemetryRecordV1 *record, bool initial, bool reliable = false)
        {
            if (draining_.load(std::memory_order_acquire) && !initial) return SMEDLEY_TELEMETRY_UNAVAILABLE;
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

        static bool HasField(const smedley::telemetry::CaptureRule &rule, std::string_view field)
        {
            return rule.fields.empty() || std::find(rule.fields.begin(), rule.fields.end(), field) != rule.fields.end();
        }

        static bool HasCountryTag(const smedley::telemetry::CaptureRule &rule, std::string_view tag)
        {
            return rule.country_tags.empty()
                || std::find(rule.country_tags.begin(), rule.country_tags.end(), tag) != rule.country_tags.end();
        }

        static std::optional<std::string_view> NormalizedCountryTag(const smedley::v2::CCountry *country)
        {
            if (country == nullptr || !country->tag().normalized_candidate()) return std::nullopt;
            return std::string_view(country->tag().str(), 3);
        }

        static bool HasProvinceId(const smedley::telemetry::CaptureRule &rule, int id)
        {
            return rule.province_ids.empty()
                || std::find(rule.province_ids.begin(), rule.province_ids.end(), id) != rule.province_ids.end();
        }

        const smedley::telemetry::CaptureRule *FindRule(std::string_view family, size_t *index = nullptr) const
        {
            for (size_t current = 0; current < config_.capture_rules.size(); ++current) {
                if (config_.capture_rules[current].family == family) {
                    if (index != nullptr) *index = current;
                    return &config_.capture_rules[current];
                }
            }
            return nullptr;
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
            const bool state_enabled = smedley::telemetry::HasCategory(config_, "state")
                && !config_.capture_rules.empty();
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
            if (state_enabled && last_global_date_ != raw_date) {
                last_global_date_ = raw_date;
                size_t rule_index = 0;
                if (const auto *rule = FindRule("world.daily", &rule_index);
                    rule != nullptr && smedley::telemetry::ShouldCaptureDate(*raw_date, *rule, &schedule_states_[rule_index])) {
                    ++family_stats_[rule_index].polls_due;
                    ++family_stats_[rule_index].collection_attempts;
                    std::array<SmedleyTelemetryFieldV1, 3> payload;
                    uint32_t count = 0;
                    if (HasField(*rule, "country_slot_count")) payload[count++] = IntField("country_slot_count", static_cast<int64_t>(game_state->country_count()));
                    if (HasField(*rule, "ai_scheduler_entry_count")) payload[count++] = IntField("ai_scheduler_entry_count", static_cast<int64_t>(game_state->country_ai_count()));
                    if (HasField(*rule, "human_control_present")) payload[count++] = BoolField("human_control_present", game_state->has_human_controlled_country());
                    AccountResult(rule_index,
                        EmitTyped("world.daily", "state", raw_date, nullptr, 0, payload.data(), count, false, true));
                }
                if (const auto *rule = FindRule("world.economy", &rule_index);
                    rule != nullptr && smedley::telemetry::ShouldCaptureDate(*raw_date, *rule, &schedule_states_[rule_index])) {
                    ++family_stats_[rule_index].polls_due;
                    ++family_stats_[rule_index].collection_attempts;
                    try {
                        const EconomicSnapshot snapshot = economic_capture_.Collect(game_state, *raw_date);
                        collection_us += snapshot.collection_us;
                        family_stats_[rule_index].collection_us += snapshot.collection_us;
                        EmitEconomicSnapshot(snapshot, *rule, rule_index);
                    } catch (const std::exception &error) {
                        logger().Failure(std::string("world economic telemetry failed: ") + error.what());
                    } catch (...) {
                        logger().Failure("world economic telemetry failed with an unknown exception");
                    }
                }
                if (const auto *rule = FindRule("world.military", &rule_index);
                    rule != nullptr && smedley::telemetry::ShouldCaptureDate(*raw_date, *rule, &schedule_states_[rule_index])) {
                    ++family_stats_[rule_index].polls_due;
                    ++family_stats_[rule_index].collection_attempts;
                    int count = 0;
                    if (!game_state->ongoing_war_count_candidate(&count)) ++family_stats_[rule_index].invalid;
                    else {
                        const auto field = IntField("ongoing_war_count_candidate", count);
                        AccountResult(rule_index, EmitTyped("world.military", "state", raw_date,
                            nullptr, 0, &field, 1, false, true));
                    }
                }
                if (const auto *rule = FindRule("world.market", &rule_index);
                    rule != nullptr && smedley::telemetry::ShouldCaptureDate(*raw_date, *rule, &schedule_states_[rule_index])) {
                    ++family_stats_[rule_index].polls_due;
                    uint32_t market_count = 0;
                    if (!interest_bug_fix::CollectWorldMarket(game_state, world_market_snapshots_.data(),
                            world_market_snapshots_.size(), &market_count)) {
                        ++family_stats_[rule_index].invalid;
                    } else {
                        for (uint32_t index = 0; index < market_count; ++index) {
                            const auto &snapshot = world_market_snapshots_[index];
                            const auto entity = IntField("good_ordinal", snapshot.good_ordinal);
                            if (HasField(*rule, "price")) {
                                const SmedleyTelemetryFieldV1 payload[] = {
                                    IntField("price_raw", snapshot.price_raw),
                                    IntField("last_price_raw", snapshot.last_price_raw),
                                };
                                ++family_stats_[rule_index].collection_attempts;
                                AccountResult(rule_index, EmitTyped("world.market.price", "state", raw_date,
                                    &entity, 1, payload, 2, false, true));
                            }
                            if (HasField(*rule, "supply")) {
                                const SmedleyTelemetryFieldV1 payload[] = {
                                    IntField("supply_raw", snapshot.supply_raw),
                                    IntField("last_supply_raw", snapshot.last_supply_raw),
                                    IntField("worldmarket_stock_raw", snapshot.worldmarket_stock_raw),
                                };
                                ++family_stats_[rule_index].collection_attempts;
                                AccountResult(rule_index, EmitTyped("world.market.supply", "state", raw_date,
                                    &entity, 1, payload, 3, false, true));
                            }
                            if (HasField(*rule, "demand")) {
                                const SmedleyTelemetryFieldV1 payload[] = {
                                    IntField("demand_raw", snapshot.demand_raw),
                                    IntField("real_demand_raw", snapshot.real_demand_raw),
                                };
                                ++family_stats_[rule_index].collection_attempts;
                                AccountResult(rule_index, EmitTyped("world.market.demand", "state", raw_date,
                                    &entity, 1, payload, 2, false, true));
                            }
                            if (HasField(*rule, "sales")) {
                                const SmedleyTelemetryFieldV1 payload[] = {
                                    IntField("actual_sold_raw", snapshot.actual_sold_raw),
                                    IntField("actual_sold_world_raw", snapshot.actual_sold_world_raw),
                                };
                                ++family_stats_[rule_index].collection_attempts;
                                AccountResult(rule_index, EmitTyped("world.market.sales", "state", raw_date,
                                    &entity, 1, payload, 2, false, true));
                            }
                        }
                    }
                }
                if (const auto *rule = FindRule("province.daily", &rule_index);
                    rule != nullptr && smedley::telemetry::ShouldCaptureDate(*raw_date, *rule, &schedule_states_[rule_index])) {
                    ++family_stats_[rule_index].polls_due;
                    for (size_t id = 0; id < game_state->province_count(); ++id) {
                        if (!HasProvinceId(*rule, static_cast<int>(id))) continue;
                        const auto *province = game_state->province(static_cast<int>(id));
                        if (province == nullptr || province->id_candidate() != static_cast<int>(id)) {
                            ++family_stats_[rule_index].invalid;
                            continue;
                        }
                        ++family_stats_[rule_index].collection_attempts;
                        if ((HasField(*rule, "owner_tag_candidate") && !province->owner_candidate().normalized_candidate())
                            || (HasField(*rule, "controller_tag_candidate")
                                && !province->controller_candidate().normalized_candidate())) {
                            ++family_stats_[rule_index].invalid;
                            continue;
                        }
                        const auto province_id = IntField("province_id", static_cast<int64_t>(id));
                        std::array<SmedleyTelemetryFieldV1, 5> payload;
                        uint32_t count = 0;
                        if (HasField(*rule, "owner_tag_candidate")) {
                            payload[count++] = StringField("owner_tag_candidate", province->owner_candidate().str());
                        }
                        if (HasField(*rule, "controller_tag_candidate")) {
                            payload[count++] = StringField("controller_tag_candidate", province->controller_candidate().str());
                        }
                        if (HasField(*rule, "colonial_level_candidate")) {
                            payload[count++] = IntField("colonial_level_candidate", province->colonial_level_candidate());
                        }
                        if (HasField(*rule, "life_rating_candidate")) {
                            payload[count++] = IntField("life_rating_candidate", province->life_rating_candidate());
                        }
                        if (HasField(*rule, "infrastructure_candidate_raw")) {
                            payload[count++] = IntField("infrastructure_candidate_raw", province->infrastructure_candidate());
                        }
                        const bool reliable = !rule->province_ids.empty() && rule->province_ids.size() <= 16;
                        AccountResult(rule_index, EmitTyped("province.daily", "state", raw_date,
                            &province_id, 1, payload.data(), count, false, reliable));
                    }
                }
                if (const auto *rule = FindRule("province.production", &rule_index);
                    rule != nullptr && smedley::telemetry::ShouldCaptureDate(*raw_date, *rule, &schedule_states_[rule_index])) {
                    ++family_stats_[rule_index].polls_due;
                    for (size_t id = 0; id < game_state->province_count(); ++id) {
                        if (!HasProvinceId(*rule, static_cast<int>(id))) continue;
                        const auto *province = game_state->province(static_cast<int>(id));
                        if (province == nullptr || province->id_candidate() != static_cast<int>(id)) {
                            ++family_stats_[rule_index].invalid;
                            continue;
                        }
                        ++family_stats_[rule_index].collection_attempts;
                        size_t building_count = 0;
                        int construction_count = 0;
                        if ((HasField(*rule, "building_slot_count_candidate")
                                && !province->building_slot_count_candidate(&building_count))
                            || (HasField(*rule, "construction_count_candidate")
                                && !province->construction_count_candidate(&construction_count))) {
                            ++family_stats_[rule_index].invalid;
                            continue;
                        }
                        const auto province_id = IntField("province_id", static_cast<int64_t>(id));
                        std::array<SmedleyTelemetryFieldV1, 2> payload;
                        uint32_t count = 0;
                        if (HasField(*rule, "building_slot_count_candidate")) {
                            payload[count++] = IntField("building_slot_count_candidate", building_count);
                        }
                        if (HasField(*rule, "construction_count_candidate")) {
                            payload[count++] = IntField("construction_count_candidate", construction_count);
                        }
                        const bool reliable = !rule->province_ids.empty() && rule->province_ids.size() <= 16;
                        AccountResult(rule_index, EmitTyped("province.production", "state", raw_date,
                            &province_id, 1, payload.data(), count, false, reliable));
                    }
                }
                if (const auto *rule = FindRule("province.rgo", &rule_index);
                    rule != nullptr && smedley::telemetry::ShouldCaptureDate(*raw_date, *rule, &schedule_states_[rule_index])) {
                    ++family_stats_[rule_index].polls_due;
                    size_t province_count = 0;
                    const bool province_vector_valid = game_state->province_count_candidate(&province_count);
                    if (!province_vector_valid) ++family_stats_[rule_index].invalid;
                    const void *registry = province_vector_valid
                        ? interest_bug_fix::ResolveStateEmploymentRegistry() : nullptr;
                    uint32_t groups = 0;
                    if (HasField(*rule, "identity")) groups |= interest_bug_fix::RGO_IDENTITY;
                    if (HasField(*rule, "employment")) groups |= interest_bug_fix::RGO_EMPLOYMENT;
                    if (HasField(*rule, "production")) groups |= interest_bug_fix::RGO_PRODUCTION;
                    if (HasField(*rule, "finance")) groups |= interest_bug_fix::RGO_FINANCE;
                    for (size_t id = 0; id < province_count; ++id) {
                        if (!HasProvinceId(*rule, static_cast<int>(id))) continue;
                        ++family_stats_[rule_index].collection_attempts;
                        interest_bug_fix::RgoSnapshot snapshot{};
                        if (!interest_bug_fix::ReadProvinceRgo(registry, game_state->province(static_cast<int>(id)),
                                static_cast<int32_t>(id), province_count, groups, &snapshot)) {
                            ++family_stats_[rule_index].invalid;
                            continue;
                        }
                        const bool reliable = !rule->province_ids.empty() && rule->province_ids.size() <= 16;
                        const auto province_id = IntField("province_id", snapshot.province_id);
                        if (HasField(*rule, "identity")) {
                            const SmedleyTelemetryFieldV1 payload[] = {
                                StringField("production_type", snapshot.production_type),
                                IntField("output_good_ordinal", snapshot.output_good_ordinal),
                                StringField("output_good", snapshot.output_good),
                            };
                            AccountResult(rule_index, EmitTyped("province.rgo.identity", "state", raw_date,
                                &province_id, 1, payload, 3, false, reliable));
                        }
                        if (HasField(*rule, "employment")) {
                            const SmedleyTelemetryFieldV1 payload[] = {
                                IntField("employment_capacity", snapshot.employment_capacity),
                                IntField("employed", snapshot.employed),
                            };
                            AccountResult(rule_index, EmitTyped("province.rgo.employment", "state", raw_date,
                                &province_id, 1, payload, 2, false, reliable));
                        }
                        if (HasField(*rule, "production")) {
                            const SmedleyTelemetryFieldV1 payload[] = {
                                IntField("base_output_per_size_raw", snapshot.base_output_per_size_raw),
                                IntField("base_size_raw_candidate", snapshot.base_size_raw_candidate),
                                IntField("output_efficiency_raw", snapshot.output_efficiency_raw),
                                IntField("throughput_raw", snapshot.throughput_raw),
                            };
                            AccountResult(rule_index, EmitTyped("province.rgo.production", "state", raw_date,
                                &province_id, 1, payload, 4, false, reliable));
                        }
                        if (HasField(*rule, "finance")) {
                            const auto income = IntField("income_raw", snapshot.income_raw);
                            AccountResult(rule_index, EmitTyped("province.rgo.finance", "state", raw_date,
                                &province_id, 1, &income, 1, false, reliable));
                        }
                    }
                }
                constexpr std::array<std::string_view, 3> pop_families = {
                    "pop.economy", "pop.demographics", "pop.aggregate"};
                economic_capture_.InvalidatePopulationCache();
                for (const auto family : pop_families) {
                    if (const auto *rule = FindRule(family, &rule_index);
                        rule != nullptr && smedley::telemetry::ShouldCaptureDate(*raw_date, *rule, &schedule_states_[rule_index])) {
                        const uint64_t pop_collection_us = family == "pop.aggregate"
                            ? EmitPopulationAggregate(game_state, *raw_date, *rule, rule_index)
                            : EmitPopulationSnapshot(game_state, *raw_date, *rule, rule_index);
                        collection_us += pop_collection_us;
                    }
                }
            }
            size_t country_rule_index = 0;
            if (const auto *rule = FindRule("country.daily", &country_rule_index);
                rule != nullptr && smedley::telemetry::ShouldCaptureDate(*raw_date, *rule, &schedule_states_[country_rule_index])) {
                AccountPoll(country_rule_index, *raw_date);
                const auto *country = event.GetCountry();
                const auto country_tag_value = NormalizedCountryTag(country);
                if (country != nullptr && !country_tag_value) ++family_stats_[country_rule_index].invalid;
                else if (country_tag_value && HasCountryTag(*rule, *country_tag_value)) {
                    ++family_stats_[country_rule_index].collection_attempts;
                    const auto country_tag = StringField("country_tag", *country_tag_value);
                    const int64_t treasury_raw = country->treasury_raw();
                    std::array<SmedleyTelemetryFieldV1, 2> payload;
                    uint32_t count = 0;
                    if (HasField(*rule, "treasury_raw")) payload[count++] = IntField("treasury_raw", treasury_raw);
                    if (HasField(*rule, "treasury")) payload[count++] = DoubleField("treasury", static_cast<double>(treasury_raw) / 32768.0);
                    const bool reliable = !rule->country_tags.empty() && rule->country_tags.size() <= 16;
                    AccountResult(country_rule_index,
                        EmitTyped("country.daily", "state", raw_date, &country_tag, 1, payload.data(), count, false, reliable));
                }
            }
            if (const auto *rule = FindRule("state.factory", &country_rule_index);
                rule != nullptr && smedley::telemetry::ShouldCaptureDate(*raw_date, *rule, &schedule_states_[country_rule_index])) {
                AccountPoll(country_rule_index, *raw_date);
                const auto *country = event.GetCountry();
                const auto country_tag_value = NormalizedCountryTag(country);
                if (country != nullptr && !country_tag_value) ++family_stats_[country_rule_index].invalid;
                else if (country_tag_value && HasCountryTag(*rule, *country_tag_value)) {
                    uint32_t factory_count = 0;
                    uint32_t input_count = 0;
                    uint32_t flags = 0;
                    uint32_t groups = 0;
                    if (HasField(*rule, "identity")) groups |= interest_bug_fix::FACTORY_IDENTITY;
                    if (HasField(*rule, "employment")) groups |= interest_bug_fix::FACTORY_EMPLOYMENT;
                    if (HasField(*rule, "production")) groups |= interest_bug_fix::FACTORY_PRODUCTION;
                    if (HasField(*rule, "finance")) groups |= interest_bug_fix::FACTORY_FINANCE;
                    if (HasField(*rule, "inputs")) groups |= interest_bug_fix::FACTORY_INPUTS;
                    if (!interest_bug_fix::CollectCountryFactories(country, factory_snapshots_.data(),
                            factory_snapshots_.size(), &factory_count, factory_input_snapshots_.data(),
                            factory_input_snapshots_.size(), &input_count, groups, &flags)) {
                        ++family_stats_[country_rule_index].invalid;
                    } else {
                        const bool reliable = !rule->country_tags.empty() && rule->country_tags.size() <= 16;
                        for (uint32_t index = 0; index < factory_count; ++index) {
                            const auto &snapshot = factory_snapshots_[index];
                            const SmedleyTelemetryFieldV1 entities[] = {
                                StringField("country_tag", *country_tag_value),
                                IntField("state_id", snapshot.state_id),
                                StringField("factory_type", snapshot.factory_type),
                            };
                            if (HasField(*rule, "identity")) {
                                ++family_stats_[country_rule_index].collection_attempts;
                                const SmedleyTelemetryFieldV1 identity_entities[] = {
                                    StringField("country_tag", *country_tag_value),
                                    IntField("state_id", snapshot.state_id),
                                    StringField("state_region_key", snapshot.state_region_key),
                                    StringField("factory_type", snapshot.factory_type),
                                };
                                const SmedleyTelemetryFieldV1 payload[] = {
                                    IntField("anchor_province_id_candidate", snapshot.anchor_province_id_candidate),
                                    IntField("level", snapshot.level),
                                    BoolField("subsidized", snapshot.subsidized),
                                    BoolField("closed", snapshot.closed),
                                };
                                AccountResult(country_rule_index, EmitTyped("state.factory.identity", "state", raw_date,
                                    identity_entities, 4, payload, 4, false, reliable));
                            }
                            if (HasField(*rule, "employment")) {
                                ++family_stats_[country_rule_index].collection_attempts;
                                const SmedleyTelemetryFieldV1 payload[] = {
                                    IntField("employee_count", snapshot.employee_count),
                                    IntField("craftsmen_count", snapshot.craftsmen_count),
                                    IntField("clerk_count", snapshot.clerk_count),
                                };
                                AccountResult(country_rule_index, EmitTyped("state.factory.employment", "state", raw_date,
                                    entities, 3, payload, 3, false, reliable));
                            }
                            if (HasField(*rule, "production")) {
                                ++family_stats_[country_rule_index].collection_attempts;
                                const SmedleyTelemetryFieldV1 payload[] = {
                                    IntField("output_raw", snapshot.output_raw),
                                    IntField("output_good_ordinal", snapshot.output_good_ordinal),
                                    StringField("output_good", snapshot.output_good),
                                    IntField("base_output_raw", snapshot.base_output_raw),
                                };
                                AccountResult(country_rule_index, EmitTyped("state.factory.production", "state", raw_date,
                                    entities, 3, payload, 4, false, reliable));
                            }
                            if (HasField(*rule, "finance")) {
                                ++family_stats_[country_rule_index].collection_attempts;
                                const SmedleyTelemetryFieldV1 payload[] = {
                                    IntField("budget_raw", snapshot.budget_raw),
                                    IntField("market_spending_expense_raw", snapshot.market_spending_raw),
                                    IntField("sales_income_raw", snapshot.sales_income_raw),
                                    IntField("paychecks_expense_raw", snapshot.paychecks_raw),
                                    IntField("investment_income_raw", snapshot.investment_raw),
                                };
                                AccountResult(country_rule_index, EmitTyped("state.factory.finance", "state", raw_date,
                                    entities, 3, payload, 5, false, reliable));
                            }
                        }
                        if (HasField(*rule, "inputs")) {
                            for (uint32_t index = 0; index < input_count; ++index) {
                                const auto &input = factory_input_snapshots_[index];
                                if (input.factory_snapshot_index >= factory_count) {
                                    ++family_stats_[country_rule_index].invalid;
                                    continue;
                                }
                                const auto &factory = factory_snapshots_[input.factory_snapshot_index];
                                const SmedleyTelemetryFieldV1 entities[] = {
                                    StringField("country_tag", *country_tag_value),
                                    IntField("state_id", factory.state_id),
                                    StringField("factory_type", factory.factory_type),
                                    IntField("good_ordinal", input.good_ordinal),
                                };
                                const auto field = IntField("stockpile_raw", input.stockpile_raw);
                                ++family_stats_[country_rule_index].collection_attempts;
                                AccountResult(country_rule_index, EmitTyped("state.factory.input", "state", raw_date,
                                    entities, 4, &field, 1, false, reliable));
                            }
                        }
                    }
                }
            }
            if (const auto *rule = FindRule("country.metrics", &country_rule_index);
                rule != nullptr && smedley::telemetry::ShouldCaptureDate(*raw_date, *rule, &schedule_states_[country_rule_index])) {
                AccountPoll(country_rule_index, *raw_date);
                const auto *country = event.GetCountry();
                const auto country_tag_value = NormalizedCountryTag(country);
                if (country != nullptr && !country_tag_value) ++family_stats_[country_rule_index].invalid;
                else if (country_tag_value && HasCountryTag(*rule, *country_tag_value)) {
                    const auto country_tag = StringField("country_tag", *country_tag_value);
                    const bool reliable = !rule->country_tags.empty() && rule->country_tags.size() <= 16;
                    if (HasField(*rule, "power")) {
                        ++family_stats_[country_rule_index].collection_attempts;
                        const SmedleyTelemetryFieldV1 payload[] = {
                            IntField("prestige_candidate_raw", country->prestige_candidate_raw()),
                            IntField("infamy_candidate_raw", country->infamy_candidate_raw()),
                            IntField("ranking_candidate", country->ranking_candidate()),
                            IntField("military_ranking_candidate", country->military_ranking_candidate()),
                            IntField("industrial_ranking_candidate", country->industrial_ranking_candidate()),
                            IntField("prestige_ranking_candidate", country->prestige_ranking_candidate()),
                        };
                        AccountResult(country_rule_index, EmitTyped("country.metrics.power", "state", raw_date,
                            &country_tag, 1, payload, 6, false, reliable));
                    }
                    if (HasField(*rule, "politics")) {
                        ++family_stats_[country_rule_index].collection_attempts;
                        const SmedleyTelemetryFieldV1 payload[] = {
                            IntField("plurality_candidate_raw", country->plurality_candidate_raw()),
                            IntField("war_exhaustion_candidate_raw", country->war_exhaustion_candidate_raw()),
                            IntField("diplomatic_points_candidate_raw", country->diplomatic_points_candidate_raw()),
                            IntField("research_points_candidate_raw", country->research_points_candidate_raw()),
                            IntField("leadership_candidate_raw", country->leadership_candidate_raw()),
                        };
                        AccountResult(country_rule_index, EmitTyped("country.metrics.politics", "state", raw_date,
                            &country_tag, 1, payload, 5, false, reliable));
                    }
                }
            }
            if (const auto *rule = FindRule("country.military", &country_rule_index);
                rule != nullptr && smedley::telemetry::ShouldCaptureDate(*raw_date, *rule, &schedule_states_[country_rule_index])) {
                AccountPoll(country_rule_index, *raw_date);
                const auto *country = event.GetCountry();
                const auto country_tag_value = NormalizedCountryTag(country);
                if (country != nullptr && !country_tag_value) ++family_stats_[country_rule_index].invalid;
                else if (country_tag_value && HasCountryTag(*rule, *country_tag_value)) {
                    ++family_stats_[country_rule_index].collection_attempts;
                    int unit_count = 0;
                    size_t mobilization_count = 0;
                    const bool valid = (!HasField(*rule, "unit_count_candidate")
                            || country->unit_count_candidate(&unit_count))
                        && (!HasField(*rule, "scheduled_mobilization_count_candidate")
                            || country->scheduled_mobilization_count_candidate(&mobilization_count));
                    if (!valid) {
                        ++family_stats_[country_rule_index].invalid;
                    } else {
                        const auto country_tag = StringField("country_tag", *country_tag_value);
                        std::array<SmedleyTelemetryFieldV1, 5> payload;
                        uint32_t count = 0;
                        if (HasField(*rule, "unit_count_candidate")) payload[count++] = IntField("unit_count_candidate", unit_count);
                        if (HasField(*rule, "mobilized_candidate")) payload[count++] = BoolField("mobilized_candidate", country->mobilized_candidate());
                        if (HasField(*rule, "scheduled_mobilization_count_candidate")) {
                            payload[count++] = IntField("scheduled_mobilization_count_candidate", mobilization_count);
                        }
                        if (HasField(*rule, "leadership_candidate_raw")) payload[count++] = IntField("leadership_candidate_raw", country->leadership_candidate_raw());
                        if (HasField(*rule, "military_ranking_candidate")) payload[count++] = IntField("military_ranking_candidate", country->military_ranking_candidate());
                        const bool reliable = !rule->country_tags.empty() && rule->country_tags.size() <= 16;
                        AccountResult(country_rule_index, EmitTyped("country.military", "state", raw_date,
                            &country_tag, 1, payload.data(), count, false, reliable));
                    }
                }
            }
            if (const auto *rule = FindRule("country.diplomacy", &country_rule_index);
                rule != nullptr && smedley::telemetry::ShouldCaptureDate(*raw_date, *rule, &schedule_states_[country_rule_index])) {
                AccountPoll(country_rule_index, *raw_date);
                const auto *country = event.GetCountry();
                const auto country_tag_value = NormalizedCountryTag(country);
                if (country != nullptr && !country_tag_value) ++family_stats_[country_rule_index].invalid;
                else if (country_tag_value && HasCountryTag(*rule, *country_tag_value)) {
                    const auto country_tag = StringField("country_tag", *country_tag_value);
                    const bool reliable = !rule->country_tags.empty() && rule->country_tags.size() <= 16;
                    if (HasField(*rule, "status")) {
                        ++family_stats_[country_rule_index].collection_attempts;
                        if (!country->overlord_candidate().normalized_candidate()
                            || !country->sphere_leader_candidate().normalized_candidate()) {
                            ++family_stats_[country_rule_index].invalid;
                        } else {
                            const SmedleyTelemetryFieldV1 payload[] = {
                                BoolField("substate_candidate", country->substate_candidate()),
                                BoolField("vassal_candidate", country->vassal_candidate()),
                                StringField("overlord_tag_candidate", country->overlord_candidate().str()),
                                StringField("sphere_leader_tag_candidate", country->sphere_leader_candidate().str()),
                            };
                            AccountResult(country_rule_index, EmitTyped("country.diplomacy.status", "state", raw_date,
                                &country_tag, 1, payload, 4, false, reliable));
                        }
                    }
                    if (HasField(*rule, "relations")) {
                        ++family_stats_[country_rule_index].collection_attempts;
                        size_t spherelings = 0, vassals = 0, allies = 0, guaranteed = 0, neighbors = 0;
                        if (!country->sphereling_count_candidate(&spherelings)
                            || !country->vassal_count_candidate(&vassals)
                            || !country->ally_count_candidate(&allies)
                            || !country->guaranteed_count_candidate(&guaranteed)
                            || !country->neighbor_count_candidate(&neighbors)) {
                            ++family_stats_[country_rule_index].invalid;
                        } else {
                            const SmedleyTelemetryFieldV1 payload[] = {
                                IntField("sphereling_count_candidate", spherelings),
                                IntField("vassal_count_candidate", vassals),
                                IntField("ally_count_candidate", allies),
                                IntField("guaranteed_count_candidate", guaranteed),
                                IntField("neighbor_count_candidate", neighbors),
                            };
                            AccountResult(country_rule_index, EmitTyped("country.diplomacy.relations", "state", raw_date,
                                &country_tag, 1, payload, 5, false, reliable));
                        }
                    }
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
        std::array<smedley::telemetry::ScheduleState, smedley::telemetry::kMaxCaptureRules> schedule_states_;
        std::array<FamilyStats, smedley::telemetry::kMaxCaptureRules> family_stats_;
        std::array<std::optional<int>, smedley::telemetry::kMaxCaptureRules> last_family_poll_dates_;
        std::array<PopAggregate, interest_bug_fix::max_sample_pops> pop_aggregates_;
        std::array<interest_bug_fix::FactorySnapshot, interest_bug_fix::max_sample_factories> factory_snapshots_;
        std::array<interest_bug_fix::FactoryInputSnapshot, interest_bug_fix::max_sample_factory_inputs> factory_input_snapshots_;
        std::array<interest_bug_fix::WorldMarketSnapshot, 64> world_market_snapshots_;
        std::optional<int> last_global_date_;
        std::optional<int> last_progress_date_;
        std::optional<int> last_observed_date_;

        void EmitEconomicSnapshot(const EconomicSnapshot &snapshot, const smedley::telemetry::CaptureRule &rule,
                                  size_t rule_index)
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
            if (HasField(rule, "health") || !snapshot.complete()) {
                AccountResult(rule_index,
                    EmitTyped("world.economy.health", "state", snapshot.date_raw, nullptr, 0, health, 8, false, true));
            }
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
            if (HasField(rule, "capacity")) {
                AccountResult(rule_index,
                    EmitTyped("world.economy.capacity", "state", snapshot.date_raw, nullptr, 0, capacity, 7, false, true));
            }

            const SmedleyTelemetryFieldV1 holdings[] = {
                IntField("treasury_observed_raw", snapshot.treasury_observed_raw),
                IntField("pop_money_observed_raw", snapshot.pop_money_observed_raw),
                IntField("pop_savings_observed_raw", snapshot.pop_savings_observed_raw),
                IntField("bank_interest_accumulator_raw", snapshot.bank_interest_accumulator_raw),
                IntField("positive_money_pops", snapshot.positive_money_pops),
                IntField("positive_savings_pops", snapshot.positive_savings_pops),
                IntField("negative_treasury_countries", snapshot.countries_with_negative_treasury),
            };
            if (HasField(rule, "holdings")) {
                AccountResult(rule_index,
                    EmitTyped("world.economy.holdings", "state", snapshot.date_raw, nullptr, 0, holdings, 7, false, true));
            }
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
            if (HasField(rule, "credit")) {
                AccountResult(rule_index,
                    EmitTyped("world.economy.credit", "state", snapshot.date_raw, nullptr, 0, credit, 7, false, true));
            }
        }

        uint64_t EmitPopulationSnapshot(const smedley::v2::CCurrentGameState *game_state, int32_t date_raw,
                                        const smedley::telemetry::CaptureRule &rule, size_t rule_index)
        {
            ++family_stats_[rule_index].polls_due;
            const PopulationCapture capture = economic_capture_.CollectPopulation(game_state, date_raw);
            family_stats_[rule_index].collection_us += capture.collection_us;
            if (!capture.complete()) {
                ++family_stats_[rule_index].invalid;
                return capture.collection_us;
            }
            for (uint32_t index = 0; index < capture.pop_count; ++index) {
                const auto &detail = economic_capture_.population_detail(index);
                if (!HasProvinceId(rule, detail.province_id_candidate)) continue;
                ++family_stats_[rule_index].collection_attempts;
                const SmedleyTelemetryFieldV1 entities[] = {
                    IntField("province_id_candidate", detail.province_id_candidate),
                    IntField("pop_type_id_candidate", detail.pop_type_id_candidate),
                    IntField("snapshot_index", index),
                };
                std::array<SmedleyTelemetryFieldV1, 5> payload;
                uint32_t count = 0;
                if (rule.family == "pop.economy") {
                    if (HasField(rule, "money_raw")) payload[count++] = IntField("money_raw", detail.economy.money_raw);
                    if (HasField(rule, "savings_raw")) payload[count++] = IntField("savings_raw", detail.economy.savings_raw);
                    if (HasField(rule, "interest_cash_flow_raw")) {
                        payload[count++] = IntField("interest_cash_flow_raw", detail.economy.interest_cash_flow_raw);
                    }
                    if (HasField(rule, "total_cash_flow_raw")) {
                        payload[count++] = IntField("total_cash_flow_raw", detail.economy.total_cash_flow_raw);
                    }
                } else {
                    if (HasField(rule, "size_candidate")) payload[count++] = IntField("size_candidate", detail.size_candidate);
                    if (HasField(rule, "employed_candidate")) payload[count++] = IntField("employed_candidate", detail.employed_candidate);
                    if (HasField(rule, "consciousness_candidate_raw")) {
                        payload[count++] = IntField("consciousness_candidate_raw", detail.consciousness_candidate_raw);
                    }
                    if (HasField(rule, "militancy_candidate_raw")) {
                        payload[count++] = IntField("militancy_candidate_raw", detail.militancy_candidate_raw);
                    }
                    if (HasField(rule, "literacy_candidate_raw")) {
                        payload[count++] = IntField("literacy_candidate_raw", detail.literacy_candidate_raw);
                    }
                }
                const bool reliable = !rule.province_ids.empty() && rule.province_ids.size() <= 16;
                AccountResult(rule_index, EmitTyped(rule.family.c_str(), "state", date_raw,
                    entities, 3, payload.data(), count, false, reliable));
            }
            return capture.collection_us;
        }

        uint64_t EmitPopulationAggregate(const smedley::v2::CCurrentGameState *game_state, int32_t date_raw,
                                         const smedley::telemetry::CaptureRule &rule, size_t rule_index)
        {
            ++family_stats_[rule_index].polls_due;
            const PopulationCapture capture = economic_capture_.CollectPopulation(game_state, date_raw);
            family_stats_[rule_index].collection_us += capture.collection_us;
            if (!capture.complete()) {
                ++family_stats_[rule_index].invalid;
                return capture.collection_us;
            }
            size_t aggregate_count = 0;
            for (uint32_t index = 0; index < capture.pop_count; ++index) {
                const auto &detail = economic_capture_.population_detail(index);
                if (!HasProvinceId(rule, detail.province_id_candidate)) continue;
                pop_aggregates_[aggregate_count++] = {
                    detail.province_id_candidate, detail.pop_type_id_candidate, 1,
                    detail.size_candidate, detail.employed_candidate,
                    detail.economy.money_raw, detail.economy.savings_raw};
            }
            std::sort(pop_aggregates_.begin(), pop_aggregates_.begin() + aggregate_count,
                [](const PopAggregate &left, const PopAggregate &right) {
                    return left.province_id < right.province_id
                        || (left.province_id == right.province_id && left.pop_type_id < right.pop_type_id);
                });
            size_t merged_count = 0;
            const auto add = [&](int64_t value, int64_t *sum) {
                if ((value > 0 && *sum > (std::numeric_limits<int64_t>::max)() - value)
                    || (value < 0 && *sum < (std::numeric_limits<int64_t>::min)() - value)) return false;
                *sum += value;
                return true;
            };
            for (size_t index = 0; index < aggregate_count; ++index) {
                const auto &next = pop_aggregates_[index];
                if (merged_count == 0 || pop_aggregates_[merged_count - 1].province_id != next.province_id
                    || pop_aggregates_[merged_count - 1].pop_type_id != next.pop_type_id) {
                    pop_aggregates_[merged_count++] = next;
                    continue;
                }
                auto &current = pop_aggregates_[merged_count - 1];
                if (!add(next.pop_count, &current.pop_count) || !add(next.size, &current.size)
                    || !add(next.employed, &current.employed) || !add(next.money_raw, &current.money_raw)
                    || !add(next.savings_raw, &current.savings_raw)) {
                    ++family_stats_[rule_index].invalid;
                    return capture.collection_us;
                }
            }
            for (size_t index = 0; index < merged_count; ++index) {
                const auto &aggregate = pop_aggregates_[index];
                ++family_stats_[rule_index].collection_attempts;
                const SmedleyTelemetryFieldV1 entities[] = {
                    IntField("province_id_candidate", aggregate.province_id),
                    IntField("pop_type_id_candidate", aggregate.pop_type_id),
                };
                std::array<SmedleyTelemetryFieldV1, 5> payload;
                uint32_t count = 0;
                if (HasField(rule, "pop_count")) payload[count++] = IntField("pop_count", aggregate.pop_count);
                if (HasField(rule, "size_candidate")) payload[count++] = IntField("size_candidate", aggregate.size);
                if (HasField(rule, "employed_candidate")) payload[count++] = IntField("employed_candidate", aggregate.employed);
                if (HasField(rule, "money_raw")) payload[count++] = IntField("money_raw", aggregate.money_raw);
                if (HasField(rule, "savings_raw")) payload[count++] = IntField("savings_raw", aggregate.savings_raw);
                const bool reliable = !rule.province_ids.empty() && rule.province_ids.size() <= 16;
                AccountResult(rule_index, EmitTyped("pop.aggregate", "state", date_raw,
                    entities, 2, payload.data(), count, false, reliable));
            }
            return capture.collection_us;
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
