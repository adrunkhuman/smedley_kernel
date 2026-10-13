#pragma once

#include <cstddef>
#include <cstdint>

namespace smedley::game_state::v2_304_layout
{
    struct ForeignVector
    {
        const void *begin;
        const void *end;
        const void *capacity;
    };

    struct ForeignList
    {
        const void *first;
        const void *last;
        int32_t count;
        uint32_t reserved;
    };

    struct EngineString
    {
        union Storage
        {
            char inline_buffer[16];
            char *pointer;
        } storage;
        uint32_t size;
        uint32_t capacity;
        uint32_t allocator;
    };

    static_assert(sizeof(void *) == 4);
    static_assert(sizeof(ForeignVector) == 0x0c);
    static_assert(sizeof(ForeignList) == 0x10);
    static_assert(sizeof(EngineString) == 0x1c);

    constexpr uintptr_t current_game_state_rva = 0x00e588e8;
    constexpr uintptr_t state_employment_registry_rva = 0x00e58728;
    constexpr uintptr_t loaded_goods_count_rva = 0x00e587f4;
    constexpr uintptr_t campaign_defines_rva = 0x00e586dc;

    constexpr uintptr_t give_money_rva = 0x0055a5f0;
    constexpr uintptr_t return_country_to_ai_rva = 0x00287a70;
    constexpr uintptr_t native_tag_handler_rva = 0x0001f720;
    constexpr uintptr_t debug_command_handler_rva = 0x00020eb0;
    constexpr uintptr_t fog_enabled_rva = 0x00b092fb;
    constexpr uintptr_t toggle_pause_rva = 0x0026a2c0;
    constexpr uintptr_t request_quit_rva = 0x0024edb0;
    constexpr uintptr_t speed_up_rva = 0x0032ee90;
    constexpr uintptr_t speed_down_rva = 0x0032efe0;
    constexpr uintptr_t endgame_check_rva = 0x0025548d;
    constexpr uintptr_t frontend_constructor_rva = 0x36a2f0;
    constexpr uintptr_t main_menu_constructor_rva = 0x354a00;
    constexpr uintptr_t frontend_destructor_rva = 0x36b030;
    constexpr uintptr_t main_menu_destructor_rva = 0x354df0;
    constexpr uintptr_t signal_press_rva = 0x5ee510;
    constexpr uintptr_t signal_release_rva = 0x5ee550;
    constexpr uintptr_t load_save_rva = 0x27f1d0;
    constexpr uintptr_t frontend_vtable_rva = 0xa14ed0;
    constexpr uintptr_t main_menu_vtable_rva = 0xa13dbc;

    constexpr size_t game_state_idler_offset = 0x0b24;
    constexpr size_t idler_pause_state_offset = 0x1538;
    constexpr size_t idler_quit_requested_offset = 0x1d20;
    constexpr size_t idler_endgame_dialog_offset = 0x1d6c;
    constexpr size_t campaign_defines_end_date_offset = 0x00c;
    constexpr size_t gui_element_visible_offset = 0x067;
    constexpr uintptr_t gui_window_vtable_rva = 0x00a3c0d8;

    [[nodiscard]] constexpr bool IsGameOverState(
        int32_t current_date_raw, int32_t end_date_raw, uint8_t dialog_visible) noexcept
    {
        return current_date_raw > end_date_raw && dialog_visible == 1;
    }

    constexpr size_t bank_owner_offset = 0x008;
    constexpr size_t country_tag_offset = 0x01c;
    constexpr size_t country_minimum_size = 0xe9c;
    constexpr size_t country_states_offset = 0xe44;
    constexpr size_t country_ai_offset = 0x208;
    constexpr size_t country_owned_provinces_offset = 0x9d8;
    constexpr size_t country_mobilized_offset = 0x120;
    constexpr size_t country_plurality_offset = 0x1a8;
    constexpr size_t country_diplomatic_points_offset = 0x65c;
    constexpr size_t country_war_exhaustion_offset = 0x680;
    constexpr size_t country_units_offset = 0x7b4;
    constexpr size_t country_leadership_offset = 0x7d0;
    constexpr size_t country_substate_offset = 0xcf4;
    constexpr size_t country_vassal_offset = 0xcf5;
    constexpr size_t country_overlord_offset = 0xcf8;
    constexpr size_t country_vassals_offset = 0xd38;
    constexpr size_t country_allies_offset = 0xd58;
    constexpr size_t country_guaranteed_offset = 0xd78;
    constexpr size_t country_neighbors_offset = 0xd88;
    constexpr size_t country_treasury_offset = 0xe78;
    constexpr size_t country_bank_offset = 0xe88;
    constexpr size_t country_creditors_offset = 0xe8c;
    constexpr size_t country_research_points_offset = 0xe3c;
    constexpr size_t country_prestige_offset = 0xea0;
    constexpr size_t country_ranking_offset = 0x1404;
    constexpr size_t country_spherelings_offset = 0x1418;
    constexpr size_t country_sphere_leader_offset = 0x1428;
    constexpr size_t country_infamy_offset = 0x1430;
    constexpr size_t country_scheduled_mobilizations_offset = 0x15dc;
    constexpr size_t game_state_country_ais_offset = 0x0a4;
    constexpr size_t game_state_provinces_offset = 0x0acc;
    constexpr size_t game_state_countries_offset = 0x0adc;
    constexpr size_t game_state_date_offset = 0x0b0c;
    constexpr size_t game_state_player_nations_offset = 0x0aec;
    constexpr size_t game_state_player_tag_offset = 0x0b5c;
    constexpr size_t game_state_speed_index_offset = 0x0b28;
    constexpr size_t game_state_wars_offset = 0x0b3c;
    constexpr size_t game_state_world_market_offset = 0xbcc;
    constexpr size_t state_size = 0x290;
    constexpr size_t state_id_offset = 0x00c;
    constexpr size_t state_provinces_offset = 0x048;
    constexpr size_t state_factories_offset = 0x060;
    constexpr size_t state_region_offset = 0x250;
    constexpr size_t state_savings_offset = 0x258;
    constexpr size_t state_interest_offset = 0x260;
    constexpr size_t state_rgo_capacity_offset = 0x0c8;
    constexpr size_t state_population_by_type_offset = 0x118;
    constexpr size_t province_id_offset = 0x058;
    constexpr size_t province_constructions_offset = 0x0d8;
    constexpr size_t province_buildings_offset = 0x118;
    constexpr size_t province_owner_offset = 0x128;
    constexpr size_t province_controller_offset = 0x130;
    constexpr size_t province_state_offset = 0x188;
    constexpr size_t province_colonial_level_offset = 0x190;
    constexpr size_t province_pop_lists_offset = 0x194;
    constexpr size_t province_life_rating_offset = 0x1a4;
    constexpr size_t province_rgo_capacity_offset = 0x1ac;
    constexpr size_t province_infrastructure_offset = 0x2b8;
    constexpr size_t pop_money_offset = 0x180;
    constexpr size_t pop_interest_cash_flow_offset = 0x210;
    constexpr size_t pop_total_cash_flow_offset = 0x218;
    constexpr size_t pop_savings_offset = 0x250;
    constexpr size_t pop_size_offset = 0x058;
    constexpr size_t pop_employed_offset = 0x060;
    constexpr size_t pop_province_offset = 0x064;
    constexpr size_t pop_type_offset = 0x068;
    constexpr size_t pop_culture_offset = 0x06c;
    constexpr size_t pop_religion_offset = 0x070;
    constexpr size_t pop_consciousness_offset = 0x118;
    constexpr size_t pop_militancy_offset = 0x120;
    constexpr size_t pop_literacy_offset = 0x128;
    constexpr size_t pop_life_needs_satisfaction_offset = 0x130;
    constexpr size_t pop_everyday_needs_satisfaction_offset = 0x138;
    constexpr size_t pop_luxury_needs_satisfaction_offset = 0x140;
    constexpr size_t pop_economy_offset = 0x1d4;
    constexpr size_t pop_next_offset = 0x27c;
    constexpr size_t pop_id_offset = 0x00c;
    constexpr size_t artisan_need_pool_offset = 0x058;
    constexpr size_t artisan_production_type_offset = 0x0b0;
    constexpr size_t artisan_last_spending_offset = 0x0b8;
    constexpr size_t artisan_current_producing_offset = 0x0c0;
    constexpr size_t artisan_percent_afforded_offset = 0x0c8;
    constexpr size_t artisan_percent_sold_domestic_offset = 0x0d0;
    constexpr size_t artisan_percent_sold_export_offset = 0x0d8;
    constexpr size_t artisan_leftover_offset = 0x0e0;
    constexpr size_t artisan_throttle_offset = 0x0e8;
    constexpr size_t artisan_needs_cost_offset = 0x0f0;
    constexpr size_t artisan_production_income_offset = 0x0f8;
    constexpr size_t creditor_tag_offset = 0x008;
    constexpr size_t creditor_interest_offset = 0x010;
    constexpr size_t creditor_debt_offset = 0x018;
    constexpr size_t creditor_was_paid_offset = 0x020;
    constexpr size_t bank_interest_offset = 0x020;
    constexpr size_t region_key_offset = 0x018;
    constexpr size_t culture_key_offset = 0x018;
    constexpr size_t religion_key_offset = 0x010;
    constexpr size_t pop_type_id_offset = 0x028;
    constexpr size_t pop_type_key_offset = 0x008;
    constexpr size_t state_building_size = 0x220;
    constexpr size_t state_building_definition_offset = 0x018;
    constexpr size_t state_building_level_offset = 0x020;
    constexpr size_t state_building_stockpile_index_offset = 0x030;
    constexpr size_t state_building_stockpile_values_offset = 0x070;
    constexpr size_t state_building_requested_input_index_offset = 0x088;
    constexpr size_t state_building_requested_input_values_offset = 0x0c8;
    constexpr size_t state_building_output_offset = 0x0d8;
    constexpr size_t state_building_employment_offset = 0x0f0;
    constexpr size_t state_building_employees_offset = 0x128;
    constexpr size_t state_building_budget_offset = 0x150;
    constexpr size_t state_building_market_spending_offset = 0x158;
    constexpr size_t state_building_sales_income_offset = 0x160;
    constexpr size_t state_building_paychecks_offset = 0x168;
    constexpr size_t state_building_investment_offset = 0x170;
    constexpr size_t state_building_subsidized_offset = 0x180;
    constexpr size_t state_building_closed_offset = 0x188;
    constexpr size_t building_definition_key_offset = 0x020;
    constexpr size_t building_definition_production_type_offset = 0x12c;
    constexpr size_t production_type_output_good_offset = 0x080;
    constexpr size_t production_type_base_output_offset = 0x088;
    constexpr size_t production_type_owner_modifier_offset = 0x0f0;
    constexpr size_t owner_modifier_pop_type_ordinal_offset = 0x028;
    constexpr size_t goods_ordinal_offset = 0x008;
    constexpr size_t goods_key_offset = 0x00c;
    constexpr size_t market_supply_offset = 0x008;
    constexpr size_t market_last_supply_offset = 0x060;
    constexpr size_t market_stock_offset = 0x120;
    constexpr size_t market_demand_offset = 0x178;
    constexpr size_t market_real_demand_offset = 0x1d0;
    constexpr size_t market_price_offset = 0x280;
    constexpr size_t market_last_price_offset = 0x2d8;
    constexpr size_t market_actual_sold_offset = 0x434;
    constexpr size_t market_actual_sold_world_offset = 0x4f4;
    constexpr size_t pop_employment_size = 0x010;
    constexpr size_t pop_employment_pop_offset = 0x008;
    constexpr size_t pop_employment_count_offset = 0x00c;
    constexpr size_t state_employment_record_size = 0x0b0;
    constexpr size_t state_employment_production_type_offset = 0x008;
    constexpr size_t state_employment_output_good_offset = 0x00c;
    constexpr size_t state_employment_province_offset = 0x01c;
    constexpr size_t state_employment_output_efficiency_offset = 0x038;
    constexpr size_t state_employment_throughput_offset = 0x040;
    constexpr size_t state_employment_employed_offset = 0x058;
    constexpr size_t state_employment_income_offset = 0x080;
    constexpr size_t state_employment_percent_sold_domestic_offset = 0x090;
    constexpr size_t state_employment_percent_sold_export_offset = 0x098;
    constexpr size_t state_employment_leftover_offset = 0x0a0;
    constexpr size_t state_employment_base_size_offset = 0x088;
    constexpr size_t frontend_gui_offset = 0x278;
    constexpr size_t main_menu_gui_offset = 0x704;
    constexpr size_t selected_save_offset = 0x590;
    constexpr size_t save_request_offset = 0x5bc;
    constexpr size_t save_complete_offset = 0x5bd;
    constexpr size_t control_signal_offset = 0x054;
    constexpr size_t scheduled_mobilization_size = 0x060;
}
