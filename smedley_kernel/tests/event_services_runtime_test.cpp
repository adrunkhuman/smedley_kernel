#include "event_services_runtime.hpp"

#include <smedley/game_state/game_services_abi.hpp>

#include <smedley/campaign_runtime_api.h>
#include <smedley/campaign_automation_api.h>
#include <smedley/interest_pool_api.h>
#include <smedley/telemetry_game_api.h>
#include <smedley/telemetry_observation_api.h>

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

    struct InterestAuthorityState
    {
        SmedleyInterestPoolApiV1 *api = nullptr;
        SmedleyInterestPoolResult during = SMEDLEY_INTEREST_POOL_UNAVAILABLE;
        SmedleyBankInterestAuthority authority = 0;
    };

    struct CrossThreadAuthorityState
    {
        SmedleyInterestPoolApiV1 *api = nullptr;
        SmedleyInterestPoolResult result = SMEDLEY_INTEREST_POOL_SUCCESS;
    };

    SmedleyEventServicesCallbackResult SMEDLEY_EVENT_SERVICES_CALL CheckAuthorityThread(
        void *context, const SmedleyBankInterestEventV1 *event)
    {
        auto *state = static_cast<CrossThreadAuthorityState *>(context);
        std::thread caller([&] {
            state->result = state->api->collect(event->authority, nullptr, 0, nullptr, nullptr, 0, nullptr, nullptr);
        });
        caller.join();
        return SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE;
    }

    SmedleyEventServicesCallbackResult SMEDLEY_EVENT_SERVICES_CALL CaptureInterestAuthority(
        void *context, const SmedleyBankInterestEventV1 *event)
    {
        auto *state = static_cast<InterestAuthorityState *>(context);
        state->authority = event->authority;
        state->during = state->api->collect(event->authority, nullptr, 0, nullptr, nullptr, 0, nullptr, nullptr);
        return SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE;
    }

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

TEST(GameServicesApiV1Test, DiscoversDomainTablesAndRejectsReservedFields)
{
    SmedleyCampaignRuntimeApiV1 campaign{};
    campaign.struct_size = sizeof(campaign);
    campaign.version = SMEDLEY_CAMPAIGN_RUNTIME_API_VERSION_V1;
    EXPECT_EQ(SmedleyGetCampaignRuntimeApiV1(&campaign), SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS);
    EXPECT_NE(campaign.open_session, nullptr);
    campaign.reserved[0] = 1;
    EXPECT_EQ(SmedleyGetCampaignRuntimeApiV1(&campaign), SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT);

    SmedleyCampaignAutomationApiV1 automation{};
    automation.struct_size = sizeof(automation);
    automation.version = SMEDLEY_CAMPAIGN_AUTOMATION_API_VERSION_V1;
    EXPECT_EQ(SmedleyGetCampaignAutomationApiV1(&automation), SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS);
    EXPECT_NE(automation.install, nullptr);
    EXPECT_NE(automation.read_process_metrics, nullptr);
    automation.reserved[0] = 1;
    EXPECT_EQ(SmedleyGetCampaignAutomationApiV1(&automation), SMEDLEY_CAMPAIGN_AUTOMATION_INVALID_ARGUMENT);

    SmedleyCampaignAutomationOptionsV1 options{};
    options.struct_size = sizeof(options);
    options.version = 1;
    SmedleyCampaignAutomation automation_handle = 0;
    EXPECT_EQ(automation.install(UINT64_C(1), &options, &automation_handle),
        SMEDLEY_CAMPAIGN_AUTOMATION_STALE_HANDLE);
    EXPECT_EQ(automation.set_observer_mode(UINT64_C(1), 2),
        SMEDLEY_CAMPAIGN_AUTOMATION_INVALID_ARGUMENT);

    SmedleyInterestPoolApiV1 interest{};
    interest.struct_size = sizeof(interest);
    interest.version = SMEDLEY_INTEREST_POOL_API_VERSION_V1;
    EXPECT_EQ(SmedleyGetInterestPoolApiV1(&interest), SMEDLEY_INTEREST_POOL_SUCCESS);
    EXPECT_EQ(interest.collect(UINT64_C(1), nullptr, 0, nullptr, nullptr, 0, nullptr, nullptr),
        SMEDLEY_INTEREST_POOL_STALE_AUTHORITY);

    SmedleyTelemetryGameApiV1 telemetry{};
    telemetry.struct_size = sizeof(telemetry);
    telemetry.version = SMEDLEY_TELEMETRY_GAME_API_VERSION_V1;
    EXPECT_EQ(SmedleyGetTelemetryGameApiV1(&telemetry), SMEDLEY_TELEMETRY_GAME_SUCCESS);
    SmedleyTelemetryWorldSnapshotV1 world{};
    world.struct_size = sizeof(world);
    world.version = 1;
    EXPECT_EQ(telemetry.read_world(UINT64_C(1), &world), SMEDLEY_TELEMETRY_GAME_STALE_HANDLE);

    SmedleyTelemetryObservationApiV1 observations{};
    observations.struct_size = sizeof(observations);
    observations.version = SMEDLEY_TELEMETRY_OBSERVATION_API_VERSION_V1;
    EXPECT_EQ(SmedleyGetTelemetryObservationApiV1(&observations), SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS);
    EXPECT_NE(observations.open_session, nullptr);
    EXPECT_NE(observations.read_artisan, nullptr);
    EXPECT_NE(observations.read_province, nullptr);
    SmedleyTelemetryObservationSession observation_session = 0;
    EXPECT_EQ(observations.open_session(UINT64_C(1), &observation_session), SMEDLEY_TELEMETRY_OBSERVATION_STALE_HANDLE);
    SmedleyTelemetryObservationResult cross_thread = SMEDLEY_TELEMETRY_OBSERVATION_SUCCESS;
    SmedleyTelemetryWorldObservationV1 cross_thread_world{};
    cross_thread_world.struct_size = sizeof(cross_thread_world);
    cross_thread_world.version = 1;
    std::thread wrong_thread([&] { cross_thread = observations.read_world(UINT64_C(1), &cross_thread_world); });
    wrong_thread.join();
    EXPECT_EQ(cross_thread, SMEDLEY_TELEMETRY_OBSERVATION_WRONG_THREAD);
    SmedleyTelemetryObservationApiV1 malformed_observations = observations;
    malformed_observations.reserved[0] = 1;
    EXPECT_EQ(SmedleyGetTelemetryObservationApiV1(&malformed_observations), SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT);

    SmedleyTelemetryWorldObservationV1 observation_world{};
    observation_world.struct_size = sizeof(observation_world);
    observation_world.version = 1;
    EXPECT_EQ(observations.read_world(UINT64_C(1), &observation_world), SMEDLEY_TELEMETRY_OBSERVATION_STALE_HANDLE);
    observation_world.reserved[0] = 1;
    EXPECT_EQ(observations.read_world(UINT64_C(1), &observation_world), SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT);

    int32_t country_ordinal = 0;
    EXPECT_EQ(observations.resolve_daily_country(UINT64_C(1), nullptr, &country_ordinal),
        SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT);
    SmedleyDailyEventV1 daily{};
    daily.struct_size = sizeof(daily);
    daily.version = SMEDLEY_DAILY_EVENT_VERSION_V1;
    std::memcpy(daily.country_tag, "PRU", sizeof(daily.country_tag));
    daily.reserved[0] = 1;
    EXPECT_EQ(observations.resolve_daily_country(UINT64_C(1), &daily, &country_ordinal),
        SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT);
    uint32_t market_count = 0;
    EXPECT_EQ(observations.read_market(UINT64_C(1), 0, nullptr, 0, &market_count),
        SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT);
    EXPECT_EQ(observations.read_market(UINT64_C(1), UINT32_C(1) << 31, nullptr, 0, &market_count),
        SMEDLEY_TELEMETRY_OBSERVATION_INVALID_ARGUMENT);
}

TEST(GameServicesApiV1Test, EncodesSharedTelemetryIdentityWithoutNativeAddressBits)
{
    const uint64_t observation = smedley::game_state::TelemetryOpaqueEntityHandle(7, 41);
    const uint64_t hook = smedley::game_state::TelemetryOpaqueEntityHandle(7, 41);

    EXPECT_EQ(observation, hook);
    EXPECT_NE(observation, smedley::game_state::TelemetryOpaqueEntityHandle(8, 41));
    EXPECT_NE(observation, smedley::game_state::TelemetryOpaqueEntityHandle(7, 42));
    EXPECT_EQ(smedley::game_state::TelemetryOpaqueEntityHandle(0, 41), 0u);
    EXPECT_EQ(smedley::game_state::TelemetryOpaqueEntityHandle(7, 0), 0u);
    EXPECT_EQ(observation >> 32, 7u);
    EXPECT_EQ(static_cast<uint32_t>(observation), 41u);
}

TEST(GameServicesApiV1Test, ResolvesTelemetryEntitiesByOpaqueSerial)
{
    smedley::game_state::TelemetryEntityIndex<4> identities{};
    constexpr uintptr_t first_address = UINT32_C(0x12345000);
    constexpr uintptr_t second_address = UINT32_C(0x56789000);

    const uint32_t first_id = identities.find_or_insert(first_address);
    EXPECT_EQ(identities.find_or_insert(first_address), first_id);
    EXPECT_EQ(identities.find_or_insert(second_address), 2u);
    EXPECT_EQ(identities.find(first_id), first_address);
    EXPECT_EQ(identities.find(2), second_address);
    EXPECT_EQ(identities.find(3), 0u);
    EXPECT_EQ(identities.find_or_insert(0), 0u);

    smedley::game_state::TelemetryEntityIndex<1> full{};
    EXPECT_EQ(full.find_or_insert(first_address), 1u);
    EXPECT_EQ(full.find_or_insert(first_address), 1u);
    EXPECT_EQ(full.find_or_insert(second_address), 0u);
}

TEST(GameServicesApiV1Test, InterestAuthorityExpiresAfterItsCallback)
{
    SmedleyEventServicesApiV1 events = Api();
    SmedleyInterestPoolApiV1 interest{};
    interest.struct_size = sizeof(interest);
    interest.version = SMEDLEY_INTEREST_POOL_API_VERSION_V1;
    ASSERT_EQ(SmedleyGetInterestPoolApiV1(&interest), SMEDLEY_INTEREST_POOL_SUCCESS);
    InterestAuthorityState state{&interest};
    SmedleyEventServicesRegistration registration = 0;
    ASSERT_EQ(events.register_bank_interest(&CaptureInterestAuthority, &state, &registration), SMEDLEY_EVENT_SERVICES_SUCCESS);
    smedley::DispatchBankInterestEventServices(SMEDLEY_BANK_INTEREST_AFTER, 7, true);
    EXPECT_NE(state.authority, 0u);
    EXPECT_EQ(interest.collect(state.authority, nullptr, 0, nullptr, nullptr, 0, nullptr, nullptr),
        SMEDLEY_INTEREST_POOL_STALE_AUTHORITY);
    EXPECT_EQ(events.unregister(registration), SMEDLEY_EVENT_SERVICES_SUCCESS);
}

TEST(GameServicesApiV1Test, BankAuthorityIsThreadLocal)
{
    SmedleyEventServicesApiV1 events = Api();
    SmedleyInterestPoolApiV1 interest{};
    interest.struct_size = sizeof(interest);
    interest.version = SMEDLEY_INTEREST_POOL_API_VERSION_V1;
    ASSERT_EQ(SmedleyGetInterestPoolApiV1(&interest), SMEDLEY_INTEREST_POOL_SUCCESS);
    CrossThreadAuthorityState state{&interest};
    SmedleyEventServicesRegistration registration = 0;
    ASSERT_EQ(events.register_bank_interest(&CheckAuthorityThread, &state, &registration), SMEDLEY_EVENT_SERVICES_SUCCESS);
    smedley::DispatchBankInterestEventServices(SMEDLEY_BANK_INTEREST_AFTER, 7, true);
    EXPECT_EQ(state.result, SMEDLEY_INTEREST_POOL_STALE_AUTHORITY);
    EXPECT_EQ(events.unregister(registration), SMEDLEY_EVENT_SERVICES_SUCCESS);
}

TEST(GameServicesApiV1Test, RejectsBoundedTextWithEmbeddedNulWithoutReadingPastInput)
{
    SmedleyCampaignRuntimeApiV1 campaign{};
    campaign.struct_size = sizeof(campaign);
    campaign.version = SMEDLEY_CAMPAIGN_RUNTIME_API_VERSION_V1;
    ASSERT_EQ(SmedleyGetCampaignRuntimeApiV1(&campaign), SMEDLEY_CAMPAIGN_RUNTIME_SUCCESS);
    const char embedded_nul[] = {'a', '\0', 'b'};
    EXPECT_EQ(campaign.request_save(UINT64_C(1), embedded_nul, sizeof(embedded_nul)),
        SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT);
    EXPECT_EQ(campaign.dispatch_frontend_control(UINT64_C(1), embedded_nul, sizeof(embedded_nul)),
        SMEDLEY_CAMPAIGN_RUNTIME_INVALID_ARGUMENT);
    const char exact_text[] = {'a', 'b', 'c'};
    EXPECT_EQ(campaign.request_save(UINT64_C(1), exact_text, sizeof(exact_text)), SMEDLEY_CAMPAIGN_RUNTIME_STALE_HANDLE);
    EXPECT_EQ(campaign.close_session(UINT64_C(1)), SMEDLEY_CAMPAIGN_RUNTIME_STALE_HANDLE);

    SmedleyTelemetryGameApiV1 telemetry{};
    telemetry.struct_size = sizeof(telemetry);
    telemetry.version = SMEDLEY_TELEMETRY_GAME_API_VERSION_V1;
    ASSERT_EQ(SmedleyGetTelemetryGameApiV1(&telemetry), SMEDLEY_TELEMETRY_GAME_SUCCESS);
    EXPECT_EQ(telemetry.close_session(UINT64_C(1)), SMEDLEY_TELEMETRY_GAME_STALE_HANDLE);
    SmedleyTelemetryHookOptionsV1 hooks{};
    hooks.struct_size = sizeof(hooks);
    hooks.version = 1;
    hooks.hooks = SMEDLEY_TELEMETRY_HOOK_POP_CASH_FLOW;
    SmedleyTelemetryHookSubscription subscription = 0;
    EXPECT_EQ(telemetry.subscribe_hooks(UINT64_C(1), &hooks, &subscription), SMEDLEY_TELEMETRY_GAME_STALE_HANDLE);
}
