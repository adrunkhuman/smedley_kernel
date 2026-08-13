#pragma once

#include "telemetry_services.hpp"

#include <cstdint>
#include <memory>

namespace telemetry_plugin
{
    class EconomicCapture;

    namespace collectors
    {
        class CollectorRuntime;

        class WorldCollector
        {
        public:
            WorldCollector(EconomicCapture *economic_capture, CollectorRuntime *runtime);
            ~WorldCollector();
            uint64_t Collect(const smedley::game_state::TelemetryCurrentState &game_state, int32_t date_raw);

        private:
            EconomicCapture *economic_capture_;
            CollectorRuntime *runtime_;
            struct Storage;
            std::unique_ptr<Storage> storage_;
        };
    }
}
