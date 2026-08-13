#include "factory_economy_collector.hpp"

#include "collector_runtime.hpp"
#include "economic_capture.hpp"
#include "producer_sales_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
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
        std::optional<std::string_view> Tag(const smedley::game_state::TelemetryCountrySnapshot *country) { if (country == nullptr || !country->tag().normalized_candidate()) return std::nullopt; return std::string_view(country->tag().str(), 3); }
        bool ReadCountry(smedley::events::DailyUpdateEvent &event, smedley::game_state::TelemetryCountrySnapshot *snapshot) { return smedley::game_state::ReadTelemetryCountry(smedley::game_state::DailyUpdateCountry(event), snapshot); }
    }

    FactoryEconomyCollector::FactoryEconomyCollector(CollectorRuntime *runtime, EconomicCapture *economic_capture,
                                                     const double *gold_to_cash_rate)
        : runtime_(runtime), economic_capture_(economic_capture), gold_to_cash_rate_(gold_to_cash_rate) {}

    void FactoryEconomyCollector::AccountFactoryFlowInvalid(uint64_t count)
    {
        size_t index = 0;
        if (const auto *factory = runtime_->FindRule("state.factory", &index);
            factory != nullptr && HasField(*factory, "flows")) {
            runtime_->Invalid(index, count);
        }
        if (runtime_->FindRule("country.economy", &index) != nullptr) runtime_->Invalid(index, count);
    }

    void FactoryEconomyCollector::ObserveFactoryFlows(const services::FactorySettlementHookRecord *records,
        uint32_t count, uint64_t dropped, int32_t date_raw)
    {
        factory_hook_records_ = records; factory_hook_record_count_ = count; factory_hook_dropped_ = dropped; factory_hook_date_raw_ = date_raw;
        size_t index = 0;
        if (dropped != 0) AccountFactoryFlowInvalid(dropped);
        if (const auto *factory = runtime_->FindRule("state.factory", &index);
            (factory != nullptr && HasField(*factory, "flows")) || runtime_->FindRule("country.economy", &index) != nullptr) UpdateFactoryDailyFlows();
    }

    void FactoryEconomyCollector::ObserveFactorySales(const services::FactorySalesHookRecord *records,
        uint32_t count, uint64_t dropped, int32_t)
    {
        factory_sales_hook_records_ = records; factory_sales_hook_record_count_ = count;
        size_t index = 0;
        if (dropped != 0 && runtime_->FindRule("state.factory", &index) != nullptr) runtime_->Invalid(index, dropped);
    }

// MSVC's x86 optimizer exhausts its 32-bit heap on this fixed-array state machine.
#pragma optimize("", off)
    void FactoryEconomyCollector::UpdateFactoryDailyFlows()
    {
        uint32_t retained = 0;
        for (uint32_t index = 0; index < factory_previous_flow_count_; ++index) {
            const auto &previous = factory_previous_flows_[index];
            if (factory_hook_date_raw_ < previous.date_raw || factory_hook_date_raw_ - previous.date_raw > 30 * 24) continue;
            factory_previous_flows_[retained++] = previous;
        }
        factory_previous_flow_count_ = retained; factory_daily_flow_count_ = 0;
        for (uint32_t record_index = 0; record_index < factory_hook_record_count_; ++record_index) {
            const auto &record = factory_hook_records_[record_index];
            if (factory_daily_flow_count_ == 0 || factory_daily_flows_[factory_daily_flow_count_ - 1].address.address() != record.factory.address()) {
                if (factory_daily_flow_count_ >= factory_daily_flows_.size()) { AccountFactoryFlowInvalid(); continue; }
                auto &next = factory_daily_flows_[factory_daily_flow_count_++]; next = {}; next.address = record.factory; next.date_raw = factory_hook_date_raw_;
            }
            auto &flow = factory_daily_flows_[factory_daily_flow_count_ - 1];
            if (record.pool > 3) { AccountFactoryFlowInvalid(); continue; }
            auto &aggregate = flow.aggregate;
            auto &target = record.pool == 0 ? aggregate.opening_raw : record.pool == 1 ? aggregate.pre_add_raw : record.pool == 2 ? aggregate.first_raw : aggregate.second_raw;
            bool overflow = false;
            for (size_t good = 0; good < target.size(); ++good) { const auto amount = record.quantity_raw[good]; if (amount < 0 || (amount > 0 && target[good] > (std::numeric_limits<int64_t>::max)() - amount)) { overflow = true; break; } target[good] += amount; }
            if (overflow) { AccountFactoryFlowInvalid(); continue; }
            if (record.pool == 0) aggregate.opening_seen = true; else if (record.pool == 1) aggregate.pre_add_seen = true; else if (record.pool == 2) aggregate.first_seen = true; else { aggregate.second_seen = true; ++aggregate.pair_count; }
        }
        for (uint32_t index = 0; index < factory_daily_flow_count_; ++index) {
            auto &flow = factory_daily_flows_[index]; const auto &aggregate = flow.aggregate;
            const bool purchased = aggregate.pre_add_seen || aggregate.first_seen || aggregate.second_seen || aggregate.pair_count != 0;
            flow.closing_valid = aggregate.opening_seen && (!purchased || (aggregate.pre_add_seen && aggregate.first_seen && aggregate.second_seen && aggregate.pair_count != 0 && aggregate.opening_raw == aggregate.pre_add_raw));
            if (flow.closing_valid) for (size_t good = 0; good < flow.closing_raw.size(); ++good) { if (!purchased) flow.closing_raw[good] = aggregate.opening_raw[good]; else if (aggregate.first_raw[good] > (std::numeric_limits<int64_t>::max)() - aggregate.second_raw[good] || aggregate.pre_add_raw[good] > (std::numeric_limits<int64_t>::max)() - aggregate.first_raw[good] - aggregate.second_raw[good]) { flow.closing_valid = false; AccountFactoryFlowInvalid(); break; } else flow.closing_raw[good] = aggregate.pre_add_raw[good] + aggregate.first_raw[good] + aggregate.second_raw[good]; }
            const auto previous = std::lower_bound(factory_previous_flows_.begin(), factory_previous_flows_.begin() + factory_previous_flow_count_, flow.address, [](const DailyFactoryFlow &candidate, smedley::game_state::FactoryRef address) { return candidate.address.address() < address.address(); });
            const auto *old = previous != factory_previous_flows_.begin() + factory_previous_flow_count_ && previous->address.address() == flow.address.address() && flow.date_raw >= previous->date_raw && flow.date_raw - previous->date_raw <= 30 * 24 ? &*previous : nullptr;
            if (old != nullptr) { flow.identity_seen = old->identity_seen; flow.state_id = old->state_id; flow.factory_type = old->factory_type; }
            flow.consumption_valid = flow.closing_valid && old != nullptr && old->closing_valid;
            if (flow.consumption_valid) for (size_t good = 0; good < flow.consumed_raw.size(); ++good) { if (old->closing_raw[good] < aggregate.opening_raw[good]) { flow.consumption_valid = false; break; } flow.consumed_raw[good] = old->closing_raw[good] - aggregate.opening_raw[good]; }
        }
        const auto previous_count = factory_previous_flow_count_;
        for (uint32_t index = 0; index < factory_daily_flow_count_; ++index) { const auto &current = factory_daily_flows_[index]; if (!current.closing_valid) continue; const auto existing = std::lower_bound(factory_previous_flows_.begin(), factory_previous_flows_.begin() + previous_count, current.address, [](const DailyFactoryFlow &candidate, smedley::game_state::FactoryRef address) { return candidate.address.address() < address.address(); }); if (existing == factory_previous_flows_.begin() + previous_count || existing->address.address() != current.address.address()) { if (factory_previous_flow_count_ >= factory_previous_flows_.size()) { AccountFactoryFlowInvalid(); continue; } factory_previous_flows_[factory_previous_flow_count_++] = current; } else *existing = current; }
        std::sort(factory_previous_flows_.begin(), factory_previous_flows_.begin() + factory_previous_flow_count_, [](const DailyFactoryFlow &left, const DailyFactoryFlow &right) { return left.address.address() < right.address.address(); });
        ++factory_flow_observed_dates_;
    }
#pragma optimize("", on)

#pragma optimize("", off)
    CountryEconomyDay *FactoryEconomyCollector::CountryEconomyDayFor(std::string_view tag, size_t *count)
    {
        for (size_t index = 0; index < *count; ++index) if (std::string_view(country_economy_days_[index].country_tag.data(), 3) == tag) return &country_economy_days_[index];
        if (*count >= country_economy_days_.size() || tag.size() != 3) return nullptr;
        auto &day = country_economy_days_[(*count)++]; day = {}; std::copy_n(tag.data(), 3, day.country_tag.data()); return &day;
    }

    bool FactoryEconomyCollector::AddCountryEconomyValue(int64_t quantity_raw, int64_t nominal_price_raw,
        int64_t real_price_raw, long double *nominal, long double *real)
    {
        if (quantity_raw < 0 || nominal_price_raw < 0 || real_price_raw < 0) return false;
        constexpr long double scale = 32768.0L * 32768.0L;
        *nominal += static_cast<long double>(quantity_raw) * nominal_price_raw / scale;
        *real += static_cast<long double>(quantity_raw) * real_price_raw / scale;
        return std::isfinite(*nominal) && std::isfinite(*real);
    }

    void FactoryEconomyCollector::EmitCountryEconomyPeriod(const smedley::telemetry::CaptureRule &rule,
        size_t rule_index, bool final)
    {
        if (!country_economy_accumulator_.active()) return;
        runtime_->Poll(rule_index);
        for (size_t index = 0; index < country_economy_accumulator_.period_count(); ++index) {
            const auto &period = country_economy_accumulator_.period(index); const std::string_view tag(period.country_tag.data(), 3);
            if (!HasCountryTag(rule, tag) || period.observation_days == 0) continue;
            const bool complete = country_economy_accumulator_.observed_days() == country_economy_accumulator_.expected_days() && period.observation_days == country_economy_accumulator_.observed_days() && period.invalid_days == 0;
            const long double population = period.population_sum / period.observation_days;
            long double nominal = 0, real = 0; for (size_t component = 0; component < country_economy_component_count; ++component) { nominal += period.nominal[component]; real += period.real[component]; }
            const auto country = StringField("country_tag", tag);
            if (HasField(rule, "totals")) {
                const SmedleyTelemetryFieldV1 interval[] = {IntField("period_start_raw", country_economy_accumulator_.period_start_raw()), IntField("period_end_raw", country_economy_accumulator_.period_end_raw()), IntField("observation_days", period.observation_days), IntField("expected_days", country_economy_accumulator_.expected_days()), IntField("invalid_days", period.invalid_days), BoolField("complete", complete), DoubleField("population_average", static_cast<double>(population))};
                runtime_->Attempt(rule_index); runtime_->Account(rule_index, runtime_->EmitState("country.economy.interval", country_economy_accumulator_.period_end_raw(), &country, 1, interval, 7, true, final));
                const SmedleyTelemetryFieldV1 totals[] = {DoubleField("nominal_gdp", static_cast<double>(nominal)), DoubleField("real_gdp", static_cast<double>(real)), IntField("base_date_raw", country_economy_base_date_raw_), DoubleField("gold_to_cash_rate", *gold_to_cash_rate_)};
                runtime_->Attempt(rule_index); runtime_->Account(rule_index, runtime_->EmitState("country.economy.total", country_economy_accumulator_.period_end_raw(), &country, 1, totals, 4, true, final));
                const auto quality = IntField("unsettled_output_candidates", static_cast<int64_t>(std::min<uint64_t>(period.unsettled_factory_candidates, (std::numeric_limits<int64_t>::max)())));
                runtime_->Attempt(rule_index); runtime_->Account(rule_index, runtime_->EmitState("country.economy.quality", country_economy_accumulator_.period_end_raw(), &country, 1, &quality, 1, true, final));
            }
            if (HasField(rule, "per_capita") && population > 0) { const SmedleyTelemetryFieldV1 payload[] = {DoubleField("nominal_gdp_per_capita", static_cast<double>(nominal / population)), DoubleField("real_gdp_per_capita", static_cast<double>(real / population))}; runtime_->Attempt(rule_index); runtime_->Account(rule_index, runtime_->EmitState("country.economy.per_capita", country_economy_accumulator_.period_end_raw(), &country, 1, payload, 2, true, final)); }
            if (HasField(rule, "components")) for (size_t component = 0; component < country_economy_component_count; ++component) { const SmedleyTelemetryFieldV1 entities[] = {country, StringField("component", CountryEconomyComponentName(static_cast<CountryEconomyComponent>(component)))}; const SmedleyTelemetryFieldV1 payload[] = {DoubleField("nominal_value_added", static_cast<double>(period.nominal[component])), DoubleField("real_value_added", static_cast<double>(period.real[component]))}; runtime_->Attempt(rule_index); runtime_->Account(rule_index, runtime_->EmitState("country.economy.component", country_economy_accumulator_.period_end_raw(), entities, 2, payload, 2, true, final)); }
        }
    }

    bool FactoryEconomyCollector::CollectCountryEconomyDay(const smedley::game_state::TelemetryCurrentState &game_state,
        int32_t date_raw, const smedley::telemetry::CaptureRule &rule, size_t rule_index)
    {
        size_t day_count = 0; uint32_t market_count = 0; std::array<smedley::game_state::WorldMarketSnapshot, 64> market{};
        if (!smedley::game_state::CollectWorldMarket(game_state.game_state, market.data(), market.size(), &market_count) || market_count == 0) return false;
        std::array<int64_t, 64> prices{}; std::array<bool, 64> price_seen{};
        for (uint32_t index = 0; index < market_count; ++index) { const auto &value = market[index]; if (value.good_ordinal < 0 || value.good_ordinal >= static_cast<int32_t>(prices.size()) || value.price_raw < 0) return false; prices[value.good_ordinal] = value.price_raw; price_seen[value.good_ordinal] = true; }
        if (!country_economy_base_ready_) { country_economy_base_prices_ = prices; country_economy_base_price_seen_ = price_seen; country_economy_base_date_raw_ = date_raw; country_economy_base_ready_ = true; }
        const auto valid_good = [&](int32_t good) { return good >= 0 && good < static_cast<int32_t>(prices.size()) && price_seen[good] && country_economy_base_price_seen_[good]; };
        for (uint32_t ordinal = 1; ordinal < max_world_countries; ++ordinal) {
            const auto ref = smedley::game_state::ResolveCountry(game_state.game_state, ordinal); smedley::game_state::TelemetryCountrySnapshot snapshot{};
            const auto *country = smedley::game_state::ReadTelemetryCountry(ref, &snapshot) ? &snapshot : nullptr; const auto tag = Tag(country);
            if (!tag || !HasCountryTag(rule, *tag)) continue;
            auto *day = CountryEconomyDayFor(*tag, &day_count); if (day == nullptr) return false; if (factory_hook_dropped_ != 0) day->complete = false;
            uint32_t factory_count = 0, input_count = 0, flags = 0;
            if (!smedley::game_state::CollectCountryFactories(ref, factory_snapshots_.data(), factory_snapshots_.size(), &factory_count, factory_input_snapshots_.data(), factory_input_snapshots_.size(), &input_count, smedley::game_state::FACTORY_PRODUCTION, &flags) || flags != 0) { day->complete = false; continue; }
            for (uint32_t factory_index = 0; factory_index < factory_count; ++factory_index) {
                const auto &factory = factory_snapshots_[factory_index]; if (factory.output_raw > 0 && factory_flow_observed_dates_ < 2) day->complete = false;
                const auto found = std::lower_bound(factory_daily_flows_.begin(), factory_daily_flows_.begin() + factory_daily_flow_count_, factory.address.address(), [](const DailyFactoryFlow &candidate, uint64_t address) { return candidate.address.address() < address; });
                auto *flow = found != factory_daily_flows_.begin() + factory_daily_flow_count_ && found->address.address() == factory.address.address() ? &*found : nullptr;
                if (flow != nullptr) { const bool mismatch = flow->identity_seen && (flow->state_id != factory.state_id || std::strncmp(flow->factory_type.data(), factory.factory_type, flow->factory_type.size()) != 0); if (mismatch) flow->consumption_valid = false; flow->identity_seen = true; flow->state_id = factory.state_id; std::copy_n(factory.factory_type, flow->factory_type.size(), flow->factory_type.data()); const auto retained = std::lower_bound(factory_previous_flows_.begin(), factory_previous_flows_.begin() + factory_previous_flow_count_, factory.address.address(), [](const DailyFactoryFlow &candidate, uint64_t address) { return candidate.address.address() < address; }); if (retained != factory_previous_flows_.begin() + factory_previous_flow_count_ && retained->address.address() == factory.address.address()) { retained->identity_seen = true; retained->state_id = factory.state_id; retained->factory_type = flow->factory_type; } }
                if (flow != nullptr && !flow->consumption_valid) { day->complete = false; continue; }
                if (flow == nullptr) { if (factory.output_raw > 0 && factory_flow_observed_dates_ >= 2) ++day->unsettled_factory_candidates; continue; }
                if (!valid_good(factory.output_good_ordinal) || !AddCountryEconomyValue(factory.output_raw, prices[factory.output_good_ordinal], country_economy_base_prices_[factory.output_good_ordinal], &day->nominal[0], &day->real[0])) day->complete = false;
                for (size_t good = 0; good < flow->consumed_raw.size(); ++good) { if (flow->consumed_raw[good] == 0) continue; long double nominal = 0, real = 0; if (!valid_good(static_cast<int32_t>(good)) || !AddCountryEconomyValue(flow->consumed_raw[good], prices[good], country_economy_base_prices_[good], &nominal, &real)) { day->complete = false; continue; } day->nominal[0] -= nominal; day->real[0] -= real; }
            }
        }
        size_t province_count = 0; smedley::game_state::EmploymentRegistryRef registry{};
        if (!game_state.province_count_candidate(&province_count) || !(registry = smedley::game_state::ResolveStateEmploymentRegistry())) return false;
        for (size_t province_id = 0; province_id < province_count; ++province_id) {
            smedley::game_state::TelemetryProvinceSnapshot province_snapshot{};
            const auto *province = smedley::game_state::ReadTelemetryProvince(smedley::game_state::ResolveProvince(game_state.game_state, static_cast<int>(province_id)), &province_snapshot) ? &province_snapshot : nullptr;
            if (province == nullptr || !province->owner_candidate().normalized_candidate()) continue;
            const std::string_view tag(province->owner_candidate().str(), 3); if (!HasCountryTag(rule, tag)) continue;
            auto *day = CountryEconomyDayFor(tag, &day_count); if (day == nullptr) return false;
            smedley::game_state::RgoSnapshot rgo{};
            if (!smedley::game_state::ReadProvinceRgo(registry, province->province, static_cast<int32_t>(province_id), province_count, smedley::game_state::RGO_IDENTITY | smedley::game_state::RGO_PRODUCTION, &rgo)) { day->complete = false; continue; }
            if (rgo.gross_output_raw < 0) return false;
            if (std::strcmp(rgo.output_good, "precious_metal") == 0) { const long double value = static_cast<long double>(rgo.gross_output_raw) * *gold_to_cash_rate_ / 32768.0L; if (!std::isfinite(value) || !std::isfinite(day->nominal[1] + value) || !std::isfinite(day->real[1] + value)) day->complete = false; else { day->nominal[1] += value; day->real[1] += value; } }
            else if (!valid_good(rgo.output_good_ordinal) || !AddCountryEconomyValue(rgo.gross_output_raw, prices[rgo.output_good_ordinal], country_economy_base_prices_[rgo.output_good_ordinal], &day->nominal[1], &day->real[1])) day->complete = false;
        }
        economic_capture_->InvalidatePopulationCache(); const PopulationCapture population = economic_capture_->CollectPopulation(game_state.game_state, date_raw); runtime_->CollectionTime(rule_index, population.collection_us);
        if (!population.complete()) return false;
        for (uint32_t index = 0; index < population.pop_count; ++index) {
            const auto &detail = economic_capture_->population_detail(index); smedley::game_state::TelemetryProvinceSnapshot province_snapshot{};
            const auto *province = smedley::game_state::ReadTelemetryProvince(smedley::game_state::ResolveProvince(game_state.game_state, detail.province_id_candidate), &province_snapshot) ? &province_snapshot : nullptr;
            if (province == nullptr || !province->owner_candidate().normalized_candidate()) continue;
            const std::string_view tag(province->owner_candidate().str(), 3); if (!HasCountryTag(rule, tag)) continue;
            auto *day = CountryEconomyDayFor(tag, &day_count); if (day == nullptr || detail.size_candidate < 0 || day->population > (std::numeric_limits<int64_t>::max)() - detail.size_candidate) return false;
            day->population += detail.size_candidate; if (detail.pop_type_id_candidate != 2) continue;
            smedley::game_state::ArtisanSnapshot artisan{}; std::array<smedley::game_state::ArtisanInputSnapshot, 64> inputs{}; uint32_t input_count = 0;
            if (!smedley::game_state::ReadArtisanSnapshot(economic_capture_->population_candidate(index).address, &artisan, inputs.data(), inputs.size(), &input_count, smedley::game_state::ARTISAN_IDENTITY | smedley::game_state::ARTISAN_PRODUCTION | smedley::game_state::ARTISAN_INPUTS)) { int32_t inactive = -1; if (!smedley::game_state::ReadInactiveArtisan(economic_capture_->population_candidate(index).address, &inactive)) day->complete = false; continue; }
            if (!valid_good(artisan.output_good_ordinal) || !AddCountryEconomyValue(artisan.gross_output_raw, prices[artisan.output_good_ordinal], country_economy_base_prices_[artisan.output_good_ordinal], &day->nominal[2], &day->real[2])) day->complete = false;
            for (uint32_t input_index = 0; input_index < input_count; ++input_index) { const auto &input = inputs[input_index]; if (!valid_good(input.good_ordinal) || input.need_raw < 0 || artisan.current_producing_raw < 0) { day->complete = false; continue; } const long double consumed = static_cast<long double>(input.need_raw) * artisan.current_producing_raw / 32768.0L; day->nominal[2] -= consumed * prices[input.good_ordinal] / (32768.0L * 32768.0L); day->real[2] -= consumed * country_economy_base_prices_[input.good_ordinal] / (32768.0L * 32768.0L); }
        }
        for (size_t index = 0; index < day_count; ++index) { for (size_t component = 0; component < country_economy_component_count; ++component) if (!std::isfinite(country_economy_days_[index].nominal[component]) || !std::isfinite(country_economy_days_[index].real[component])) return false; if (!country_economy_accumulator_.AddDay(date_raw, country_economy_days_[index])) return false; }
        return true;
    }

    void FactoryEconomyCollector::ProcessCountryEconomy(const smedley::game_state::TelemetryCurrentState &game_state, int32_t date_raw)
    {
        size_t rule_index = 0; const auto *rule = runtime_->FindRule("country.economy", &rule_index); if (rule == nullptr) return;
        if (!smedley::telemetry::IsDateInRange(*rule, date_raw)) { if (country_economy_accumulator_.active() && rule->end_date_raw && date_raw > *rule->end_date_raw) { EmitCountryEconomyPeriod(*rule, rule_index); country_economy_accumulator_.Reset(); } return; }
        const auto transition = country_economy_accumulator_.ObserveDate(date_raw, rule->cadence, rule->fixed_days);
        if (transition == EconomyDateTransition::Regression) { country_economy_accumulator_.Reset(); country_economy_base_ready_ = false; factory_flow_observed_dates_ = 1; country_economy_accumulator_.StartPeriod(date_raw, rule->cadence, rule->fixed_days); }
        else if (transition == EconomyDateTransition::NewPeriod) { EmitCountryEconomyPeriod(*rule, rule_index); country_economy_accumulator_.StartPeriod(date_raw, rule->cadence, rule->fixed_days); }
        if (!CollectCountryEconomyDay(game_state, date_raw, *rule, rule_index)) runtime_->Invalid(rule_index);
    }

    void FactoryEconomyCollector::Flush()
    {
        size_t rule_index = 0; if (const auto *rule = runtime_->FindRule("country.economy", &rule_index); rule != nullptr) { EmitCountryEconomyPeriod(*rule, rule_index, true); country_economy_accumulator_.Reset(); }
    }
#pragma optimize("", on)
// MSVC's x86 optimizer exhausts its 32-bit heap on these field-heavy emitters.
#pragma optimize("", off)
    __declspec(noinline) void FactoryEconomyCollector::EmitFactoryDailyConsumption(int32_t date_raw, size_t rule_index,
        std::string_view country_tag, uint32_t factory_count, bool reliable)
    {
        for (uint32_t index = 0; index < factory_count; ++index) {
            const auto &factory = factory_snapshots_[index];
            const auto flow = std::lower_bound(factory_daily_flows_.begin(), factory_daily_flows_.begin() + factory_daily_flow_count_, factory.address.address(), [](const DailyFactoryFlow &candidate, uint64_t address) { return candidate.address.address() < address; });
            const auto *daily = flow != factory_daily_flows_.begin() + factory_daily_flow_count_ && flow->address.address() == factory.address.address() ? &*flow : nullptr;
            const SmedleyTelemetryFieldV1 entities[] = {StringField("country_tag", country_tag), IntField("state_id", factory.state_id), StringField("factory_type", factory.factory_type)};
            const auto seen = BoolField("consumption_seen", daily != nullptr && daily->consumption_valid);
            runtime_->Attempt(rule_index); runtime_->Account(rule_index, runtime_->EmitState("state.factory.input.consumption.summary", date_raw, entities, 3, &seen, 1, reliable));
            if (daily == nullptr || !daily->consumption_valid) continue;
            for (size_t good = 0; good < daily->consumed_raw.size(); ++good) {
                if (daily->consumed_raw[good] == 0) continue;
                const SmedleyTelemetryFieldV1 input_entities[] = {entities[0], entities[1], entities[2], IntField("good_ordinal", good)};
                const auto consumed = IntField("consumed_raw", daily->consumed_raw[good]);
                runtime_->Attempt(rule_index); runtime_->Account(rule_index, runtime_->EmitState("state.factory.input.consumption", date_raw, input_entities, 4, &consumed, 1, reliable));
            }
        }
    }

    void FactoryEconomyCollector::CollectFactories(smedley::events::DailyUpdateEvent &event, int32_t date_raw)
    {
        size_t rule_index = 0;
        const auto *rule = runtime_->DueRule("state.factory", date_raw, &rule_index);
        if (rule == nullptr) return;
        runtime_->PollOnce(rule_index, date_raw);
        smedley::game_state::TelemetryCountrySnapshot country_snapshot{};
        const auto *country = ReadCountry(event, &country_snapshot) ? &country_snapshot : nullptr;
        const auto tag = Tag(country);
        if (country != nullptr && !tag) { runtime_->Invalid(rule_index); return; }
        if (!tag || !HasCountryTag(*rule, *tag)) return;
        uint32_t factory_count = 0, input_count = 0, flags = 0, groups = 0;
        if (HasField(*rule, "identity")) groups |= smedley::game_state::FACTORY_IDENTITY;
        if (HasField(*rule, "employment")) groups |= smedley::game_state::FACTORY_EMPLOYMENT;
        if (HasField(*rule, "production")) groups |= smedley::game_state::FACTORY_PRODUCTION;
        if (HasField(*rule, "finance")) groups |= smedley::game_state::FACTORY_FINANCE;
        if (HasField(*rule, "inputs")) groups |= smedley::game_state::FACTORY_INPUTS;
        if (HasField(*rule, "sales")) groups |= smedley::game_state::FACTORY_IDENTITY | smedley::game_state::FACTORY_PRODUCTION | smedley::game_state::FACTORY_FINANCE;
        if (!smedley::game_state::CollectCountryFactories(country->country, factory_snapshots_.data(), factory_snapshots_.size(), &factory_count, factory_input_snapshots_.data(), factory_input_snapshots_.size(), &input_count, groups, &flags)) { runtime_->Invalid(rule_index); return; }
        const bool reliable = false;
        if (HasField(*rule, "flows")) {
            for (uint32_t index = 0; index < factory_count; ++index) factory_hook_aggregates_[index] = {};
            for (uint32_t record_index = 0; record_index < factory_hook_record_count_; ++record_index) {
                const auto &record = factory_hook_records_[record_index];
                for (uint32_t factory_index = 0; factory_index < factory_count; ++factory_index) {
                    if (factory_snapshots_[factory_index].address.address() != record.factory.address()) continue;
                    auto &aggregate = factory_hook_aggregates_[factory_index];
                    if (record.pool > 3) { runtime_->Invalid(rule_index); break; }
                    auto &target = record.pool == 0 ? aggregate.opening_raw : record.pool == 1 ? aggregate.pre_add_raw : record.pool == 2 ? aggregate.first_raw : aggregate.second_raw;
                    auto candidate = target; bool overflow = false;
                    for (size_t good = 0; good < candidate.size(); ++good) { const auto amount = record.quantity_raw[good]; if ((amount > 0 && candidate[good] > (std::numeric_limits<int64_t>::max)() - amount) || (amount < 0 && candidate[good] < (std::numeric_limits<int64_t>::min)() - amount)) { overflow = true; break; } candidate[good] += amount; }
                    if (overflow) runtime_->Invalid(rule_index); else { target = candidate; if (record.pool == 0) aggregate.opening_seen = true; else if (record.pool == 1) aggregate.pre_add_seen = true; else if (record.pool == 2) aggregate.first_seen = true; else { aggregate.second_seen = true; ++aggregate.pair_count; } }
                    break;
                }
            }
            EmitFactoryDailyConsumption(date_raw, rule_index, *tag, factory_count, reliable);
        }
        for (uint32_t index = 0; index < factory_count; ++index) {
            const auto &snapshot = factory_snapshots_[index];
            const SmedleyTelemetryFieldV1 entities[] = {StringField("country_tag", *tag), IntField("state_id", snapshot.state_id), StringField("factory_type", snapshot.factory_type)};
            const auto emit = [&](const char *event, const SmedleyTelemetryFieldV1 *payload, uint32_t count) { runtime_->Attempt(rule_index); runtime_->Account(rule_index, runtime_->EmitState(event, date_raw, entities, 3, payload, count, reliable)); };
            if (HasField(*rule, "identity")) { const SmedleyTelemetryFieldV1 identity_entities[] = {entities[0], entities[1], StringField("state_region_key", snapshot.state_region_key), entities[2]}; const SmedleyTelemetryFieldV1 payload[] = {IntField("anchor_province_id_candidate", snapshot.anchor_province_id_candidate), IntField("level", snapshot.level), BoolField("subsidized", snapshot.subsidized), BoolField("closed", snapshot.closed)}; runtime_->Attempt(rule_index); runtime_->Account(rule_index, runtime_->EmitState("state.factory.identity", date_raw, identity_entities, 4, payload, 4, reliable)); }
            if (HasField(*rule, "employment")) { const SmedleyTelemetryFieldV1 payload[] = {IntField("employee_count", snapshot.employee_count), IntField("craftsmen_count", snapshot.craftsmen_count), IntField("clerk_count", snapshot.clerk_count)}; emit("state.factory.employment", payload, 3); }
            if (HasField(*rule, "production")) { const SmedleyTelemetryFieldV1 payload[] = {IntField("output_raw", snapshot.output_raw), IntField("output_good_ordinal", snapshot.output_good_ordinal), StringField("output_good", snapshot.output_good), IntField("base_output_raw", snapshot.base_output_raw)}; emit("state.factory.production", payload, 4); }
            if (HasField(*rule, "finance")) { const SmedleyTelemetryFieldV1 payload[] = {IntField("budget_raw", snapshot.budget_raw), IntField("market_spending_expense_raw", snapshot.market_spending_raw), IntField("sales_income_raw", snapshot.sales_income_raw), IntField("paychecks_expense_raw", snapshot.paychecks_raw), IntField("investment_income_raw", snapshot.investment_raw)}; emit("state.factory.finance", payload, 5); }
            if (HasField(*rule, "sales")) {
                const auto begin = factory_sales_hook_records_; const auto end = begin + factory_sales_hook_record_count_;
                const auto match = std::lower_bound(begin, end, snapshot.address.address(), [](const services::FactorySalesHookRecord &candidate, uint64_t address) { return candidate.factory.address() < address; });
                const auto match_end = std::upper_bound(match, end, snapshot.address.address(), [](uint64_t address, const services::FactorySalesHookRecord &candidate) { return address < candidate.factory.address(); });
                const auto settlements = static_cast<int64_t>(match_end - match); ProducerSale sale{};
                const bool complete = settlements == 1 && ReconcileProducerSale(match->opening_inventory_raw, match->produced_raw, match->closing_inventory_raw, match->proceeds_raw, &sale) && match->produced_raw == snapshot.output_raw && match->proceeds_raw == snapshot.sales_income_raw;
                const SmedleyTelemetryFieldV1 summary[] = {BoolField("settlement_seen", settlements != 0), IntField("settlement_count", settlements), BoolField("complete", complete)}; emit("state.factory.sales.summary", summary, 3);
                if (settlements > 1 || (settlements == 1 && !complete)) runtime_->Invalid(rule_index);
                if (complete) { const SmedleyTelemetryFieldV1 quantity[] = {IntField("output_good_ordinal", snapshot.output_good_ordinal), IntField("opening_inventory_raw", sale.opening_inventory_raw), IntField("produced_raw", sale.produced_raw), IntField("sold_raw", sale.sold_raw), IntField("closing_inventory_raw", sale.closing_inventory_raw)}; emit("state.factory.sales.quantity", quantity, 5); const auto proceeds = IntField("proceeds_raw", sale.proceeds_raw); emit("state.factory.sales.revenue", &proceeds, 1); }
            }
        }
        if (HasField(*rule, "inputs")) for (uint32_t index = 0; index < input_count; ++index) { const auto &input = factory_input_snapshots_[index]; if (input.factory_snapshot_index >= factory_count) { runtime_->Invalid(rule_index); continue; } const auto &factory = factory_snapshots_[input.factory_snapshot_index]; const SmedleyTelemetryFieldV1 entities[] = {StringField("country_tag", *tag), IntField("state_id", factory.state_id), StringField("factory_type", factory.factory_type), IntField("good_ordinal", input.good_ordinal)}; const SmedleyTelemetryFieldV1 payload[] = {IntField("stockpile_raw", input.stockpile_raw), IntField("requested_raw", input.requested_raw)}; runtime_->Attempt(rule_index); runtime_->Account(rule_index, runtime_->EmitState("state.factory.input", date_raw, entities, 4, payload, 2, reliable)); }
        if (HasField(*rule, "flows")) for (uint32_t index = 0; index < factory_count; ++index) { const auto &factory = factory_snapshots_[index]; const auto &flow = factory_hook_aggregates_[index]; const SmedleyTelemetryFieldV1 entities[] = {StringField("country_tag", *tag), IntField("state_id", factory.state_id), StringField("factory_type", factory.factory_type)}; const SmedleyTelemetryFieldV1 summary[] = {BoolField("post_consumption_seen", flow.opening_seen), BoolField("pre_purchase_seen", flow.pre_add_seen), BoolField("primary_delivery_seen", flow.first_seen), BoolField("secondary_delivery_seen", flow.second_seen), IntField("settlement_count", flow.pair_count)}; runtime_->Attempt(rule_index); runtime_->Account(rule_index, runtime_->EmitState("state.factory.input.flow.summary", date_raw, entities, 3, summary, 5, reliable)); for (size_t good = 0; good < flow.first_raw.size(); ++good) { if (flow.opening_raw[good] == 0 && flow.pre_add_raw[good] == 0 && flow.first_raw[good] == 0 && flow.second_raw[good] == 0) continue; const SmedleyTelemetryFieldV1 input_entities[] = {entities[0], entities[1], entities[2], IntField("good_ordinal", good)}; const SmedleyTelemetryFieldV1 payload[] = {IntField("post_consumption_raw", flow.opening_raw[good]), IntField("pre_purchase_raw", flow.pre_add_raw[good]), IntField("delivered_primary_raw", flow.first_raw[good]), IntField("delivered_secondary_raw", flow.second_raw[good])}; runtime_->Attempt(rule_index); runtime_->Account(rule_index, runtime_->EmitState("state.factory.input.flow", date_raw, input_entities, 4, payload, 4, reliable)); } }
    }
#pragma optimize("", on)
}
