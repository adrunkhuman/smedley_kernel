#pragma once

#include "telemetry_services.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace telemetry_plugin
{
    class EconomicCapture;

    namespace collectors
    {
        class CollectorRuntime;

        class PopulationCollector
        {
        public:
            PopulationCollector(CollectorRuntime *runtime, EconomicCapture *economic_capture);
            ~PopulationCollector();

            services::ArtisanSettlementHookRecord *artisan_records();
            size_t artisan_record_capacity() const;
            services::PopCashFlowHookRecord *cash_flow_records();
            size_t cash_flow_record_capacity() const;
            void ObserveArtisanFlows(const services::ArtisanSettlementHookRecord *records, uint32_t count,
                                     uint64_t dropped, int32_t date_raw);
            void ObserveCashFlows(const services::PopCashFlowHookRecord *records, uint32_t count,
                                  const services::PopCashFlowHookStats &stats, int32_t date_raw);
            void Reset();
            uint64_t Collect(const smedley::game_state::TelemetryCurrentState &game_state, int32_t date_raw);

        private:
            struct Storage;

            CollectorRuntime *runtime_;
            EconomicCapture *economic_capture_;
            std::unique_ptr<Storage> storage_;
        };
    }
}
