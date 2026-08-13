#pragma once

#include "telemetry_services.hpp"

#include <cstdint>
#include <memory>

namespace telemetry_plugin::collectors
{
    class CollectorRuntime;

    class ProvinceCollector
    {
    public:
        explicit ProvinceCollector(CollectorRuntime *runtime);
        ~ProvinceCollector();
        void Reset();
        void Collect(const smedley::game_state::TelemetryCurrentState &game_state, int32_t date_raw);

    private:
        CollectorRuntime *runtime_;
        struct Storage;
        std::unique_ptr<Storage> storage_;
    };
}
