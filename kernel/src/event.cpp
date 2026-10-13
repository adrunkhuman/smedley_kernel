#include "event.hpp"
#include "eventregistry.hpp"
#include "events/console.hpp"
#include "events/bankinterest.hpp"
#include "events/dailyupdate.hpp"
#include "events/dailyinterest.hpp"

namespace smedley
{

    void Event::Cancel()
    {
        _is_cancelled = _cancelable;
    }

}
