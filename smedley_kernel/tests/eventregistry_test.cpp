#include "eventregistry.hpp"
#include "events/dailyupdate.hpp"

#include <gtest/gtest.h>

using smedley::EventRegistry;
using smedley::events::DailyUpdateEvent;

TEST(EventRegistryTests, UnregisterRemovesMatchingHandler)
{
    int calls = 0;
    EventRegistry<DailyUpdateEvent>::Register(nullptr, "unregister-test", [&calls](DailyUpdateEvent &) {
        calls++;
    });
    EventRegistry<DailyUpdateEvent>::Unregister(nullptr, "unregister-test");

    DailyUpdateEvent event(nullptr);
    EventRegistry<DailyUpdateEvent>::Notify(event);

    EXPECT_EQ(calls, 0);
}
