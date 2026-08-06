#include <smedley/game_state/runtime.hpp>
#include <smedley/game_state/artisan_consumption_hook.hpp>
#include <smedley/game_state/factory_consumption_hook.hpp>
#include <smedley/game_state/factory_sales_hook.hpp>
#include <smedley/game_state/pop_cash_flow_hook.hpp>

#include <smedley/events/dailyinterest.hpp>
#include <smedley/events/bankinterest.hpp>
#include <smedley/eventregistry.hpp>
#include <smedley/memory.hpp>

#include <gtest/gtest.h>

#include <windows.h>

#include <cstddef>
#include <cstring>
#include <limits>
#include <thread>
#include <type_traits>

namespace smedley::game_state
{
    namespace
    {
        static_assert(!std::is_copy_constructible_v<DailyInterestAccess>);
        static_assert(!std::is_copy_assignable_v<DailyInterestAccess>);
        static_assert(!std::is_copy_constructible_v<BankInterestAccess>);
        static_assert(!std::is_copy_assignable_v<BankInterestAccess>);
        static_assert(std::is_trivially_copyable_v<ArtisanSettlementHookRecord>);
        static_assert(std::is_trivially_copyable_v<FactorySettlementHookRecord>);
        static_assert(std::is_trivially_copyable_v<FactorySalesHookRecord>);
        static_assert(std::is_trivially_copyable_v<PopCashFlowHookRecord>);
        static_assert(std::is_same_v<decltype(ArtisanSettlementHookRecord{}.pop), PopRef>);
        static_assert(std::is_same_v<decltype(FactorySettlementHookRecord{}.factory), FactoryRef>);
        static_assert(std::is_same_v<decltype(FactorySalesHookRecord{}.factory), FactoryRef>);
        static_assert(std::is_same_v<decltype(PopCashFlowHookRecord{}.pop), PopRef>);
        static_assert(std::is_trivially_copyable_v<ObserverTag>);
        static_assert(std::is_trivially_copyable_v<ObserverCountrySnapshot>);
        static_assert(std::is_trivially_copyable_v<ObserverStateSnapshot>);
        static_assert(std::is_trivially_copyable_v<CampaignConsoleArguments>);
        static_assert(std::is_trivially_copyable_v<CampaignConsoleResponse>);
        static_assert(std::is_trivially_copyable_v<FrontendControllerToken>);
        static_assert(!std::is_constructible_v<FrontendControllerToken, uint64_t, FrontendControllerKind>);
        static_assert(!std::is_constructible_v<CountryRef, TelemetryCountrySnapshot *>);

        constexpr uintptr_t game_state_instance_rva = 0x00e588e8;
        constexpr uintptr_t give_money_rva = 0x0055a5f0;
        constexpr std::array<uint8_t, 10> give_money_signature{
            0x55, 0x8b, 0xec, 0x83, 0xb8, 0x84, 0x01, 0x00, 0x00, 0x00,
        };
        constexpr size_t game_state_countries_offset = 0x0adc;
        constexpr size_t game_state_provinces_offset = 0x0acc;
        constexpr size_t game_state_date_offset = 0x0b0c;
        constexpr size_t country_tag_offset = 0x01c;
        constexpr size_t country_overlord_offset = 0xcf8;
        constexpr size_t country_vassals_offset = 0xd38;
        constexpr size_t country_sphere_leader_offset = 0x1428;
        constexpr size_t province_id_offset = 0x058;
        constexpr size_t province_owner_offset = 0x128;
        constexpr size_t province_controller_offset = 0x130;
        constexpr size_t province_buildings_offset = 0x118;

        struct ForeignVector
        {
            const void *begin;
            const void *end;
            const void *capacity;
        };

        template <typename T, size_t Size>
        void WriteField(std::array<std::byte, Size> *object, size_t offset, const T &value)
        {
            ASSERT_LE(offset + sizeof(value), object->size());
            std::memcpy(object->data() + offset, &value, sizeof(value));
        }

        template <size_t Size>
        void WriteTag(std::array<std::byte, Size> *object, size_t offset, const char value[4])
        {
            ASSERT_LE(offset + 4, object->size());
            std::memcpy(object->data() + offset, value, 4);
        }

        class RuntimeFixture : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                module_ = static_cast<uint8_t *>(VirtualAlloc(nullptr, 0x1000000,
                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
                ASSERT_NE(module_, nullptr);
                previous_base_ = smedley::memory::Map::base_addr;
                smedley::memory::Map::base_addr = reinterpret_cast<uintptr_t>(module_);
                std::memcpy(module_ + give_money_rva, give_money_signature.data(), give_money_signature.size());
                const void *game_state = &game_state_;
                std::memcpy(module_ + game_state_instance_rva, &game_state, sizeof(game_state));
            }

            void TearDown() override
            {
                smedley::memory::Map::base_addr = previous_base_;
                ASSERT_NE(VirtualFree(module_, 0, MEM_RELEASE), FALSE);
            }

            events::DailyInterestEvent AfterEvent() const
            {
                return events::DailyInterestEvent(reinterpret_cast<v2::CCountry *>(1), events::DailyInterestPhase::AFTER);
            }

            std::array<std::byte, 0xb10> game_state_{};
            uint8_t *module_ = nullptr;
            uintptr_t previous_base_ = 0;
        };
    }

    TEST_F(RuntimeFixture, RejectsInvalidAmountsBeforeAnyStateAccess)
    {
        events::DailyInterestEvent event(reinterpret_cast<v2::CCountry *>(1), events::DailyInterestPhase::BEFORE);
        auto access = DailyInterestAccess::FromEvent(event);
        PopInterestBatchEntry entry{{}, 0};
        PopInterestBatchResult result{};

        EXPECT_EQ(ApplyPopInterestBatch(access, &entry, 1, &result), PopInterestMutationStatus::invalid_amount);
        EXPECT_EQ(entry.status, PopInterestMutationStatus::invalid_amount);
        entry.amount = -1;
        EXPECT_EQ(ApplyPopInterestBatch(access, &entry, 1, &result), PopInterestMutationStatus::invalid_amount);
    }

    TEST_F(RuntimeFixture, ValidatesCompleteBatchInputBeforeRuntimeAccess)
    {
        events::DailyInterestEvent event(reinterpret_cast<v2::CCountry *>(1), events::DailyInterestPhase::BEFORE);
        auto access = DailyInterestAccess::FromEvent(event);
        std::array<PopInterestBatchEntry, 2> entries{{
            {PopRef{reinterpret_cast<const void *>(1)}, 1},
            {PopRef{reinterpret_cast<const void *>(2)}, 0},
        }};
        PopInterestBatchResult result{};

        EXPECT_EQ(ApplyPopInterestBatch(access, entries.data(), entries.size(), &result),
            PopInterestMutationStatus::invalid_amount);
        EXPECT_EQ(result.failed_index, 1u);
        EXPECT_EQ(result.write_count, 0u);
        EXPECT_EQ(result.verified_count, 0u);
        EXPECT_EQ(entries[1].status, PopInterestMutationStatus::invalid_amount);
        EXPECT_EQ(ApplyPopInterestBatch(access, nullptr, 1, &result),
            PopInterestMutationStatus::invalid_context);
        EXPECT_EQ(ApplyPopInterestBatch(access, nullptr, 0, &result),
            PopInterestMutationStatus::success);
        EXPECT_EQ(ApplyPopInterestBatch(access, nullptr, 0, nullptr),
            PopInterestMutationStatus::invalid_context);
    }

    TEST_F(RuntimeFixture, RejectsDuplicateBatchPopsBeforeRuntimeAccess)
    {
        events::DailyInterestEvent event(reinterpret_cast<v2::CCountry *>(1), events::DailyInterestPhase::BEFORE);
        auto access = DailyInterestAccess::FromEvent(event);
        const PopRef pop{reinterpret_cast<const void *>(0x1000)};
        std::array<PopInterestBatchEntry, 2> entries{{{pop, 1}, {pop, 2}}};
        PopInterestBatchResult result{};

        EXPECT_EQ(ApplyPopInterestBatch(access, entries.data(), entries.size(), &result),
            PopInterestMutationStatus::state_changed);
        EXPECT_EQ(result.failed_index, 1u);
        EXPECT_EQ(result.write_count, 0u);
        EXPECT_EQ(result.verified_count, 0u);
        EXPECT_EQ(entries[1].status, PopInterestMutationStatus::state_changed);
    }

    TEST_F(RuntimeFixture, RejectsUntrustedAfterEventAndBeforePhase)
    {
        auto after_event = AfterEvent();
        auto after_access = DailyInterestAccess::FromEvent(after_event);
        PopInterestBatchEntry entry{PopRef{reinterpret_cast<const void *>(1)}, 1};
        PopInterestBatchResult result{};
        EXPECT_EQ(ApplyPopInterestBatch(after_access, &entry, 1, &result),
            PopInterestMutationStatus::invalid_context);

        EventRegistry<events::DailyInterestEvent>::Register(nullptr, "runtime-untrusted-notify-test",
            [&](events::DailyInterestEvent &event) {
                auto access = DailyInterestAccess::FromEvent(event);
                EXPECT_EQ(ApplyPopInterestBatch(access, &entry, 1, &result),
                    PopInterestMutationStatus::invalid_context);
            });
        EventRegistry<events::DailyInterestEvent>::Notify(after_event);
        EventRegistry<events::DailyInterestEvent>::Unregister(nullptr, "runtime-untrusted-notify-test");

        events::DailyInterestEvent before_event(reinterpret_cast<v2::CCountry *>(1), events::DailyInterestPhase::BEFORE);
        auto before_access = DailyInterestAccess::FromEvent(before_event);
        EXPECT_EQ(ApplyPopInterestBatch(before_access, &entry, 1, &result),
            PopInterestMutationStatus::invalid_phase);
    }

    TEST_F(RuntimeFixture, RejectsUntrustedBankInterestCapabilities)
    {
        std::array<std::byte, 0x28> bank{};
        std::array<std::byte, 0xe9c> country{};
        std::array<const void *, 2> countries{nullptr, country.data()};
        const ForeignVector country_vector{countries.data(), countries.data() + countries.size(),
            countries.data() + countries.size()};
        WriteField(&game_state_, game_state_countries_offset, country_vector);
        const void *owner = country.data();
        WriteField(&bank, 0x08, owner);
        events::BankInterestEvent after_event(
            reinterpret_cast<v2::CBank *>(bank.data()), events::BankInterestPhase::AFTER);
        auto after_access = BankInterestAccess::FromEvent(after_event);
        EXPECT_TRUE(after_access.first_country());
        StateInterestCandidate state{};
        state.state = StateRef{reinterpret_cast<const void *>(1)};
        state.state_id = 1;
        state.interest_raw = 1;
        PopInterestBatchResult payout_result{};
        EXPECT_EQ(ApplyStateInterestPayout(after_access, state, nullptr, 0, &payout_result),
            PopInterestMutationStatus::invalid_context);

        events::BankInterestEvent before_event(
            reinterpret_cast<v2::CBank *>(bank.data()), events::BankInterestPhase::BEFORE);
        auto before_access = BankInterestAccess::FromEvent(before_event);
        StateInterestInitializationResult initialization{};
        EXPECT_EQ(DiscardStateInterestPools(before_access, &initialization),
            PopInterestMutationStatus::invalid_context);
    }

    TEST_F(RuntimeFixture, RejectsAccessFromAnotherThread)
    {
        auto event = AfterEvent();
        auto access = DailyInterestAccess::FromEvent(event);
        PopInterestMutationStatus status = PopInterestMutationStatus::success;
        std::thread worker([&] {
            PopInterestBatchEntry entry{PopRef{reinterpret_cast<const void *>(1)}, 1};
            PopInterestBatchResult result{};
            status = ApplyPopInterestBatch(access, &entry, 1, &result);
        });
        worker.join();

        EXPECT_EQ(status, PopInterestMutationStatus::invalid_thread);
    }

    TEST_F(RuntimeFixture, AdvancesEpochAfterObservedGameSessionReplacement)
    {
        std::array<std::byte, 0x260> replacement_game_state{};
        auto event = AfterEvent();
        auto access = DailyInterestAccess::FromEvent(event);
        const uint64_t original_epoch = access.session_epoch();
        const void *replacement = &replacement_game_state;
        std::memcpy(module_ + game_state_instance_rva, &replacement, sizeof(replacement));

        const GameSession replacement_session = CurrentGameSession();
        const GameSession repeated_session = CurrentGameSession();

        EXPECT_EQ(replacement_session.game_state.address(), reinterpret_cast<uintptr_t>(&replacement_game_state));
        EXPECT_GT(replacement_session.epoch, original_epoch);
        EXPECT_EQ(repeated_session.epoch, replacement_session.epoch);
    }

    TEST_F(RuntimeFixture, ValidatesWritableSpanAcrossPageBoundaries)
    {
        SYSTEM_INFO system_info{};
        GetSystemInfo(&system_info);
        const size_t page_size = system_info.dwPageSize;
        ASSERT_NE(page_size, 0u);
        auto *pages = static_cast<std::byte *>(VirtualAlloc(
            nullptr, page_size * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        ASSERT_NE(pages, nullptr);

        const PopRef pop{static_cast<const void *>(pages + page_size - 0x180 - 1)};
        EXPECT_TRUE(IsPopInterestWritable(pop));

        DWORD writable_protection = 0;
        const BOOL made_readonly = VirtualProtect(
            pages + page_size, page_size, PAGE_READONLY, &writable_protection);
        EXPECT_NE(made_readonly, FALSE);
        if (made_readonly != FALSE) {
            EXPECT_FALSE(IsPopInterestWritable(pop));
            DWORD restored_protection = 0;
            EXPECT_NE(VirtualProtect(pages + page_size, page_size, writable_protection, &restored_protection), FALSE);
        }
        EXPECT_FALSE(IsPopInterestWritable({}));
        EXPECT_NE(VirtualFree(pages, 0, MEM_RELEASE), FALSE);
    }

    TEST(PauseOperationTest, RejectsUnsupportedHostBeforeResolvingGameState)
    {
        EXPECT_FALSE(IsPauseOperationAvailable());
        EXPECT_EQ(PauseGame(), PauseOperationStatus::signature_mismatch);
    }

    TEST(CampaignRuntimeOperationTest, FailsClosedOutsideTheSupportedGame)
    {
        CampaignRuntimeSnapshot snapshot{};

        EXPECT_EQ(ReadCampaignRuntime(nullptr), CampaignRuntimeObservationStatus::invalid_state);
        EXPECT_EQ(ReadCampaignRuntime(&snapshot), CampaignRuntimeObservationStatus::signature_mismatch);
        EXPECT_EQ(SetCampaignPaused(false), CampaignOperationStatus::signature_mismatch);
        EXPECT_EQ(SetCampaignSpeedIndex(-1), CampaignOperationStatus::invalid_state);
        EXPECT_EQ(SetCampaignSpeedIndex(0), CampaignOperationStatus::signature_mismatch);
        EXPECT_EQ(RequestCampaignQuit(), CampaignOperationStatus::signature_mismatch);
        EXPECT_EQ(InstallCampaignAutomationHooks({}), CampaignOperationStatus::signature_mismatch);
    }

    TEST(TelemetryHookOperationTest, FailsClosedOutsideTheSupportedGame)
    {
        const uint32_t country_key = 1;
        std::string error;

        EXPECT_FALSE(InstallArtisanConsumptionHook(&country_key, 1, &error));
        EXPECT_EQ(error, "supported game module is unavailable");
        error.clear();
        EXPECT_FALSE(InstallFactoryConsumptionHook(&error));
        EXPECT_EQ(error, "supported game module is unavailable");
        error.clear();
        EXPECT_FALSE(InstallFactorySalesHook(&error));
        EXPECT_EQ(error, "supported game module is unavailable");
        error.clear();
        EXPECT_FALSE(InstallPopCashFlowHook(&error));
        EXPECT_EQ(error, "supported game module is unavailable");
    }

    TEST(FrontendRuntimeOperationTest, FailsClosedWithoutAValidatedFrontend)
    {
        FrontendControllerToken token{};
        FrontendSaveSnapshot save{};

        EXPECT_EQ(InstallFrontendAutomationHooks(), FrontendOperationStatus::signature_mismatch);
        EXPECT_EQ(SetFrontendControllerCaptureCallback(nullptr), FrontendOperationStatus::unavailable);
        EXPECT_EQ(AcquireFrontendController(FrontendControllerKind::frontend, nullptr), FrontendOperationStatus::unavailable);
        EXPECT_EQ(ReleaseFrontendController(token), FrontendOperationStatus::invalid_token);
        EXPECT_EQ(ObserveFrontendSave(token, &save), FrontendOperationStatus::invalid_token);
        EXPECT_EQ(RequestFrontendSave(token, "autosave.v2"), FrontendOperationStatus::invalid_token);
        EXPECT_EQ(RequestFrontendSave(token, "save/path.v2"), FrontendOperationStatus::precondition_failed);
        EXPECT_EQ(DispatchFrontendControl(token, "play_button"), FrontendOperationStatus::invalid_token);
        EXPECT_EQ(DispatchMainMenuSinglePlayer(token), FrontendOperationStatus::invalid_token);
    }

    TEST(ObserverRuntimeOperationTest, FailsClosedOutsideTheSupportedGame)
    {
        ObserverStateSnapshot state{};
        ObserverCountrySnapshot country{};
        std::memcpy(country.tag.value, "JAN", 4);
        country.tag.ordinal = 1;

        EXPECT_EQ(ReadObserverState(nullptr), ObserverObservationStatus::invalid_state);
        EXPECT_EQ(ReadObserverState(&state), ObserverObservationStatus::signature_mismatch);
        EXPECT_EQ(ReadObserverCountry(-1, &country), ObserverObservationStatus::invalid_state);
        EXPECT_EQ(ReadObserverCountry(1, &country), ObserverObservationStatus::signature_mismatch);
        EXPECT_EQ(ResolveObserverCountry("jaN", &country), ObserverObservationStatus::invalid_state);
        EXPECT_EQ(ResolveObserverCountry("JAN", &country), ObserverObservationStatus::signature_mismatch);
        EXPECT_EQ(FindHealthyObserverCountry(-1, &country), ObserverObservationStatus::signature_mismatch);
        std::memcpy(country.tag.value, "JAN", 4);
        country.tag.ordinal = 1;
        EXPECT_EQ(ReturnObserverCountryToAI(country), ObserverOperationStatus::signature_mismatch);
        EXPECT_EQ(SetObserverViewCountry(country), ObserverOperationStatus::signature_mismatch);
        EXPECT_FALSE(IsCampaignObserverConsoleReady());
        EXPECT_FALSE(RegisterCampaignConsoleCapture(nullptr));
        EXPECT_EQ(EnableObserverFullMapVisibility(), ObserverOperationStatus::invalid_state);
    }

    TEST_F(RuntimeFixture, MarksMalformedCurrentStateVectorUnavailableWithoutDiscardingOtherGroups)
    {
        std::array<std::byte, 0xd20> telemetry_game_state{};
        const void *game_state = telemetry_game_state.data();
        std::memcpy(module_ + game_state_instance_rva, &game_state, sizeof(game_state));
        WriteField(&telemetry_game_state, game_state_date_offset, int32_t{1234});
        const ForeignVector malformed{reinterpret_cast<const void *>(0x1000), nullptr, nullptr};
        WriteField(&telemetry_game_state, game_state_countries_offset, malformed);

        TelemetryCurrentState snapshot{};

        ASSERT_TRUE(ReadTelemetryCurrentState(&snapshot));
        EXPECT_EQ(snapshot.date_raw, 1234);
        EXPECT_FALSE(snapshot.world_daily_available());
        size_t province_count = 1;
        EXPECT_TRUE(snapshot.province_count_candidate(&province_count));
        EXPECT_EQ(province_count, 0u);
    }

    TEST_F(RuntimeFixture, PreservesIndependentCountryAndProvinceGroups)
    {
        std::array<std::byte, 0x1600> country{};
        WriteTag(&country, country_tag_offset, "PRU");
        WriteTag(&country, country_overlord_offset, "---");
        WriteTag(&country, country_sphere_leader_offset, "---");
        const ForeignVector malformed{reinterpret_cast<const void *>(0x1000), nullptr, nullptr};
        WriteField(&country, country_vassals_offset, malformed);

        TelemetryCountrySnapshot country_snapshot{};
        ASSERT_TRUE(ReadTelemetryCountry(CountryRef{static_cast<const void *>(country.data())}, &country_snapshot));
        EXPECT_TRUE(country_snapshot.daily_available());
        EXPECT_TRUE(country_snapshot.power_available());
        EXPECT_TRUE(country_snapshot.politics_available());
        EXPECT_TRUE(country_snapshot.military_available());
        EXPECT_TRUE(country_snapshot.diplomacy_status_available());
        EXPECT_FALSE(country_snapshot.diplomacy_relations_available());
        size_t vassals = 1;
        EXPECT_FALSE(country_snapshot.vassal_count_candidate(&vassals));

        std::array<std::byte, 0x2c0> province{};
        WriteField(&province, province_id_offset, int32_t{42});
        WriteTag(&province, province_owner_offset, "PRU");
        WriteTag(&province, province_controller_offset, "PRU");
        WriteField(&province, province_buildings_offset, malformed);

        TelemetryProvinceSnapshot province_snapshot{};
        ASSERT_TRUE(ReadTelemetryProvince(ProvinceRef{static_cast<const void *>(province.data())}, &province_snapshot));
        EXPECT_TRUE(province_snapshot.daily_available());
        EXPECT_FALSE(province_snapshot.production_available());
        size_t building_slots = 1;
        EXPECT_FALSE(province_snapshot.building_slot_count_candidate(&building_slots));
    }
}
