#include "interest_allocation.hpp"
#include "interest_batch.hpp"
#include "interest_mutation_status.hpp"
#include "telemetry_bridge.hpp"

#include <smedley/events/bankinterest.hpp>
#include <smedley/game_state/readers.hpp>
#include <smedley/game_state/runtime.hpp>
#include <smedley/plugin.hpp>

#include <shellapi.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string_view>
#include <thread>

namespace interest_bug_fix
{
    using namespace smedley::game_state;

    namespace
    {
        constexpr size_t result_queue_capacity = 1024;
        constexpr size_t max_country_states = 512;

        bool DebugEnabled()
        {
            int count = 0;
            wchar_t **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
            if (arguments == nullptr) return false;
            bool enabled = false;
            for (int index = 1; index < count; ++index) {
                if (std::wstring_view(arguments[index]) == L"-smedley-interest-fix-debug=1") enabled = true;
            }
            LocalFree(arguments);
            return enabled;
        }

        enum class FixStatus : uint32_t
        {
            initialized,
            paid,
            collection_failed,
            no_eligible_savings,
            allocation_overflow,
            allocation_invalid,
            pop_balance_overflow,
            pop_not_writable,
            duplicate_pop,
            postcondition_failed,
            conservation_failed,
            mutation_unavailable,
            mutation_precondition_changed,
            partial_mutation,
            campaign_disabled,
        };

        struct FixResult
        {
            int32_t date_raw = 0;
            char country_tag[4]{'-', '-', '-', '\0'};
            int32_t state_id = -1;
            FixStatus status = FixStatus::collection_failed;
            uint32_t flags = 0;
            uint32_t state_count = 0;
            uint32_t province_count = 0;
            uint32_t pop_count = 0;
            uint32_t paid_pop_count = 0;
            uint32_t verified_pop_count = 0;
            AllocationStatus allocation_status = AllocationStatus::success;
            int64_t state_pool_raw = 0;
            int64_t payout_raw = 0;
            int64_t discarded_raw = 0;
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

        const char *StatusName(FixStatus status)
        {
            switch (status) {
            case FixStatus::initialized: return "initialized";
            case FixStatus::paid: return "paid";
            case FixStatus::collection_failed: return "collection_failed";
            case FixStatus::no_eligible_savings: return "no_eligible_savings";
            case FixStatus::allocation_overflow: return "allocation_overflow";
            case FixStatus::allocation_invalid: return "allocation_invalid";
            case FixStatus::pop_balance_overflow: return "pop_balance_overflow";
            case FixStatus::pop_not_writable: return "pop_not_writable";
            case FixStatus::duplicate_pop: return "duplicate_pop";
            case FixStatus::postcondition_failed: return "postcondition_failed";
            case FixStatus::conservation_failed: return "conservation_failed";
            case FixStatus::mutation_unavailable: return "mutation_unavailable";
            case FixStatus::mutation_precondition_changed: return "mutation_precondition_changed";
            case FixStatus::partial_mutation: return "partial_mutation";
            case FixStatus::campaign_disabled: return "campaign_disabled";
            }
            return "unknown";
        }

        const char *AllocationStatusName(AllocationStatus status)
        {
            switch (status) {
            case AllocationStatus::success: return "success";
            case AllocationStatus::no_payment: return "no_payment";
            case AllocationStatus::no_eligible_savings: return "no_eligible_savings";
            case AllocationStatus::invalid_input: return "invalid_input";
            case AllocationStatus::overflow: return "overflow";
            case AllocationStatus::scratch_too_small: return "scratch_too_small";
            }
            return "unknown";
        }

        FixStatus AllocationFixStatus(AllocationStatus status)
        {
            if (status == AllocationStatus::no_eligible_savings) return FixStatus::no_eligible_savings;
            if (status == AllocationStatus::overflow) return FixStatus::allocation_overflow;
            return FixStatus::allocation_invalid;
        }

        FixStatus MutationFixStatus(PopInterestMutationStatus status, bool partial)
        {
            switch (ClassifyAppliedPopInterestFailure(status, partial)) {
            case PopInterestFailureClass::balance: return FixStatus::pop_balance_overflow;
            case PopInterestFailureClass::not_writable: return FixStatus::pop_not_writable;
            case PopInterestFailureClass::unavailable: return FixStatus::mutation_unavailable;
            case PopInterestFailureClass::postcondition_failed: return FixStatus::postcondition_failed;
            case PopInterestFailureClass::precondition_changed: return FixStatus::mutation_precondition_changed;
            case PopInterestFailureClass::partial_mutation: return FixStatus::partial_mutation;
            }
            return FixStatus::mutation_precondition_changed;
        }
    }

    class InterestFix final : public smedley::Plugin
    {
    public:
        void OnLoad() override
        {
            debug_ = DebugEnabled();
            if (debug_) StartDiagnostics();
            try {
                AddEventHandler<smedley::events::BankInterestEvent>(
                    "interest_bug_fix.state_pool", [this](smedley::events::BankInterestEvent &event) {
                        OnBankInterest(event);
                    });
            } catch (...) {
                RemoveEventHandler<smedley::events::BankInterestEvent>("interest_bug_fix.state_pool");
                StopDiagnostics();
                throw;
            }
            if (debug_) logger().Info("enabled state-interest payout diagnostics");
        }

        void OnUnload() override
        {
            RemoveEventHandler<smedley::events::BankInterestEvent>("interest_bug_fix.state_pool");
            StopDiagnostics();
        }

    private:
        void OnBankInterest(smedley::events::BankInterestEvent &event)
        {
            const auto started = std::chrono::steady_clock::now();
            try {
                if (event.GetPhase() == smedley::events::BankInterestPhase::BEFORE
                    && event.GetCountryIndex() == 0) {
                    initialized_ = false;
                    disabled_ = true;
                    paid_pops_.Reset();
                }
                BankInterestAccess access = BankInterestAccess::FromEvent(event);
                int32_t date_raw = 0;
                if (!access.game_state() || !ReadCurrentDate(access.game_state(), &date_raw)) return;
                if (!access.after()) {
                    if (access.first_country()) InitializeDailyPass(access, date_raw, started);
                    return;
                }
                if (initialized_ && !disabled_) PayCountryPools(access, date_raw, started);
            } catch (...) {
                disabled_ = true;
                logger().Failure("interest fix disabled after a state-payout exception");
            }
        }

        void InitializeDailyPass(BankInterestAccess &access, int32_t date_raw,
                                 std::chrono::steady_clock::time_point started)
        {
            StateInterestInitializationResult initialization{};
            const PopInterestMutationStatus status = DiscardStateInterestPools(access, &initialization);
            FixResult result{};
            result.date_raw = date_raw;
            result.state_count = initialization.state_count;
            result.flags = initialization.flags;
            result.discarded_raw = initialization.discarded_raw;
            result.status = status == PopInterestMutationStatus::success
                ? FixStatus::initialized : MutationFixStatus(status, initialization.cleared_state_count != 0);
            Publish(result, started);
            if (status == PopInterestMutationStatus::success) {
                initialized_ = true;
                disabled_ = false;
            } else {
                logger().Failure("interest fix disabled because campaign state-interest initialization failed");
            }
        }

        void PayCountryPools(BankInterestAccess &access, int32_t date_raw,
                             std::chrono::steady_clock::time_point started)
        {
            uint32_t state_count = 0;
            uint32_t pop_count = 0;
            CountryEconomySnapshot quality{};
            if (!CollectCountryStateInterest(access.country(), access.game_state(), date_raw,
                    states_.data(), states_.size(), &state_count, nullptr, 0, 0, &pop_count, &quality)) {
                PublishCollectionFailure(date_raw, quality, started);
                return;
            }
            bool has_interest = false;
            for (uint32_t index = 0; index < state_count; ++index) {
                has_interest = has_interest || states_[index].interest_raw > 0;
            }
            if (!has_interest) return;

            if (!CollectCountryStateInterest(access.country(), access.game_state(), date_raw,
                    states_.data(), states_.size(), &state_count, candidates_.data(), candidates_.size(),
                    max_sample_destination_provinces, &pop_count, &quality)) {
                PublishCollectionFailure(date_raw, quality, started);
                return;
            }
            for (uint32_t index = 0; index < state_count && !disabled_; ++index) {
                if (states_[index].interest_raw <= 0) continue;
                PayStatePool(access, states_[index], quality, date_raw, started);
            }
        }

        void PublishCollectionFailure(int32_t date_raw, const CountryEconomySnapshot &quality,
                                      std::chrono::steady_clock::time_point started)
        {
            FixResult result{};
            result.date_raw = date_raw;
            std::memcpy(result.country_tag, quality.country_tag, sizeof(result.country_tag));
            result.status = FixStatus::collection_failed;
            result.flags = quality.flags;
            result.province_count = quality.destination_province_attempts;
            result.pop_count = quality.destination_pop_attempts;
            Publish(result, started);
        }

        void PayStatePool(BankInterestAccess &access, const StateInterestCandidate &state,
                          const CountryEconomySnapshot &quality, int32_t date_raw,
                          std::chrono::steady_clock::time_point started)
        {
            FixResult result{};
            result.date_raw = date_raw;
            std::memcpy(result.country_tag, quality.country_tag, sizeof(result.country_tag));
            result.state_id = state.state_id;
            result.state_pool_raw = state.interest_raw;
            result.province_count = state.province_count;
            result.pop_count = state.pop_count;
            const uint32_t first = state.first_pop_index;
            for (uint32_t index = 0; index < state.pop_count; ++index) {
                allocations_[index].savings_raw = candidates_[first + index].savings_raw;
            }
            result.allocation_status = AllocateInterest(state.interest_raw, allocations_.data(), state.pop_count,
                order_scratch_.data(), order_scratch_.size());
            if (result.allocation_status != AllocationStatus::success) {
                result.status = AllocationFixStatus(result.allocation_status);
                Publish(result, started);
                return;
            }
            if (state.interest_raw > (std::numeric_limits<int64_t>::max)() / 1000) {
                result.status = FixStatus::conservation_failed;
                Publish(result, started);
                return;
            }

            uint32_t payment_count = 0;
            int64_t payout_total = 0;
            for (uint32_t index = 0; index < state.pop_count; ++index) {
                const int64_t payout = allocations_[index].payout_raw;
                if (payout == 0) continue;
                if (paid_pops_.Contains(candidates_[first + index].address.address())) {
                    result.status = FixStatus::duplicate_pop;
                    disabled_ = true;
                    Publish(result, started);
                    return;
                }
                if (payout_total > (std::numeric_limits<int64_t>::max)() - payout) {
                    result.status = FixStatus::conservation_failed;
                    Publish(result, started);
                    return;
                }
                payments_[payment_count].pop = candidates_[first + index].address;
                payments_[payment_count].amount = payout;
                payout_total += payout;
                ++payment_count;
            }
            if (payout_total != state.interest_raw * 1000) {
                result.status = FixStatus::conservation_failed;
                Publish(result, started);
                return;
            }
            result.payout_raw = payout_total;
            if (payment_count > daily_pop_set_capacity - paid_pops_.size()) {
                result.status = FixStatus::duplicate_pop;
                disabled_ = true;
                Publish(result, started);
                return;
            }
            for (uint32_t index = 0; index < payment_count; ++index) {
                if (paid_pops_.Insert(payments_[index].pop.address()) != PointerInsertStatus::inserted) {
                    result.status = FixStatus::duplicate_pop;
                    disabled_ = true;
                    Publish(result, started);
                    return;
                }
            }

            PopInterestBatchResult batch_result{};
            const PopInterestMutationStatus status = ApplyStateInterestPayout(
                access, state, payments_.data(), payment_count, &batch_result);
            result.paid_pop_count = batch_result.write_count;
            result.verified_pop_count = batch_result.verified_count;
            if (status == PopInterestMutationStatus::success) {
                result.status = FixStatus::paid;
            } else {
                const bool partial = batch_result.write_count != 0;
                result.status = MutationFixStatus(status, partial);
                if (IsUnsafeAppliedPopInterestFailure(status, partial)) {
                    disabled_ = true;
                    logger().Failure("interest fix disabled after an incomplete state-interest payout");
                }
            }
            Publish(result, started);
        }

        void Publish(FixResult &result, std::chrono::steady_clock::time_point started)
        {
            if (!debug_) return;
            result.callback_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started).count());
            const auto country = TelemetryStringField("country_tag", result.country_tag);
            const SmedleyTelemetryFieldV1 health[] = {
                TelemetryStringField("status", StatusName(result.status)),
                TelemetryIntField("flags", result.flags),
                TelemetryIntField("state_id", result.state_id),
                TelemetryIntField("pop_count", result.pop_count),
                TelemetryIntField("verified_pop_count", result.verified_pop_count),
                TelemetryIntField("callback_us", static_cast<int64_t>(result.callback_us)),
            };
            result.health_telemetry_result = telemetry_.Emit(
                "interest.fix.health", "verified-runtime", result.date_raw, &country, 1, health, 6, true);
            if (result.status == FixStatus::paid || result.status == FixStatus::initialized) {
                const SmedleyTelemetryFieldV1 value[] = {
                    TelemetryIntField("state_id", result.state_id),
                    TelemetryIntField("state_pool_raw", result.state_pool_raw),
                    TelemetryIntField("payout_raw", result.payout_raw),
                    TelemetryIntField("discarded_raw", result.discarded_raw),
                };
                result.value_telemetry_result = telemetry_.Emit(
                    "interest.fix.value", "verified-runtime", result.date_raw, &country, 1, value, 4, true);
            }
            if (!queue_.TryPush(result)) dropped_.fetch_add(1, std::memory_order_relaxed);
        }

        void StartDiagnostics()
        {
            output_.open("interest_bug_fix.csv", std::ios::trunc);
            if (!output_) throw std::runtime_error("cannot open interest_bug_fix.csv in the game directory");
            output_ << "date_raw,country,state_id,status,flags,state_count,province_count,pop_count,"
                       "paid_pop_count,verified_pop_count,state_pool_raw,payout_raw,discarded_raw,"
                       "allocation_status,callback_us,health_telemetry_result,value_telemetry_result,dropped_results\n";
            output_.flush();
            if (!output_) throw std::runtime_error("cannot initialize interest_bug_fix.csv in the game directory");
            worker_ = std::thread([this] { WriteResults(); });
        }

        void StopDiagnostics() noexcept
        {
            if (!debug_) return;
            stop_.store(true, std::memory_order_release);
            if (worker_.joinable()) worker_.join();
            output_.flush();
        }

        void WriteResults()
        {
            while (!stop_.load(std::memory_order_acquire) || !queue_.Empty()) {
                bool wrote = false;
                FixResult result{};
                while (queue_.TryPop(&result)) {
                    output_ << result.date_raw << ',' << result.country_tag << ',' << result.state_id << ','
                            << StatusName(result.status) << ",0x" << std::hex << result.flags << std::dec << ','
                            << result.state_count << ',' << result.province_count << ',' << result.pop_count << ','
                            << result.paid_pop_count << ',' << result.verified_pop_count << ','
                            << result.state_pool_raw << ',' << result.payout_raw << ',' << result.discarded_raw << ','
                            << AllocationStatusName(result.allocation_status) << ',' << result.callback_us << ','
                            << result.health_telemetry_result << ',' << result.value_telemetry_result << ','
                            << dropped_.load(std::memory_order_relaxed) << '\n';
                    wrote = true;
                }
                if (wrote) output_.flush();
                else std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        ResultQueue<result_queue_capacity> queue_{};
        std::array<StateInterestCandidate, max_country_states> states_{};
        std::array<PopCandidate, max_sample_pops> candidates_{};
        std::array<AllocationEntry, max_sample_pops> allocations_{};
        std::array<PopInterestBatchEntry, max_sample_pops> payments_{};
        std::array<uint32_t, max_sample_pops> order_scratch_{};
        TelemetryBridge telemetry_{};
        std::ofstream output_;
        bool initialized_ = false;
        bool disabled_ = false;
        bool debug_ = false;
        DailyPopSet paid_pops_{};
        std::atomic<uint64_t> dropped_{0};
        std::atomic<bool> stop_{false};
        std::thread worker_;
    };
}

PLUGIN_API smedley::Plugin *CreatePlugin()
{
    return new interest_bug_fix::InterestFix();
}
