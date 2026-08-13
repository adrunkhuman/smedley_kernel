#include "world_collector.hpp"

#include "collector_runtime.hpp"
#include "economic_capture.hpp"
#include "telemetry_services.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

namespace telemetry_plugin::collectors
{
    namespace
    {
        SmedleyTelemetryFieldV1 IntField(const char *key, int64_t value)
        {
            SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key,
                static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_INT64, 0, {}};
            field.value.int64_value = value;
            return field;
        }

        SmedleyTelemetryFieldV1 BoolField(const char *key, bool value)
        {
            SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key,
                static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_BOOL, 0, {}};
            field.value.bool_value = value ? 1u : 0u;
            return field;
        }

        bool HasField(const smedley::telemetry::CaptureRule &rule, std::string_view field)
        {
            return rule.fields.empty() || std::find(rule.fields.begin(), rule.fields.end(), field) != rule.fields.end();
        }

        void EmitEconomicSnapshot(CollectorRuntime &runtime, const EconomicSnapshot &snapshot,
                                  const smedley::telemetry::CaptureRule &rule, size_t rule_index)
        {
            using namespace smedley::game_state;
            const SmedleyTelemetryFieldV1 health[] = {
                BoolField("complete", snapshot.complete()), IntField("snapshot_flags", snapshot.snapshot_flags),
                IntField("collection_flags", snapshot.collection_flags), IntField("credit_flags", snapshot.credit_flags),
                IntField("country_count", snapshot.country_count), IntField("state_count", snapshot.state_count),
                IntField("province_count", snapshot.province_count), IntField("pop_count", snapshot.pop_count),
            };
            if (HasField(rule, "health") || !snapshot.complete()) {
                runtime.Account(rule_index, runtime.EmitState("world.economy.health", snapshot.date_raw, nullptr, 0, health, 8, true));
            }
            if (!snapshot.complete()) return;
            const SmedleyTelemetryFieldV1 capacity[] = {
                IntField("country_limit", max_world_countries), IntField("province_limit", max_sample_destination_provinces),
                IntField("pop_limit", max_sample_pops),
                IntField("country_utilization_bp", UtilizationBasisPoints(snapshot.country_count, max_world_countries)),
                IntField("province_utilization_bp", UtilizationBasisPoints(snapshot.province_count, max_sample_destination_provinces)),
                IntField("pop_utilization_bp", UtilizationBasisPoints(snapshot.pop_count, max_sample_pops)),
                IntField("collection_us", static_cast<int64_t>(snapshot.collection_us)),
            };
            if (HasField(rule, "capacity")) {
                runtime.Account(rule_index, runtime.EmitState("world.economy.capacity", snapshot.date_raw, nullptr, 0, capacity, 7, true));
            }
            const SmedleyTelemetryFieldV1 holdings[] = {
                IntField("treasury_observed_raw", snapshot.treasury_observed_raw), IntField("pop_money_observed_raw", snapshot.pop_money_observed_raw),
                IntField("pop_savings_observed_raw", snapshot.pop_savings_observed_raw), IntField("bank_interest_accumulator_raw", snapshot.bank_interest_accumulator_raw),
                IntField("positive_money_pops", snapshot.positive_money_pops), IntField("positive_savings_pops", snapshot.positive_savings_pops),
                IntField("negative_treasury_countries", snapshot.countries_with_negative_treasury),
            };
            if (HasField(rule, "holdings")) {
                runtime.Account(rule_index, runtime.EmitState("world.economy.holdings", snapshot.date_raw, nullptr, 0, holdings, 7, true));
            }
            if (snapshot.credit_flags != 0) return;
            const SmedleyTelemetryFieldV1 credit[] = {
                IntField("creditor_count", snapshot.creditor_count), IntField("creditors_was_paid", snapshot.creditors_was_paid),
                IntField("countries_with_creditors", snapshot.countries_with_creditors),
                IntField("creditor_interest_candidate_raw", snapshot.creditor_interest_candidate_raw),
                IntField("creditor_debt_candidate_raw", snapshot.creditor_debt_candidate_raw),
                IntField("state_savings_candidate_raw", snapshot.state_savings_candidate_raw),
                IntField("state_interest_candidate_raw", snapshot.state_interest_candidate_raw),
            };
            if (HasField(rule, "credit")) {
                runtime.Account(rule_index, runtime.EmitState("world.economy.credit", snapshot.date_raw, nullptr, 0, credit, 7, true));
            }
        }
    }

    struct WorldCollector::Storage { std::array<smedley::game_state::WorldMarketSnapshot, 64> market_snapshots{}; };

    WorldCollector::WorldCollector(EconomicCapture *economic_capture, CollectorRuntime *runtime)
        : economic_capture_(economic_capture), runtime_(runtime), storage_(std::make_unique<Storage>()) {}
    WorldCollector::~WorldCollector() = default;

    uint64_t WorldCollector::Collect(const smedley::game_state::TelemetryCurrentState &game_state, int32_t date_raw)
    {
        uint64_t collection_us = 0;
        size_t rule_index = 0;
        if (const auto *rule = runtime_->DueRule("world.daily", date_raw, &rule_index); rule != nullptr) {
            runtime_->Poll(rule_index);
            runtime_->Attempt(rule_index);
            if (!game_state.world_daily_available()) runtime_->Invalid(rule_index);
            else {
                std::array<SmedleyTelemetryFieldV1, 3> payload;
                uint32_t count = 0;
                if (HasField(*rule, "country_slot_count")) payload[count++] = IntField("country_slot_count", static_cast<int64_t>(game_state.country_count()));
                if (HasField(*rule, "ai_scheduler_entry_count")) payload[count++] = IntField("ai_scheduler_entry_count", static_cast<int64_t>(game_state.country_ai_count()));
                if (HasField(*rule, "human_control_present")) payload[count++] = BoolField("human_control_present", game_state.has_human_controlled_country());
                runtime_->Account(rule_index, runtime_->EmitState("world.daily", date_raw, nullptr, 0, payload.data(), count, true));
            }
        }
        if (const auto *rule = runtime_->DueRule("world.economy", date_raw, &rule_index); rule != nullptr) {
            runtime_->Poll(rule_index);
            runtime_->Attempt(rule_index);
            try {
                const EconomicSnapshot snapshot = economic_capture_->Collect(game_state.game_state, date_raw);
                collection_us += snapshot.collection_us;
                runtime_->CollectionTime(rule_index, snapshot.collection_us);
                EmitEconomicSnapshot(*runtime_, snapshot, *rule, rule_index);
            } catch (...) { runtime_->Invalid(rule_index); }
        }
        if (const auto *rule = runtime_->DueRule("world.military", date_raw, &rule_index); rule != nullptr) {
            runtime_->Poll(rule_index);
            runtime_->Attempt(rule_index);
            int count = 0;
            if (!game_state.ongoing_war_count_candidate(&count)) runtime_->Invalid(rule_index);
            else {
                const auto field = IntField("ongoing_war_count_candidate", count);
                runtime_->Account(rule_index, runtime_->EmitState("world.military", date_raw, nullptr, 0, &field, 1, true));
            }
        }
        if (const auto *rule = runtime_->DueRule("world.market", date_raw, &rule_index); rule != nullptr) {
            runtime_->Poll(rule_index);
            uint32_t market_count = 0;
            if (!smedley::game_state::CollectWorldMarket(game_state.game_state, storage_->market_snapshots.data(), storage_->market_snapshots.size(), &market_count)) {
                runtime_->Invalid(rule_index);
            } else for (uint32_t index = 0; index < market_count; ++index) {
                const auto &snapshot = storage_->market_snapshots[index];
                const auto entity = IntField("good_ordinal", snapshot.good_ordinal);
                if (HasField(*rule, "price")) {
                    const SmedleyTelemetryFieldV1 payload[] = {IntField("price_raw", snapshot.price_raw), IntField("last_price_raw", snapshot.last_price_raw)};
                    runtime_->Attempt(rule_index); runtime_->Account(rule_index, runtime_->EmitState("world.market.price", date_raw, &entity, 1, payload, 2, true));
                }
                if (HasField(*rule, "supply")) {
                    const SmedleyTelemetryFieldV1 payload[] = {IntField("supply_raw", snapshot.supply_raw), IntField("last_supply_raw", snapshot.last_supply_raw), IntField("worldmarket_stock_raw", snapshot.worldmarket_stock_raw)};
                    runtime_->Attempt(rule_index); runtime_->Account(rule_index, runtime_->EmitState("world.market.supply", date_raw, &entity, 1, payload, 3, true));
                }
                if (HasField(*rule, "demand")) {
                    const SmedleyTelemetryFieldV1 payload[] = {IntField("demand_raw", snapshot.demand_raw), IntField("real_demand_raw", snapshot.real_demand_raw)};
                    runtime_->Attempt(rule_index); runtime_->Account(rule_index, runtime_->EmitState("world.market.demand", date_raw, &entity, 1, payload, 2, true));
                }
                if (HasField(*rule, "sales")) {
                    const SmedleyTelemetryFieldV1 payload[] = {IntField("actual_sold_raw", snapshot.actual_sold_raw), IntField("actual_sold_world_raw", snapshot.actual_sold_world_raw)};
                    runtime_->Attempt(rule_index); runtime_->Account(rule_index, runtime_->EmitState("world.market.sales", date_raw, &entity, 1, payload, 2, true));
                }
            }
        }
        return collection_us;
    }
}
