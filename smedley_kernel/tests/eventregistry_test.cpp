#include "eventregistry.hpp"
#include "events/dailyupdate.hpp"
#include "events/dailyinterest.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <windows.h>

using smedley::EventRegistry;
using smedley::events::DailyUpdateEvent;
using smedley::events::DailyInterestEvent;
using smedley::events::DailyInterestPhase;

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

TEST(EventRegistryTests, ContainsMutationAcrossDllBoundary)
{
    wchar_t executable[MAX_PATH]{};
    ASSERT_NE(GetModuleFileNameW(nullptr, executable, MAX_PATH), 0u);
    const auto fixture = std::filesystem::path(executable).parent_path() / L"smedley_event_mutation_fixture.dll";
    HMODULE module = LoadLibraryW(fixture.c_str());
    ASSERT_NE(module, nullptr);
    using RegisterFixtureFn = void (*)(int *);
    auto register_fixture = reinterpret_cast<RegisterFixtureFn>(GetProcAddress(module, "SmedleyRegisterEventMutationFixture"));
    ASSERT_NE(register_fixture, nullptr);

    int completed = 0;
    register_fixture(&completed);
    DailyInterestEvent event(nullptr, DailyInterestPhase::BEFORE);
    EXPECT_EQ(EventRegistry<DailyInterestEvent>::NotifyContained(event), 1u);
    EXPECT_EQ(completed, 1);

    EventRegistry<DailyInterestEvent>::Unregister(nullptr, "cross-dll-mutator");
    EventRegistry<DailyInterestEvent>::Unregister(nullptr, "cross-dll-continuation");
    EXPECT_NE(FreeLibrary(module), FALSE);
}
