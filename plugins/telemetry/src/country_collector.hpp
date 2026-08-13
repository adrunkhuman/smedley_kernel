#pragma once

#include "telemetry_services.hpp"

#include <cstdint>

namespace telemetry_plugin::collectors
{
    class CollectorRuntime;

    class CountryCollector
    {
    public:
        explicit CountryCollector(CollectorRuntime *runtime);
        void Collect(smedley::events::DailyUpdateEvent &event, int32_t date_raw);

    private:
        CollectorRuntime *runtime_;
    };
}
