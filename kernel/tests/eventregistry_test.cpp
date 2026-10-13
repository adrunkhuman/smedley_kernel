#include "eventregistry.hpp"
#include "events/dailyupdate.hpp"
#include "events/bankinterest.hpp"
#include "events/dailyinterest.hpp"

#include <gtest/gtest.h>

using smedley::EventRegistry;
using smedley::events::DailyUpdateEvent;
using smedley::events::DailyInterestEvent;
using smedley::events::DailyInterestPhase;
using smedley::events::BankInterestEvent;
using smedley::events::BankInterestPhase;

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

TEST(EventRegistryTests, PreservesDailyInterestBoundaryPhase)
{
    DailyInterestPhase observed = DailyInterestPhase::BEFORE;
    EventRegistry<DailyInterestEvent>::Register(nullptr, "interest-phase-test", [&observed](DailyInterestEvent &event) {
        observed = event.GetPhase();
    });

    DailyInterestEvent event(nullptr, DailyInterestPhase::AFTER);
    EventRegistry<DailyInterestEvent>::Notify(event);
    EventRegistry<DailyInterestEvent>::Unregister(nullptr, "interest-phase-test");

    EXPECT_EQ(observed, DailyInterestPhase::AFTER);
}

TEST(EventRegistryTests, PreservesBankInterestBoundaryPhase)
{
    BankInterestPhase observed = BankInterestPhase::BEFORE;
    bool distributes_to_states = false;
    EventRegistry<BankInterestEvent>::Register(nullptr, "bank-interest-phase-test", [&](BankInterestEvent &event) {
        observed = event.GetPhase();
        distributes_to_states = event.DistributesToStates();
    });

    BankInterestEvent event(nullptr, BankInterestPhase::AFTER, 7);
    EventRegistry<BankInterestEvent>::Notify(event);
    EventRegistry<BankInterestEvent>::Unregister(nullptr, "bank-interest-phase-test");

    EXPECT_EQ(observed, BankInterestPhase::AFTER);
    EXPECT_EQ(event.GetCountryIndex(), 7u);
    EXPECT_TRUE(distributes_to_states);
}

TEST(EventRegistryTests, ContainsMutationAndContinuesNotification)
{
    int completed = 0;
    EventRegistry<DailyInterestEvent>::Register(nullptr, "interest-mutation-test", [](DailyInterestEvent &) {
        EventRegistry<DailyInterestEvent>::Unregister(nullptr, "interest-mutation-test");
    });
    EventRegistry<DailyInterestEvent>::Register(nullptr, "interest-continue-test", [&completed](DailyInterestEvent &) {
        completed++;
    });

    DailyInterestEvent event(nullptr, DailyInterestPhase::BEFORE);
    EXPECT_EQ(EventRegistry<DailyInterestEvent>::NotifyContained(event), 1u);
    EXPECT_EQ(completed, 1);

    EventRegistry<DailyInterestEvent>::Unregister(nullptr, "interest-mutation-test");
    EventRegistry<DailyInterestEvent>::Unregister(nullptr, "interest-continue-test");
}
