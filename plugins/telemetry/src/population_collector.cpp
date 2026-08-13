#include "population_collector.hpp"

#include "collector_runtime.hpp"
#include "economic_capture.hpp"
#include "pop_cash_flow_core.hpp"
#include "pop_identity_core.hpp"
#include "producer_sales_core.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>

namespace telemetry_plugin::collectors
{
    using services::ArtisanSettlementHookRecord;
    using services::PopCashFlowHookRecord;
    using services::PopCashFlowHookStats;
    constexpr auto max_artisan_flow_records = services::max_artisan_flow_records;
    constexpr auto max_pop_cash_flow_records = services::max_pop_cash_flow_records;
    constexpr auto pop_cash_flow_component_count = services::pop_cash_flow_component_count;

    namespace
    {
        SmedleyTelemetryFieldV1 IntField(const char *key, int64_t value) { SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key, static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_INT64, 0, {}}; field.value.int64_value = value; return field; }
        SmedleyTelemetryFieldV1 BoolField(const char *key, bool value) { SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key, static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_BOOL, 0, {}}; field.value.bool_value = value ? 1u : 0u; return field; }
        SmedleyTelemetryFieldV1 StringField(const char *key, const char *value) { SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key, static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_UTF8_STRING, 0, {}}; field.value.string_value = {value, static_cast<uint32_t>(std::strlen(value)), 0}; return field; }
        SmedleyTelemetryFieldV1 StringField(const char *key, std::string_view value) { SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key, static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_UTF8_STRING, 0, {}}; field.value.string_value = {value.data(), static_cast<uint32_t>(value.size()), 0}; return field; }
        bool HasField(const smedley::telemetry::CaptureRule &rule, std::string_view field) { return rule.fields.empty() || std::find(rule.fields.begin(), rule.fields.end(), field) != rule.fields.end(); }
        bool HasCountryTag(const smedley::telemetry::CaptureRule &rule, std::string_view tag) { return rule.country_tags.empty() || std::find(rule.country_tags.begin(), rule.country_tags.end(), tag) != rule.country_tags.end(); }
        bool HasProvinceId(const smedley::telemetry::CaptureRule &rule, int id) { return rule.province_ids.empty() || std::find(rule.province_ids.begin(), rule.province_ids.end(), id) != rule.province_ids.end(); }

        struct ProducerInventoryState
        {
            int32_t date_raw = 0;
            int32_t good_ordinal = -1;
            int32_t producer_id = -1;
            int32_t province_id = -1;
            uint32_t country_key = 0;
            int64_t closing_inventory_raw = 0;
            bool seen = false;
        };

        struct ArtisanGoodsFlowAggregate
        {
            bool opening_seen = false, pre_add_seen = false, first_seen = false, second_seen = false;
            uint32_t pair_count = 0;
            std::array<int64_t, 64> opening_raw{}, pre_add_raw{}, first_raw{}, second_raw{};
        };

        struct PopCashFlowState
        {
            int32_t date_raw = 0;
            int32_t pop_id = -1;
            int32_t province_id = -1;
            int32_t pop_type_id = -1;
            uint32_t country_key = 0;
            int64_t money_raw = 0;
            bool seen = false;
        };

        struct PopCashFlowAggregate
        {
            std::array<char, 4> country_tag{};
            int32_t pop_type_id = -1;
            int64_t pop_count = 0;
            int64_t opening_pop_count = 0;
            int64_t individual_reconciled_count = 0;
            int64_t individual_unreconciled_count = 0;
            int64_t opening_money_raw = 0;
            int64_t closing_money_raw = 0;
            int64_t money_delta_raw = 0;
            int64_t residual_raw = 0;
            std::array<int64_t, pop_cash_flow_component_count> posted_raw{};
            std::array<int64_t, pop_cash_flow_component_count> component_money_delta_raw{};
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

    }

    struct PopulationCollector::Storage
    {
        Storage(CollectorRuntime *runtime, EconomicCapture *economic_capture) : runtime_(runtime), economic_capture_(economic_capture) {
            size_t index = 0;
            if (runtime_->FindRule("pop.aggregate", &index)) pop_aggregates_ = std::make_unique<std::array<PopAggregate, smedley::game_state::max_sample_pops>>();
            if (runtime_->FindRule("pop.lifecycle", &index)) { pop_identity_current_ = std::make_unique<std::array<PopIdentityState, smedley::game_state::max_sample_pops>>(); pop_identity_previous_ = std::make_unique<std::array<PopIdentityState, smedley::game_state::max_sample_pops>>(); pop_identity_changes_ = std::make_unique<std::array<PopIdentityChange, smedley::game_state::max_sample_pops * 2>>(); }
            if (const auto *rule = runtime_->FindRule("pop.artisan", &index); rule != nullptr && HasField(*rule, "sales")) artisan_inventory_states_ = std::make_unique<std::array<ProducerInventoryState, smedley::game_state::max_sample_pops>>();
            if (runtime_->FindRule("pop.cashflow", &index) || runtime_->FindRule("pop.cashflow.aggregate", &index)) { pop_cash_flow_states_ = std::make_unique<std::array<PopCashFlowState, smedley::game_state::max_sample_pops>>(); pop_cash_flow_aggregates_ = std::make_unique<std::array<PopCashFlowAggregate, smedley::game_state::max_sample_pops * 2>>(); pop_cash_flow_previous_aggregates_ = std::make_unique<std::array<PopCashFlowAggregate, smedley::game_state::max_sample_pops>>(); }
        }
        smedley::telemetry::PublicationResult EmitTyped(const char *event, const char *, int32_t date,
                                                        const SmedleyTelemetryFieldV1 *entities, uint32_t entity_count,
                                                        const SmedleyTelemetryFieldV1 *payload, uint32_t payload_count,
                                                        bool, bool = false)
        {
            const auto *family = smedley::telemetry::FindMetricFamilyForEvent(event);
            size_t rule_index = 0;
            if (family == nullptr || runtime_->FindRule(family->id, &rule_index) == nullptr) {
                return {SMEDLEY_TELEMETRY_INVALID, 0};
            }
            return runtime_->EmitFamilyState(rule_index, event, date, entities, entity_count, payload,
                                             payload_count);
        }
        CollectorRuntime *runtime_;
        EconomicCapture *economic_capture_;
        std::unique_ptr<std::array<PopAggregate, smedley::game_state::max_sample_pops>> pop_aggregates_;
        std::unique_ptr<std::array<PopIdentityState, smedley::game_state::max_sample_pops>> pop_identity_current_;
        std::unique_ptr<std::array<PopIdentityState, smedley::game_state::max_sample_pops>> pop_identity_previous_;
        std::unique_ptr<std::array<PopIdentityChange, smedley::game_state::max_sample_pops * 2>> pop_identity_changes_;
        size_t pop_identity_previous_count_ = 0;
        std::optional<int> pop_identity_previous_date_;
        std::unique_ptr<std::array<PopCashFlowHookRecord, max_pop_cash_flow_records>> pop_cash_flow_records_;
        std::unique_ptr<std::array<PopCashFlowState, smedley::game_state::max_sample_pops>> pop_cash_flow_states_;
        std::unique_ptr<std::array<PopCashFlowAggregate, smedley::game_state::max_sample_pops * 2>>
            pop_cash_flow_aggregates_;
        std::unique_ptr<std::array<PopCashFlowAggregate, smedley::game_state::max_sample_pops>>
            pop_cash_flow_previous_aggregates_;
        std::array<PopCashFlowAggregate, max_world_countries> pop_cash_flow_country_aggregates_{};
        size_t pop_cash_flow_previous_aggregate_count_ = 0;
        std::optional<int> pop_cash_flow_previous_aggregate_date_;
        uint32_t pop_cash_flow_record_count_ = 0;
        PopCashFlowHookStats pop_cash_flow_hook_stats_{};
        std::optional<int> pop_cash_flow_hook_date_;
        std::unique_ptr<std::array<ProducerInventoryState, smedley::game_state::max_sample_pops>> artisan_inventory_states_;
        std::unique_ptr<std::array<ArtisanSettlementHookRecord, max_artisan_flow_records>> artisan_hook_records_;
        uint32_t artisan_hook_record_count_ = 0;
        uint64_t artisan_hook_dropped_ = 0;
        std::optional<int> artisan_hook_date_;
        void Reset() { if (artisan_inventory_states_) artisan_inventory_states_->fill({}); if (pop_cash_flow_states_) pop_cash_flow_states_->fill({}); pop_cash_flow_previous_aggregate_count_ = 0; pop_cash_flow_previous_aggregate_date_.reset(); }

        void CollectArtisanFlowAggregate(smedley::game_state::PopRef pop, size_t rule_index, ArtisanGoodsFlowAggregate *aggregate)
        {
            *aggregate = {};
            if (!artisan_hook_records_) return;
            const auto first = artisan_hook_records_->begin();
            const auto end = first + artisan_hook_record_count_;
            const uint64_t address = pop.address();
            auto record = std::lower_bound(first, end, address,
                [](const ArtisanSettlementHookRecord &candidate, uint64_t target) {
                    return candidate.pop.address() < target;
                });
            while (record != end && record->pop.address() == pop.address()) {
                if (record->pool > 3) {
                    ++runtime_->Stats(rule_index).invalid;
                    ++record;
                    continue;
                }
                auto &target = record->pool == 0 ? aggregate->opening_raw
                    : record->pool == 1 ? aggregate->pre_add_raw
                    : record->pool == 2 ? aggregate->first_raw : aggregate->second_raw;
                auto candidate = target;
                bool overflow = false;
                for (size_t good = 0; good < candidate.size(); ++good) {
                    const int64_t amount = record->quantity_raw[good];
                    if ((amount > 0 && candidate[good] > (std::numeric_limits<int64_t>::max)() - amount)
                        || (amount < 0 && candidate[good] < (std::numeric_limits<int64_t>::min)() - amount)) {
                        overflow = true;
                        break;
                    }
                    candidate[good] += amount;
                }
                if (overflow) ++runtime_->Stats(rule_index).invalid;
                else {
                    target = candidate;
                    if (record->pool == 0) aggregate->opening_seen = true;
                    else if (record->pool == 1) aggregate->pre_add_seen = true;
                    else if (record->pool == 2) aggregate->first_seen = true;
                    else if (record->pool == 3) {
                        aggregate->second_seen = true;
                        ++aggregate->pair_count;
                    }
                }
                ++record;
            }
        }

        ProducerInventoryState *ArtisanInventoryStateFor(int32_t pop_id, int32_t date_raw)
        {
            if (!artisan_inventory_states_ || pop_id < 0) return nullptr;
            size_t slot = static_cast<uint32_t>(pop_id) * 2654435761u % artisan_inventory_states_->size();
            ProducerInventoryState *stale = nullptr;
            for (size_t attempt = 0; attempt < artisan_inventory_states_->size(); ++attempt) {
                auto &state = (*artisan_inventory_states_)[slot];
                if (state.seen && state.producer_id == pop_id) return &state;
                if (!state.seen) return stale == nullptr ? &state : stale;
                if (static_cast<int64_t>(state.date_raw) + 24 < date_raw && stale == nullptr) stale = &state;
                slot = (slot + 1) % artisan_inventory_states_->size();
            }
            return stale;
        }

        PopCashFlowState *PopCashFlowStateFor(int32_t pop_id, int32_t date_raw)
        {
            if (!pop_cash_flow_states_ || pop_id < 0) return nullptr;
            size_t slot = static_cast<uint32_t>(pop_id) * 2654435761u % pop_cash_flow_states_->size();
            PopCashFlowState *stale = nullptr;
            for (size_t attempt = 0; attempt < pop_cash_flow_states_->size(); ++attempt) {
                auto &state = (*pop_cash_flow_states_)[slot];
                if (state.seen && state.pop_id == pop_id) return &state;
                if (!state.seen) return stale == nullptr ? &state : stale;
                if (static_cast<int64_t>(state.date_raw) + 24 < date_raw && stale == nullptr) stale = &state;
                slot = (slot + 1) % pop_cash_flow_states_->size();
            }
            return stale;
        }

        static bool AddCashFlowValue(int64_t value, int64_t *sum)
        {
            if ((value > 0 && *sum > (std::numeric_limits<int64_t>::max)() - value)
                || (value < 0 && *sum < (std::numeric_limits<int64_t>::min)() - value)) return false;
            *sum += value;
            return true;
        }

        static bool SubtractCashFlowValue(int64_t left, int64_t right, int64_t *difference)
        {
            if ((right > 0 && left < (std::numeric_limits<int64_t>::min)() + right)
                || (right < 0 && left > (std::numeric_limits<int64_t>::max)() + right)) return false;
            *difference = left - right;
            return true;
        }

// MSVC's x86 optimizer exhausts its 32-bit heap on these field-heavy emitters.
#pragma optimize("", off)
        __declspec(noinline) uint64_t EmitPopCashFlows(
            const smedley::game_state::TelemetryCurrentState &game_state, int32_t date_raw,
            const smedley::telemetry::CaptureRule *detail_rule, size_t detail_rule_index,
            const smedley::telemetry::CaptureRule *aggregate_rule, size_t aggregate_rule_index)
        {
            const bool detail_due = detail_rule != nullptr;
            const bool aggregate_due = aggregate_rule != nullptr;

            const PopulationCapture capture = economic_capture_->CollectPopulation(game_state.game_state, date_raw);
            if (detail_due) {
                ++runtime_->Stats(detail_rule_index).polls_due;
                runtime_->Stats(detail_rule_index).collection_us += capture.collection_us;
            }
            if (aggregate_due) {
                ++runtime_->Stats(aggregate_rule_index).polls_due;
                runtime_->Stats(aggregate_rule_index).collection_us += capture.collection_us;
            }
            if (!capture.complete()) {
                if (detail_due) ++runtime_->Stats(detail_rule_index).invalid;
                if (aggregate_due) ++runtime_->Stats(aggregate_rule_index).invalid;
                return capture.collection_us;
            }
            const bool capture_complete = pop_cash_flow_hook_stats_.complete();
            if (!capture_complete) {
                if (detail_due) ++runtime_->Stats(detail_rule_index).invalid;
                if (aggregate_due) ++runtime_->Stats(aggregate_rule_index).invalid;
            }

            size_t aggregate_count = 0;
            for (uint32_t index = 0; index < capture.pop_count; ++index) {
                const auto &candidate = economic_capture_->population_candidate(index);
                const auto &detail = economic_capture_->population_detail(index);
                smedley::game_state::TelemetryProvinceSnapshot province_snapshot{};
                const auto *province = smedley::game_state::ReadTelemetryProvince(
                    smedley::game_state::ResolveProvince(game_state.game_state, detail.province_id_candidate), &province_snapshot)
                    ? &province_snapshot : nullptr;
                if (province == nullptr || !province->owner_candidate().normalized_candidate()) {
                    if (detail_due && HasProvinceId(*detail_rule, detail.province_id_candidate)) {
                        ++runtime_->Stats(detail_rule_index).invalid;
                    }
                    if (aggregate_due) ++runtime_->Stats(aggregate_rule_index).invalid;
                    continue;
                }
                const std::string_view country_tag(province->owner_candidate().str(), 3);
                uint32_t country_key = 0;
                std::memcpy(&country_key, country_tag.data(), 3);
                auto *state = PopCashFlowStateFor(detail.pop_id, date_raw);
                if (state == nullptr) {
                    if (detail_due) ++runtime_->Stats(detail_rule_index).invalid;
                    if (aggregate_due) ++runtime_->Stats(aggregate_rule_index).invalid;
                    continue;
                }
                const bool opening_seen = state->seen && state->date_raw + 24 == date_raw
                    && state->province_id == detail.province_id_candidate
                    && state->pop_type_id == detail.pop_type_id_candidate
                    && state->country_key == country_key;
                const auto record_it = std::lower_bound(pop_cash_flow_records_->begin(),
                    pop_cash_flow_records_->begin() + pop_cash_flow_record_count_, candidate.address.address(),
                    [](const PopCashFlowHookRecord &record, uint64_t address) {
                        return record.pop.address() < address;
                    });
                const PopCashFlowHookRecord *record = record_it != pop_cash_flow_records_->begin()
                    + pop_cash_flow_record_count_ && record_it->pop.address() == candidate.address.address() ? &*record_it : nullptr;
                std::array<int64_t, pop_cash_flow_component_count> posted{}, component_delta{};
                uint32_t call_count = 0;
                if (record != nullptr) {
                    posted = record->posted_raw;
                    component_delta = record->money_delta_raw;
                    call_count = record->call_count;
                }
                int64_t posted_total = 0, money_delta = 0;
                bool math_valid = true;
                for (size_t component = 0; component < pop_cash_flow_component_count; ++component) {
                    math_valid = math_valid && AddCashFlowValue(posted[component], &posted_total)
                        && AddCashFlowValue(component_delta[component], &money_delta);
                }
                int64_t observed_delta = 0, residual = 0;
                if (opening_seen) {
                    math_valid = math_valid
                        && SubtractCashFlowValue(detail.economy.money_raw, state->money_raw, &observed_delta)
                        && SubtractCashFlowValue(observed_delta, money_delta, &residual);
                }
                const bool reconciled = opening_seen && capture_complete && math_valid && residual == 0;

                const bool detail_selected = detail_due
                    && HasProvinceId(*detail_rule, detail.province_id_candidate)
                    && HasCountryTag(*detail_rule, country_tag);
                if (detail_selected) {
                    const bool reliable = (!detail_rule->country_tags.empty()
                            && detail_rule->country_tags.size() <= 16)
                        || (!detail_rule->province_ids.empty() && detail_rule->province_ids.size() <= 16);
                    const SmedleyTelemetryFieldV1 entities[] = {
                        StringField("country_tag", country_tag),
                        IntField("province_id", detail.province_id_candidate),
                        IntField("pop_type_id_candidate", detail.pop_type_id_candidate),
                        IntField("pop_id", detail.pop_id),
                    };
                    if (HasField(*detail_rule, "summary")) {
                        const SmedleyTelemetryFieldV1 payload[] = {
                            BoolField("opening_money_seen", opening_seen),
                            BoolField("capture_complete", capture_complete),
                            BoolField("reconciled", reconciled),
                            IntField("call_count", call_count),
                        };
                        ++runtime_->Stats(detail_rule_index).collection_attempts;
                        runtime_->Account(detail_rule_index, EmitTyped("pop.cashflow.summary", "state", date_raw,
                            entities, 4, payload, 4, false, reliable));
                    }
                    if (opening_seen && math_valid && HasField(*detail_rule, "account")) {
                        const SmedleyTelemetryFieldV1 payload[] = {
                            IntField("opening_money_raw", state->money_raw),
                            IntField("closing_money_raw", detail.economy.money_raw),
                            IntField("money_delta_raw", money_delta),
                            IntField("residual_raw", residual),
                        };
                        ++runtime_->Stats(detail_rule_index).collection_attempts;
                        runtime_->Account(detail_rule_index, EmitTyped("pop.cashflow.account", "state", date_raw,
                            entities, 4, payload, 4, false, reliable));
                    }
                    if (HasField(*detail_rule, "components")) {
                        for (size_t component = 0; component < pop_cash_flow_component_count; ++component) {
                            if (posted[component] == 0 && component_delta[component] == 0) continue;
                            const SmedleyTelemetryFieldV1 component_entities[] = {
                                entities[0], entities[1], entities[2], entities[3],
                                IntField("cash_flow_index", component),
                                StringField("component", PopCashFlowName(component)),
                            };
                            const SmedleyTelemetryFieldV1 payload[] = {
                                IntField("posted_raw", posted[component]),
                                IntField("money_delta_raw", component_delta[component]),
                            };
                            ++runtime_->Stats(detail_rule_index).collection_attempts;
                            runtime_->Account(detail_rule_index, EmitTyped("pop.cashflow.component", "state", date_raw,
                                component_entities, 6, payload, 2, false, reliable));
                        }
                    }
                    if (opening_seen && capture_complete && !math_valid) {
                        ++runtime_->Stats(detail_rule_index).invalid;
                    }
                }

                if (aggregate_due && HasCountryTag(*aggregate_rule, country_tag)) {
                    if (aggregate_count >= pop_cash_flow_aggregates_->size()) {
                        ++runtime_->Stats(aggregate_rule_index).invalid;
                    } else {
                        auto &aggregate = (*pop_cash_flow_aggregates_)[aggregate_count++];
                        aggregate = {};
                        std::memcpy(aggregate.country_tag.data(), country_tag.data(), 3);
                        aggregate.pop_type_id = detail.pop_type_id_candidate;
                        aggregate.pop_count = 1;
                        aggregate.individual_reconciled_count = reconciled ? 1 : 0;
                        aggregate.individual_unreconciled_count = reconciled ? 0 : 1;
                        aggregate.closing_money_raw = detail.economy.money_raw;
                        aggregate.money_delta_raw = money_delta;
                        aggregate.posted_raw = posted;
                        aggregate.component_money_delta_raw = component_delta;
                    }
                }

                state->date_raw = date_raw;
                state->pop_id = detail.pop_id;
                state->province_id = detail.province_id_candidate;
                state->pop_type_id = detail.pop_type_id_candidate;
                state->country_key = country_key;
                state->money_raw = detail.economy.money_raw;
                state->seen = true;
            }

            if (!aggregate_due) return capture.collection_us;
            const bool previous_aggregate_seen = pop_cash_flow_previous_aggregate_date_
                && *pop_cash_flow_previous_aggregate_date_ + 24 == date_raw;
            if (previous_aggregate_seen) {
                for (size_t index = 0; index < pop_cash_flow_previous_aggregate_count_; ++index) {
                    if (aggregate_count >= pop_cash_flow_aggregates_->size()) {
                        ++runtime_->Stats(aggregate_rule_index).invalid;
                        return capture.collection_us;
                    }
                    const auto &previous = (*pop_cash_flow_previous_aggregates_)[index];
                    auto &empty_current = (*pop_cash_flow_aggregates_)[aggregate_count++];
                    empty_current = {};
                    empty_current.country_tag = previous.country_tag;
                    empty_current.pop_type_id = previous.pop_type_id;
                }
            }
            std::sort(pop_cash_flow_aggregates_->begin(), pop_cash_flow_aggregates_->begin() + aggregate_count,
                [](const PopCashFlowAggregate &left, const PopCashFlowAggregate &right) {
                    const int country = std::memcmp(left.country_tag.data(), right.country_tag.data(), 3);
                    return country != 0 ? country < 0 : left.pop_type_id < right.pop_type_id;
                });
            size_t merged_count = 0;
            for (size_t index = 0; index < aggregate_count; ++index) {
                const auto &next = (*pop_cash_flow_aggregates_)[index];
                if (merged_count == 0
                    || std::memcmp((*pop_cash_flow_aggregates_)[merged_count - 1].country_tag.data(),
                        next.country_tag.data(), 3) != 0
                    || (*pop_cash_flow_aggregates_)[merged_count - 1].pop_type_id != next.pop_type_id) {
                    (*pop_cash_flow_aggregates_)[merged_count++] = next;
                    continue;
                }
                auto &current = (*pop_cash_flow_aggregates_)[merged_count - 1];
                bool valid = AddCashFlowValue(next.pop_count, &current.pop_count)
                    && AddCashFlowValue(next.individual_reconciled_count, &current.individual_reconciled_count)
                    && AddCashFlowValue(next.individual_unreconciled_count, &current.individual_unreconciled_count)
                    && AddCashFlowValue(next.closing_money_raw, &current.closing_money_raw)
                    && AddCashFlowValue(next.money_delta_raw, &current.money_delta_raw);
                for (size_t component = 0; valid && component < pop_cash_flow_component_count; ++component) {
                    valid = AddCashFlowValue(next.posted_raw[component], &current.posted_raw[component])
                        && AddCashFlowValue(next.component_money_delta_raw[component],
                            &current.component_money_delta_raw[component]);
                }
                if (!valid) {
                    ++runtime_->Stats(aggregate_rule_index).invalid;
                    return capture.collection_us;
                }
            }
            for (size_t index = 0; index < merged_count; ++index) {
                auto &aggregate = (*pop_cash_flow_aggregates_)[index];
                if (!previous_aggregate_seen) continue;
                const auto previous = std::lower_bound(pop_cash_flow_previous_aggregates_->begin(),
                    pop_cash_flow_previous_aggregates_->begin() + pop_cash_flow_previous_aggregate_count_, aggregate,
                    [](const PopCashFlowAggregate &left, const PopCashFlowAggregate &right) {
                        const int country = std::memcmp(left.country_tag.data(), right.country_tag.data(), 3);
                        return country != 0 ? country < 0 : left.pop_type_id < right.pop_type_id;
                    });
                if (previous == pop_cash_flow_previous_aggregates_->begin()
                        + pop_cash_flow_previous_aggregate_count_
                    || std::memcmp(previous->country_tag.data(), aggregate.country_tag.data(), 3) != 0
                    || previous->pop_type_id != aggregate.pop_type_id) continue;
                aggregate.opening_pop_count = previous->pop_count;
                aggregate.opening_money_raw = previous->closing_money_raw;
                int64_t observed_delta = 0;
                if (!SubtractCashFlowValue(aggregate.closing_money_raw, aggregate.opening_money_raw, &observed_delta)
                    || !SubtractCashFlowValue(observed_delta, aggregate.money_delta_raw, &aggregate.residual_raw)) {
                    ++runtime_->Stats(aggregate_rule_index).invalid;
                }
            }
            const bool reliable = aggregate_rule->country_tags.empty()
                || aggregate_rule->country_tags.size() <= 16;
            for (size_t index = 0; index < merged_count; ++index) {
                const auto &aggregate = (*pop_cash_flow_aggregates_)[index];
                const SmedleyTelemetryFieldV1 entities[] = {
                    StringField("country_tag", std::string_view(aggregate.country_tag.data(), 3)),
                    IntField("pop_type_id_candidate", aggregate.pop_type_id),
                };
                const bool opening_seen = previous_aggregate_seen && aggregate.opening_pop_count != 0;
                const bool aggregate_reconciled = opening_seen && capture_complete && aggregate.residual_raw == 0;
                if (HasField(*aggregate_rule, "summary")) {
                    const SmedleyTelemetryFieldV1 payload[] = {
                        IntField("opening_pop_count", aggregate.opening_pop_count),
                        IntField("closing_pop_count", aggregate.pop_count),
                        BoolField("opening_money_seen", opening_seen),
                        BoolField("reconciled", aggregate_reconciled),
                    };
                    ++runtime_->Stats(aggregate_rule_index).collection_attempts;
                    runtime_->Account(aggregate_rule_index, EmitTyped("pop.cashflow.aggregate.summary", "state",
                        date_raw, entities, 2, payload, 4, false, reliable));
                }
                if (opening_seen && HasField(*aggregate_rule, "account")) {
                    const SmedleyTelemetryFieldV1 payload[] = {
                        IntField("opening_money_raw", aggregate.opening_money_raw),
                        IntField("closing_money_raw", aggregate.closing_money_raw),
                        IntField("money_delta_raw", aggregate.money_delta_raw),
                        IntField("residual_raw", aggregate.residual_raw),
                    };
                    ++runtime_->Stats(aggregate_rule_index).collection_attempts;
                    runtime_->Account(aggregate_rule_index, EmitTyped("pop.cashflow.aggregate.account", "state",
                        date_raw, entities, 2, payload, 4, false, reliable));
                }
                if (HasField(*aggregate_rule, "components")) {
                    for (size_t component = 0; component < pop_cash_flow_component_count; ++component) {
                        if (aggregate.posted_raw[component] == 0
                            && aggregate.component_money_delta_raw[component] == 0) continue;
                        const SmedleyTelemetryFieldV1 component_entities[] = {
                            entities[0], entities[1], IntField("cash_flow_index", component),
                            StringField("component", PopCashFlowName(component)),
                        };
                        const SmedleyTelemetryFieldV1 payload[] = {
                            IntField("posted_raw", aggregate.posted_raw[component]),
                            IntField("money_delta_raw", aggregate.component_money_delta_raw[component]),
                        };
                        ++runtime_->Stats(aggregate_rule_index).collection_attempts;
                        runtime_->Account(aggregate_rule_index, EmitTyped("pop.cashflow.aggregate.component", "state",
                            date_raw, component_entities, 4, payload, 2, false, reliable));
                    }
                }
            }
            size_t country_count = 0;
            for (size_t index = 0; index < merged_count; ++index) {
                const auto &type = (*pop_cash_flow_aggregates_)[index];
                if (country_count == 0
                    || std::memcmp(pop_cash_flow_country_aggregates_[country_count - 1].country_tag.data(),
                        type.country_tag.data(), 3) != 0) {
                    pop_cash_flow_country_aggregates_[country_count] = {};
                    pop_cash_flow_country_aggregates_[country_count].country_tag = type.country_tag;
                    ++country_count;
                }
                auto &country = pop_cash_flow_country_aggregates_[country_count - 1];
                bool valid = AddCashFlowValue(type.pop_count, &country.pop_count)
                    && AddCashFlowValue(type.opening_pop_count, &country.opening_pop_count)
                    && AddCashFlowValue(type.individual_reconciled_count, &country.individual_reconciled_count)
                    && AddCashFlowValue(type.individual_unreconciled_count, &country.individual_unreconciled_count)
                    && AddCashFlowValue(type.opening_money_raw, &country.opening_money_raw)
                    && AddCashFlowValue(type.closing_money_raw, &country.closing_money_raw)
                    && AddCashFlowValue(type.money_delta_raw, &country.money_delta_raw)
                    && AddCashFlowValue(type.residual_raw, &country.residual_raw);
                for (size_t component = 0; valid && component < pop_cash_flow_component_count; ++component) {
                    valid = AddCashFlowValue(type.posted_raw[component], &country.posted_raw[component])
                        && AddCashFlowValue(type.component_money_delta_raw[component],
                            &country.component_money_delta_raw[component]);
                }
                if (!valid) {
                    ++runtime_->Stats(aggregate_rule_index).invalid;
                    return capture.collection_us;
                }
            }
            for (size_t index = 0; index < country_count; ++index) {
                const auto &aggregate = pop_cash_flow_country_aggregates_[index];
                const auto country_entity = StringField(
                    "country_tag", std::string_view(aggregate.country_tag.data(), 3));
                const bool opening_seen = previous_aggregate_seen && aggregate.opening_pop_count != 0;
                const bool reconciled = opening_seen && capture_complete && aggregate.residual_raw == 0;
                if (HasField(*aggregate_rule, "summary")) {
                    const SmedleyTelemetryFieldV1 payload[] = {
                        IntField("opening_pop_count", aggregate.opening_pop_count),
                        IntField("closing_pop_count", aggregate.pop_count),
                        BoolField("opening_money_seen", opening_seen),
                        BoolField("reconciled", reconciled),
                    };
                    ++runtime_->Stats(aggregate_rule_index).collection_attempts;
                    runtime_->Account(aggregate_rule_index, EmitTyped("pop.cashflow.country.summary", "state",
                        date_raw, &country_entity, 1, payload, 4, false, reliable));
                }
                if (opening_seen && HasField(*aggregate_rule, "account")) {
                    const SmedleyTelemetryFieldV1 payload[] = {
                        IntField("opening_money_raw", aggregate.opening_money_raw),
                        IntField("closing_money_raw", aggregate.closing_money_raw),
                        IntField("money_delta_raw", aggregate.money_delta_raw),
                        IntField("residual_raw", aggregate.residual_raw),
                    };
                    ++runtime_->Stats(aggregate_rule_index).collection_attempts;
                    runtime_->Account(aggregate_rule_index, EmitTyped("pop.cashflow.country.account", "state",
                        date_raw, &country_entity, 1, payload, 4, false, reliable));
                }
                if (HasField(*aggregate_rule, "components")) {
                    for (size_t component = 0; component < pop_cash_flow_component_count; ++component) {
                        if (aggregate.posted_raw[component] == 0
                            && aggregate.component_money_delta_raw[component] == 0) continue;
                        const SmedleyTelemetryFieldV1 component_entities[] = {
                            country_entity, IntField("cash_flow_index", component),
                            StringField("component", PopCashFlowName(component)),
                        };
                        const SmedleyTelemetryFieldV1 payload[] = {
                            IntField("posted_raw", aggregate.posted_raw[component]),
                            IntField("money_delta_raw", aggregate.component_money_delta_raw[component]),
                        };
                        ++runtime_->Stats(aggregate_rule_index).collection_attempts;
                        runtime_->Account(aggregate_rule_index, EmitTyped("pop.cashflow.country.component", "state",
                            date_raw, component_entities, 3, payload, 2, false, reliable));
                    }
                }
            }
            pop_cash_flow_previous_aggregate_count_ = 0;
            for (size_t index = 0; index < merged_count; ++index) {
                if ((*pop_cash_flow_aggregates_)[index].pop_count == 0) continue;
                (*pop_cash_flow_previous_aggregates_)[pop_cash_flow_previous_aggregate_count_++]
                    = (*pop_cash_flow_aggregates_)[index];
            }
            pop_cash_flow_previous_aggregate_date_ = date_raw;
            return capture.collection_us;
        }

        __declspec(noinline) void EmitArtisanGroups(int32_t date_raw,
                                                    const smedley::telemetry::CaptureRule &rule,
                                                     size_t rule_index,
                                                     const char *country_tag,
                                                     int32_t province_id,
                                                      const smedley::game_state::ArtisanSnapshot &artisan,
                                                     const smedley::game_state::ArtisanInputSnapshot *inputs,
                                                    uint32_t input_count,
                                                     const ArtisanGoodsFlowAggregate &flow,
                                                    bool reliable)
        {
            const SmedleyTelemetryFieldV1 entities[] = {
                StringField("country_tag", country_tag), IntField("province_id", province_id),
                IntField("pop_id", artisan.pop_id),
            };
            if (HasField(rule, "identity")) {
                const SmedleyTelemetryFieldV1 payload[] = {
                    StringField("production_type", artisan.production_type),
                    IntField("output_good_ordinal", artisan.output_good_ordinal),
                    StringField("output_good", artisan.output_good),
                };
                ++runtime_->Stats(rule_index).collection_attempts;
                runtime_->Account(rule_index, EmitTyped("pop.artisan.identity", "state", date_raw,
                    entities, 3, payload, 3, false, reliable));
            }
            if (HasField(rule, "production")) {
                const SmedleyTelemetryFieldV1 payload[] = {
                    IntField("base_output_raw", artisan.base_output_raw),
                    IntField("current_producing_raw", artisan.current_producing_raw),
                    IntField("gross_output_raw", artisan.gross_output_raw),
                };
                ++runtime_->Stats(rule_index).collection_attempts;
                runtime_->Account(rule_index, EmitTyped("pop.artisan.production", "state", date_raw,
                    entities, 3, payload, 3, false, reliable));
            }
            if (HasField(rule, "inputs")) {
                for (uint32_t index = 0; index < input_count; ++index) {
                    const SmedleyTelemetryFieldV1 input_entities[] = {
                        entities[0], entities[1], entities[2], IntField("good_ordinal", inputs[index].good_ordinal),
                    };
                    const SmedleyTelemetryFieldV1 payload[] = {
                        IntField("stockpile_raw", inputs[index].stockpile_raw),
                        IntField("need_raw", inputs[index].need_raw),
                    };
                    ++runtime_->Stats(rule_index).collection_attempts;
                    runtime_->Account(rule_index, EmitTyped("pop.artisan.input", "state", date_raw,
                        input_entities, 4, payload, 2, false, reliable));
                }
            }
            if (HasField(rule, "finance")) {
                const SmedleyTelemetryFieldV1 payload[] = {
                    IntField("last_spending_raw", artisan.last_spending_raw),
                    IntField("needs_cost_raw", artisan.needs_cost_raw),
                    IntField("production_income_raw", artisan.production_income_raw),
                    IntField("percent_afforded_raw", artisan.percent_afforded_raw),
                    IntField("throttle_raw", artisan.throttle_raw),
                };
                ++runtime_->Stats(rule_index).collection_attempts;
                runtime_->Account(rule_index, EmitTyped("pop.artisan.finance", "state", date_raw,
                    entities, 3, payload, 5, false, reliable));
            }
            if (HasField(rule, "sales")) {
                auto *previous = ArtisanInventoryStateFor(artisan.pop_id, date_raw);
                if (previous == nullptr) {
                    ++runtime_->Stats(rule_index).invalid;
                } else {
                    uint32_t country_key = 0;
                    std::memcpy(&country_key, country_tag, 3);
                    ProducerSale sale{};
                    const bool boundary_complete = previous->seen && previous->date_raw + 24 == date_raw
                        && previous->good_ordinal == artisan.output_good_ordinal
                        && previous->province_id == province_id && previous->country_key == country_key;
                    const bool complete = boundary_complete
                        && ReconcileProducerSale(previous->closing_inventory_raw, artisan.gross_output_raw,
                            artisan.leftover_raw, artisan.production_income_raw, &sale);
                    const SmedleyTelemetryFieldV1 summary_payload[] = {
                        BoolField("settlement_seen", true),
                        BoolField("opening_inventory_seen", boundary_complete),
                        BoolField("complete", complete),
                    };
                    ++runtime_->Stats(rule_index).collection_attempts;
                    runtime_->Account(rule_index, EmitTyped("pop.artisan.sales.summary", "state", date_raw,
                        entities, 3, summary_payload, 3, false, reliable));
                    if (boundary_complete && !complete) ++runtime_->Stats(rule_index).invalid;
                    if (complete) {
                        const SmedleyTelemetryFieldV1 quantity_payload[] = {
                            IntField("output_good_ordinal", artisan.output_good_ordinal),
                            IntField("opening_inventory_raw", sale.opening_inventory_raw),
                            IntField("produced_raw", sale.produced_raw),
                            IntField("sold_raw", sale.sold_raw),
                            IntField("closing_inventory_raw", sale.closing_inventory_raw),
                        };
                        ++runtime_->Stats(rule_index).collection_attempts;
                        runtime_->Account(rule_index, EmitTyped("pop.artisan.sales.quantity", "state", date_raw,
                            entities, 3, quantity_payload, 5, false, reliable));
                        const SmedleyTelemetryFieldV1 revenue_payload[] = {
                            IntField("proceeds_raw", sale.proceeds_raw),
                            IntField("percent_sold_domestic_raw", artisan.percent_sold_domestic_raw),
                            IntField("percent_sold_export_raw", artisan.percent_sold_export_raw),
                        };
                        ++runtime_->Stats(rule_index).collection_attempts;
                        runtime_->Account(rule_index, EmitTyped("pop.artisan.sales.revenue", "state", date_raw,
                            entities, 3, revenue_payload, 3, false, reliable));
                    }
                    previous->date_raw = date_raw;
                    previous->good_ordinal = artisan.output_good_ordinal;
                    previous->producer_id = artisan.pop_id;
                    previous->province_id = province_id;
                    previous->country_key = country_key;
                    previous->closing_inventory_raw = artisan.leftover_raw;
                    previous->seen = true;
                }
            }
            if (HasField(rule, "flows")) {
                const SmedleyTelemetryFieldV1 summary_payload[] = {
                    BoolField("post_consumption_seen", flow.opening_seen),
                    BoolField("pre_purchase_seen", flow.pre_add_seen),
                    BoolField("primary_delivery_seen", flow.first_seen),
                    BoolField("secondary_delivery_seen", flow.second_seen),
                    IntField("settlement_count", flow.pair_count),
                };
                ++runtime_->Stats(rule_index).collection_attempts;
                runtime_->Account(rule_index, EmitTyped("pop.artisan.input.flow.summary", "state", date_raw,
                    entities, 3, summary_payload, 5, false, reliable));
                for (size_t good = 0; good < flow.first_raw.size(); ++good) {
                    if (flow.opening_raw[good] == 0 && flow.pre_add_raw[good] == 0
                        && flow.first_raw[good] == 0 && flow.second_raw[good] == 0) continue;
                    const SmedleyTelemetryFieldV1 flow_entities[] = {
                        entities[0], entities[1], entities[2], IntField("good_ordinal", good),
                    };
                    const SmedleyTelemetryFieldV1 payload[] = {
                        IntField("post_consumption_raw", flow.opening_raw[good]),
                        IntField("pre_purchase_raw", flow.pre_add_raw[good]),
                        IntField("delivered_primary_raw", flow.first_raw[good]),
                        IntField("delivered_secondary_raw", flow.second_raw[good]),
                    };
                    ++runtime_->Stats(rule_index).collection_attempts;
                    runtime_->Account(rule_index, EmitTyped("pop.artisan.input.flow", "state", date_raw,
                        flow_entities, 4, payload, 4, false, reliable));
                }
            }
        }

        uint64_t EmitPopLifecycle(const smedley::game_state::TelemetryCurrentState &game_state, int32_t date_raw,
                                  const smedley::telemetry::CaptureRule &rule, size_t rule_index)
        {
            ++runtime_->Stats(rule_index).polls_due;
            const PopulationCapture capture = economic_capture_->CollectPopulation(game_state.game_state, date_raw);
            runtime_->Stats(rule_index).collection_us += capture.collection_us;
            if (!capture.complete() || !pop_identity_current_ || !pop_identity_previous_ || !pop_identity_changes_) {
                ++runtime_->Stats(rule_index).invalid;
                pop_identity_previous_count_ = 0;
                pop_identity_previous_date_.reset();
                return capture.collection_us;
            }
            const size_t current_count = capture.pop_count;
            bool identity_complete = true;
            for (size_t index = 0; index < current_count; ++index) {
                const auto &detail = economic_capture_->population_detail(index);
                smedley::game_state::TelemetryProvinceSnapshot province_snapshot{};
                const auto *province = smedley::game_state::ReadTelemetryProvince(
                    smedley::game_state::ResolveProvince(game_state.game_state, detail.province_id_candidate), &province_snapshot)
                    ? &province_snapshot : nullptr;
                if (province == nullptr || !province->owner_candidate().normalized_candidate()) {
                    identity_complete = false;
                    break;
                }
                uint32_t country_key = 0;
                std::memcpy(&country_key, province->owner_candidate().str(), 3);
                (*pop_identity_current_)[index] = {detail.pop_id, detail.province_id_candidate,
                    detail.pop_type_id_candidate, detail.size_candidate, country_key};
            }
            if (!identity_complete) {
                ++runtime_->Stats(rule_index).invalid;
                pop_identity_previous_count_ = 0;
                pop_identity_previous_date_.reset();
                return capture.collection_us;
            }
            std::sort(pop_identity_current_->begin(), pop_identity_current_->begin() + current_count,
                [](const PopIdentityState &left, const PopIdentityState &right) {
                    return left.pop_id < right.pop_id;
                });
            const bool opening_seen = pop_identity_previous_date_
                && *pop_identity_previous_date_ <= (std::numeric_limits<int32_t>::max)() - 24
                && *pop_identity_previous_date_ + 24 == date_raw;
            size_t change_count = 0;
            PopIdentityDiff diff{};
            bool complete = opening_seen;
            if (opening_seen) {
                complete = DiffPopIdentities(pop_identity_previous_->data(), pop_identity_previous_count_,
                    pop_identity_current_->data(), current_count, pop_identity_changes_->data(),
                    pop_identity_changes_->size(), &change_count, &diff);
                if (!complete) ++runtime_->Stats(rule_index).invalid;
            }
            if (HasField(rule, "summary")) {
                const SmedleyTelemetryFieldV1 payload[] = {
                    BoolField("opening_seen", opening_seen),
                    IntField("opening_pop_count", opening_seen ? pop_identity_previous_count_ : 0),
                    IntField("closing_pop_count", current_count),
                    IntField("observed_appeared_count", diff.appeared),
                    IntField("observed_disappeared_count", diff.disappeared),
                    IntField("scope_changed_count", diff.scope_changed),
                    IntField("unchanged_count", diff.unchanged),
                    BoolField("complete", complete),
                };
                ++runtime_->Stats(rule_index).collection_attempts;
                runtime_->Account(rule_index, EmitTyped("pop.lifecycle.summary", "state", date_raw,
                    nullptr, 0, payload, 8, false, true));
            }
            const auto tag = [](uint32_t key) {
                std::array<char, 4> value{};
                std::memcpy(value.data(), &key, 3);
                return value;
            };
            const auto selected = [&](const PopIdentityState &state) {
                if (!HasProvinceId(rule, state.province_id)) return false;
                if (rule.country_tags.empty()) return true;
                if (state.country_key == 0) return false;
                const auto country = tag(state.country_key);
                return HasCountryTag(rule, std::string_view(country.data(), 3));
            };
            const bool reliable = true;
            if (opening_seen && complete) {
                for (size_t index = 0; index < change_count; ++index) {
                    const auto &change = (*pop_identity_changes_)[index];
                    const bool appeared = change.kind == PopObservationKind::Appeared;
                    const bool disappeared = change.kind == PopObservationKind::Disappeared;
                    const auto &state = appeared ? change.current : change.previous;
                    if (change.kind == PopObservationKind::ScopeChanged) {
                        if (!HasField(rule, "scope_changed")
                            || (!selected(change.previous) && !selected(change.current))) continue;
                        const auto previous_country = tag(change.previous.country_key);
                        const auto current_country = tag(change.current.country_key);
                        const auto pop_id = IntField("pop_id", change.current.pop_id);
                        const SmedleyTelemetryFieldV1 payload[] = {
                            StringField("previous_country_tag_candidate", previous_country.data()),
                            IntField("previous_province_id_candidate", change.previous.province_id),
                            IntField("previous_pop_type_id_candidate", change.previous.pop_type_id),
                            StringField("current_country_tag_candidate", current_country.data()),
                            IntField("current_province_id_candidate", change.current.province_id),
                            IntField("current_pop_type_id_candidate", change.current.pop_type_id),
                        };
                        ++runtime_->Stats(rule_index).collection_attempts;
                        runtime_->Account(rule_index, EmitTyped("pop.lifecycle.scope_changed", "state", date_raw,
                            &pop_id, 1, payload, 6, false, reliable));
                        continue;
                    }
                    const char *field = appeared ? "appeared" : "disappeared";
                    if (!HasField(rule, field) || !selected(state)) continue;
                    std::array<SmedleyTelemetryFieldV1, 4> entities;
                    uint32_t entity_count = 0;
                    entities[entity_count++] = IntField("pop_id", state.pop_id);
                    entities[entity_count++] = IntField("province_id_candidate", state.province_id);
                    entities[entity_count++] = IntField("pop_type_id_candidate", state.pop_type_id);
                    const auto country = tag(state.country_key);
                    if (state.country_key != 0) entities[entity_count++] = StringField("country_tag_candidate", country.data());
                    const auto size = IntField("size_candidate", state.size);
                    ++runtime_->Stats(rule_index).collection_attempts;
                    runtime_->Account(rule_index, EmitTyped(appeared ? "pop.lifecycle.observed_appeared"
                                                                  : "pop.lifecycle.observed_disappeared",
                        "state", date_raw, entities.data(), entity_count, &size, 1, false, reliable));
                }
            }
            std::copy(pop_identity_current_->begin(), pop_identity_current_->begin() + current_count,
                pop_identity_previous_->begin());
            pop_identity_previous_count_ = current_count;
            pop_identity_previous_date_ = date_raw;
            return capture.collection_us;
        }

        uint64_t EmitPopulationSnapshot(const smedley::game_state::TelemetryCurrentState &game_state, int32_t date_raw,
                                        const smedley::telemetry::CaptureRule &rule, size_t rule_index)
        {
            ++runtime_->Stats(rule_index).polls_due;
            const PopulationCapture capture = economic_capture_->CollectPopulation(game_state.game_state, date_raw);
            runtime_->Stats(rule_index).collection_us += capture.collection_us;
            if (!capture.complete()) {
                ++runtime_->Stats(rule_index).invalid;
                return capture.collection_us;
            }
            for (uint32_t index = 0; index < capture.pop_count; ++index) {
                const auto &detail = economic_capture_->population_detail(index);
                if (!HasProvinceId(rule, detail.province_id_candidate)) continue;
                smedley::game_state::TelemetryProvinceSnapshot province_snapshot{};
                const auto *province = smedley::game_state::ReadTelemetryProvince(
                    smedley::game_state::ResolveProvince(game_state.game_state, detail.province_id_candidate), &province_snapshot)
                    ? &province_snapshot : nullptr;
                const bool owner_required = smedley::telemetry::PopulationOwnerRequired(
                    rule.family, rule.country_tags.size());
                if (owner_required) {
                    if (province == nullptr || !province->owner_candidate().normalized_candidate()) {
                        ++runtime_->Stats(rule_index).invalid;
                        continue;
                    }
                    if (!HasCountryTag(rule, province->owner_candidate().str())) continue;
                }
                ++runtime_->Stats(rule_index).collection_attempts;
                const SmedleyTelemetryFieldV1 entities[] = {
                    IntField("province_id_candidate", detail.province_id_candidate),
                    IntField("pop_type_id_candidate", detail.pop_type_id_candidate),
                    IntField("pop_id", detail.pop_id),
                };
                if (rule.family == "pop.artisan") {
                    smedley::game_state::ArtisanSnapshot artisan{};
                    smedley::game_state::ArtisanReadFailure failure{};
                    std::array<smedley::game_state::ArtisanInputSnapshot, 64> inputs{};
                    uint32_t input_count = 0;
                    uint32_t groups = 0;
                    if (HasField(rule, "identity")) groups |= smedley::game_state::ARTISAN_IDENTITY;
                    if (HasField(rule, "production")) groups |= smedley::game_state::ARTISAN_PRODUCTION;
                    if (HasField(rule, "inputs")) groups |= smedley::game_state::ARTISAN_INPUTS;
                    if (HasField(rule, "finance")) groups |= smedley::game_state::ARTISAN_FINANCE;
                    if (HasField(rule, "flows")) groups |= smedley::game_state::ARTISAN_FLOWS;
                    if (HasField(rule, "sales")) {
                        groups |= smedley::game_state::ARTISAN_IDENTITY | smedley::game_state::ARTISAN_PRODUCTION
                            | smedley::game_state::ARTISAN_FINANCE;
                    }
                    if (!smedley::game_state::ReadArtisanSnapshot(economic_capture_->population_candidate(index).address,
                            &artisan, inputs.data(), inputs.size(), &input_count, groups, &failure)) {
                        int32_t inactive_pop_id = -1;
                        if (smedley::game_state::ReadInactiveArtisan(
                                economic_capture_->population_candidate(index).address, &inactive_pop_id)) {
                            const SmedleyTelemetryFieldV1 inactive_entities[] = {
                                StringField("country_tag", province->owner_candidate().str()),
                                IntField("province_id", detail.province_id_candidate),
                                IntField("pop_id", inactive_pop_id),
                            };
                            ++runtime_->Stats(rule_index).collection_attempts;
                            runtime_->Account(rule_index, EmitTyped("pop.artisan.inactive", "state", date_raw,
                                inactive_entities, 3, nullptr, 0, false, true));
                        } else if (detail.pop_type_id_candidate == 2) {
                            const SmedleyTelemetryFieldV1 invalid_entities[] = {
                                StringField("country_tag", province->owner_candidate().str()),
                                IntField("province_id", detail.province_id_candidate),
                            };
                            const SmedleyTelemetryFieldV1 invalid_payload[] = {
                                IntField("pop_id_candidate", failure.pop_id),
                                StringField("reason", smedley::game_state::ArtisanReadFailureName(failure.reason)),
                                IntField("offending_raw", failure.offending_raw),
                            };
                            runtime_->Account(rule_index, EmitTyped("pop.artisan.invalid", "state", date_raw,
                                invalid_entities, 2, invalid_payload, 3, false, true));
                            ++runtime_->Stats(rule_index).invalid;
                        }
                        continue;
                    }
                    const bool reliable = (rule.country_tags.empty() && rule.province_ids.empty())
                        || (!rule.country_tags.empty() && rule.country_tags.size() <= 16)
                        || (!rule.province_ids.empty() && rule.province_ids.size() <= 16);
                    ArtisanGoodsFlowAggregate flow;
                    CollectArtisanFlowAggregate(economic_capture_->population_candidate(index).address,
                        rule_index, &flow);
                    EmitArtisanGroups(date_raw, rule, rule_index, province->owner_candidate().str(),
                        detail.province_id_candidate, artisan, inputs.data(), input_count, flow, reliable);
                    continue;
                }
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
                } else if (rule.family == "pop.demographics") {
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
                } else if (rule.family == "pop.identity") {
                    smedley::game_state::PopIdentityDimensions identity{};
                    if (!smedley::game_state::ReadPopIdentityDimensions(
                            economic_capture_->population_candidate(index).address, &identity)) {
                        ++runtime_->Stats(rule_index).invalid;
                        continue;
                    }
                    if (HasField(rule, "pop_type_tag_candidate")) {
                        payload[count++] = StringField("pop_type_tag_candidate", identity.pop_type_tag_candidate);
                    }
                    if (HasField(rule, "culture_tag_candidate")) {
                        payload[count++] = StringField("culture_tag_candidate", identity.culture_tag_candidate);
                    }
                    if (HasField(rule, "religion_tag_candidate")) {
                        payload[count++] = StringField("religion_tag_candidate", identity.religion_tag_candidate);
                    }
                } else {
                    smedley::game_state::PopNeedsSnapshot needs{};
                    if (!smedley::game_state::ReadPopNeedsSnapshot(
                            economic_capture_->population_candidate(index).address, &needs)) {
                        ++runtime_->Stats(rule_index).invalid;
                        continue;
                    }
                    if (HasField(rule, "life_satisfaction_candidate_raw")) {
                        payload[count++] = IntField("life_satisfaction_candidate_raw",
                            needs.life_satisfaction_candidate_raw);
                    }
                    if (HasField(rule, "everyday_satisfaction_candidate_raw")) {
                        payload[count++] = IntField("everyday_satisfaction_candidate_raw",
                            needs.everyday_satisfaction_candidate_raw);
                    }
                    if (HasField(rule, "luxury_satisfaction_candidate_raw")) {
                        payload[count++] = IntField("luxury_satisfaction_candidate_raw",
                            needs.luxury_satisfaction_candidate_raw);
                    }
                }
                const bool reliable = (!rule.country_tags.empty() && rule.country_tags.size() <= 16)
                    || (!rule.province_ids.empty() && rule.province_ids.size() <= 16);
                runtime_->Account(rule_index, EmitTyped(rule.family.c_str(), "state", date_raw,
                    entities, 3, payload.data(), count, false, reliable));
            }
            return capture.collection_us;
        }
#pragma optimize("", on)

        uint64_t EmitPopulationAggregate(const smedley::game_state::TelemetryCurrentState &game_state, int32_t date_raw,
                                         const smedley::telemetry::CaptureRule &rule, size_t rule_index)
        {
            ++runtime_->Stats(rule_index).polls_due;
            const PopulationCapture capture = economic_capture_->CollectPopulation(game_state.game_state, date_raw);
            runtime_->Stats(rule_index).collection_us += capture.collection_us;
            if (!capture.complete()) {
                ++runtime_->Stats(rule_index).invalid;
                return capture.collection_us;
            }
            size_t aggregate_count = 0;
            for (uint32_t index = 0; index < capture.pop_count; ++index) {
                const auto &detail = economic_capture_->population_detail(index);
                if (!HasProvinceId(rule, detail.province_id_candidate)) continue;
                smedley::game_state::TelemetryProvinceSnapshot province_snapshot{};
                const auto *province = smedley::game_state::ReadTelemetryProvince(
                    smedley::game_state::ResolveProvince(game_state.game_state, detail.province_id_candidate), &province_snapshot)
                    ? &province_snapshot : nullptr;
                if (province == nullptr || !province->owner_candidate().normalized_candidate()) {
                    ++runtime_->Stats(rule_index).invalid;
                    continue;
                }
                if (!HasCountryTag(rule, province->owner_candidate().str())) continue;
                (*pop_aggregates_)[aggregate_count++] = {
                    detail.province_id_candidate, detail.pop_type_id_candidate, 1,
                    detail.size_candidate, detail.employed_candidate,
                    detail.economy.money_raw, detail.economy.savings_raw};
            }
            std::sort(pop_aggregates_->begin(), pop_aggregates_->begin() + aggregate_count,
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
                const auto &next = (*pop_aggregates_)[index];
                if (merged_count == 0 || (*pop_aggregates_)[merged_count - 1].province_id != next.province_id
                    || (*pop_aggregates_)[merged_count - 1].pop_type_id != next.pop_type_id) {
                    (*pop_aggregates_)[merged_count++] = next;
                    continue;
                }
                auto &current = (*pop_aggregates_)[merged_count - 1];
                if (!add(next.pop_count, &current.pop_count) || !add(next.size, &current.size)
                    || !add(next.employed, &current.employed) || !add(next.money_raw, &current.money_raw)
                    || !add(next.savings_raw, &current.savings_raw)) {
                    ++runtime_->Stats(rule_index).invalid;
                    return capture.collection_us;
                }
            }
            for (size_t index = 0; index < merged_count; ++index) {
                const auto &aggregate = (*pop_aggregates_)[index];
                smedley::game_state::TelemetryProvinceSnapshot province_snapshot{};
                const auto *province = smedley::game_state::ReadTelemetryProvince(
                    smedley::game_state::ResolveProvince(game_state.game_state, aggregate.province_id), &province_snapshot)
                    ? &province_snapshot : nullptr;
                if (province == nullptr || !province->owner_candidate().normalized_candidate()) {
                    ++runtime_->Stats(rule_index).invalid;
                    continue;
                }
                ++runtime_->Stats(rule_index).collection_attempts;
                const SmedleyTelemetryFieldV1 entities[] = {
                    StringField("country_tag", province->owner_candidate().str()),
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
                const bool reliable = (rule.country_tags.empty() && rule.province_ids.empty())
                    || (!rule.country_tags.empty() && rule.country_tags.size() <= 16)
                    || (!rule.province_ids.empty() && rule.province_ids.size() <= 16);
                runtime_->Account(rule_index, EmitTyped("pop.aggregate", "state", date_raw,
                    entities, 3, payload.data(), count, false, reliable));
            }
            return capture.collection_us;
        }

        uint64_t Collect(const smedley::game_state::TelemetryCurrentState &game_state, int32_t date_raw) {
            std::array<const smedley::telemetry::CaptureRule *, smedley::telemetry::kMaxCaptureRules> due{}; std::array<size_t, smedley::telemetry::kMaxCaptureRules> indices{}; size_t count = 0;
            for (size_t index = 0; index < runtime_->RuleCount(); ++index) { const auto &configured = runtime_->RuleAt(index); const auto *definition = smedley::telemetry::FindMetricFamily(configured.family); if (definition == nullptr || definition->collector != smedley::telemetry::MetricCollector::Population) continue; size_t rule_index = 0; if (const auto *rule = runtime_->DueRule(configured.family, date_raw, &rule_index)) { due[count] = rule; indices[count++] = rule_index; } }
            const bool cashflow_state_required = pop_cash_flow_states_ != nullptr;
            if (count == 0 && !cashflow_state_required) return 0;
            economic_capture_->InvalidatePopulationCache();
            uint64_t elapsed = 0;
            const auto *cashflow = static_cast<const smedley::telemetry::CaptureRule *>(nullptr); const auto *cashflow_aggregate = static_cast<const smedley::telemetry::CaptureRule *>(nullptr); size_t cashflow_index = 0, cashflow_aggregate_index = 0;
            for (size_t index = 0; index < count; ++index) { if (due[index]->family == "pop.cashflow") { cashflow = due[index]; cashflow_index = indices[index]; } else if (due[index]->family == "pop.cashflow.aggregate") { cashflow_aggregate = due[index]; cashflow_aggregate_index = indices[index]; } }
            if (cashflow || cashflow_aggregate || cashflow_state_required) {
                elapsed += EmitPopCashFlows(game_state, date_raw, cashflow, cashflow_index,
                    cashflow_aggregate, cashflow_aggregate_index);
            }
            for (size_t index = 0; index < count; ++index) { const auto &rule = *due[index]; if (rule.family == "pop.cashflow" || rule.family == "pop.cashflow.aggregate") continue; elapsed += rule.family == "pop.aggregate" ? EmitPopulationAggregate(game_state, date_raw, rule, indices[index]) : rule.family == "pop.lifecycle" ? EmitPopLifecycle(game_state, date_raw, rule, indices[index]) : EmitPopulationSnapshot(game_state, date_raw, rule, indices[index]); }
            return elapsed;
        }
    };

    PopulationCollector::PopulationCollector(CollectorRuntime *runtime, EconomicCapture *economic_capture) : runtime_(runtime), economic_capture_(economic_capture), storage_(std::make_unique<Storage>(runtime, economic_capture)) {}
    PopulationCollector::~PopulationCollector() = default;
    services::ArtisanSettlementHookRecord *PopulationCollector::artisan_records() { if (!storage_->artisan_hook_records_) storage_->artisan_hook_records_ = std::make_unique<std::array<services::ArtisanSettlementHookRecord, services::max_artisan_flow_records>>(); return storage_->artisan_hook_records_->data(); }
    size_t PopulationCollector::artisan_record_capacity() const { return services::max_artisan_flow_records; }
    services::PopCashFlowHookRecord *PopulationCollector::cash_flow_records() { if (!storage_->pop_cash_flow_records_) storage_->pop_cash_flow_records_ = std::make_unique<std::array<services::PopCashFlowHookRecord, services::max_pop_cash_flow_records>>(); return storage_->pop_cash_flow_records_->data(); }
    size_t PopulationCollector::cash_flow_record_capacity() const { return services::max_pop_cash_flow_records; }
    void PopulationCollector::ObserveArtisanFlows(const services::ArtisanSettlementHookRecord *records, uint32_t count, uint64_t dropped, int32_t date_raw) { storage_->artisan_hook_record_count_ = count; storage_->artisan_hook_dropped_ = dropped; storage_->artisan_hook_date_ = date_raw; if (dropped != 0) { size_t index = 0; if (runtime_->FindRule("pop.artisan", &index)) runtime_->Invalid(index, dropped); } }
    void PopulationCollector::ObserveCashFlows(const services::PopCashFlowHookRecord *, uint32_t count, const services::PopCashFlowHookStats &stats, int32_t date_raw) { storage_->pop_cash_flow_record_count_ = count; storage_->pop_cash_flow_hook_stats_ = stats; storage_->pop_cash_flow_hook_date_ = date_raw; }
    void PopulationCollector::Reset() { storage_->Reset(); }
    uint64_t PopulationCollector::Collect(const smedley::game_state::TelemetryCurrentState &game_state, int32_t date_raw) { return storage_->Collect(game_state, date_raw); }
}
