#include "economic_state.hpp"

#include <smedley/events/dailyinterest.hpp>
#include <smedley/memory.hpp>
#include <smedley/plugin.hpp>
#include <smedley/v2/gamestate.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <thread>

namespace interest_bug_fix
{
    namespace
    {
        constexpr int64_t fixture_amount = 1000;
        constexpr uintptr_t give_money_rva = 0x0055a5f0;

        struct FixtureResult
        {
            int32_t date_raw = 0;
            char country_tag[4]{};
            uint32_t sample_flags = 0;
            bool addition_verified = false;
            bool restoration_verified = false;
            PopMoneySnapshot before{};
            PopMoneySnapshot added{};
            PopMoneySnapshot restored{};
        };

        bool CanAdd(int64_t value, int64_t amount)
        {
            return amount >= 0 && value <= (std::numeric_limits<int64_t>::max)() - amount;
        }

        bool Equals(const PopMoneySnapshot &left, const PopMoneySnapshot &right)
        {
            return left.money_raw == right.money_raw
                && left.interest_cash_flow_raw == right.interest_cash_flow_raw
                && left.total_cash_flow_raw == right.total_cash_flow_raw
                && left.savings_raw == right.savings_raw;
        }

        void GiveMoney(const void *pop_address, int64_t amount)
        {
            const uintptr_t function = smedley::memory::Map::base_addr + give_money_rva;
            const uint32_t amount_low = static_cast<uint32_t>(amount);
            const uint32_t amount_high = static_cast<uint32_t>(static_cast<uint64_t>(amount) >> 32);
            __asm {
                push esi
                mov eax, pop_address
                mov esi, 7
                push amount_high
                push amount_low
                call function
                pop esi
            }
        }
    }

    class PopMoneyFixture final : public smedley::Plugin
    {
    public:
        void OnLoad() override
        {
            output_.open("pop_money_fixture.csv", std::ios::trunc);
            if (!output_) throw std::runtime_error("cannot open pop_money_fixture.csv in the game directory");
            output_ << "date_raw,country,sample_flags,addition_verified,restoration_verified,"
                       "money_before,money_added,money_restored,interest_flow_before,interest_flow_added,"
                       "interest_flow_restored,total_flow_before,total_flow_added,total_flow_restored,"
                       "savings_before,savings_added,savings_restored\n";
            output_.flush();
            if (!output_) throw std::runtime_error("cannot initialize pop_money_fixture.csv in the game directory");
            worker_ = std::thread([this] { WriteResult(); });
            try {
                AddEventHandler<smedley::events::DailyInterestEvent>(
                    "pop_money_fixture.boundary",
                    [this](smedley::events::DailyInterestEvent &event) { OnDailyInterest(event); });
            } catch (...) {
                stop_.store(true, std::memory_order_release);
                worker_.join();
                throw;
            }
            logger().Info("armed reversible POP money validation fixture");
        }

        void OnUnload() override
        {
            RemoveEventHandler<smedley::events::DailyInterestEvent>("pop_money_fixture.boundary");
            stop_.store(true, std::memory_order_release);
            if (worker_.joinable()) worker_.join();
            output_.flush();
        }

    private:
        void OnDailyInterest(smedley::events::DailyInterestEvent &event)
        {
            if (event.GetPhase() != smedley::events::DailyInterestPhase::BEFORE
                || status_.load(std::memory_order_relaxed) != 0) {
                return;
            }
            const auto *game_state = smedley::v2::CCurrentGameState::instance();
            if (game_state == nullptr) return;
            const void *pop = nullptr;
            Sample sample = CollectSample(event.GetCountry(), game_state->current_date_raw(),
                ResolveCountry, ResolveProvince, game_state, &pop);
            if (pop == nullptr && sample.flags == 0) return;

            FixtureResult result{};
            result.date_raw = game_state->current_date_raw();
            std::memcpy(result.country_tag, sample.country_tag, sizeof(result.country_tag));
            result.sample_flags = sample.flags;
            if (sample.flags == 0 && ReadPopMoneySnapshot(pop, &result.before)
                && result.before.money_raw >= 0
                && CanAdd(result.before.money_raw, fixture_amount)
                && CanAdd(result.before.interest_cash_flow_raw, fixture_amount)
                && CanAdd(result.before.total_cash_flow_raw, fixture_amount)) {
                GiveMoney(pop, fixture_amount);
                if (ReadPopMoneySnapshot(pop, &result.added)) {
                    result.addition_verified = result.added.money_raw == result.before.money_raw + fixture_amount
                        && result.added.interest_cash_flow_raw == result.before.interest_cash_flow_raw + fixture_amount
                        && result.added.total_cash_flow_raw == result.before.total_cash_flow_raw + fixture_amount
                        && result.added.savings_raw == result.before.savings_raw;
                }
                GiveMoney(pop, -fixture_amount);
                if (ReadPopMoneySnapshot(pop, &result.restored)) {
                    result.restoration_verified = Equals(result.before, result.restored);
                }
            }
            result_ = result;
            status_.store(result.addition_verified && result.restoration_verified ? 1 : 2, std::memory_order_release);
        }

        void WriteResult()
        {
            while (!stop_.load(std::memory_order_acquire) && status_.load(std::memory_order_acquire) == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            const int status = status_.load(std::memory_order_acquire);
            if (status == 0) return;
            const FixtureResult result = result_;
            output_ << result.date_raw << ',' << result.country_tag << ",0x" << std::hex << result.sample_flags
                    << std::dec << ',' << (result.addition_verified ? 1 : 0) << ','
                    << (result.restoration_verified ? 1 : 0) << ','
                    << result.before.money_raw << ',' << result.added.money_raw << ',' << result.restored.money_raw << ','
                    << result.before.interest_cash_flow_raw << ',' << result.added.interest_cash_flow_raw << ','
                    << result.restored.interest_cash_flow_raw << ',' << result.before.total_cash_flow_raw << ','
                    << result.added.total_cash_flow_raw << ',' << result.restored.total_cash_flow_raw << ','
                    << result.before.savings_raw << ',' << result.added.savings_raw << ',' << result.restored.savings_raw << '\n';
            output_.flush();
        }

        static const void *ResolveCountry(const void *context, int32_t ordinal)
        {
            return static_cast<const smedley::v2::CCurrentGameState *>(context)->country(ordinal);
        }

        static const void *ResolveProvince(const void *context, int32_t id)
        {
            return static_cast<const smedley::v2::CCurrentGameState *>(context)->province(id);
        }

        std::ofstream output_;
        FixtureResult result_{};
        std::atomic<int> status_{0};
        std::atomic<bool> stop_{false};
        std::thread worker_;
    };
}

PLUGIN_API smedley::Plugin *CreatePlugin()
{
    return new interest_bug_fix::PopMoneyFixture();
}
