#include "event_services_runtime.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace
{
    struct BankState
    {
        int calls = 0;
        SmedleyBankInterestEventV1 event{};
        bool authority_active = false;
    };

    struct AuthorityIsolationState
    {
        SmedleyBankInterestAuthority first = 0;
        bool first_active_in_second_callback = false;
    };

    SmedleyEventServicesCallbackResult SMEDLEY_EVENT_SERVICES_CALL CaptureFirstAuthority(
        void *context, const SmedleyBankInterestEventV1 *event)
    {
        static_cast<AuthorityIsolationState *>(context)->first = event->authority;
        return SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE;
    }

    SmedleyEventServicesCallbackResult SMEDLEY_EVENT_SERVICES_CALL CheckFirstAuthorityExpired(
        void *context, const SmedleyBankInterestEventV1 *event)
    {
        auto *state = static_cast<AuthorityIsolationState *>(context);
        state->first_active_in_second_callback = smedley::IsBankInterestAuthorityActive(
            state->first, event->phase, event->country_index);
        return SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE;
    }

    SmedleyEventServicesCallbackResult SMEDLEY_EVENT_SERVICES_CALL CaptureBank(
        void *context, const SmedleyBankInterestEventV1 *event)
    {
        auto *state = static_cast<BankState *>(context);
        ++state->calls;
        state->event = *event;
        state->authority_active = smedley::IsBankInterestAuthorityActive(
            event->authority, event->phase, event->country_index);
        return SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE;
    }

    struct ConsoleState
    {
        int calls = 0;
        SmedleyCampaignConsoleInputV1 input{};
    };

    SmedleyEventServicesCallbackResult SMEDLEY_EVENT_SERVICES_CALL CaptureConsole(
        void *context, const SmedleyCampaignConsoleInputV1 *input, SmedleyCampaignConsoleResultV1 *result)
    {
        auto *state = static_cast<ConsoleState *>(context);
        ++state->calls;
        state->input = *input;
        result->struct_size = sizeof(*result);
        result->version = SMEDLEY_CAMPAIGN_CONSOLE_RESULT_VERSION_V1;
        result->handled = 1;
        result->success = 1;
        result->message_bytes = 2;
        std::memcpy(result->message, "ok", 2);
        return SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE;
    }

    SmedleyEventServicesCallbackResult SMEDLEY_EVENT_SERVICES_CALL MalformedConsole(
        void *, const SmedleyCampaignConsoleInputV1 *, SmedleyCampaignConsoleResultV1 *result)
    {
        result->handled = 1;
        result->message_bytes = SMEDLEY_CAMPAIGN_CONSOLE_MAX_RESULT_BYTES + 1;
        return SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE;
    }

    SmedleyEventServicesCallbackResult SMEDLEY_EVENT_SERVICES_CALL ObserveConsole(
        void *context, const SmedleyCampaignConsoleInputV1 *, SmedleyCampaignConsoleResultV1 *)
    {
        ++*static_cast<int *>(context);
        return SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE;
    }

    struct SelfUnregisterState
    {
        SmedleyEventServicesApiV1 *api = nullptr;
        SmedleyEventServicesRegistration registration = 0;
        SmedleyEventServicesResult result = SMEDLEY_EVENT_SERVICES_SUCCESS;
    };

    SmedleyEventServicesCallbackResult SMEDLEY_EVENT_SERVICES_CALL SelfUnregisterBank(
        void *context, const SmedleyBankInterestEventV1 *)
    {
        auto *state = static_cast<SelfUnregisterState *>(context);
        state->result = state->api->unregister(state->registration);
        return SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE;
    }

    struct BlockingBankState
    {
        std::atomic<bool> entered{false};
        std::atomic<bool> release{false};
    };

    SmedleyEventServicesCallbackResult SMEDLEY_EVENT_SERVICES_CALL BlockBank(
        void *context, const SmedleyBankInterestEventV1 *)
    {
        auto *state = static_cast<BlockingBankState *>(context);
        state->entered.store(true, std::memory_order_release);
        while (!state->release.load(std::memory_order_acquire)) std::this_thread::yield();
        return SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE;
    }

    SmedleyEventServicesCallbackResult SMEDLEY_EVENT_SERVICES_CALL ThrowingBank(
        void *context, const SmedleyBankInterestEventV1 *)
    {
        ++*static_cast<int *>(context);
        throw std::runtime_error("failure");
    }

    SmedleyEventServicesApiV1 Api()
    {
        SmedleyEventServicesApiV1 api{};
        api.struct_size = sizeof(api);
        api.version = SMEDLEY_EVENT_SERVICES_API_VERSION_V1;
        EXPECT_EQ(SmedleyGetEventServicesApiV1(&api), SMEDLEY_EVENT_SERVICES_SUCCESS);
        return api;
    }

    SmedleyCampaignConsoleInputV1 ConsoleInput()
    {
        SmedleyCampaignConsoleInputV1 input{};
        input.struct_size = sizeof(input);
        input.version = SMEDLEY_CAMPAIGN_CONSOLE_INPUT_VERSION_V1;
        input.command = SMEDLEY_CAMPAIGN_CONSOLE_OBSERVER_SWITCH;
        input.argument_count = 1;
        input.arguments_valid = 1;
        std::memcpy(input.first_argument, "ENG", 4);
        return input;
    }
}

TEST(EventServicesApiV1Test, CopiesBankInterestSnapshotAndScopesAuthority)
{
    auto api = Api();
    BankState state;
    SmedleyEventServicesRegistration registration = 0;
    ASSERT_EQ(api.register_bank_interest(&CaptureBank, &state, &registration), SMEDLEY_EVENT_SERVICES_SUCCESS);

    smedley::DispatchBankInterestEventServices(SMEDLEY_BANK_INTEREST_AFTER, 7, true);

    ASSERT_EQ(state.calls, 1);
    EXPECT_EQ(state.event.struct_size, sizeof(state.event));
    EXPECT_EQ(state.event.version, SMEDLEY_BANK_INTEREST_EVENT_VERSION_V1);
    EXPECT_EQ(state.event.phase, SMEDLEY_BANK_INTEREST_AFTER);
    EXPECT_EQ(state.event.country_index, 7u);
    EXPECT_EQ(state.event.distributes_to_states, 1u);
    EXPECT_TRUE(state.authority_active);
    EXPECT_FALSE(smedley::IsBankInterestAuthorityActive(
        state.event.authority, state.event.phase, state.event.country_index));
    EXPECT_EQ(api.unregister(registration), SMEDLEY_EVENT_SERVICES_SUCCESS);
}

TEST(EventServicesApiV1Test, GivesEachBankCallbackAnIsolatedAuthority)
{
    auto api = Api();
    AuthorityIsolationState state;
    SmedleyEventServicesRegistration first = 0;
    SmedleyEventServicesRegistration second = 0;
    ASSERT_EQ(api.register_bank_interest(&CaptureFirstAuthority, &state, &first), SMEDLEY_EVENT_SERVICES_SUCCESS);
    ASSERT_EQ(api.register_bank_interest(&CheckFirstAuthorityExpired, &state, &second),
        SMEDLEY_EVENT_SERVICES_SUCCESS);

    smedley::DispatchBankInterestEventServices(SMEDLEY_BANK_INTEREST_AFTER, 7, true);

    EXPECT_NE(state.first, 0u);
    EXPECT_FALSE(state.first_active_in_second_callback);
    EXPECT_EQ(api.unregister(first), SMEDLEY_EVENT_SERVICES_SUCCESS);
    EXPECT_EQ(api.unregister(second), SMEDLEY_EVENT_SERVICES_SUCCESS);
}

TEST(EventServicesApiV1Test, CopiesBoundedCampaignConsoleRecords)
{
    auto api = Api();
    ConsoleState state;
    SmedleyEventServicesRegistration registration = 0;
    ASSERT_EQ(api.register_campaign_console(&CaptureConsole, &state, &registration), SMEDLEY_EVENT_SERVICES_SUCCESS);

    SmedleyCampaignConsoleResultV1 result{};
    EXPECT_TRUE(smedley::DispatchCampaignConsoleEventServices(ConsoleInput(), &result));
    EXPECT_EQ(state.calls, 1);
    EXPECT_EQ(state.input.command, SMEDLEY_CAMPAIGN_CONSOLE_OBSERVER_SWITCH);
    EXPECT_STREQ(state.input.first_argument, "ENG");
    EXPECT_EQ(result.handled, 1u);
    EXPECT_EQ(result.success, 1u);
    EXPECT_EQ(result.message_bytes, 2u);
    EXPECT_EQ(std::memcmp(result.message, "ok", 2), 0);
    EXPECT_EQ(api.unregister(registration), SMEDLEY_EVENT_SERVICES_SUCCESS);
}

TEST(EventServicesApiV1Test, RejectsMalformedDiscoveryAndCallbackResult)
{
    SmedleyEventServicesApiV1 malformed{};
    malformed.struct_size = sizeof(malformed);
    malformed.version = SMEDLEY_EVENT_SERVICES_API_VERSION_V1;
    malformed.reserved[1] = 1;
    EXPECT_EQ(SmedleyGetEventServicesApiV1(&malformed), SMEDLEY_EVENT_SERVICES_INVALID_ARGUMENT);

    auto api = Api();
    SmedleyEventServicesRegistration registration = 0;
    ASSERT_EQ(api.register_campaign_console(&MalformedConsole, nullptr, &registration), SMEDLEY_EVENT_SERVICES_SUCCESS);
    SmedleyCampaignConsoleResultV1 result{};
    EXPECT_FALSE(smedley::DispatchCampaignConsoleEventServices(ConsoleInput(), &result));
    EXPECT_TRUE(smedley::DispatchCampaignConsoleEventServices(ConsoleInput(), &result) == false);
    EXPECT_EQ(api.unregister(registration), SMEDLEY_EVENT_SERVICES_SUCCESS);
}

TEST(EventServicesApiV1Test, KeepsUnhandledConsoleObserverRegistered)
{
    auto api = Api();
    int calls = 0;
    SmedleyEventServicesRegistration registration = 0;
    ASSERT_EQ(api.register_campaign_console(&ObserveConsole, &calls, &registration),
        SMEDLEY_EVENT_SERVICES_SUCCESS);

    SmedleyCampaignConsoleResultV1 result{};
    EXPECT_FALSE(smedley::DispatchCampaignConsoleEventServices(ConsoleInput(), &result));
    EXPECT_FALSE(smedley::DispatchCampaignConsoleEventServices(ConsoleInput(), &result));
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(api.unregister(registration), SMEDLEY_EVENT_SERVICES_SUCCESS);
}

TEST(EventServicesApiV1Test, BoundsBankInterestRegistrations)
{
    auto api = Api();
    BankState state;
    std::array<SmedleyEventServicesRegistration, SMEDLEY_EVENT_SERVICES_MAX_BANK_INTEREST_REGISTRATIONS> registrations{};
    for (auto &registration : registrations) {
        ASSERT_EQ(api.register_bank_interest(&CaptureBank, &state, &registration), SMEDLEY_EVENT_SERVICES_SUCCESS);
    }
    SmedleyEventServicesRegistration overflow = 0;
    EXPECT_EQ(api.register_bank_interest(&CaptureBank, &state, &overflow), SMEDLEY_EVENT_SERVICES_CAPACITY);
    for (const auto registration : registrations) {
        EXPECT_EQ(api.unregister(registration), SMEDLEY_EVENT_SERVICES_SUCCESS);
    }

    std::array<SmedleyEventServicesRegistration, SMEDLEY_EVENT_SERVICES_MAX_CAMPAIGN_CONSOLE_REGISTRATIONS>
        console_registrations{};
    for (auto &registration : console_registrations) {
        ASSERT_EQ(api.register_campaign_console(&CaptureConsole, &state, &registration), SMEDLEY_EVENT_SERVICES_SUCCESS);
    }
    EXPECT_EQ(api.register_campaign_console(&CaptureConsole, &state, &overflow), SMEDLEY_EVENT_SERVICES_CAPACITY);
    for (const auto registration : console_registrations) {
        EXPECT_EQ(api.unregister(registration), SMEDLEY_EVENT_SERVICES_SUCCESS);
    }
}

TEST(EventServicesApiV1Test, RejectsSelfUnregisterAndWaitsForInFlightBankCallback)
{
    auto api = Api();
    SelfUnregisterState self_unregister{&api};
    ASSERT_EQ(api.register_bank_interest(
                  &SelfUnregisterBank, &self_unregister, &self_unregister.registration),
        SMEDLEY_EVENT_SERVICES_SUCCESS);
    smedley::DispatchBankInterestEventServices(SMEDLEY_BANK_INTEREST_BEFORE, 1, false);
    EXPECT_EQ(self_unregister.result, SMEDLEY_EVENT_SERVICES_BUSY);
    EXPECT_EQ(api.unregister(self_unregister.registration), SMEDLEY_EVENT_SERVICES_SUCCESS);

    BlockingBankState blocking;
    SmedleyEventServicesRegistration registration = 0;
    ASSERT_EQ(api.register_bank_interest(&BlockBank, &blocking, &registration), SMEDLEY_EVENT_SERVICES_SUCCESS);
    std::thread dispatcher([] {
        smedley::DispatchBankInterestEventServices(SMEDLEY_BANK_INTEREST_BEFORE, 2, false);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!blocking.entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    if (!blocking.entered.load(std::memory_order_acquire)) {
        blocking.release.store(true, std::memory_order_release);
        dispatcher.join();
        api.unregister(registration);
        FAIL() << "callback did not start";
        return;
    }

    std::atomic<bool> finished{false};
    SmedleyEventServicesResult unregister_result = SMEDLEY_EVENT_SERVICES_BUSY;
    std::thread remover([&] {
        unregister_result = api.unregister(registration);
        finished.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(finished.load(std::memory_order_acquire));
    blocking.release.store(true, std::memory_order_release);
    dispatcher.join();
    remover.join();
    EXPECT_EQ(unregister_result, SMEDLEY_EVENT_SERVICES_SUCCESS);
}

TEST(EventServicesApiV1Test, ContainsThrowingCallbackAndDisablesIt)
{
    auto api = Api();
    int calls = 0;
    SmedleyEventServicesRegistration registration = 0;
    ASSERT_EQ(api.register_bank_interest(&ThrowingBank, &calls, &registration), SMEDLEY_EVENT_SERVICES_SUCCESS);
    smedley::DispatchBankInterestEventServices(SMEDLEY_BANK_INTEREST_AFTER, 3, true);
    smedley::DispatchBankInterestEventServices(SMEDLEY_BANK_INTEREST_AFTER, 3, true);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(api.unregister(registration), SMEDLEY_EVENT_SERVICES_SUCCESS);
}
