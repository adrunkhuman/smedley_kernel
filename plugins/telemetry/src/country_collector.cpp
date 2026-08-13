#include "country_collector.hpp"

#include "collector_runtime.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

namespace telemetry_plugin::collectors
{
    namespace
    {
        SmedleyTelemetryFieldV1 IntField(const char *key, int64_t value) { SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key, static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_INT64, 0, {}}; field.value.int64_value = value; return field; }
        SmedleyTelemetryFieldV1 DoubleField(const char *key, double value) { SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key, static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_DOUBLE, 0, {}}; field.value.double_value = value; return field; }
        SmedleyTelemetryFieldV1 BoolField(const char *key, bool value) { SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key, static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_BOOL, 0, {}}; field.value.bool_value = value ? 1u : 0u; return field; }
        SmedleyTelemetryFieldV1 StringField(const char *key, std::string_view value) { SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key, static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_UTF8_STRING, 0, {}}; field.value.string_value = {value.data(), static_cast<uint32_t>(value.size()), 0}; return field; }
        bool HasField(const smedley::telemetry::CaptureRule &rule, std::string_view field) { return rule.fields.empty() || std::find(rule.fields.begin(), rule.fields.end(), field) != rule.fields.end(); }
        bool HasCountryTag(const smedley::telemetry::CaptureRule &rule, std::string_view tag) { return rule.country_tags.empty() || std::find(rule.country_tags.begin(), rule.country_tags.end(), tag) != rule.country_tags.end(); }
        std::optional<std::string_view> NormalizedCountryTag(const smedley::game_state::TelemetryCountrySnapshot *country) { if (country == nullptr || !country->tag().normalized_candidate()) return std::nullopt; return std::string_view(country->tag().str(), 3); }
        bool ReadDailyCountry(smedley::events::DailyUpdateEvent &event, smedley::game_state::TelemetryCountrySnapshot *snapshot) { return smedley::game_state::ReadTelemetryCountry(smedley::game_state::DailyUpdateCountry(event), snapshot); }
    }

    CountryCollector::CountryCollector(CollectorRuntime *runtime) : runtime_(runtime) {}

    void CountryCollector::Collect(smedley::events::DailyUpdateEvent &event, int32_t date_raw)
    {
        size_t rule_index = 0;
        const auto collect = [&](std::string_view family, const auto &emit) {
            const auto *rule = runtime_->DueRule(family, date_raw, &rule_index);
            if (rule == nullptr) return;
            runtime_->PollOnce(rule_index, date_raw);
            smedley::game_state::TelemetryCountrySnapshot snapshot{};
            const auto *country = ReadDailyCountry(event, &snapshot) ? &snapshot : nullptr;
            const auto tag = NormalizedCountryTag(country);
            if (country != nullptr && !tag) { runtime_->Invalid(rule_index); return; }
            if (tag && HasCountryTag(*rule, *tag)) emit(*rule, rule_index, *country, *tag);
        };
        collect("country.daily", [&](const auto &rule, size_t index, const auto &country, std::string_view tag) {
            runtime_->Attempt(index);
            if (!country.daily_available()) { runtime_->Invalid(index); return; }
            std::array<SmedleyTelemetryFieldV1, 2> payload; uint32_t count = 0;
            if (HasField(rule, "treasury_raw")) payload[count++] = IntField("treasury_raw", country.treasury_raw());
            if (HasField(rule, "treasury")) payload[count++] = DoubleField("treasury", static_cast<double>(country.treasury_raw()) / 32768.0);
            const auto entity = StringField("country_tag", tag);
            runtime_->Account(index, runtime_->EmitState("country.daily", date_raw, &entity, 1, payload.data(), count));
        });
        collect("country.metrics", [&](const auto &rule, size_t index, const auto &country, std::string_view tag) {
            const auto entity = StringField("country_tag", tag);
            if (HasField(rule, "power")) {
                runtime_->Attempt(index);
                if (!country.power_available()) runtime_->Invalid(index);
                else { const SmedleyTelemetryFieldV1 payload[] = {IntField("prestige_candidate_raw", country.prestige_candidate_raw()), IntField("infamy_candidate_raw", country.infamy_candidate_raw()), IntField("ranking_candidate", country.ranking_candidate()), IntField("military_ranking_candidate", country.military_ranking_candidate()), IntField("industrial_ranking_candidate", country.industrial_ranking_candidate()), IntField("prestige_ranking_candidate", country.prestige_ranking_candidate())}; runtime_->Account(index, runtime_->EmitState("country.metrics.power", date_raw, &entity, 1, payload, 6)); }
            }
            if (HasField(rule, "politics")) {
                runtime_->Attempt(index);
                if (!country.politics_available()) runtime_->Invalid(index);
                else { const SmedleyTelemetryFieldV1 payload[] = {IntField("plurality_candidate_raw", country.plurality_candidate_raw()), IntField("war_exhaustion_candidate_raw", country.war_exhaustion_candidate_raw()), IntField("diplomatic_points_candidate_raw", country.diplomatic_points_candidate_raw()), IntField("research_points_candidate_raw", country.research_points_candidate_raw()), IntField("leadership_candidate_raw", country.leadership_candidate_raw())}; runtime_->Account(index, runtime_->EmitState("country.metrics.politics", date_raw, &entity, 1, payload, 5)); }
            }
        });
        collect("country.military", [&](const auto &rule, size_t index, const auto &country, std::string_view tag) {
            runtime_->Attempt(index); int units = 0; size_t mobilization = 0;
            if (!country.military_available() || (HasField(rule, "unit_count_candidate") && !country.unit_count_candidate(&units)) || (HasField(rule, "scheduled_mobilization_count_candidate") && !country.scheduled_mobilization_count_candidate(&mobilization))) { runtime_->Invalid(index); return; }
            std::array<SmedleyTelemetryFieldV1, 5> payload; uint32_t count = 0;
            if (HasField(rule, "unit_count_candidate")) payload[count++] = IntField("unit_count_candidate", units);
            if (HasField(rule, "mobilized_candidate")) payload[count++] = BoolField("mobilized_candidate", country.mobilized_candidate());
            if (HasField(rule, "scheduled_mobilization_count_candidate")) payload[count++] = IntField("scheduled_mobilization_count_candidate", mobilization);
            if (HasField(rule, "leadership_candidate_raw")) payload[count++] = IntField("leadership_candidate_raw", country.leadership_candidate_raw());
            if (HasField(rule, "military_ranking_candidate")) payload[count++] = IntField("military_ranking_candidate", country.military_ranking_candidate());
            const auto entity = StringField("country_tag", tag);
            runtime_->Account(index, runtime_->EmitState("country.military", date_raw, &entity, 1, payload.data(), count));
        });
        collect("country.diplomacy", [&](const auto &rule, size_t index, const auto &country, std::string_view tag) {
            const auto entity = StringField("country_tag", tag);
            if (HasField(rule, "status")) {
                runtime_->Attempt(index);
                if (!country.diplomacy_status_available() || !country.overlord_candidate().normalized_candidate() || !country.sphere_leader_candidate().normalized_candidate()) runtime_->Invalid(index);
                else { const SmedleyTelemetryFieldV1 payload[] = {BoolField("substate_candidate", country.substate_candidate()), BoolField("vassal_candidate", country.vassal_candidate()), StringField("overlord_tag_candidate", country.overlord_candidate().str()), StringField("sphere_leader_tag_candidate", country.sphere_leader_candidate().str())}; runtime_->Account(index, runtime_->EmitState("country.diplomacy.status", date_raw, &entity, 1, payload, 4)); }
            }
            if (HasField(rule, "relations")) {
                runtime_->Attempt(index); size_t spherelings = 0, vassals = 0, allies = 0, guaranteed = 0, neighbors = 0;
                if (!country.diplomacy_relations_available() || !country.sphereling_count_candidate(&spherelings) || !country.vassal_count_candidate(&vassals) || !country.ally_count_candidate(&allies) || !country.guaranteed_count_candidate(&guaranteed) || !country.neighbor_count_candidate(&neighbors)) runtime_->Invalid(index);
                else { const SmedleyTelemetryFieldV1 payload[] = {IntField("sphereling_count_candidate", spherelings), IntField("vassal_count_candidate", vassals), IntField("ally_count_candidate", allies), IntField("guaranteed_count_candidate", guaranteed), IntField("neighbor_count_candidate", neighbors)}; runtime_->Account(index, runtime_->EmitState("country.diplomacy.relations", date_raw, &entity, 1, payload, 5)); }
            }
        });
    }
}
