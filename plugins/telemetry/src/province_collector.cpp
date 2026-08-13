#include "province_collector.hpp"

#include "collector_runtime.hpp"
#include "producer_sales_core.hpp"
#include "telemetry_services.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace telemetry_plugin::collectors
{
    namespace
    {
        struct ProducerInventoryState { int32_t date_raw = 0, good_ordinal = -1; uint32_t country_key = 0; int64_t closing_inventory_raw = 0; bool seen = false; };
        SmedleyTelemetryFieldV1 IntField(const char *key, int64_t value) { SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key, static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_INT64, 0, {}}; field.value.int64_value = value; return field; }
        SmedleyTelemetryFieldV1 BoolField(const char *key, bool value) { SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key, static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_BOOL, 0, {}}; field.value.bool_value = value ? 1u : 0u; return field; }
        SmedleyTelemetryFieldV1 StringField(const char *key, const char *value) { SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key, static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_UTF8_STRING, 0, {}}; field.value.string_value = {value, static_cast<uint32_t>(std::strlen(value)), 0}; return field; }
        SmedleyTelemetryFieldV1 StringField(const char *key, std::string_view value) { SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key, static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_UTF8_STRING, 0, {}}; field.value.string_value = {value.data(), static_cast<uint32_t>(value.size()), 0}; return field; }
        bool HasField(const smedley::telemetry::CaptureRule &rule, std::string_view field) { return rule.fields.empty() || std::find(rule.fields.begin(), rule.fields.end(), field) != rule.fields.end(); }
        bool HasCountryTag(const smedley::telemetry::CaptureRule &rule, std::string_view tag) { return rule.country_tags.empty() || std::find(rule.country_tags.begin(), rule.country_tags.end(), tag) != rule.country_tags.end(); }
        bool HasProvinceId(const smedley::telemetry::CaptureRule &rule, int id) { return rule.province_ids.empty() || std::find(rule.province_ids.begin(), rule.province_ids.end(), id) != rule.province_ids.end(); }
    }

    struct ProvinceCollector::Storage { std::array<ProducerInventoryState, smedley::game_state::max_sample_destination_provinces> inventory{}; };
    ProvinceCollector::ProvinceCollector(CollectorRuntime *runtime) : runtime_(runtime), storage_(std::make_unique<Storage>()) {}
    ProvinceCollector::~ProvinceCollector() = default;
    void ProvinceCollector::Reset() { storage_->inventory = {}; }

    void ProvinceCollector::Collect(const smedley::game_state::TelemetryCurrentState &game_state, int32_t date_raw)
    {
        size_t rule_index = 0;
        if (const auto *rule = runtime_->DueRule("province.daily", date_raw, &rule_index); rule != nullptr) {
            runtime_->Poll(rule_index);
            size_t province_count = 0;
            if (!game_state.province_count_candidate(&province_count)) runtime_->Invalid(rule_index);
            for (size_t id = 0; id < province_count; ++id) {
                if (!HasProvinceId(*rule, static_cast<int>(id))) continue;
                smedley::game_state::TelemetryProvinceSnapshot province_snapshot{};
                const auto *province = smedley::game_state::ReadTelemetryProvince(smedley::game_state::ResolveProvince(game_state.game_state, static_cast<int>(id)), &province_snapshot) ? &province_snapshot : nullptr;
                if (province == nullptr || !province->daily_available() || province->id_candidate() != static_cast<int>(id)) { runtime_->Invalid(rule_index); continue; }
                runtime_->Attempt(rule_index);
                if ((HasField(*rule, "owner_tag_candidate") && !province->owner_candidate().normalized_candidate()) || (HasField(*rule, "controller_tag_candidate") && !province->controller_candidate().normalized_candidate())) { runtime_->Invalid(rule_index); continue; }
                const auto province_id = IntField("province_id", static_cast<int64_t>(id));
                std::array<SmedleyTelemetryFieldV1, 5> payload; uint32_t count = 0;
                if (HasField(*rule, "owner_tag_candidate")) payload[count++] = StringField("owner_tag_candidate", province->owner_candidate().str());
                if (HasField(*rule, "controller_tag_candidate")) payload[count++] = StringField("controller_tag_candidate", province->controller_candidate().str());
                if (HasField(*rule, "colonial_level_candidate")) payload[count++] = IntField("colonial_level_candidate", province->colonial_level_candidate());
                if (HasField(*rule, "life_rating_candidate")) payload[count++] = IntField("life_rating_candidate", province->life_rating_candidate());
                if (HasField(*rule, "infrastructure_candidate_raw")) payload[count++] = IntField("infrastructure_candidate_raw", province->infrastructure_candidate());
                const bool reliable = !rule->province_ids.empty() && rule->province_ids.size() <= 16;
                runtime_->Account(rule_index, runtime_->EmitState("province.daily", date_raw, &province_id, 1, payload.data(), count, reliable));
            }
        }
        if (const auto *rule = runtime_->DueRule("province.production", date_raw, &rule_index); rule != nullptr) {
            runtime_->Poll(rule_index);
            size_t province_count = 0;
            if (!game_state.province_count_candidate(&province_count)) runtime_->Invalid(rule_index);
            for (size_t id = 0; id < province_count; ++id) {
                if (!HasProvinceId(*rule, static_cast<int>(id))) continue;
                smedley::game_state::TelemetryProvinceSnapshot province_snapshot{};
                const auto *province = smedley::game_state::ReadTelemetryProvince(smedley::game_state::ResolveProvince(game_state.game_state, static_cast<int>(id)), &province_snapshot) ? &province_snapshot : nullptr;
                if (province == nullptr || province->id_candidate() != static_cast<int>(id)) { runtime_->Invalid(rule_index); continue; }
                runtime_->Attempt(rule_index);
                size_t building_count = 0; int construction_count = 0;
                if ((HasField(*rule, "building_slot_count_candidate") && !province->building_slot_count_candidate(&building_count)) || (HasField(*rule, "construction_count_candidate") && !province->construction_count_candidate(&construction_count))) { runtime_->Invalid(rule_index); continue; }
                const auto province_id = IntField("province_id", static_cast<int64_t>(id));
                std::array<SmedleyTelemetryFieldV1, 2> payload; uint32_t count = 0;
                if (HasField(*rule, "building_slot_count_candidate")) payload[count++] = IntField("building_slot_count_candidate", building_count);
                if (HasField(*rule, "construction_count_candidate")) payload[count++] = IntField("construction_count_candidate", construction_count);
                const bool reliable = !rule->province_ids.empty() && rule->province_ids.size() <= 16;
                runtime_->Account(rule_index, runtime_->EmitState("province.production", date_raw, &province_id, 1, payload.data(), count, reliable));
            }
        }
        if (const auto *rule = runtime_->DueRule("province.rgo", date_raw, &rule_index); rule != nullptr) {
            runtime_->Poll(rule_index);
            size_t province_count = 0; const bool province_vector_valid = game_state.province_count_candidate(&province_count);
            if (!province_vector_valid) runtime_->Invalid(rule_index);
            const auto registry = province_vector_valid ? smedley::game_state::ResolveStateEmploymentRegistry() : smedley::game_state::EmploymentRegistryRef{};
            uint32_t groups = 0;
            if (HasField(*rule, "identity")) groups |= smedley::game_state::RGO_IDENTITY;
            if (HasField(*rule, "employment")) groups |= smedley::game_state::RGO_EMPLOYMENT;
            if (HasField(*rule, "production")) groups |= smedley::game_state::RGO_PRODUCTION;
            if (HasField(*rule, "finance")) groups |= smedley::game_state::RGO_FINANCE;
            if (HasField(*rule, "modifiers")) groups |= smedley::game_state::RGO_MODIFIERS;
            if (HasField(*rule, "sales")) groups |= smedley::game_state::RGO_IDENTITY | smedley::game_state::RGO_PRODUCTION | smedley::game_state::RGO_FINANCE | smedley::game_state::RGO_SALES;
            for (size_t id = 0; id < province_count; ++id) {
                if (!HasProvinceId(*rule, static_cast<int>(id))) continue;
                smedley::game_state::TelemetryProvinceSnapshot province_snapshot{};
                const auto *province = smedley::game_state::ReadTelemetryProvince(smedley::game_state::ResolveProvince(game_state.game_state, static_cast<int>(id)), &province_snapshot) ? &province_snapshot : nullptr;
                if (province == nullptr) { runtime_->Invalid(rule_index); continue; }
                if (!province->owner_candidate().normalized_candidate()) { if (!rule->country_tags.empty()) runtime_->Invalid(rule_index); continue; }
                const std::string_view country_tag(province->owner_candidate().str(), 3);
                if (!HasCountryTag(*rule, country_tag)) continue;
                runtime_->Attempt(rule_index);
                smedley::game_state::RgoSnapshot snapshot{};
                if (!smedley::game_state::ReadProvinceRgo(registry, province->province, static_cast<int32_t>(id), province_count, groups, &snapshot)) { if (!rule->country_tags.empty() || !rule->province_ids.empty()) runtime_->Invalid(rule_index); continue; }
                const bool reliable = (rule->country_tags.empty() && rule->province_ids.empty()) || (!rule->country_tags.empty() && rule->country_tags.size() <= 16) || (!rule->province_ids.empty() && rule->province_ids.size() <= 16);
                const SmedleyTelemetryFieldV1 entities[] = {StringField("country_tag", country_tag), IntField("province_id", snapshot.province_id)};
                if (HasField(*rule, "identity")) { const SmedleyTelemetryFieldV1 payload[] = {StringField("production_type", snapshot.production_type), IntField("output_good_ordinal", snapshot.output_good_ordinal), StringField("output_good", snapshot.output_good)}; runtime_->Account(rule_index, runtime_->EmitState("province.rgo.identity", date_raw, entities, 2, payload, 3, reliable)); }
                if (HasField(*rule, "employment")) { const SmedleyTelemetryFieldV1 payload[] = {IntField("employment_capacity", snapshot.employment_capacity), IntField("employed", snapshot.employed)}; runtime_->Account(rule_index, runtime_->EmitState("province.rgo.employment", date_raw, entities, 2, payload, 2, reliable)); }
                if (HasField(*rule, "production")) { const SmedleyTelemetryFieldV1 payload[] = {IntField("base_output_per_size_raw", snapshot.base_output_per_size_raw), IntField("base_size_raw_candidate", snapshot.base_size_raw), IntField("base_size_raw", snapshot.base_size_raw), IntField("output_efficiency_raw", snapshot.output_efficiency_raw), IntField("throughput_raw", snapshot.throughput_raw), IntField("gross_output_raw", snapshot.gross_output_raw)}; runtime_->Account(rule_index, runtime_->EmitState("province.rgo.production", date_raw, entities, 2, payload, 6, reliable)); }
                if (HasField(*rule, "modifiers")) { const SmedleyTelemetryFieldV1 payload[] = {IntField("owner_population", snapshot.owner_population), IntField("state_rgo_employment_capacity", snapshot.state_rgo_employment_capacity), IntField("owner_output_modifier_raw", snapshot.owner_output_modifier_raw)}; runtime_->Account(rule_index, runtime_->EmitState("province.rgo.modifiers", date_raw, entities, 2, payload, 3, reliable)); }
                if (HasField(*rule, "finance")) { const auto income = IntField("income_raw", snapshot.income_raw); runtime_->Account(rule_index, runtime_->EmitState("province.rgo.finance", date_raw, entities, 2, &income, 1, reliable)); }
                if (HasField(*rule, "sales")) {
                    auto &previous = storage_->inventory[id]; uint32_t country_key = 0; std::memcpy(&country_key, country_tag.data(), 3); ProducerSale sale{};
                    const bool boundary_complete = previous.seen && previous.date_raw + 24 == date_raw && previous.good_ordinal == snapshot.output_good_ordinal && previous.country_key == country_key;
                    const bool complete = boundary_complete && ReconcileProducerSale(previous.closing_inventory_raw, snapshot.gross_output_raw, snapshot.leftover_raw, snapshot.income_raw, &sale);
                    const SmedleyTelemetryFieldV1 summary_payload[] = {BoolField("settlement_seen", true), BoolField("opening_inventory_seen", boundary_complete), BoolField("complete", complete)};
                    runtime_->Account(rule_index, runtime_->EmitState("province.rgo.sales.summary", date_raw, entities, 2, summary_payload, 3, reliable));
                    if (boundary_complete && !complete) runtime_->Invalid(rule_index);
                    if (complete) {
                        const SmedleyTelemetryFieldV1 quantity_payload[] = {IntField("output_good_ordinal", snapshot.output_good_ordinal), IntField("opening_inventory_raw", sale.opening_inventory_raw), IntField("produced_raw", sale.produced_raw), IntField("sold_raw", sale.sold_raw), IntField("closing_inventory_raw", sale.closing_inventory_raw)};
                        runtime_->Account(rule_index, runtime_->EmitState("province.rgo.sales.quantity", date_raw, entities, 2, quantity_payload, 5, reliable));
                        const SmedleyTelemetryFieldV1 revenue_payload[] = {IntField("proceeds_raw", sale.proceeds_raw), IntField("percent_sold_domestic_raw", snapshot.percent_sold_domestic_raw), IntField("percent_sold_export_raw", snapshot.percent_sold_export_raw)};
                        runtime_->Account(rule_index, runtime_->EmitState("province.rgo.sales.revenue", date_raw, entities, 2, revenue_payload, 3, reliable));
                    }
                    previous.date_raw = date_raw; previous.good_ordinal = snapshot.output_good_ordinal; previous.country_key = country_key; previous.closing_inventory_raw = snapshot.leftover_raw; previous.seen = true;
                }
            }
        }
    }
}
