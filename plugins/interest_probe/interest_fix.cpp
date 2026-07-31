#include "interest_allocation.hpp"
#include "probe_core.hpp"
#include "telemetry_bridge.hpp"

#include <smedley/events/dailyinterest.hpp>
#include <smedley/memory.hpp>
#include <smedley/plugin.hpp>
#include <smedley/v2/gamestate.hpp>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <thread>

namespace interest_probe
{
    namespace
    {
        constexpr uintptr_t give_money_rva = 0x0055a5f0;
        constexpr size_t result_queue_capacity = 1024;

        enum class FixStatus : uint32_t
        {
            paid,
            no_transfer,
            invalid_pair,
            collection_failed,
            allocation_failed,
            pop_balance_overflow,
            pop_not_writable,
            duplicate_pop,
            postcondition_failed,
            conservation_failed,
        };

        struct FixResult
        {
            int32_t date_raw = 0;
            char country_tag[4]{};
            FixStatus status = FixStatus::invalid_pair;
            uint32_t flags = 0;
            uint32_t destination_count = 0;
            uint32_t province_count = 0;
            uint32_t pop_count = 0;
            uint32_t paid_pop_count = 0;
            uint32_t verified_pop_count = 0;
            int64_t transfer_raw = 0;
            int64_t payout_raw = 0;
            uint64_t callback_us = 0;
            SmedleyTelemetryResult health_telemetry_result = SMEDLEY_TELEMETRY_UNAVAILABLE;
            SmedleyTelemetryResult value_telemetry_result = SMEDLEY_TELEMETRY_UNAVAILABLE;
        };

        template <size_t Capacity>
        class ResultQueue
        {
        public:
            bool TryPush(const FixResult &result) noexcept
            {
                const uint32_t write = write_.load(std::memory_order_relaxed);
                const uint32_t next = (write + 1) % Capacity;
                if (next == read_.load(std::memory_order_acquire)) return false;
                results_[write] = result;
                write_.store(next, std::memory_order_release);
                return true;
            }

            bool TryPop(FixResult *result) noexcept
            {
                const uint32_t read = read_.load(std::memory_order_relaxed);
                if (read == write_.load(std::memory_order_acquire)) return false;
                *result = results_[read];
                read_.store((read + 1) % Capacity, std::memory_order_release);
                return true;
            }

            bool Empty() const noexcept
            {
                return read_.load(std::memory_order_relaxed) == write_.load(std::memory_order_acquire);
            }

        private:
            std::array<FixResult, Capacity> results_{};
            std::atomic<uint32_t> write_{0};
            std::atomic<uint32_t> read_{0};
        };

        bool CanAdd(int64_t value, int64_t amount)
        {
            return amount >= 0 && value <= (std::numeric_limits<int64_t>::max)() - amount;
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

        const char *StatusName(FixStatus status)
        {
            switch (status) {
            case FixStatus::paid: return "paid";
            case FixStatus::no_transfer: return "no_transfer";
            case FixStatus::invalid_pair: return "invalid_pair";
            case FixStatus::collection_failed: return "collection_failed";
            case FixStatus::allocation_failed: return "allocation_failed";
            case FixStatus::pop_balance_overflow: return "pop_balance_overflow";
            case FixStatus::pop_not_writable: return "pop_not_writable";
            case FixStatus::duplicate_pop: return "duplicate_pop";
            case FixStatus::postcondition_failed: return "postcondition_failed";
            case FixStatus::conservation_failed: return "conservation_failed";
            }
            return "unknown";
        }
    }

    class InterestFix final : public smedley::Plugin
    {
    public:
        void OnLoad() override
        {
            output_.open("interest_fix.csv", std::ios::trunc);
            if (!output_) throw std::runtime_error("cannot open interest_fix.csv in the game directory");
            output_ << "date_raw,country,status,flags,destination_count,pop_count,paid_pop_count,"
                       "province_count,verified_pop_count,transfer_raw,payout_raw,callback_us,"
                       "health_telemetry_result,value_telemetry_result,dropped_results\n";
            output_.flush();
            if (!output_) throw std::runtime_error("cannot initialize interest_fix.csv in the game directory");
            worker_ = std::thread([this] { WriteResults(); });
            try {
                AddEventHandler<smedley::events::DailyInterestEvent>(
                    "interest_fix.boundary", [this](smedley::events::DailyInterestEvent &event) { OnDailyInterest(event); });
            } catch (...) {
                stop_.store(true, std::memory_order_release);
                worker_.join();
                throw;
            }
            logger().Info("enabled opt-in exact creditor POP interest distribution");
        }

        void OnUnload() override
        {
            RemoveEventHandler<smedley::events::DailyInterestEvent>("interest_fix.boundary");
            stop_.store(true, std::memory_order_release);
            if (worker_.joinable()) worker_.join();
            output_.flush();
        }

    private:
        void OnDailyInterest(smedley::events::DailyInterestEvent &event)
        {
            if (disabled_) return;
            const auto *game_state = smedley::v2::CCurrentGameState::instance();
            if (game_state == nullptr) return;
            if (event.GetPhase() == smedley::events::DailyInterestPhase::BEFORE) {
                pending_before_ = CollectSample(event.GetCountry(), game_state->current_date_raw(),
                    ResolveCountry, nullptr, game_state);
                has_pending_before_ = true;
                return;
            }
            if (!has_pending_before_) return;
            callback_started_ = std::chrono::steady_clock::now();

            Sample after = CollectSample(event.GetCountry(), game_state->current_date_raw(),
                ResolveCountry, nullptr, game_state);
            FixResult result{};
            result.date_raw = after.date_raw;
            std::memcpy(result.country_tag, after.country_tag, sizeof(result.country_tag));
            result.flags = pending_before_.flags | after.flags;
            if (pending_before_.date_raw != after.date_raw
                || std::memcmp(pending_before_.country_tag, after.country_tag, sizeof(after.country_tag)) != 0
                || !ComputeDestinationTransfers(pending_before_, &after)) {
                result.status = FixStatus::invalid_pair;
                result.flags |= after.flags;
                has_pending_before_ = false;
                if (pending_before_.creditor_count != 0 || after.creditor_count != 0) Publish(result);
                return;
            }
            has_pending_before_ = false;
            result.destination_count = after.destination_transfer_count;
            result.transfer_raw = after.destination_transfer_raw;
            if (after.destination_transfer_count == 0) {
                if (after.creditor_count != 0) {
                    result.status = FixStatus::no_transfer;
                    Publish(result);
                }
                return;
            }
            if (pending_before_.treasury_raw < (std::numeric_limits<int64_t>::min)() + after.destination_transfer_raw
                || after.treasury_raw != pending_before_.treasury_raw - after.destination_transfer_raw) {
                result.status = FixStatus::conservation_failed;
                Publish(result);
                return;
            }
            if (!PreparePayouts(game_state, after, &result)) {
                Publish(result);
                return;
            }
            if (!ValidateUniquePayoutPops()) {
                result.status = FixStatus::duplicate_pop;
                Publish(result);
                return;
            }
            for (uint32_t index = 0; index < candidate_count_; ++index) {
                if (allocations_[index].payout_raw != 0 && !CanWritePopMoney(candidates_[index].address)) {
                    result.status = FixStatus::pop_not_writable;
                    Publish(result);
                    return;
                }
            }
            for (uint32_t index = 0; index < candidate_count_; ++index) {
                const int64_t payout = allocations_[index].payout_raw;
                if (payout == 0) continue;
                GiveMoney(candidates_[index].address, payout);
                PopMoneySnapshot after_snapshot{};
                const PopMoneySnapshot &before_snapshot = before_snapshots_[index];
                if (!ReadPopMoneySnapshot(candidates_[index].address, &after_snapshot)
                    || after_snapshot.money_raw != before_snapshot.money_raw + payout
                    || after_snapshot.interest_cash_flow_raw != before_snapshot.interest_cash_flow_raw + payout
                    || after_snapshot.total_cash_flow_raw != before_snapshot.total_cash_flow_raw + payout
                    || after_snapshot.savings_raw != before_snapshot.savings_raw) {
                    result.status = FixStatus::postcondition_failed;
                    disabled_ = true;
                    Publish(result);
                    return;
                }
                ++result.verified_pop_count;
            }
            result.status = FixStatus::paid;
            Publish(result);
        }

        bool PreparePayouts(const smedley::v2::CCurrentGameState *game_state,
                            const Sample &after, FixResult *result)
        {
            candidate_count_ = 0;
            uint32_t province_attempt_count = 0;
            int64_t payout_total = 0;
            for (uint32_t destination = 0; destination < after.creditor_destinations; ++destination) {
                const int64_t transfer = after.destination_transfers_raw[destination];
                if (transfer == 0) continue;
                const int32_t ordinal = after.destination_ordinals[destination];
                uint32_t collected = 0;
                Sample quality{};
                if (!CollectCountryPops(game_state->country(ordinal), after.date_raw,
                        ResolveProvince, game_state, candidates_.data() + candidate_count_,
                        candidates_.size() - candidate_count_,
                        max_sample_destination_provinces - province_attempt_count,
                        &collected, &quality)) {
                    result->province_count = province_attempt_count + quality.destination_province_attempts;
                    result->pop_count = candidate_count_ + quality.destination_pop_attempts;
                    result->status = FixStatus::collection_failed;
                    result->flags |= quality.flags;
                    return false;
                }
                province_attempt_count += quality.destination_province_attempts;
                result->province_count = province_attempt_count;
                for (uint32_t index = 0; index < collected; ++index) {
                    allocations_[candidate_count_ + index].savings_raw = candidates_[candidate_count_ + index].savings_raw;
                }
                const AllocationStatus allocation = AllocateInterest(transfer,
                    allocations_.data() + candidate_count_, collected, order_scratch_.data(), order_scratch_.size());
                if (allocation != AllocationStatus::success) {
                    result->status = FixStatus::allocation_failed;
                    return false;
                }
                for (uint32_t index = 0; index < collected; ++index) {
                    const int64_t payout = allocations_[candidate_count_ + index].payout_raw;
                    if (payout == 0) continue;
                    PopMoneySnapshot snapshot{};
                    if (!ReadPopMoneySnapshot(candidates_[candidate_count_ + index].address, &snapshot)
                        || !CanWritePopMoney(candidates_[candidate_count_ + index].address)
                        || !CanAdd(snapshot.money_raw, payout)
                        || !CanAdd(snapshot.interest_cash_flow_raw, payout)
                        || !CanAdd(snapshot.total_cash_flow_raw, payout)) {
                        result->status = FixStatus::pop_balance_overflow;
                        return false;
                    }
                    before_snapshots_[candidate_count_ + index] = snapshot;
                    if (payout_total > (std::numeric_limits<int64_t>::max)() - payout) {
                        result->status = FixStatus::conservation_failed;
                        return false;
                    }
                    payout_total += payout;
                    ++result->paid_pop_count;
                }
                candidate_count_ += collected;
            }
            if (after.destination_transfer_raw > (std::numeric_limits<int64_t>::max)() / 1000
                || payout_total != after.destination_transfer_raw * 1000) {
                result->status = FixStatus::conservation_failed;
                return false;
            }
            result->pop_count = candidate_count_;
            result->payout_raw = payout_total;
            return true;
        }

        bool ValidateUniquePayoutPops()
        {
            uint32_t payout_count = 0;
            for (uint32_t index = 0; index < candidate_count_; ++index) {
                order_scratch_[payout_count++] = index;
            }
            std::sort(order_scratch_.begin(), order_scratch_.begin() + payout_count,
                [this](uint32_t left, uint32_t right) {
                    return reinterpret_cast<uintptr_t>(candidates_[left].address)
                        < reinterpret_cast<uintptr_t>(candidates_[right].address);
                });
            for (uint32_t index = 1; index < payout_count; ++index) {
                if (candidates_[order_scratch_[index - 1]].address == candidates_[order_scratch_[index]].address) {
                    return false;
                }
            }
            return true;
        }

        static const void *ResolveCountry(const void *context, int32_t ordinal)
        {
            return static_cast<const smedley::v2::CCurrentGameState *>(context)->country(ordinal);
        }

        static const void *ResolveProvince(const void *context, int32_t id)
        {
            return static_cast<const smedley::v2::CCurrentGameState *>(context)->province(id);
        }

        void Publish(FixResult result)
        {
            result.callback_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - callback_started_).count());
            const auto country = TelemetryStringField("country_tag", result.country_tag);
            const SmedleyTelemetryFieldV1 health[] = {
                TelemetryStringField("status", StatusName(result.status)),
                TelemetryIntField("flags", result.flags),
                TelemetryIntField("destination_count", result.destination_count),
                TelemetryIntField("province_count", result.province_count),
                TelemetryIntField("pop_count", result.pop_count),
                TelemetryIntField("verified_pop_count", result.verified_pop_count),
                TelemetryIntField("callback_us", static_cast<int64_t>(result.callback_us)),
            };
            static_assert(1 + std::size(health) <= SMEDLEY_TELEMETRY_MAX_FIELDS);
            result.health_telemetry_result = telemetry_.Emit(
                "interest.fix.health", "verified-runtime", result.date_raw, &country, 1, health, 7);
            const SmedleyTelemetryFieldV1 value[] = {
                TelemetryIntField("transfer_raw", result.transfer_raw),
                TelemetryIntField("payout_raw", result.payout_raw),
                TelemetryIntField("paid_pop_count", result.paid_pop_count),
                TelemetryIntField("verified_pop_count", result.verified_pop_count),
            };
            static_assert(1 + std::size(value) <= SMEDLEY_TELEMETRY_MAX_FIELDS);
            if (result.status == FixStatus::paid) {
                result.value_telemetry_result = telemetry_.Emit(
                    "interest.fix.value", "verified-runtime", result.date_raw, &country, 1, value, 4);
            }
            if (!queue_.TryPush(result)) dropped_.fetch_add(1, std::memory_order_relaxed);
        }

        void WriteResults()
        {
            while (!stop_.load(std::memory_order_acquire) || !queue_.Empty()) {
                bool wrote = false;
                FixResult result{};
                while (queue_.TryPop(&result)) {
                    output_ << result.date_raw << ',' << result.country_tag << ',' << StatusName(result.status)
                            << ",0x" << std::hex << result.flags << std::dec << ',' << result.destination_count << ','
                            << result.pop_count << ',' << result.paid_pop_count << ',' << result.province_count << ','
                            << result.verified_pop_count << ',' << result.transfer_raw << ',' << result.payout_raw << ','
                            << result.callback_us << ',' << result.health_telemetry_result << ','
                            << result.value_telemetry_result << ','
                            << dropped_.load(std::memory_order_relaxed) << '\n';
                    wrote = true;
                }
                if (wrote) output_.flush();
                else std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        std::ofstream output_;
        ResultQueue<result_queue_capacity> queue_{};
        Sample pending_before_{};
        bool has_pending_before_ = false;
        bool disabled_ = false;
        uint32_t candidate_count_ = 0;
        std::array<PopCandidate, max_sample_pops> candidates_{};
        std::array<AllocationEntry, max_sample_pops> allocations_{};
        std::array<PopMoneySnapshot, max_sample_pops> before_snapshots_{};
        std::array<uint32_t, max_sample_pops> order_scratch_{};
        TelemetryBridge telemetry_{};
        std::chrono::steady_clock::time_point callback_started_{};
        std::atomic<uint64_t> dropped_{0};
        std::atomic<bool> stop_{false};
        std::thread worker_;
    };
}

PLUGIN_API smedley::Plugin *CreatePlugin()
{
    return new interest_probe::InterestFix();
}
