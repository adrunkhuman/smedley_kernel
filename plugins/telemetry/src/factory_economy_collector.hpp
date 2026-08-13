#pragma once

#include "country_economy_core.hpp"
#include "telemetry_services.hpp"

#include <array>
#include <cstdint>

namespace telemetry_plugin
{
    class EconomicCapture;

    namespace collectors
    {
        class CollectorRuntime;

        class FactoryEconomyCollector
        {
        public:
            FactoryEconomyCollector(CollectorRuntime *runtime, EconomicCapture *economic_capture,
                                    const double *gold_to_cash_rate);

            void ObserveFactoryFlows(const services::FactorySettlementHookRecord *records, uint32_t count,
                                     uint64_t dropped, int32_t date_raw);
            void ObserveFactorySales(const services::FactorySalesHookRecord *records, uint32_t count,
                                     uint64_t dropped, int32_t date_raw);
            void CollectFactories(smedley::events::DailyUpdateEvent &event, int32_t date_raw);
            void ProcessCountryEconomy(const smedley::game_state::TelemetryCurrentState &game_state,
                                       int32_t date_raw);
            void Flush();

        private:
            struct GoodsFlowAggregate
            {
                bool opening_seen = false, pre_add_seen = false, first_seen = false, second_seen = false;
                uint32_t pair_count = 0;
                std::array<int64_t, 64> opening_raw{}, pre_add_raw{}, first_raw{}, second_raw{};
            };
            struct DailyFactoryFlow
            {
                smedley::game_state::FactoryRef address{};
                int32_t date_raw = 0, state_id = -1;
                std::array<char, 64> factory_type{};
                bool identity_seen = false, consumption_valid = false, closing_valid = false;
                GoodsFlowAggregate aggregate;
                std::array<int64_t, 64> consumed_raw{}, closing_raw{};
            };

            void AccountFactoryFlowInvalid(uint64_t count = 1);
            void UpdateFactoryDailyFlows();
            void EmitFactoryDailyConsumption(int32_t date_raw, size_t rule_index, std::string_view country_tag,
                                             uint32_t factory_count, bool reliable);
            CountryEconomyDay *CountryEconomyDayFor(std::string_view tag, size_t *count);
            bool AddCountryEconomyValue(int64_t quantity_raw, int64_t nominal_price_raw,
                                        int64_t real_price_raw, long double *nominal, long double *real);
            void EmitCountryEconomyPeriod(const smedley::telemetry::CaptureRule &rule, size_t rule_index,
                                          bool final = false);
            bool CollectCountryEconomyDay(const smedley::game_state::TelemetryCurrentState &game_state,
                                          int32_t date_raw, const smedley::telemetry::CaptureRule &rule,
                                          size_t rule_index);

            CollectorRuntime *runtime_;
            EconomicCapture *economic_capture_;
            const double *gold_to_cash_rate_;
            std::array<smedley::game_state::FactorySnapshot, smedley::game_state::max_sample_factories> factory_snapshots_{};
            std::array<smedley::game_state::FactoryInputSnapshot, smedley::game_state::max_sample_factory_inputs> factory_input_snapshots_{};
            std::array<GoodsFlowAggregate, smedley::game_state::max_sample_factories> factory_hook_aggregates_{};
            std::array<DailyFactoryFlow, smedley::game_state::max_sample_factories> factory_daily_flows_{};
            std::array<DailyFactoryFlow, smedley::game_state::max_sample_factories> factory_previous_flows_{};
            const services::FactorySettlementHookRecord *factory_hook_records_ = nullptr;
            uint32_t factory_hook_record_count_ = 0;
            uint64_t factory_hook_dropped_ = 0;
            const services::FactorySalesHookRecord *factory_sales_hook_records_ = nullptr;
            uint32_t factory_sales_hook_record_count_ = 0;
            uint32_t factory_daily_flow_count_ = 0;
            uint32_t factory_previous_flow_count_ = 0;
            uint32_t factory_flow_observed_dates_ = 0;
            int32_t factory_hook_date_raw_ = 0;
            std::array<CountryEconomyDay, max_country_economy_slots> country_economy_days_{};
            CountryEconomyAccumulator country_economy_accumulator_;
            std::array<int64_t, 64> country_economy_base_prices_{};
            std::array<bool, 64> country_economy_base_price_seen_{};
            int32_t country_economy_base_date_raw_ = 0;
            bool country_economy_base_ready_ = false;
        };
    }
}
