#include <smedley/game_state/runtime.hpp>

#include <smedley/events/dailyinterest.hpp>
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

        constexpr uintptr_t game_state_instance_rva = 0x00e588e8;
        constexpr uintptr_t give_money_rva = 0x0055a5f0;
        constexpr std::array<uint8_t, 10> give_money_signature{
            0x55, 0x8b, 0xec, 0x83, 0xb8, 0x84, 0x01, 0x00, 0x00, 0x00,
        };

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

            std::array<std::byte, 0x260> game_state_{};
            uint8_t *module_ = nullptr;
            uintptr_t previous_base_ = 0;
        };
    }

    TEST_F(RuntimeFixture, RejectsInvalidAmountsBeforeAnyStateAccess)
    {
        events::DailyInterestEvent event(reinterpret_cast<v2::CCountry *>(1), events::DailyInterestPhase::BEFORE);
        auto access = DailyInterestAccess::FromEvent(event);
        PopInterestPreflight preflight{};

        EXPECT_EQ(PreparePopInterest(access, {}, 0, &preflight), PopInterestMutationStatus::invalid_amount);
        EXPECT_EQ(preflight.status, PopInterestMutationStatus::invalid_amount);
        EXPECT_EQ(PreparePopInterest(access, {}, -1, &preflight), PopInterestMutationStatus::invalid_amount);
    }

    TEST_F(RuntimeFixture, RejectsUntrustedAfterEventAndBeforePhase)
    {
        auto after_event = AfterEvent();
        auto after_access = DailyInterestAccess::FromEvent(after_event);
        PopInterestPreflight preflight{};
        EXPECT_EQ(PreparePopInterest(after_access, PopRef{reinterpret_cast<const void *>(1)}, 1, &preflight),
            PopInterestMutationStatus::invalid_context);

        EventRegistry<events::DailyInterestEvent>::Register(nullptr, "runtime-untrusted-notify-test",
            [&](events::DailyInterestEvent &event) {
                auto access = DailyInterestAccess::FromEvent(event);
                EXPECT_EQ(PreparePopInterest(access, PopRef{reinterpret_cast<const void *>(1)}, 1, &preflight),
                    PopInterestMutationStatus::invalid_context);
            });
        EventRegistry<events::DailyInterestEvent>::Notify(after_event);
        EventRegistry<events::DailyInterestEvent>::Unregister(nullptr, "runtime-untrusted-notify-test");

        events::DailyInterestEvent before_event(reinterpret_cast<v2::CCountry *>(1), events::DailyInterestPhase::BEFORE);
        auto before_access = DailyInterestAccess::FromEvent(before_event);
        EXPECT_EQ(PreparePopInterest(before_access, PopRef{reinterpret_cast<const void *>(1)}, 1, &preflight),
            PopInterestMutationStatus::invalid_phase);
    }

    TEST_F(RuntimeFixture, RejectsAccessFromAnotherThread)
    {
        auto event = AfterEvent();
        auto access = DailyInterestAccess::FromEvent(event);
        PopInterestMutationStatus status = PopInterestMutationStatus::success;
        std::thread worker([&] {
            PopInterestPreflight preflight{};
            status = PreparePopInterest(access, PopRef{reinterpret_cast<const void *>(1)}, 1, &preflight);
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

        const PopRef pop{pages + page_size - 0x180 - 1};
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
}
