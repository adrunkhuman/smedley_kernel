#include "telemetry_core.hpp"
#include "collector_runtime.hpp"
#include "country_collector.hpp"
#include "factory_economy_collector.hpp"
#include "province_collector.hpp"
#include "population_collector.hpp"
#include "telemetry_services.hpp"
#include "world_collector.hpp"
#include "economic_capture.hpp"

#include <smedley/event_api.h>
#include <smedley/logging_api.h>
#define SMEDLEY_PLUGIN_BUILD
#include <smedley/plugin_abi.h>

#include <shellapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>

namespace telemetry_plugin
{
    using services::ArtisanSettlementHookRecord;
    using services::FactorySalesHookRecord;
    using services::FactorySettlementHookRecord;
    using services::PopCashFlowHookRecord;
    using services::PopCashFlowHookStats;
    constexpr auto max_factory_flow_records = services::max_factory_flow_records;
    constexpr auto max_factory_sales_records = services::max_factory_sales_records;
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

    class Plugin final : public collectors::CollectorRuntime
    {
    public:
        ~Plugin()
        {
            services::SetActiveApi(nullptr);
            services_.Close();
        }

        void OnLoad()
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
            std::vector<std::vector<std::string_view>> selected_fields;
            selected_fields.reserve(config_.capture_rules.size());
            std::vector<smedley::telemetry::MetricRuleSelection> selections;
            selections.reserve(config_.capture_rules.size());
            for (const auto &rule : config_.capture_rules) {
                auto &fields = selected_fields.emplace_back();
                fields.assign(rule.fields.begin(), rule.fields.end());
                selections.push_back({rule.family, fields.data(), fields.size()});
            }
            runtime_plan_ = smedley::telemetry::BuildRuntimePlan(selections.data(), selections.size());
            if (!services_.Acquire(&error)) {
                throw std::runtime_error(error);
            }
            services::SetActiveApi(&services_);
            if (!config_.capture_rules.empty()) {
                economic_capture_ = std::make_unique<EconomicCapture>();
                world_collector_ = std::make_unique<collectors::WorldCollector>(economic_capture_.get(), this);
                province_collector_ = std::make_unique<collectors::ProvinceCollector>(this);
                country_collector_ = std::make_unique<collectors::CountryCollector>(this);
                factory_economy_collector_ = std::make_unique<collectors::FactoryEconomyCollector>(this,
                    economic_capture_.get(), config_.gold_to_cash_rate ? &*config_.gold_to_cash_rate : nullptr);
                if (std::any_of(
                        config_.capture_rules.begin(), config_.capture_rules.end(),
                        [](const smedley::telemetry::CaptureRule &rule) {
                            const auto *definition = smedley::telemetry::FindMetricFamily(rule.family);
                            return definition != nullptr
                                && definition->collector == smedley::telemetry::MetricCollector::Population;
                        })) {
                    population_collector_ = std::make_unique<collectors::PopulationCollector>(this, economic_capture_.get());
                }
            }
            if (runtime_plan_.install_pop_cashflow_hook) {
                pop_cash_flow_hook_installed_ = true;
                if (!InstallPopCashFlowHook(&error)) {
                    if (!StopConsumptionHooks(false)) TerminateProcess(GetCurrentProcess(), ERROR_OPERATION_ABORTED);
                    logger().Failure("POP cash-flow hook did not start: " + error);
                    throw std::runtime_error(error);
                }
            }
            if (runtime_plan_.install_factory_sales_hook) {
                factory_sales_hook_records_ = std::make_unique<
                    std::array<FactorySalesHookRecord, max_factory_sales_records>>();
                factory_sales_hook_installed_ = true;
                if (!InstallFactorySalesHook(&error)) {
                    logger().Failure("factory sales hook did not start: " + error);
                    throw std::runtime_error(error);
                }
            }
            if (runtime_plan_.install_factory_flow_hook) {
                factory_hook_records_ = std::make_unique<std::array<FactorySettlementHookRecord, max_factory_flow_records>>();
                factory_consumption_hook_installed_ = true;
                if (!InstallFactoryConsumptionHook(&error)) {
                    StopConsumptionHooks(false);
                    logger().Failure("factory consumption hook did not start: " + error);
                    throw std::runtime_error(error);
                }
            }
            if (const auto *rule = FindRule("pop.artisan"); runtime_plan_.install_artisan_flow_hook && rule != nullptr) {
                if (rule->country_tags.empty() || rule->country_tags.size() > artisan_country_keys_.size()) {
                    StopConsumptionHooks(false);
                    throw std::runtime_error("artisan flows require 1 to 16 country_tags");
                }
                for (size_t index = 0; index < rule->country_tags.size(); ++index) {
                    std::memcpy(&artisan_country_keys_[index], rule->country_tags[index].data(), 3);
                }
                artisan_consumption_hook_installed_ = true;
                if (!InstallArtisanConsumptionHook(artisan_country_keys_.data(), rule->country_tags.size(), &error)) {
                    StopConsumptionHooks(false);
                    logger().Failure("artisan consumption hook did not start: " + error);
                    throw std::runtime_error(error);
                }
            }
            writer_ = std::make_unique<smedley::telemetry::Writer>(config_);
            if (!writer_->Start(&error)) {
                StopConsumptionHooks(false);
                logger().Failure("telemetry did not start: " + error);
                writer_.reset();
                throw std::runtime_error(error);
            }
            const auto plugin = StringField("plugin", "telemetry");
            const auto start_result = EmitTyped("session.started", "lifecycle", std::nullopt, nullptr, 0, &plugin, 1, true);
            if (start_result.status != SMEDLEY_TELEMETRY_ACCEPTED && start_result.status != SMEDLEY_TELEMETRY_FILTERED) {
                writer_->Stop();
                writer_.reset();
                StopConsumptionHooks(false);
                throw std::runtime_error("could not queue telemetry session start");
            }
            if (!EmitCaptureMetadata()) {
                writer_->Stop();
                writer_.reset();
                StopConsumptionHooks(false);
                throw std::runtime_error("could not queue telemetry capture metadata");
            }
            bool handler_registered = false;
            try {
                logger().Info("writing bounded JSON Lines telemetry to " + config_.output_path.string());
                if (!config_.capture_rules.empty()) {
                    if (services_.events().register_daily(&NotifyDailyUpdate, this, &daily_registration_)
                        != SMEDLEY_EVENT_SUCCESS) throw std::runtime_error("telemetry daily event registration failed");
                    handler_registered = true;
                    handler_registered_ = true;
                }
                std::unique_lock<std::shared_timed_mutex> lock(active_sink_mutex);
                active_sink = this;
            } catch (...) {
                if (handler_registered && daily_registration_ != 0) services_.events().unregister(daily_registration_);
                daily_registration_ = 0;
                writer_->Stop();
                writer_.reset();
                StopConsumptionHooks(false);
                throw;
            }
        }

        void OnUnload()
        {
            {
                std::unique_lock<std::shared_timed_mutex> lock(active_sink_mutex);
                if (active_sink == this) active_sink = nullptr;
            }
            (void)DrainUntil((std::chrono::steady_clock::time_point::max)());
            const bool hooks_stopped = StopConsumptionHooks(true);
            writer_.reset();
            services::SetActiveApi(nullptr);
            services_.Close();
            if (!hooks_stopped) {
                throw std::runtime_error("telemetry consumption hooks could not be restored and were disabled");
            }
        }

        SmedleyTelemetryResult EmitExternal(const SmedleyTelemetryRecordV1 *record, bool reliable)
        {
            return EmitRecord(record, false, reliable).status;
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
                    services_.events().unregister(daily_registration_);
                    daily_registration_ = 0;
                    handler_registered_ = false;
                }
                if (factory_economy_collector_) factory_economy_collector_->Flush();
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
        class Logger final
        {
        public:
            explicit Logger(services::Api *services) : services_(services) {}
            void Info(const std::string &message) const { services_->Log(SMEDLEY_LOG_INFO, message); }
            void Failure(const std::string &message) const { services_->Log(SMEDLEY_LOG_FAILURE, message); }
        private:
            services::Api *services_;
        };

        Logger logger() { return Logger(&services_); }

        static SmedleyEventCallbackResult SMEDLEY_EVENT_CALL NotifyDailyUpdate(
            void *context, const SmedleyDailyEventV1 *event) noexcept
        {
            auto *plugin = static_cast<Plugin *>(context);
            if (plugin == nullptr || event == nullptr) return SMEDLEY_EVENT_CALLBACK_DISABLE;
            try {
                smedley::events::DailyUpdateEvent compatibility_event(*event);
                plugin->OnDailyUpdate(compatibility_event);
                return SMEDLEY_EVENT_CALLBACK_CONTINUE;
            } catch (...) {
                return SMEDLEY_EVENT_CALLBACK_DISABLE;
            }
        }

        bool InstallPopCashFlowHook(std::string *) { requested_hooks_ |= SMEDLEY_TELEMETRY_HOOK_POP_CASH_FLOW; return true; }
        bool InstallFactoryConsumptionHook(std::string *) { requested_hooks_ |= SMEDLEY_TELEMETRY_HOOK_FACTORY_CONSUMPTION; return true; }
        bool InstallFactorySalesHook(std::string *) { requested_hooks_ |= SMEDLEY_TELEMETRY_HOOK_FACTORY_SALES; return true; }
        bool InstallArtisanConsumptionHook(const uint32_t *, size_t count, std::string *) { requested_hooks_ |= SMEDLEY_TELEMETRY_HOOK_ARTISAN_CONSUMPTION; artisan_country_key_count_ = static_cast<uint32_t>(count); return true; }
        bool UninstallPopCashFlowHook(std::string *) { return true; }
        bool UninstallFactoryConsumptionHook(std::string *) { return true; }
        bool UninstallFactorySalesHook(std::string *) { return true; }
        bool UninstallArtisanConsumptionHook(std::string *) { return true; }
        bool DrainFactoryConsumptionHook(FactorySettlementHookRecord *records, size_t capacity, uint32_t *count, uint64_t *dropped) { return services_.DrainFactoryConsumption(records, static_cast<uint32_t>(capacity), count, dropped); }
        bool DrainFactorySalesHook(FactorySalesHookRecord *records, size_t capacity, uint32_t *count, uint64_t *dropped) { return services_.DrainFactorySales(records, static_cast<uint32_t>(capacity), count, dropped); }
        bool DrainArtisanConsumptionHook(ArtisanSettlementHookRecord *records, size_t capacity, uint32_t *count, uint64_t *dropped) { return services_.DrainArtisanConsumption(records, static_cast<uint32_t>(capacity), count, dropped); }
        bool DrainPopCashFlowHook(PopCashFlowHookRecord *records, size_t capacity, uint32_t *count, PopCashFlowHookStats *stats) { return services_.DrainPopCashFlow(records, static_cast<uint32_t>(capacity), count, stats); }
        bool StopConsumptionHooks(bool report_errors) noexcept
        {
            bool stopped = true;
            if (factory_sales_hook_installed_) {
                std::string error;
                if (UninstallFactorySalesHook(&error)) factory_sales_hook_installed_ = false;
                else {
                    stopped = false;
                    if (report_errors) logger().Failure(error);
                }
            }
            if (pop_cash_flow_hook_installed_) {
                std::string error;
                if (UninstallPopCashFlowHook(&error)) pop_cash_flow_hook_installed_ = false;
                else {
                    stopped = false;
                    if (report_errors) logger().Failure(error);
                }
            }
            if (artisan_consumption_hook_installed_) {
                std::string error;
                if (UninstallArtisanConsumptionHook(&error)) artisan_consumption_hook_installed_ = false;
                else {
                    stopped = false;
                    if (report_errors) logger().Failure(error);
                }
            }
            if (factory_consumption_hook_installed_) {
                std::string error;
                if (UninstallFactoryConsumptionHook(&error)) factory_consumption_hook_installed_ = false;
                else {
                    stopped = false;
                    if (report_errors) logger().Failure(error);
                }
            }
            return stopped;
        }

        void AccountResult(size_t rule_index, smedley::telemetry::PublicationResult result)
        {
            auto &stats = family_stats_[rule_index];
            if (result.status == SMEDLEY_TELEMETRY_ACCEPTED) {
                ++stats.accepted;
                stats.accepted_bytes += result.formatted_bytes;
            }
            else if (result.status == SMEDLEY_TELEMETRY_FILTERED) ++stats.filtered;
            else if (result.status == SMEDLEY_TELEMETRY_INVALID) ++stats.invalid;
            else {
                ++stats.dropped;
                stats.dropped_bytes += result.formatted_bytes;
            }
        }

        void AccountPoll(size_t rule_index, int32_t date_raw)
        {
            if (last_family_poll_dates_[rule_index] == date_raw) return;
            last_family_poll_dates_[rule_index] = date_raw;
            ++family_stats_[rule_index].polls_due;
        }

        const smedley::telemetry::CaptureRule *DueRule(std::string_view family, int32_t date_raw,
                                                       size_t *rule_index) override
        {
            const auto *rule = FindRule(family, rule_index);
            return rule != nullptr && smedley::telemetry::ShouldCaptureDate(date_raw, *rule, &schedule_states_[*rule_index])
                ? rule : nullptr;
        }

        const smedley::telemetry::CaptureRule *FindRule(std::string_view family,
                                                         size_t *rule_index = nullptr) const override
        {
            return FindRuleInternal(family, rule_index);
        }

        size_t RuleCount() const override { return config_.capture_rules.size(); }
        const smedley::telemetry::CaptureRule &RuleAt(size_t index) const override { return config_.capture_rules[index]; }
        collectors::FamilyStats &Stats(size_t rule_index) override { return family_stats_[rule_index]; }

        void Poll(size_t rule_index) override { ++family_stats_[rule_index].polls_due; }
        void PollOnce(size_t rule_index, int32_t date_raw) override { AccountPoll(rule_index, date_raw); }
        void Attempt(size_t rule_index) override { ++family_stats_[rule_index].collection_attempts; }
        void Invalid(size_t rule_index) override { ++family_stats_[rule_index].invalid; }
        void Invalid(size_t rule_index, uint64_t count) override
        {
            auto &invalid = family_stats_[rule_index].invalid;
            invalid = count > (std::numeric_limits<uint64_t>::max)() - invalid
                ? (std::numeric_limits<uint64_t>::max)() : invalid + count;
        }
        void CollectionTime(size_t rule_index, uint64_t collection_us) override { family_stats_[rule_index].collection_us += collection_us; }
        void Account(size_t rule_index, smedley::telemetry::PublicationResult result) override { AccountResult(rule_index, result); }
        smedley::telemetry::PublicationResult EmitFamilyState(
            size_t rule_index, const char *event_type, int32_t date_raw,
            const SmedleyTelemetryFieldV1 *entities, uint32_t entity_count,
            const SmedleyTelemetryFieldV1 *payload, uint32_t payload_count, bool initial = false) override
        {
            if (rule_index >= config_.capture_rules.size()) return {SMEDLEY_TELEMETRY_INVALID, 0};
            const auto *family = smedley::telemetry::FindMetricFamily(config_.capture_rules[rule_index].family);
            if (family == nullptr || !smedley::telemetry::MetricFamilyEmitsEvent(*family, event_type)) {
                return {SMEDLEY_TELEMETRY_INVALID, 0};
            }
            const auto *event = smedley::telemetry::FindMetricEvent(event_type);
            std::array<std::string_view, SMEDLEY_TELEMETRY_MAX_FIELDS> entity_keys;
            std::array<std::string_view, SMEDLEY_TELEMETRY_MAX_FIELDS> payload_keys;
            if (event == nullptr || entity_count > entity_keys.size() || payload_count > payload_keys.size()) {
                return {SMEDLEY_TELEMETRY_INVALID, 0};
            }
            for (uint32_t index = 0; index < entity_count; ++index) {
                entity_keys[index] = std::string_view(entities[index].key, entities[index].key_length);
            }
            for (uint32_t index = 0; index < payload_count; ++index) {
                payload_keys[index] = std::string_view(payload[index].key, payload[index].key_length);
            }
            if (!smedley::telemetry::MetricEventMatchesSchema(
                    *event, entity_keys.data(), entity_count, payload_keys.data(), payload_count)) {
                return {SMEDLEY_TELEMETRY_INVALID, 0};
            }
            const auto &rule = config_.capture_rules[rule_index];
            const bool reliable = smedley::telemetry::MetricFamilyAdmission(
                *family, rule.country_tags.size(), rule.province_ids.size()) == smedley::telemetry::MetricAdmission::Reliable;
            return EmitTyped(event_type, "state", date_raw, entities, entity_count, payload, payload_count, initial, reliable);
        }

        void EmitFamilySummaries()
        {
            if (!smedley::telemetry::HasCategory(config_, "lifecycle")) return;
            const auto health_value = [](uint64_t value) {
                return static_cast<int64_t>(std::min<uint64_t>(value, (std::numeric_limits<int64_t>::max)()));
            };
            for (size_t index = 0; index < config_.capture_rules.size(); ++index) {
                const auto family = StringField("family", config_.capture_rules[index].family.c_str());
                const auto &stats = family_stats_[index];
                const SmedleyTelemetryFieldV1 payload[] = {
                    IntField("polls_due", health_value(stats.polls_due)),
                    IntField("collection_attempts", health_value(stats.collection_attempts)),
                    IntField("accepted", health_value(stats.accepted)),
                    IntField("filtered", health_value(stats.filtered)),
                    IntField("dropped", health_value(stats.dropped)),
                    IntField("invalid", health_value(stats.invalid)),
                    IntField("collection_us", health_value(stats.collection_us)),
                    IntField("accepted_bytes", health_value(stats.accepted_bytes)),
                    IntField("dropped_bytes", health_value(stats.dropped_bytes)),
                };
                (void)EmitTyped("telemetry.family.summary", "lifecycle", std::nullopt,
                                &family, 1, payload, 9, true, true);
            }
        }

        bool EmitCaptureMetadata()
        {
            for (const auto &rule : config_.capture_rules) {
                const auto *definition = smedley::telemetry::FindMetricFamily(rule.family);
                if (definition == nullptr) return false;
                const auto family = StringField("family", rule.family.c_str());
                const std::string cadence_name = smedley::telemetry::CaptureCadenceName(rule.cadence);
                const SmedleyTelemetryFieldV1 payload[] = {
                    StringField("cadence", cadence_name), BoolField("all_fields", rule.fields.empty()),
                    IntField("country_filter_count", rule.country_tags.size()),
                    IntField("province_filter_count", rule.province_ids.size()),
                    BoolField("bounded_dates", rule.start_date_raw.has_value() || rule.end_date_raw.has_value()),
                    IntField("projected_entity_count", rule.country_tags.empty() && rule.province_ids.empty() ? -1
                        : static_cast<int64_t>(rule.country_tags.size() + rule.province_ids.size())),
                    StringField("operational_admission",
                        smedley::telemetry::MetricFamilyAdmission(*definition, rule.country_tags.size(), rule.province_ids.size())
                            == smedley::telemetry::MetricAdmission::Reliable ? "reliable" : "best-effort"),
                };
                static_assert(1 + std::size(payload) <= SMEDLEY_TELEMETRY_MAX_FIELDS);
                const auto result = EmitTyped("telemetry.capture.rule", "lifecycle", std::nullopt,
                    &family, 1, payload, static_cast<uint32_t>(std::size(payload)), false, true);
                if (result.status != SMEDLEY_TELEMETRY_ACCEPTED && result.status != SMEDLEY_TELEMETRY_FILTERED) return false;
                for (const auto &field_name : rule.fields) {
                    const SmedleyTelemetryFieldV1 entities[] = {
                        family, StringField("field", field_name.c_str()),
                    };
                    const auto field_result = EmitTyped("telemetry.capture.field", "lifecycle", std::nullopt,
                        entities, 2, nullptr, 0, false, true);
                    if (field_result.status != SMEDLEY_TELEMETRY_ACCEPTED
                        && field_result.status != SMEDLEY_TELEMETRY_FILTERED) return false;
                }
                for (const auto &country_tag : rule.country_tags) {
                    const SmedleyTelemetryFieldV1 entities[] = {
                        family, StringField("country_tag", country_tag.c_str()),
                    };
                    const auto country_result = EmitTyped("telemetry.capture.country", "lifecycle", std::nullopt,
                        entities, 2, nullptr, 0, false, true);
                    if (country_result.status != SMEDLEY_TELEMETRY_ACCEPTED
                        && country_result.status != SMEDLEY_TELEMETRY_FILTERED) return false;
                }
            }
            return true;
        }

        smedley::telemetry::PublicationResult EmitRecord(const SmedleyTelemetryRecordV1 *record, bool initial, bool reliable = false)
        {
            if (draining_.load(std::memory_order_acquire) && !initial) return {};
            if (!writer_) return {};
            std::string error;
            if (!smedley::telemetry::ValidateRecordV1(record, &error)) return {SMEDLEY_TELEMETRY_INVALID, 0};
            if (!smedley::telemetry::HasCategory(config_, std::string_view(record->category, record->category_length))) {
                return {SMEDLEY_TELEMETRY_FILTERED, 0};
            }
            smedley::telemetry::PreparedRecordV1 prepared;
            if (!smedley::telemetry::PrepareRecordV1(record, config_.run_id, smedley::telemetry::UtcNow(),
                                                      smedley::telemetry::MonotonicMicroseconds(), &prepared, &error)) {
                return {SMEDLEY_TELEMETRY_INVALID, 0};
            }
            return smedley::telemetry::PublishPreparedRecord(
                prepared, &sequence_, &emission_mutex_, initial || reliable,
                [this, initial, reliable](std::string_view line) {
                    return initial ? writer_->WriteInitial(line)
                        : reliable ? writer_->WriteReliable(line) : writer_->TryWrite(line);
                },
                [this] { writer_->MarkDropped(); });
        }

        smedley::telemetry::PublicationResult EmitTyped(const char *event_type, const char *category, std::optional<int> game_date_raw,
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

        static std::optional<std::string_view> NormalizedCountryTag(
            const smedley::game_state::TelemetryCountrySnapshot *country)
        {
            if (country == nullptr || !country->tag().normalized_candidate()) return std::nullopt;
            return std::string_view(country->tag().str(), 3);
        }

        static bool ReadDailyCountry(smedley::events::DailyUpdateEvent &event,
                                     smedley::game_state::TelemetryCountrySnapshot *snapshot)
        {
            return smedley::game_state::ReadTelemetryCountry(
                smedley::game_state::DailyUpdateCountry(event), snapshot);
        }

        static bool HasProvinceId(const smedley::telemetry::CaptureRule &rule, int id)
        {
            return rule.province_ids.empty()
                || std::find(rule.province_ids.begin(), rule.province_ids.end(), id) != rule.province_ids.end();
        }

        const smedley::telemetry::CaptureRule *FindRuleInternal(std::string_view family, size_t *index = nullptr) const
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
            std::shared_lock<std::shared_timed_mutex> producer_lock(producer_mutex_, std::try_to_lock);
            if (!producer_lock.owns_lock()) return;
            const uint64_t started = smedley::telemetry::MonotonicMicroseconds();
            uint64_t collection_us = 0;
            auto finish = [&] {
                const uint64_t elapsed = smedley::telemetry::MonotonicMicroseconds() - started;
                callback_overhead_us_.fetch_add(elapsed > collection_us ? elapsed - collection_us : 0, std::memory_order_relaxed);
                callback_count_.fetch_add(1, std::memory_order_relaxed);
            };
            if (draining_.load(std::memory_order_acquire) || !writer_) { finish(); return; }
            const bool lifecycle_enabled = smedley::telemetry::HasCategory(config_, "lifecycle");
            const bool state_enabled = smedley::telemetry::HasCategory(config_, "state")
                && !config_.capture_rules.empty();
            if (!state_enabled) { finish(); return; }
            if (!services_.EnsureOpen(requested_hooks_, artisan_country_keys_.data(), artisan_country_key_count_)) {
                if (state_enabled) skipped_unsampleable_.fetch_add(1, std::memory_order_relaxed);
                finish();
                return;
            }
            if (writer_->stats().write_failed) write_failure_logged_.store(true, std::memory_order_relaxed);
            smedley::game_state::TelemetryCurrentState game_state{};
            const std::optional<int> raw_date = smedley::game_state::ReadTelemetryCurrentState(&game_state)
                ? std::optional<int>(game_state.date_raw) : std::nullopt;
            if (!raw_date) { if (state_enabled) skipped_unsampleable_.fetch_add(1, std::memory_order_relaxed); finish(); return; }
            services_.BeginHookDrain();
            if (factory_consumption_hook_installed_ && factory_hook_date_ != raw_date) {
                factory_hook_record_count_ = 0;
                factory_hook_dropped_ = 0;
                if (!DrainFactoryConsumptionHook(factory_hook_records_->data(), factory_hook_records_->size(),
                        &factory_hook_record_count_, &factory_hook_dropped_)) {
                    factory_hook_dropped_ = 1;
                }
                std::sort(factory_hook_records_->begin(), factory_hook_records_->begin() + factory_hook_record_count_,
                    [](const FactorySettlementHookRecord &left, const FactorySettlementHookRecord &right) {
                        const auto left_address = left.factory.address();
                        const auto right_address = right.factory.address();
                        return left_address != right_address ? left_address < right_address : left.pool < right.pool;
                    });
                factory_hook_date_ = raw_date;
                factory_economy_collector_->ObserveFactoryFlows(factory_hook_records_->data(),
                    factory_hook_record_count_, factory_hook_dropped_, *raw_date);
            }
            if (factory_sales_hook_installed_ && factory_sales_hook_date_ != raw_date) {
                factory_sales_hook_record_count_ = 0;
                factory_sales_hook_dropped_ = 0;
                if (!DrainFactorySalesHook(factory_sales_hook_records_->data(), factory_sales_hook_records_->size(),
                        &factory_sales_hook_record_count_, &factory_sales_hook_dropped_)) {
                    factory_sales_hook_dropped_ = 1;
                }
                std::sort(factory_sales_hook_records_->begin(),
                    factory_sales_hook_records_->begin() + factory_sales_hook_record_count_,
                    [](const FactorySalesHookRecord &left, const FactorySalesHookRecord &right) {
                        return left.factory.address() < right.factory.address();
                    });
                factory_sales_hook_date_ = raw_date;
                factory_economy_collector_->ObserveFactorySales(factory_sales_hook_records_->data(),
                    factory_sales_hook_record_count_, factory_sales_hook_dropped_, *raw_date);
            }
            if (artisan_consumption_hook_installed_ && artisan_hook_date_ != raw_date) {
                artisan_hook_record_count_ = 0;
                artisan_hook_dropped_ = 0;
                auto *artisan_records = population_collector_->artisan_records();
                if (!DrainArtisanConsumptionHook(artisan_records, population_collector_->artisan_record_capacity(),
                        &artisan_hook_record_count_, &artisan_hook_dropped_)) {
                    artisan_hook_dropped_ = 1;
                }
                std::sort(artisan_records, artisan_records + artisan_hook_record_count_,
                    [](const ArtisanSettlementHookRecord &left, const ArtisanSettlementHookRecord &right) {
                        const auto left_address = left.pop.address();
                        const auto right_address = right.pop.address();
                        return left_address != right_address ? left_address < right_address : left.pool < right.pool;
                    });
                artisan_hook_date_ = raw_date;
                population_collector_->ObserveArtisanFlows(artisan_records, artisan_hook_record_count_,
                    artisan_hook_dropped_, *raw_date);
            }
            if (pop_cash_flow_hook_installed_ && pop_cash_flow_hook_date_ != raw_date) {
                pop_cash_flow_record_count_ = 0;
                pop_cash_flow_hook_stats_ = {};
                auto *cash_flow_records = population_collector_->cash_flow_records();
                if (!DrainPopCashFlowHook(cash_flow_records, population_collector_->cash_flow_record_capacity(),
                        &pop_cash_flow_record_count_, &pop_cash_flow_hook_stats_)) {
                    pop_cash_flow_hook_stats_.output_overflow = 1;
                }
                std::sort(cash_flow_records, cash_flow_records + pop_cash_flow_record_count_,
                    [](const PopCashFlowHookRecord &left, const PopCashFlowHookRecord &right) {
                        return left.pop.address() < right.pop.address();
                    });
                pop_cash_flow_hook_date_ = raw_date;
                population_collector_->ObserveCashFlows(cash_flow_records, pop_cash_flow_record_count_,
                    pop_cash_flow_hook_stats_, *raw_date);
            }
            const std::optional<int> previous_date = last_observed_date_;
            int64_t delta = 0;
            const bool regressed = smedley::telemetry::ObserveDateRegression(*raw_date, &last_observed_date_, &delta);
            if (regressed) {
                province_collector_->Reset();
                if (population_collector_) population_collector_->Reset();
            }
            if (lifecycle_enabled && regressed) {
                const SmedleyTelemetryFieldV1 payload[] = {
                    IntField("previous_date_raw", *previous_date), IntField("current_date_raw", *raw_date), IntField("delta_raw", delta)};
                EmitTyped("date.regressed", "lifecycle", raw_date, nullptr, 0, payload, 3, false, true);
            }
            const bool progress = lifecycle_enabled && (!last_progress_date_ || *raw_date != *last_progress_date_);
            if (state_enabled && last_global_date_ != raw_date) {
                last_global_date_ = raw_date;
                factory_economy_collector_->ProcessCountryEconomy(game_state, *raw_date);
                collection_us += world_collector_->Collect(game_state, *raw_date);
                province_collector_->Collect(game_state, *raw_date);
                if (population_collector_) collection_us += population_collector_->Collect(game_state, *raw_date);
            }
            country_collector_->Collect(event, *raw_date);
            factory_economy_collector_->CollectFactories(event, *raw_date);
            smedley::telemetry::PreparedRecordV1 progress_record;
            if (progress && PrepareEnvelope("telemetry.progress", "lifecycle", raw_date, "{}", StatsPayload(writer_->stats()), &progress_record)
                && smedley::telemetry::PublishPreparedRecord(
                    progress_record, &sequence_, &emission_mutex_, true,
                    [this](std::string_view line) { return writer_->WriteReliable(line); },
                    [this] { writer_->MarkDropped(); }).status == SMEDLEY_TELEMETRY_ACCEPTED) {
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
            return result.status == SMEDLEY_TELEMETRY_ACCEPTED ? line : std::string{};
        }

        smedley::telemetry::Config config_;
        smedley::telemetry::RuntimePlan runtime_plan_;
        services::Api services_;
        uint32_t requested_hooks_ = 0;
        uint32_t artisan_country_key_count_ = 0;
        SmedleyEventRegistration daily_registration_ = 0;
        std::unique_ptr<EconomicCapture> economic_capture_;
        std::unique_ptr<collectors::WorldCollector> world_collector_;
        std::unique_ptr<collectors::ProvinceCollector> province_collector_;
        std::unique_ptr<collectors::CountryCollector> country_collector_;
        std::unique_ptr<collectors::FactoryEconomyCollector> factory_economy_collector_;
        std::unique_ptr<collectors::PopulationCollector> population_collector_;
        std::unique_ptr<smedley::telemetry::Writer> writer_;
        std::atomic<uint64_t> sequence_{0};
        std::mutex emission_mutex_;
        std::atomic<uint64_t> callback_count_{0};
        std::atomic<uint64_t> callback_overhead_us_{0};
        std::atomic<uint64_t> skipped_unsampleable_{0};
        std::atomic<bool> write_failure_logged_{false};
        std::atomic<bool> draining_{false};
        bool handler_registered_ = false;
        bool factory_consumption_hook_installed_ = false;
        bool artisan_consumption_hook_installed_ = false;
        bool factory_sales_hook_installed_ = false;
        bool pop_cash_flow_hook_installed_ = false;
        std::shared_timed_mutex producer_mutex_;
        std::timed_mutex drain_mutex_;
        std::condition_variable_any drain_complete_cv_;
        std::thread drain_thread_;
        bool drain_started_ = false;
        bool drain_complete_ = false;
        SmedleyTelemetryDrainResult drain_result_ = SMEDLEY_TELEMETRY_DRAIN_FAILED;
        std::array<smedley::telemetry::ScheduleState, smedley::telemetry::kMaxCaptureRules> schedule_states_;
        std::array<collectors::FamilyStats, smedley::telemetry::kMaxCaptureRules> family_stats_;
        std::array<std::optional<int>, smedley::telemetry::kMaxCaptureRules> last_family_poll_dates_;
        uint32_t pop_cash_flow_record_count_ = 0;
        PopCashFlowHookStats pop_cash_flow_hook_stats_{};
        std::optional<int> pop_cash_flow_hook_date_;
        std::unique_ptr<std::array<FactorySettlementHookRecord, max_factory_flow_records>> factory_hook_records_;
        uint32_t factory_hook_record_count_ = 0;
        uint64_t factory_hook_dropped_ = 0;
        std::optional<int> factory_hook_date_;
        std::unique_ptr<std::array<FactorySalesHookRecord, max_factory_sales_records>> factory_sales_hook_records_;
        uint32_t factory_sales_hook_record_count_ = 0;
        uint64_t factory_sales_hook_dropped_ = 0;
        std::optional<int> factory_sales_hook_date_;
        std::array<uint32_t, 16> artisan_country_keys_{};
        uint32_t artisan_hook_record_count_ = 0;
        uint64_t artisan_hook_dropped_ = 0;
        std::optional<int> artisan_hook_date_;
        std::optional<int> last_global_date_;
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

namespace
{
    struct TelemetryInstance
    {
        telemetry_plugin::Plugin *plugin = nullptr;
    };

    SmedleyPluginResult SMEDLEY_PLUGIN_CALL CreateTelemetry(void *instance, uint32_t size)
    {
        if (instance == nullptr || size != sizeof(TelemetryInstance)) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
        try {
            static_cast<TelemetryInstance *>(instance)->plugin = new telemetry_plugin::Plugin{};
            return SMEDLEY_PLUGIN_SUCCESS;
        } catch (...) {
            return SMEDLEY_PLUGIN_FAILURE;
        }
    }

    SmedleyPluginResult SMEDLEY_PLUGIN_CALL LoadTelemetry(void *instance)
    {
        auto *state = static_cast<TelemetryInstance *>(instance);
        if (state == nullptr || state->plugin == nullptr) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
        try {
            state->plugin->OnLoad();
            return SMEDLEY_PLUGIN_SUCCESS;
        } catch (...) {
            return SMEDLEY_PLUGIN_FAILURE;
        }
    }

    SmedleyPluginResult SMEDLEY_PLUGIN_CALL UnloadTelemetry(void *instance)
    {
        auto *state = static_cast<TelemetryInstance *>(instance);
        if (state == nullptr || state->plugin == nullptr) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
        try {
            state->plugin->OnUnload();
            return SMEDLEY_PLUGIN_SUCCESS;
        } catch (...) {
            return SMEDLEY_PLUGIN_FAILURE;
        }
    }

    void SMEDLEY_PLUGIN_CALL DestroyTelemetry(void *instance)
    {
        if (instance == nullptr) return;
        auto *state = static_cast<TelemetryInstance *>(instance);
        delete state->plugin;
        state->plugin = nullptr;
    }
}

SMEDLEY_PLUGIN_EXPORT SmedleyPluginResult SMEDLEY_PLUGIN_CALL SmedleyPluginGetApiV1(SmedleyPluginApiV1 *api)
{
    if (api == nullptr || api->struct_size != sizeof(*api) || api->version != SMEDLEY_PLUGIN_ABI_VERSION_V1
        || api->reserved[0] || api->reserved[1] || api->reserved[2] || api->reserved[3]) {
        return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
    }
    api->instance_size = sizeof(TelemetryInstance);
    api->instance_alignment = alignof(TelemetryInstance);
    api->create = &CreateTelemetry;
    api->load = &LoadTelemetry;
    api->unload = &UnloadTelemetry;
    api->destroy = &DestroyTelemetry;
    return SMEDLEY_PLUGIN_SUCCESS;
}
