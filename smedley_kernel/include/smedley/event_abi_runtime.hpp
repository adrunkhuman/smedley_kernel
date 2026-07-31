#pragma once

#include <smedley/event_api.h>

namespace smedley::v2
{
    class CCountry;
}

namespace smedley
{
    void NotifyDailyEventApi(v2::CCountry *country) noexcept;
    void DispatchDailyEventApi(const SmedleyDailyEventV1 &event) noexcept;
}
