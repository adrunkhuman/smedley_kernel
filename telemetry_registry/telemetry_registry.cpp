#include <smedley/telemetry_registry.hpp>

#include <algorithm>

namespace smedley::telemetry
{
    namespace
    {
        constexpr std::string_view world_daily_fields[] = {"country_slot_count", "ai_scheduler_entry_count", "human_control_present"};
        constexpr std::string_view world_economy_fields[] = {"health", "capacity", "holdings", "credit"};
        constexpr std::string_view world_military_fields[] = {"ongoing_war_count_candidate"};
        constexpr std::string_view country_daily_fields[] = {"treasury_raw", "treasury"};
        constexpr std::string_view country_metrics_fields[] = {"power", "politics"};
        constexpr std::string_view country_economy_fields[] = {"totals", "components", "per_capita"};
        constexpr std::string_view country_military_fields[] = {"unit_count_candidate", "mobilized_candidate", "scheduled_mobilization_count_candidate", "leadership_candidate_raw", "military_ranking_candidate"};
        constexpr std::string_view country_diplomacy_fields[] = {"status", "relations"};
        constexpr std::string_view factory_fields[] = {"identity", "employment", "production", "finance", "inputs", "flows", "sales"};
        constexpr std::string_view market_fields[] = {"price", "supply", "demand", "sales"};
        constexpr std::string_view province_daily_fields[] = {"owner_tag_candidate", "controller_tag_candidate", "colonial_level_candidate", "life_rating_candidate", "infrastructure_candidate_raw"};
        constexpr std::string_view province_production_fields[] = {"building_slot_count_candidate", "construction_count_candidate"};
        constexpr std::string_view rgo_fields[] = {"identity", "employment", "production", "finance", "modifiers", "sales"};
        constexpr std::string_view artisan_fields[] = {"identity", "production", "inputs", "finance", "flows", "sales"};
        constexpr std::string_view pop_economy_fields[] = {"money_raw", "savings_raw", "interest_cash_flow_raw", "total_cash_flow_raw"};
        constexpr std::string_view pop_demographics_fields[] = {"size_candidate", "employed_candidate", "consciousness_candidate_raw", "militancy_candidate_raw", "literacy_candidate_raw"};
        constexpr std::string_view pop_identity_fields[] = {"pop_type_tag_candidate", "culture_tag_candidate", "religion_tag_candidate"};
        constexpr std::string_view pop_needs_fields[] = {"life_satisfaction_candidate_raw", "everyday_satisfaction_candidate_raw", "luxury_satisfaction_candidate_raw"};
        constexpr std::string_view pop_aggregate_fields[] = {"pop_count", "size_candidate", "employed_candidate", "money_raw", "savings_raw"};
        constexpr std::string_view lifecycle_fields[] = {"summary", "appeared", "disappeared", "scope_changed"};
        constexpr std::string_view cashflow_fields[] = {"summary", "account", "components"};

        constexpr MetricEvent world_daily_events[] = {
            {"world.daily", "-", "country_slot_count?,ai_scheduler_entry_count?,human_control_present?"},
        };
        constexpr MetricEvent world_economy_events[] = {
            {"world.economy.health", "-", "complete,snapshot_flags,collection_flags,credit_flags,country_count,state_count,province_count,pop_count"},
            {"world.economy.capacity", "-", "country_limit,province_limit,pop_limit,country_utilization_bp,province_utilization_bp,pop_utilization_bp,collection_us"},
            {"world.economy.holdings", "-", "treasury_observed_raw,pop_money_observed_raw,pop_savings_observed_raw,bank_interest_accumulator_raw,positive_money_pops,positive_savings_pops,negative_treasury_countries"},
            {"world.economy.credit", "-", "creditor_count,creditors_was_paid,countries_with_creditors,creditor_interest_candidate_raw,creditor_debt_candidate_raw,state_savings_candidate_raw,state_interest_candidate_raw"},
        };
        constexpr MetricEvent world_military_events[] = {
            {"world.military", "-", "ongoing_war_count_candidate"},
        };
        constexpr MetricEvent country_daily_events[] = {
            {"country.daily", "country_tag", "treasury_raw?,treasury?"},
        };
        constexpr MetricEvent country_metrics_events[] = {
            {"country.metrics.power", "country_tag", "prestige_candidate_raw,infamy_candidate_raw,ranking_candidate,military_ranking_candidate,industrial_ranking_candidate,prestige_ranking_candidate"},
            {"country.metrics.politics", "country_tag", "plurality_candidate_raw,war_exhaustion_candidate_raw,diplomatic_points_candidate_raw,research_points_candidate_raw,leadership_candidate_raw"},
        };
        constexpr MetricEvent country_economy_events[] = {
            {"country.economy.interval", "country_tag", "period_start_raw,period_end_raw,observation_days,expected_days,invalid_days,complete,population_average"},
            {"country.economy.total", "country_tag", "nominal_gdp,real_gdp,base_date_raw,gold_to_cash_rate"},
            {"country.economy.quality", "country_tag", "unsettled_output_candidates"},
            {"country.economy.per_capita", "country_tag", "nominal_gdp_per_capita,real_gdp_per_capita"},
            {"country.economy.component", "country_tag,component", "nominal_value_added,real_value_added"},
        };
        constexpr MetricEvent country_military_events[] = {
            {"country.military", "country_tag", "unit_count_candidate?,mobilized_candidate?,scheduled_mobilization_count_candidate?,leadership_candidate_raw?,military_ranking_candidate?"},
        };
        constexpr MetricEvent country_diplomacy_events[] = {
            {"country.diplomacy.status", "country_tag", "substate_candidate,vassal_candidate,overlord_tag_candidate,sphere_leader_tag_candidate"},
            {"country.diplomacy.relations", "country_tag", "sphereling_count_candidate,vassal_count_candidate,ally_count_candidate,guaranteed_count_candidate,neighbor_count_candidate"},
        };
        constexpr MetricEvent factory_events[] = {
            {"state.factory.identity", "country_tag,state_id,state_region_key,factory_type", "anchor_province_id_candidate,level,subsidized,closed"},
            {"state.factory.employment", "country_tag,state_id,factory_type", "employee_count,craftsmen_count,clerk_count"},
            {"state.factory.production", "country_tag,state_id,factory_type", "output_raw,output_good_ordinal,output_good,base_output_raw"},
            {"state.factory.finance", "country_tag,state_id,factory_type", "budget_raw,market_spending_expense_raw,sales_income_raw,paychecks_expense_raw,investment_income_raw"},
            {"state.factory.input", "country_tag,state_id,factory_type,good_ordinal", "stockpile_raw,requested_raw"},
            {"state.factory.input.flow.summary", "country_tag,state_id,factory_type", "post_consumption_seen,pre_purchase_seen,primary_delivery_seen,secondary_delivery_seen,settlement_count"},
            {"state.factory.input.flow", "country_tag,state_id,factory_type,good_ordinal", "post_consumption_raw,pre_purchase_raw,delivered_primary_raw,delivered_secondary_raw"},
            {"state.factory.sales.summary", "country_tag,state_id,factory_type", "settlement_seen,settlement_count,complete"},
            {"state.factory.sales.quantity", "country_tag,state_id,factory_type", "output_good_ordinal,opening_inventory_raw,produced_raw,sold_raw,closing_inventory_raw"},
            {"state.factory.sales.revenue", "country_tag,state_id,factory_type", "proceeds_raw"},
            {"state.factory.input.consumption.summary", "country_tag,state_id,factory_type", "consumption_seen"},
            {"state.factory.input.consumption", "country_tag,state_id,factory_type,good_ordinal", "consumed_raw"},
        };
        constexpr MetricEvent market_events[] = {
            {"world.market.price", "good_ordinal", "price_raw,last_price_raw"},
            {"world.market.supply", "good_ordinal", "supply_raw,last_supply_raw,worldmarket_stock_raw"},
            {"world.market.demand", "good_ordinal", "demand_raw,real_demand_raw"},
            {"world.market.sales", "good_ordinal", "actual_sold_raw,actual_sold_world_raw"},
        };
        constexpr MetricEvent province_daily_events[] = {
            {"province.daily", "province_id", "owner_tag_candidate?,controller_tag_candidate?,colonial_level_candidate?,life_rating_candidate?,infrastructure_candidate_raw?"},
        };
        constexpr MetricEvent province_production_events[] = {
            {"province.production", "province_id", "building_slot_count_candidate?,construction_count_candidate?"},
        };
        constexpr MetricEvent rgo_events[] = {
            {"province.rgo.identity", "country_tag,province_id", "production_type,output_good_ordinal,output_good"},
            {"province.rgo.employment", "country_tag,province_id", "employment_capacity,employed"},
            {"province.rgo.production", "country_tag,province_id", "base_output_per_size_raw,base_size_raw_candidate,base_size_raw,output_efficiency_raw,throughput_raw,gross_output_raw"},
            {"province.rgo.modifiers", "country_tag,province_id", "owner_population,state_rgo_employment_capacity,owner_output_modifier_raw"},
            {"province.rgo.finance", "country_tag,province_id", "income_raw"},
            {"province.rgo.sales.summary", "country_tag,province_id", "settlement_seen,opening_inventory_seen,complete"},
            {"province.rgo.sales.quantity", "country_tag,province_id", "output_good_ordinal,opening_inventory_raw,produced_raw,sold_raw,closing_inventory_raw"},
            {"province.rgo.sales.revenue", "country_tag,province_id", "proceeds_raw,percent_sold_domestic_raw,percent_sold_export_raw"},
        };
        constexpr MetricEvent artisan_events[] = {
            {"pop.artisan.identity", "country_tag,province_id,pop_id", "production_type,output_good_ordinal,output_good"},
            {"pop.artisan.production", "country_tag,province_id,pop_id", "base_output_raw,current_producing_raw,gross_output_raw"},
            {"pop.artisan.input", "country_tag,province_id,pop_id,good_ordinal", "stockpile_raw,need_raw"},
            {"pop.artisan.finance", "country_tag,province_id,pop_id", "last_spending_raw,needs_cost_raw,production_income_raw,percent_afforded_raw,throttle_raw"},
            {"pop.artisan.sales.summary", "country_tag,province_id,pop_id", "settlement_seen,opening_inventory_seen,complete"},
            {"pop.artisan.sales.quantity", "country_tag,province_id,pop_id", "output_good_ordinal,opening_inventory_raw,produced_raw,sold_raw,closing_inventory_raw"},
            {"pop.artisan.sales.revenue", "country_tag,province_id,pop_id", "proceeds_raw,percent_sold_domestic_raw,percent_sold_export_raw"},
            {"pop.artisan.input.flow.summary", "country_tag,province_id,pop_id", "post_consumption_seen,pre_purchase_seen,primary_delivery_seen,secondary_delivery_seen,settlement_count"},
            {"pop.artisan.input.flow", "country_tag,province_id,pop_id,good_ordinal", "post_consumption_raw,pre_purchase_raw,delivered_primary_raw,delivered_secondary_raw"},
            {"pop.artisan.inactive", "country_tag,province_id,pop_id", "-"},
            {"pop.artisan.invalid", "country_tag,province_id", "pop_id_candidate,reason,offending_raw"},
        };
        constexpr MetricEvent pop_economy_events[] = {
            {"pop.economy", "province_id_candidate,pop_type_id_candidate,pop_id", "money_raw?,savings_raw?,interest_cash_flow_raw?,total_cash_flow_raw?"},
        };
        constexpr MetricEvent pop_demographics_events[] = {
            {"pop.demographics", "province_id_candidate,pop_type_id_candidate,pop_id", "size_candidate?,employed_candidate?,consciousness_candidate_raw?,militancy_candidate_raw?,literacy_candidate_raw?"},
        };
        constexpr MetricEvent pop_identity_events[] = {
            {"pop.identity", "province_id_candidate,pop_type_id_candidate,pop_id", "pop_type_tag_candidate?,culture_tag_candidate?,religion_tag_candidate?"},
        };
        constexpr MetricEvent pop_needs_events[] = {
            {"pop.needs", "province_id_candidate,pop_type_id_candidate,pop_id", "life_satisfaction_candidate_raw?,everyday_satisfaction_candidate_raw?,luxury_satisfaction_candidate_raw?"},
        };
        constexpr MetricEvent pop_aggregate_events[] = {
            {"pop.aggregate", "country_tag,province_id_candidate,pop_type_id_candidate", "pop_count?,size_candidate?,employed_candidate?,money_raw?,savings_raw?"},
        };
        constexpr MetricEvent lifecycle_events[] = {
            {"pop.lifecycle.summary", "-", "opening_seen,opening_pop_count,closing_pop_count,observed_appeared_count,observed_disappeared_count,scope_changed_count,unchanged_count,complete"},
            {"pop.lifecycle.scope_changed", "pop_id", "previous_country_tag_candidate,previous_province_id_candidate,previous_pop_type_id_candidate,current_country_tag_candidate,current_province_id_candidate,current_pop_type_id_candidate"},
            {"pop.lifecycle.observed_appeared", "pop_id,province_id_candidate,pop_type_id_candidate,country_tag_candidate?", "size_candidate"},
            {"pop.lifecycle.observed_disappeared", "pop_id,province_id_candidate,pop_type_id_candidate,country_tag_candidate?", "size_candidate"},
        };
        constexpr MetricEvent cashflow_events[] = {
            {"pop.cashflow.summary", "country_tag,province_id,pop_type_id_candidate,pop_id", "opening_money_seen,capture_complete,reconciled,call_count"},
            {"pop.cashflow.account", "country_tag,province_id,pop_type_id_candidate,pop_id", "opening_money_raw,closing_money_raw,money_delta_raw,residual_raw"},
            {"pop.cashflow.component", "country_tag,province_id,pop_type_id_candidate,pop_id,cash_flow_index,component", "posted_raw,money_delta_raw"},
        };
        constexpr MetricEvent cashflow_aggregate_events[] = {
            {"pop.cashflow.aggregate.summary", "country_tag,pop_type_id_candidate", "opening_pop_count,closing_pop_count,opening_money_seen,reconciled"},
            {"pop.cashflow.aggregate.account", "country_tag,pop_type_id_candidate", "opening_money_raw,closing_money_raw,money_delta_raw,residual_raw"},
            {"pop.cashflow.aggregate.component", "country_tag,pop_type_id_candidate,cash_flow_index,component", "posted_raw,money_delta_raw"},
            {"pop.cashflow.country.summary", "country_tag", "opening_pop_count,closing_pop_count,opening_money_seen,reconciled"},
            {"pop.cashflow.country.account", "country_tag", "opening_money_raw,closing_money_raw,money_delta_raw,residual_raw"},
            {"pop.cashflow.country.component", "country_tag,cash_flow_index,component", "posted_raw,money_delta_raw"},
        };

        constexpr MetricFamily families[] = {
            {"world.daily", world_daily_fields, 3, "world.daily", "world", "v2game-3.04", "provisional", MetricCostClass::Low, MetricCollector::World, MetricAdmissionPriority::Important, false, false, false, false, false, false, world_daily_events, 1},
            {"world.economy", world_economy_fields, 4, "world.economy", "world", "v2game-3.04", "provisional", MetricCostClass::High, MetricCollector::World, MetricAdmissionPriority::Important, false, false, false, false, false, false, world_economy_events, 4},
            {"world.military", world_military_fields, 1, "world.military", "world", "v2game-3.04", "provisional", MetricCostClass::Medium, MetricCollector::World, MetricAdmissionPriority::BestEffort, false, false, false, false, false, false, world_military_events, 1},
            {"country.daily", country_daily_fields, 2, "country.daily", "country", "v2game-3.04", "provisional", MetricCostClass::Low, MetricCollector::Country, MetricAdmissionPriority::Important, true, false, false, false, false, false, country_daily_events, 1},
            {"country.metrics", country_metrics_fields, 2, "country.metrics", "country", "v2game-3.04", "provisional", MetricCostClass::Low, MetricCollector::Country, MetricAdmissionPriority::BestEffort, true, false, false, false, false, false, country_metrics_events, 2},
            {"country.economy", country_economy_fields, 3, "country.economy", "country", "v2game-3.04", "provisional", MetricCostClass::High, MetricCollector::Country, MetricAdmissionPriority::Important, true, false, false, false, false, true, country_economy_events, 5},
            {"country.military", country_military_fields, 5, "country.military", "country", "v2game-3.04", "provisional", MetricCostClass::Medium, MetricCollector::Country, MetricAdmissionPriority::BestEffort, true, false, false, false, false, false, country_military_events, 1},
            {"country.diplomacy", country_diplomacy_fields, 2, "country.diplomacy", "country", "v2game-3.04", "provisional", MetricCostClass::Medium, MetricCollector::Country, MetricAdmissionPriority::BestEffort, true, false, false, false, false, false, country_diplomacy_events, 2},
            {"state.factory", factory_fields, 7, "state.factory", "factory", "v2game-3.04", "provisional", MetricCostClass::High, MetricCollector::Country, MetricAdmissionPriority::Important, true, false, false, false, true, false, factory_events, 12},
            {"world.market", market_fields, 4, "world.market", "good", "v2game-3.04", "provisional", MetricCostClass::High, MetricCollector::World, MetricAdmissionPriority::Important, false, false, false, false, false, false, market_events, 4},
            {"province.daily", province_daily_fields, 5, "province.daily", "province", "v2game-3.04", "provisional", MetricCostClass::Medium, MetricCollector::Province, MetricAdmissionPriority::BestEffort, true, true, false, false, false, false, province_daily_events, 1},
            {"province.production", province_production_fields, 2, "province.production", "province", "v2game-3.04", "provisional", MetricCostClass::Medium, MetricCollector::Province, MetricAdmissionPriority::BestEffort, false, true, false, false, false, false, province_production_events, 1},
            {"province.rgo", rgo_fields, 6, "province.rgo", "province", "v2game-3.04", "provisional", MetricCostClass::High, MetricCollector::Province, MetricAdmissionPriority::Important, true, true, false, false, true, false, rgo_events, 8},
            {"pop.artisan", artisan_fields, 6, "pop.artisan", "pop", "v2game-3.04", "provisional", MetricCostClass::High, MetricCollector::Population, MetricAdmissionPriority::Important, true, true, false, false, true, false, artisan_events, 11},
            {"pop.economy", pop_economy_fields, 4, "pop.economy", "pop", "v2game-3.04", "provisional", MetricCostClass::High, MetricCollector::Population, MetricAdmissionPriority::BestEffort, true, true, false, false, false, false, pop_economy_events, 1},
            {"pop.demographics", pop_demographics_fields, 5, "pop.demographics", "pop", "v2game-3.04", "provisional", MetricCostClass::High, MetricCollector::Population, MetricAdmissionPriority::BestEffort, true, true, false, false, false, false, pop_demographics_events, 1},
            {"pop.identity", pop_identity_fields, 3, "pop.identity", "pop", "v2game-3.04", "provisional", MetricCostClass::High, MetricCollector::Population, MetricAdmissionPriority::BestEffort, true, true, false, false, false, false, pop_identity_events, 1},
            {"pop.needs", pop_needs_fields, 3, "pop.needs", "pop", "v2game-3.04", "provisional", MetricCostClass::High, MetricCollector::Population, MetricAdmissionPriority::BestEffort, true, true, false, false, false, false, pop_needs_events, 1},
            {"pop.aggregate", pop_aggregate_fields, 5, "pop.aggregate", "pop", "v2game-3.04", "provisional", MetricCostClass::High, MetricCollector::Population, MetricAdmissionPriority::Important, true, true, false, false, false, false, pop_aggregate_events, 1},
            {"pop.lifecycle", lifecycle_fields, 4, "pop.lifecycle", "pop", "v2game-3.04", "provisional", MetricCostClass::High, MetricCollector::Population, MetricAdmissionPriority::ReliableTerminal, true, true, false, true, false, false, lifecycle_events, 4},
            {"pop.cashflow", cashflow_fields, 3, "pop.cashflow", "pop", "v2game-3.04", "provisional", MetricCostClass::High, MetricCollector::Population, MetricAdmissionPriority::Important, true, true, true, true, false, false, cashflow_events, 3},
            {"pop.cashflow.aggregate", cashflow_fields, 3, "pop.cashflow", "country", "v2game-3.04", "provisional", MetricCostClass::High, MetricCollector::Population, MetricAdmissionPriority::Important, true, false, false, true, false, false, cashflow_aggregate_events, 6},
        };
    }

    const MetricFamily *MetricFamilies(size_t *count)
    {
        if (count != nullptr) *count = sizeof(families) / sizeof(families[0]);
        return families;
    }

    const MetricFamily *FindMetricFamily(std::string_view family)
    {
        const auto *end = families + sizeof(families) / sizeof(families[0]);
        const auto it = std::find_if(families, end, [family](const MetricFamily &item) { return item.id == family; });
        return it == end ? nullptr : it;
    }

    const MetricFamily *FindMetricFamilyForEvent(std::string_view event)
    {
        const auto *end = families + sizeof(families) / sizeof(families[0]);
        const auto it = std::find_if(families, end, [event](const MetricFamily &family) {
            return MetricFamilyEmitsEvent(family, event);
        });
        return it == end ? nullptr : it;
    }

    const MetricEvent *FindMetricEvent(std::string_view event)
    {
        const auto *family = FindMetricFamilyForEvent(event);
        if (family == nullptr) return nullptr;
        const auto *end = family->events + family->event_count;
        const auto it = std::find_if(family->events, end, [event](const MetricEvent &candidate) {
            return candidate.id == event;
        });
        return it == end ? nullptr : it;
    }

    namespace
    {
        bool ContainsField(const std::string_view *fields, size_t field_count, std::string_view expected)
        {
            for (size_t index = 0; index < field_count; ++index) {
                if (fields[index] == expected) return true;
            }
            return false;
        }

        bool FieldsMatch(std::string_view schema, const std::string_view *fields, size_t field_count)
        {
            if (schema == "-") return field_count == 0;
            size_t schema_count = 0;
            for (size_t begin = 0; begin <= schema.size();) {
                const size_t end = schema.find(',', begin);
                std::string_view field = schema.substr(begin, end == std::string_view::npos ? end : end - begin);
                const bool optional = !field.empty() && field.back() == '?';
                if (optional) field.remove_suffix(1);
                if (!optional && !ContainsField(fields, field_count, field)) return false;
                ++schema_count;
                if (end == std::string_view::npos) break;
                begin = end + 1;
            }
            if (field_count > schema_count) return false;
            for (size_t index = 0; index < field_count; ++index) {
                bool known = false;
                for (size_t begin = 0; begin <= schema.size();) {
                    const size_t end = schema.find(',', begin);
                    std::string_view field = schema.substr(begin, end == std::string_view::npos ? end : end - begin);
                    if (!field.empty() && field.back() == '?') field.remove_suffix(1);
                    if (fields[index] == field) { known = true; break; }
                    if (end == std::string_view::npos) break;
                    begin = end + 1;
                }
                if (!known) return false;
            }
            return true;
        }
    }

    bool MetricEventMatchesSchema(const MetricEvent &event,
                                  const std::string_view *entity_fields, size_t entity_field_count,
                                  const std::string_view *payload_fields, size_t payload_field_count)
    {
        return FieldsMatch(event.entity_schema, entity_fields, entity_field_count)
            && FieldsMatch(event.payload_schema, payload_fields, payload_field_count);
    }

    bool MetricFamilySupportsField(const MetricFamily &family, std::string_view field)
    {
        return std::find(family.fields, family.fields + family.field_count, field) != family.fields + family.field_count;
    }

    MetricValidationError MetricFamilyValidate(const MetricFamily &family, CaptureCadence cadence, int fixed_days,
                                                const std::string_view *fields, size_t field_count,
                                                size_t country_filter_count, size_t province_filter_count)
    {
        const bool daily = cadence == CaptureCadence::Daily || (cadence == CaptureCadence::FixedDays && fixed_days == 1);
        if ((family.daily_only || (family.sales_daily_only
                && (field_count == 0 || std::find(fields, fields + field_count, "sales") != fields + field_count))) && !daily) {
            return MetricValidationError::DailyCadenceRequired;
        }
        if (family.requires_entity_filter && country_filter_count == 0 && province_filter_count == 0) {
            return MetricValidationError::EntityFilterRequired;
        }
        return MetricValidationError::None;
    }

    std::string_view MetricValidationErrorMessage(MetricValidationError error)
    {
        switch (error) {
        case MetricValidationError::None: return {};
        case MetricValidationError::DailyCadenceRequired: return "capture requires daily cadence";
        case MetricValidationError::EntityFilterRequired: return "capture requires a country or province filter";
        }
        return "capture validation failed";
    }

    bool MetricFamilyEmitsEvent(const MetricFamily &family, std::string_view event)
    {
        return std::any_of(family.events, family.events + family.event_count,
                           [event](const MetricEvent &candidate) { return candidate.id == event; });
    }

    MetricAdmission MetricFamilyAdmission(const MetricFamily &family, size_t country_filter_count,
                                          size_t province_filter_count)
    {
        if (family.admission_priority == MetricAdmissionPriority::ReliableTerminal) return MetricAdmission::Reliable;
        const size_t filters = country_filter_count + province_filter_count;
        return family.admission_priority == MetricAdmissionPriority::Important && filters != 0 && filters <= 16
            ? MetricAdmission::Reliable : MetricAdmission::BestEffort;
    }

    RuntimePlan BuildRuntimePlan(const MetricRuleSelection *rules, size_t rule_count)
    {
        RuntimePlan plan;
        for (size_t index = 0; index < rule_count; ++index) {
            const auto *family = FindMetricFamily(rules[index].family);
            if (family == nullptr) continue;
            plan.open_observation = true;
            const bool all_fields = rules[index].fields == nullptr || rules[index].field_count == 0;
            const auto selected = [&](std::string_view field) {
                return all_fields || std::find(rules[index].fields, rules[index].fields + rules[index].field_count, field)
                    != rules[index].fields + rules[index].field_count;
            };
            plan.install_factory_sales_hook = plan.install_factory_sales_hook
                || (family->id == "state.factory" && selected("sales"));
            plan.install_factory_flow_hook = plan.install_factory_flow_hook
                || (family->id == "state.factory" && selected("flows")) || family->id == "country.economy";
            plan.install_artisan_flow_hook = plan.install_artisan_flow_hook
                || (family->id == "pop.artisan" && selected("flows"));
            plan.install_pop_cashflow_hook = plan.install_pop_cashflow_hook
                || family->id == "pop.cashflow" || family->id == "pop.cashflow.aggregate";
        }
        return plan;
    }

    std::string_view CaptureCadenceNameView(CaptureCadence cadence)
    {
        switch (cadence) {
        case CaptureCadence::FixedDays: return "fixed-days";
        case CaptureCadence::Daily: return "daily";
        case CaptureCadence::Weekly: return "weekly";
        case CaptureCadence::Monthly: return "monthly";
        case CaptureCadence::Yearly: return "yearly";
        }
        return {};
    }
}
