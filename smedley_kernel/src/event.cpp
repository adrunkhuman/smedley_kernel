#include "event.hpp"
#include "eventregistry.hpp"
#include "events/console.hpp"
#include "events/dailyupdate.hpp"
#include "events/dailyinterest.hpp"
#include "events/westernize.hpp"
#include "events/monthlyupdate.hpp"
#include "events/addtosphere.hpp"

#define DECLARE_REGISTRY(Ev) \
    smedley::EventRegistry<Ev>::Handlers smedley::EventRegistry<Ev>::_handlers = smedley::EventRegistry<Ev>::Handlers{}; \
    uint32_t smedley::EventRegistry<Ev>::_notification_depth = 0;

using namespace smedley::events;

DECLARE_REGISTRY(ConsoleCmdManagerInitEvent);
DECLARE_REGISTRY(DailyUpdateEvent);
DECLARE_REGISTRY(DailyInterestEvent);
DECLARE_REGISTRY(WesternizeEvent);
DECLARE_REGISTRY(MonthlyUpdateEvent);
DECLARE_REGISTRY(AddToSphereEvent);

namespace smedley
{

    void Event::Cancel()
    {
        _is_cancelled = _cancelable;
    }

}
