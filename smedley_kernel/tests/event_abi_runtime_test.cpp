#include "event_abi_runtime.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace
{
    struct CallbackState
    {
        int calls = 0;
        SmedleyDailyEventV1 event{};
        SmedleyEventApiV1 *api = nullptr;
        SmedleyEventRegistration registration = 0;
        SmedleyEventResult self_unregister_result = SMEDLEY_EVENT_SUCCESS;
    };

    SmedleyEventCallbackResult SMEDLEY_EVENT_CALL Capture(void *context, const SmedleyDailyEventV1 *event)
    {
        auto *state = static_cast<CallbackState *>(context);
        ++state->calls;
        state->event = *event;
        return SMEDLEY_EVENT_CALLBACK_CONTINUE;
    }

    SmedleyEventCallbackResult SMEDLEY_EVENT_CALL Disable(void *context, const SmedleyDailyEventV1 *)
    {
        ++static_cast<CallbackState *>(context)->calls;
        return SMEDLEY_EVENT_CALLBACK_DISABLE;
    }

    SmedleyEventCallbackResult SMEDLEY_EVENT_CALL SelfUnregister(void *context, const SmedleyDailyEventV1 *)
    {
        auto *state = static_cast<CallbackState *>(context);
        ++state->calls;
        state->self_unregister_result = state->api->unregister(state->registration);
        return SMEDLEY_EVENT_CALLBACK_CONTINUE;
    }

    struct BlockingState
    {
        std::atomic<bool> entered{false};
        std::atomic<bool> release{false};
    };

    SmedleyEventCallbackResult SMEDLEY_EVENT_CALL Block(void *context, const SmedleyDailyEventV1 *)
    {
        auto *state = static_cast<BlockingState *>(context);
        state->entered.store(true, std::memory_order_release);
        while (!state->release.load(std::memory_order_acquire)) std::this_thread::yield();
        return SMEDLEY_EVENT_CALLBACK_CONTINUE;
    }

    SmedleyEventApiV1 Api()
    {
        SmedleyEventApiV1 api{};
        api.struct_size = sizeof(api);
        api.version = SMEDLEY_EVENT_API_VERSION_V1;
        EXPECT_EQ(SmedleyGetEventApiV1(&api), SMEDLEY_EVENT_SUCCESS);
        return api;
    }

    SmedleyDailyEventV1 Event()
    {
        SmedleyDailyEventV1 event{};
        event.struct_size = sizeof(event);
        event.version = SMEDLEY_DAILY_EVENT_VERSION_V1;
        event.treasury_raw = 123456;
        event.game_date_raw = 789;
        event.country_slot_count = 250;
        event.ai_scheduler_entry_count = 200;
        event.country_tag[0] = 'E';
        event.country_tag[1] = 'N';
        event.country_tag[2] = 'G';
        event.has_owned_province = 1;
        return event;
    }
}

TEST(EventApiV1Test, RegistersCopiesAndUnregistersDailySnapshot)
{
    auto api = Api();
    CallbackState state;
    ASSERT_EQ(api.register_daily(&Capture, &state, &state.registration), SMEDLEY_EVENT_SUCCESS);
    const auto event = Event();
    smedley::DispatchDailyEventApi(event);
    ASSERT_EQ(state.calls, 1);
    EXPECT_EQ(state.event.treasury_raw, event.treasury_raw);
    EXPECT_EQ(state.event.game_date_raw, event.game_date_raw);
    EXPECT_EQ(std::string(state.event.country_tag, 3), "ENG");
    EXPECT_EQ(api.unregister(state.registration), SMEDLEY_EVENT_SUCCESS);
    smedley::DispatchDailyEventApi(event);
    EXPECT_EQ(state.calls, 1);
    EXPECT_EQ(api.unregister(state.registration), SMEDLEY_EVENT_NOT_FOUND);
}

TEST(EventApiV1Test, CallbackCanDisableWithoutMutatingRegistry)
{
    auto api = Api();
    CallbackState state;
    ASSERT_EQ(api.register_daily(&Disable, &state, &state.registration), SMEDLEY_EVENT_SUCCESS);
    const auto event = Event();
    smedley::DispatchDailyEventApi(event);
    smedley::DispatchDailyEventApi(event);
    EXPECT_EQ(state.calls, 1);
    EXPECT_EQ(api.unregister(state.registration), SMEDLEY_EVENT_SUCCESS);
}

TEST(EventApiV1Test, RejectsSelfUnregisterWithoutDeadlocking)
{
    auto api = Api();
    CallbackState state;
    state.api = &api;
    ASSERT_EQ(api.register_daily(&SelfUnregister, &state, &state.registration), SMEDLEY_EVENT_SUCCESS);
    smedley::DispatchDailyEventApi(Event());
    EXPECT_EQ(state.calls, 1);
    EXPECT_EQ(state.self_unregister_result, SMEDLEY_EVENT_BUSY);
    EXPECT_EQ(api.unregister(state.registration), SMEDLEY_EVENT_SUCCESS);
}

TEST(EventApiV1Test, BoundsRegistrationsAndValidatesDiscovery)
{
    SmedleyEventApiV1 malformed{};
    malformed.struct_size = sizeof(malformed);
    malformed.version = SMEDLEY_EVENT_API_VERSION_V1;
    malformed.reserved[0] = 1;
    EXPECT_EQ(SmedleyGetEventApiV1(&malformed), SMEDLEY_EVENT_INVALID_ARGUMENT);

    auto api = Api();
    CallbackState state;
    std::array<SmedleyEventRegistration, SMEDLEY_EVENT_MAX_DAILY_REGISTRATIONS> registrations{};
    for (auto &registration : registrations) {
        ASSERT_EQ(api.register_daily(&Capture, &state, &registration), SMEDLEY_EVENT_SUCCESS);
    }
    SmedleyEventRegistration overflow = 0;
    EXPECT_EQ(api.register_daily(&Capture, &state, &overflow), SMEDLEY_EVENT_CAPACITY);
    for (const auto registration : registrations) {
        EXPECT_EQ(api.unregister(registration), SMEDLEY_EVENT_SUCCESS);
    }
}

TEST(EventApiV1Test, ContainsThrowingCallbackAndDisablesIt)
{
    auto api = Api();
    CallbackState state;
    const auto throwing = [](void *context, const SmedleyDailyEventV1 *) -> SmedleyEventCallbackResult {
        ++static_cast<CallbackState *>(context)->calls;
        throw std::runtime_error("failure");
    };
    ASSERT_EQ(api.register_daily(throwing, &state, &state.registration), SMEDLEY_EVENT_SUCCESS);
    smedley::DispatchDailyEventApi(Event());
    smedley::DispatchDailyEventApi(Event());
    EXPECT_EQ(state.calls, 1);
    EXPECT_EQ(api.unregister(state.registration), SMEDLEY_EVENT_SUCCESS);
}

TEST(EventApiV1Test, UnregisterWaitsForAcquiredCallback)
{
    auto api = Api();
    BlockingState state;
    SmedleyEventRegistration registration = 0;
    ASSERT_EQ(api.register_daily(&Block, &state, &registration), SMEDLEY_EVENT_SUCCESS);
    std::thread dispatcher([] { smedley::DispatchDailyEventApi(Event()); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!state.entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    if (!state.entered.load(std::memory_order_acquire)) {
        state.release.store(true, std::memory_order_release);
        dispatcher.join();
        api.unregister(registration);
        FAIL() << "callback did not start";
        return;
    }

    std::atomic<bool> unregister_started{false};
    std::atomic<bool> unregister_finished{false};
    SmedleyEventResult unregister_result = SMEDLEY_EVENT_BUSY;
    std::thread remover([&] {
        unregister_started.store(true, std::memory_order_release);
        unregister_result = api.unregister(registration);
        unregister_finished.store(true, std::memory_order_release);
    });
    while (!unregister_started.load(std::memory_order_acquire)) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(unregister_finished.load(std::memory_order_acquire));
    state.release.store(true, std::memory_order_release);
    dispatcher.join();
    remover.join();
    EXPECT_TRUE(unregister_finished.load(std::memory_order_acquire));
    EXPECT_EQ(unregister_result, SMEDLEY_EVENT_SUCCESS);
}
