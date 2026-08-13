#include "telemetry_services.hpp"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace telemetry_plugin::services
{
    namespace
    {
        template <class T> void Initialize(T *value, uint32_t version)
        {
            *value = {}; value->struct_size = sizeof(*value); value->version = version;
        }
        bool ValidTag(const char tag[4]) { return tag[3] == '\0' && tag[0] != '\0' && tag[1] != '\0' && tag[2] != '\0'; }
        uint32_t MarketGroups(uint32_t groups)
        {
            return groups == 0 ? SMEDLEY_TELEMETRY_OBSERVATION_GROUP_MARKET_PRICE | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_MARKET_SUPPLY
                | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_MARKET_DEMAND | SMEDLEY_TELEMETRY_OBSERVATION_GROUP_MARKET_SALES : groups;
        }
    }

    bool Api::Acquire(std::string *error)
    {
        const HMODULE kernel = GetModuleHandleW(L"smedley_kernel.dll");
        if (kernel == nullptr) { *error = "smedley kernel is not loaded"; return false; }
        const auto events = reinterpret_cast<SmedleyGetEventApiV1Fn>(GetProcAddress(kernel, SMEDLEY_EVENT_GET_API_V1_SYMBOL));
        const auto logging = reinterpret_cast<SmedleyGetLoggingApiV1Fn>(GetProcAddress(kernel, SMEDLEY_LOGGING_GET_API_V1_SYMBOL));
        const auto game = reinterpret_cast<SmedleyGetTelemetryGameApiV1Fn>(GetProcAddress(kernel, SMEDLEY_TELEMETRY_GAME_GET_API_V1_SYMBOL));
        const auto observations = reinterpret_cast<SmedleyGetTelemetryObservationApiV1Fn>(GetProcAddress(kernel, SMEDLEY_TELEMETRY_OBSERVATION_GET_API_V1_SYMBOL));
        if (!events || !logging || !game || !observations) { *error = "required telemetry C services are unavailable"; return false; }
        Initialize(&event_api_, SMEDLEY_EVENT_API_VERSION_V1); Initialize(&logging_api_, SMEDLEY_LOGGING_API_VERSION_V1);
        Initialize(&game_api_, SMEDLEY_TELEMETRY_GAME_API_VERSION_V1); Initialize(&observation_api_, SMEDLEY_TELEMETRY_OBSERVATION_API_VERSION_V1);
        if (events(&event_api_) != SMEDLEY_EVENT_SUCCESS || logging(&logging_api_) != SMEDLEY_LOGGING_SUCCESS
            || game(&game_api_) != SMEDLEY_TELEMETRY_GAME_SUCCESS || observations(&observation_api_) != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS
            || !event_api_.register_daily || !event_api_.unregister || !logging_api_.write || !game_api_.open_session || !game_api_.close_session
            || !game_api_.subscribe_hooks || !game_api_.drain_hooks || !game_api_.unsubscribe_hooks || !observation_api_.open_session
            || !observation_api_.close_session || !observation_api_.read_world || !observation_api_.read_country || !observation_api_.read_province
            || !observation_api_.read_market || !observation_api_.read_country_economy || !observation_api_.read_pops
            || !observation_api_.read_factories || !observation_api_.read_rgo
            || !observation_api_.read_pop_identity || !observation_api_.read_pop_needs || !observation_api_.read_artisan || !observation_api_.resolve_daily_country) {
            *error = "required telemetry C service version is unavailable"; return false;
        }
        return true;
    }
    bool Api::Open(std::string *error)
    {
        try {
            pop_values_ = std::make_unique<std::array<SmedleyTelemetryPopObservationV1, max_sample_pops>>();
            factory_values_ = std::make_unique<std::array<SmedleyTelemetryFactoryObservationV1, max_sample_factories>>();
            factory_input_values_ = std::make_unique<std::array<SmedleyTelemetryFactoryInputObservationV1, max_sample_factory_inputs>>();
        } catch (...) {
            *error = "telemetry observation scratch storage is unavailable";
            return false;
        }
        if (game_api_.open_session(&session_) != SMEDLEY_TELEMETRY_GAME_SUCCESS || session_ == 0) { *error = "telemetry parent session is unavailable"; return false; }
        if (observation_api_.open_session(session_, &observation_) != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS || observation_ == 0) { Close(); *error = "telemetry observation session is unavailable"; return false; }
        return true;
    }
    bool Api::EnsureOpen(uint32_t hooks, const uint32_t *artisan_keys, uint32_t artisan_count) noexcept
    {
        TelemetryCurrentState current{};
        if (usable() && ReadCurrent(&current)) return true;
        Close();
        std::string error;
        return Open(&error) && Subscribe(hooks, artisan_keys, artisan_count, &error);
    }
    bool Api::Close()
    {
        bool complete = true;
        if (hooks_ != 0) complete = Unsubscribe() && complete;
        if (observation_ != 0) { complete = observation_api_.close_session(observation_) == SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS && complete; observation_ = 0; }
        if (session_ != 0) { complete = game_api_.close_session(session_) == SMEDLEY_TELEMETRY_GAME_SUCCESS && complete; session_ = 0; }
        pop_values_.reset(); factory_values_.reset(); factory_input_values_.reset();
        return complete;
    }
    bool Api::Subscribe(uint32_t hooks, const uint32_t *artisan_keys, uint32_t artisan_count, std::string *error)
    {
        if (hooks == 0) return true;
        SmedleyTelemetryHookOptionsV1 options{}; Initialize(&options, SMEDLEY_TELEMETRY_GAME_API_VERSION_V1);
        options.hooks = hooks; options.artisan_country_count = artisan_count;
        if (artisan_count != 0) std::memcpy(options.artisan_country_keys, artisan_keys, artisan_count * sizeof(*artisan_keys));
        if (game_api_.subscribe_hooks(session_, &options, &hooks_) != SMEDLEY_TELEMETRY_GAME_SUCCESS || hooks_ == 0) { *error = "telemetry hook subscription is unavailable"; return false; }
        constexpr uint32_t capacity = max_pop_cash_flow_records + max_factory_flow_records + max_factory_sales_records + max_artisan_flow_records;
        try { hook_records_.resize(capacity); }
        catch (...) { Unsubscribe(); *error = "telemetry hook drain storage is unavailable"; return false; }
        return true;
    }
    void Api::BeginHookDrain()
    {
        hooks_drained_ = false;
        hook_dropped_ = 0;
        hook_record_count_ = 0;
    }
    bool Api::Unsubscribe()
    {
        if (hooks_ == 0) return true;
        const bool complete = game_api_.unsubscribe_hooks(hooks_) == SMEDLEY_TELEMETRY_GAME_SUCCESS;
        hooks_ = 0; hook_records_.clear(); hook_record_count_ = 0; return complete;
    }
    bool Api::ObservationOk(SmedleyTelemetryObservationResult result) const
    {
        return result == SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS;
    }
    bool Api::ReadCurrent(TelemetryCurrentState *out) const
    {
        SmedleyTelemetryWorldObservationV1 value{}; Initialize(&value, 1);
        if (!ObservationOk(observation_api_.read_world(observation_, &value))) return false;
        out->date_raw = value.date_raw; out->country_count_value = value.country_count; out->country_ai_count_value = value.country_ai_count;
        out->province_count_value = value.province_count; out->availability_flags = value.availability_flags | (value.human_control_present ? 0x80000000u : 0);
        out->ongoing_war_count_value = value.ongoing_war_count; return true;
    }
    bool Api::ReadCountry(CountryRef country, TelemetryCountrySnapshot *out) const
    {
        SmedleyTelemetryCountryObservationV1 value{}; Initialize(&value, 1);
        if (!ObservationOk(observation_api_.read_country(observation_, country.ordinal, &value)) || !ValidTag(value.tag)) return false;
        *out = {}; out->country = country; std::memcpy(out->tag_value.value, value.tag, 4); std::memcpy(out->overlord_value.value, value.overlord_tag, 4); std::memcpy(out->sphere_value.value, value.sphere_leader_tag, 4);
        out->availability_flags = value.availability_flags; out->unit_count_value = value.unit_count; out->scheduled_mobilization_count_value = value.scheduled_mobilization_count;
        out->sphereling_count_value = value.sphereling_count; out->vassal_count_value = value.vassal_count; out->ally_count_value = value.ally_count; out->guaranteed_count_value = value.guarantee_count; out->neighbor_count_value = value.neighbor_count;
        out->mobilized_value = value.mobilized; out->substate_value = value.substate; out->vassal_value = value.vassal; out->ranking_value = value.ranking; out->military_ranking_value = value.military_ranking; out->industrial_ranking_value = value.industrial_ranking; out->prestige_ranking_value = value.prestige_ranking;
        out->treasury_value = value.treasury_raw; out->prestige_value = value.prestige_raw; out->infamy_value = value.infamy_raw; out->plurality_value = value.plurality_raw; out->war_exhaustion_value = value.war_exhaustion_raw; out->diplomatic_points_value = value.diplomatic_points_raw; out->research_points_value = value.research_points_raw; out->leadership_value = value.leadership_raw;
        return true;
    }
    bool Api::ReadProvince(ProvinceRef province, TelemetryProvinceSnapshot *out) const
    {
        SmedleyTelemetryProvinceObservationV1 value{}; Initialize(&value, 1);
        if (!ObservationOk(observation_api_.read_province(observation_, province.id, &value)) || value.province_id != province.id) return false;
        *out = {}; out->province = province; std::memcpy(out->owner_value.value, value.owner_tag, 4); std::memcpy(out->controller_value.value, value.controller_tag, 4); out->id_value = value.province_id; out->infrastructure_value = value.infrastructure_raw; out->colonial_level_value = value.colonial_level; out->life_rating_value = value.life_rating; out->availability_flags = value.availability_flags; out->building_slot_count_value = value.building_slot_count; out->construction_count_value = value.construction_count; return true;
    }
    bool Api::ReadMarket(WorldMarketSnapshot *out, uint32_t capacity, uint32_t *count, uint32_t groups) const
    {
        std::array<SmedleyTelemetryMarketObservationV1, 64> values{};
        const auto result = observation_api_.read_market(observation_, MarketGroups(groups), values.data(), capacity, count);
        if (result != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS && result != SMEDLEY_TELEMETRY_OBSERVATION_TRUNCATED) return false;
        if (result == SMEDLEY_TELEMETRY_OBSERVATION_TRUNCATED) return false;
        for (uint32_t i = 0; i < *count; ++i) out[i] = {values[i].good_ordinal, values[i].price_raw, values[i].last_price_raw, values[i].supply_raw, values[i].last_supply_raw, values[i].worldmarket_stock_raw, values[i].demand_raw, values[i].real_demand_raw, values[i].actual_sold_raw, values[i].actual_sold_world_raw};
        return true;
    }
    bool Api::ReadPops(CountryRef country, PopCandidate *candidates, PopDetailSnapshot *details, uint32_t capacity, uint32_t *count) const
    {
        if (!pop_values_) return false;
        auto &values = *pop_values_; uint32_t flags = 0;
        const auto result = observation_api_.read_pops(observation_, country.ordinal, values.data(), capacity, count, &flags);
        if (result != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS || flags != 0) return false;
        for (uint32_t i = 0; i < *count; ++i) { candidates[i] = {{values[i].pop}, values[i].savings_raw}; details[i] = {values[i].pop_id, values[i].province_id_candidate, values[i].pop_type_id_candidate, values[i].size_candidate, values[i].employed_candidate, values[i].consciousness_candidate_raw, values[i].militancy_candidate_raw, values[i].literacy_candidate_raw, {values[i].money_raw, values[i].interest_cash_flow_raw, values[i].total_cash_flow_raw, values[i].savings_raw}}; }
        return true;
    }
    bool Api::ReadCountryEconomy(CountryRef country, CountryEconomySnapshot *out) const
    {
        SmedleyTelemetryCountryEconomyObservationV1 value{}; Initialize(&value, 1);
        std::array<SmedleyTelemetryCreditorDestinationObservationV1, 512> destinations{}; uint32_t count = 0;
        const auto result = observation_api_.read_country_economy(observation_, country.ordinal, &value, destinations.data(), static_cast<uint32_t>(destinations.size()), &count);
        if (result != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS) return false;
        *out = {}; out->date_raw = value.date_raw; std::memcpy(out->country_tag, value.country_tag, 4); out->country_ordinal = value.country_ordinal; out->state_count_reported = value.state_count_reported;
        out->states_walked = value.states_walked; out->province_element_candidates = value.province_element_candidates; out->states_with_savings = value.states_with_savings; out->states_with_interest = value.states_with_interest; out->creditor_count = value.creditor_count; out->creditor_destinations = value.creditor_destinations; out->creditors_was_paid = value.creditors_was_paid;
        out->treasury_raw = value.treasury_raw; out->state_savings_raw = value.state_savings_raw; out->state_interest_raw = value.state_interest_raw; out->bank_interest_raw = value.bank_interest_raw; out->creditor_interest_raw = value.creditor_interest_raw; out->creditor_debt_raw = value.creditor_debt_raw; out->destination_bank_interest_raw = value.destination_bank_interest_raw; out->destination_state_savings_raw = value.destination_state_savings_raw; out->destination_state_interest_raw = value.destination_state_interest_raw; out->destination_pop_savings_raw = value.destination_pop_savings_raw; out->destination_pop_savings_state_scale_raw = value.destination_pop_savings_state_scale_raw; out->flags = value.source_flags;
        for (uint32_t index = 0; index < count; ++index) { std::memcpy(&out->destination_keys[index], destinations[index].tag, 4); out->destination_ordinals[index] = destinations[index].country_ordinal; out->destination_bank_interests_raw[index] = destinations[index].bank_interest_raw; }
        return true;
    }
    bool Api::ReadFactories(CountryRef country, FactorySnapshot *factories, uint32_t factory_capacity, uint32_t *factory_count, FactoryInputSnapshot *inputs, uint32_t input_capacity, uint32_t *input_count, uint32_t groups, uint32_t *flags) const
    {
        if (!factory_values_ || !factory_input_values_) return false;
        auto &values = *factory_values_; auto &input_values = *factory_input_values_;
        const auto result = observation_api_.read_factories(observation_, country.ordinal, groups, values.data(), factory_capacity, factory_count, input_values.data(), input_capacity, input_count, flags);
        if (result != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS) return false;
        for (uint32_t i = 0; i < *factory_count; ++i) { auto &source = values[i]; auto &target = factories[i]; target = {}; target.address = {source.factory}; target.state_index = source.state_index; target.factory_index = source.factory_index; target.state_id = source.state_id; target.anchor_province_id_candidate = source.anchor_province_id_candidate; target.level = source.level; target.employee_count = source.employee_count; target.craftsmen_count = source.craftsmen_count; target.clerk_count = source.clerk_count; target.output_raw = source.output_raw; target.output_good_ordinal = source.output_good_ordinal; target.base_output_raw = source.base_output_raw; target.subsidized = source.subsidized != 0; target.closed = source.closed != 0; std::memcpy(target.state_region_key, source.state_region_key, 64); std::memcpy(target.factory_type, source.factory_type, 64); std::memcpy(target.output_good, source.output_good, 64); target.budget_raw = source.budget_raw; target.market_spending_raw = source.market_spending_raw; target.sales_income_raw = source.sales_income_raw; target.paychecks_raw = source.paychecks_raw; target.investment_raw = source.investment_raw; }
        for (uint32_t i = 0; i < *input_count; ++i) { inputs[i] = {input_values[i].factory_observation_index, input_values[i].good_ordinal, input_values[i].stockpile_raw, input_values[i].requested_raw}; }
        return true;
    }
    bool Api::ReadRgo(ProvinceRef province, uint32_t groups, RgoSnapshot *out) const
    {
        SmedleyTelemetryRgoObservationV1 value{}; Initialize(&value, 1);
        if (!ObservationOk(observation_api_.read_rgo(observation_, province.id, groups, &value))) return false;
        *out = {}; out->province_id = value.province_id; out->output_good_ordinal = value.output_good_ordinal; out->employment_capacity = value.employment_capacity; out->employed = value.employed; out->owner_population = value.owner_population; out->state_rgo_employment_capacity = value.state_rgo_employment_capacity; std::memcpy(out->production_type, value.production_type, 64); std::memcpy(out->output_good, value.output_good, 64); out->base_output_per_size_raw = value.base_output_per_size_raw; out->base_size_raw = value.base_size_raw; out->output_efficiency_raw = value.output_efficiency_raw; out->throughput_raw = value.throughput_raw; out->gross_output_raw = value.gross_output_raw; out->owner_output_modifier_raw = value.owner_output_modifier_raw; out->income_raw = value.income_raw; out->percent_sold_domestic_raw = value.percent_sold_domestic_raw; out->percent_sold_export_raw = value.percent_sold_export_raw; out->leftover_raw = value.leftover_raw; return true;
    }
    bool Api::ReadPopIdentity(PopRef pop, PopIdentityDimensions *out) const { SmedleyTelemetryPopIdentityObservationV1 value{}; Initialize(&value, 1); if (!ObservationOk(observation_api_.read_pop_identity(observation_, pop.value, &value))) return false; std::memcpy(out->pop_type_tag_candidate, value.pop_type_tag, 64); std::memcpy(out->culture_tag_candidate, value.culture_tag, 64); std::memcpy(out->religion_tag_candidate, value.religion_tag, 64); return true; }
    bool Api::ReadPopNeeds(PopRef pop, PopNeedsSnapshot *out) const { SmedleyTelemetryPopNeedsObservationV1 value{}; Initialize(&value, 1); if (!ObservationOk(observation_api_.read_pop_needs(observation_, pop.value, &value))) return false; *out = {value.life_satisfaction_raw, value.everyday_satisfaction_raw, value.luxury_satisfaction_raw}; return true; }
    bool Api::ReadArtisan(PopRef pop, ArtisanSnapshot *out, ArtisanInputSnapshot *inputs, uint32_t capacity, uint32_t *count, uint32_t groups, ArtisanReadFailure *failure) const
    {
        SmedleyTelemetryArtisanObservationV1 value{}; SmedleyTelemetryArtisanFailureV1 rejected{}; Initialize(&value, 1); Initialize(&rejected, 1); std::array<SmedleyTelemetryArtisanInputObservationV1, 64> input_values{};
        const auto result = observation_api_.read_artisan(observation_, pop.value, groups, &value, input_values.data(), capacity, count, &rejected);
        if (result != SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS) { if (failure) *failure = {rejected.reason, rejected.pop_id, rejected.offending_raw}; return false; }
        if (value.inactive != 0) return false;
        *out = {}; out->address = pop; out->pop_id = value.pop_id; out->output_good_ordinal = value.output_good_ordinal; std::memcpy(out->production_type, value.production_type, 64); std::memcpy(out->output_good, value.output_good, 64); out->base_output_raw = value.base_output_raw; out->current_producing_raw = value.current_producing_raw; out->gross_output_raw = value.gross_output_raw; out->last_spending_raw = value.last_spending_raw; out->percent_afforded_raw = value.percent_afforded_raw; out->percent_sold_domestic_raw = value.percent_sold_domestic_raw; out->percent_sold_export_raw = value.percent_sold_export_raw; out->leftover_raw = value.leftover_raw; out->throttle_raw = value.throttle_raw; out->needs_cost_raw = value.needs_cost_raw; out->production_income_raw = value.production_income_raw;
        for (uint32_t i = 0; i < *count; ++i) inputs[i] = {input_values[i].good_ordinal, input_values[i].stockpile_raw, input_values[i].need_raw}; return true;
    }
    bool Api::DrainHooks(SmedleyTelemetryHookRecordV1 *records, uint32_t capacity, uint32_t *count, uint64_t *dropped) const { return hooks_ != 0 && game_api_.drain_hooks(hooks_, records, capacity, count, dropped) == SMEDLEY_TELEMETRY_GAME_SUCCESS; }
    bool Api::FillHookCache()
    {
        if (hooks_drained_) return true;
        hooks_drained_ = true;
        uint32_t count = 0;
        if (!DrainHooks(hook_records_.data(), static_cast<uint32_t>(hook_records_.size()), &count, &hook_dropped_)) {
            return false;
        }
        hook_record_count_ = count;
        return true;
    }
    bool Api::DrainFactoryConsumption(FactorySettlementHookRecord *records, uint32_t capacity, uint32_t *count, uint64_t *dropped)
    {
        if (!FillHookCache()) return false;
        *dropped = hook_dropped_;
        *count = 0;
        for (uint32_t index = 0; index < hook_record_count_; ++index) { const auto &source = hook_records_[index]; if (source.kind == SMEDLEY_TELEMETRY_HOOK_FACTORY_CONSUMPTION) {
            if (*count >= capacity || source.value_count != 64) { ++*dropped; continue; }
            auto &out = records[(*count)++]; out = {}; out.factory = {source.entity_id}; out.pool = source.pool;
            std::copy_n(source.values, out.quantity_raw.size(), out.quantity_raw.data());
        }}
        return true;
    }
    bool Api::DrainFactorySales(FactorySalesHookRecord *records, uint32_t capacity, uint32_t *count, uint64_t *dropped)
    {
        if (!FillHookCache()) return false;
        *dropped = hook_dropped_;
        *count = 0;
        for (uint32_t index = 0; index < hook_record_count_; ++index) { const auto &source = hook_records_[index]; if (source.kind == SMEDLEY_TELEMETRY_HOOK_FACTORY_SALES) {
            if (*count >= capacity || source.value_count != 4) { ++*dropped; continue; }
            records[(*count)++] = {{source.entity_id}, source.values[0], source.values[1], source.values[2], source.values[3]};
        }}
        return true;
    }
    bool Api::DrainArtisanConsumption(ArtisanSettlementHookRecord *records, uint32_t capacity, uint32_t *count, uint64_t *dropped)
    {
        if (!FillHookCache()) return false;
        *dropped = hook_dropped_;
        *count = 0;
        for (uint32_t index = 0; index < hook_record_count_; ++index) { const auto &source = hook_records_[index]; if (source.kind == SMEDLEY_TELEMETRY_HOOK_ARTISAN_CONSUMPTION) {
            if (*count >= capacity || source.value_count != 64) { ++*dropped; continue; }
            auto &out = records[(*count)++]; out = {}; out.pop = {source.entity_id}; out.pool = source.pool;
            std::copy_n(source.values, out.quantity_raw.size(), out.quantity_raw.data());
        }}
        return true;
    }
    bool Api::DrainPopCashFlow(PopCashFlowHookRecord *records, uint32_t capacity, uint32_t *count, PopCashFlowHookStats *stats)
    {
        if (!FillHookCache()) return false;
        *count = 0; *stats = {};
        stats->output_overflow = hook_dropped_;
        for (uint32_t index = 0; index < hook_record_count_; ++index) { const auto &source = hook_records_[index]; if (source.kind == SMEDLEY_TELEMETRY_HOOK_POP_CASH_FLOW) {
            if (*count >= capacity || source.value_count != 16) { ++stats->output_overflow; continue; }
            auto &out = records[(*count)++]; out = {}; out.pop = {source.entity_id}; out.call_count = source.call_count; out.clamped_call_count = source.auxiliary_count;
            std::copy_n(source.values, out.posted_raw.size(), out.posted_raw.data());
            std::copy_n(source.values + out.posted_raw.size(), out.money_delta_raw.size(), out.money_delta_raw.data());
        }}
        return true;
    }
    bool Api::ResolveDailyCountry(const SmedleyDailyEventV1 *event, CountryRef *out) const { int32_t ordinal = -1; if (!ObservationOk(observation_api_.resolve_daily_country(observation_, event, &ordinal))) return false; *out = {ordinal}; return true; }
    void Api::Log(SmedleyLogLevel level, const std::string &message) const { static constexpr char component[] = "telemetry"; if (logging_api_.write) logging_api_.write(level, component, sizeof(component) - 1, message.data(), static_cast<uint32_t>((std::min)(message.size(), static_cast<size_t>(SMEDLEY_LOGGING_MAX_MESSAGE_BYTES)))); }
}
