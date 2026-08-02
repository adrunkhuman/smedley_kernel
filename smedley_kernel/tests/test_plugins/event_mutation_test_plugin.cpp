#include <smedley/eventregistry.hpp>
#include <smedley/events/dailyinterest.hpp>

extern "C" __declspec(dllexport) void SmedleyRegisterEventMutationFixture(int *completed)
{
    using Event = smedley::events::DailyInterestEvent;
    smedley::EventRegistry<Event>::Register(nullptr, "cross-dll-mutator", [](Event &) {
        smedley::EventRegistry<Event>::Unregister(nullptr, "cross-dll-mutator");
    });
    smedley::EventRegistry<Event>::Register(nullptr, "cross-dll-continuation", [completed](Event &) {
        ++*completed;
    });
}
