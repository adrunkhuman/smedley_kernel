#include "interest_allocation.hpp"
#include "interest_batch.hpp"
#include "interest_mutation_status.hpp"
#include "interest_reconciliation.hpp"
#include "telemetry_bridge.hpp"

#include <smedley/game_state/readers.hpp>
#include <smedley/game_state/runtime.hpp>

#include <smedley/events/dailyinterest.hpp>
#include <smedley/events/dailyupdate.hpp>
#include <smedley/plugin.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <thread>

namespace interest_bug_fix
{
    using namespace smedley::game_state;

    namespace
    {
        constexpr size_t result_queue_capacity = 1024;

        enum class FixStatus : uint32_t
        {
            paid,
            invalid_pair,
            batch_invalid,
            day_incomplete,
            day_summary,
            day_partial,
            recipient_identity_invalid,
            collection_failed,
            no_eligible_savings,
            allocation_overflow,
            allocation_invalid,
            pop_balance_overflow,
            pop_not_writable,
            duplicate_pop,
            pop_identity_limit,
            postcondition_failed,
            conservation_failed,
            mutation_unavailable,
            mutation_precondition_changed,
            partial_mutation,
        };

        struct FixResult
        {
            int32_t date_raw = 0;
            char country_tag[4]{};
            FixStatus status = FixStatus::invalid_pair;
            uint32_t flags = 0;
            uint32_t source_count = 0;
            uint32_t province_count = 0;
            uint32_t pop_count = 0;
            uint32_t paid_pop_count = 0;
            uint32_t verified_pop_count = 0;
            uint32_t rejected_debtors = 0;
            AllocationStatus allocation_status = AllocationStatus::success;
            int64_t transfer_raw = 0;
            int64_t domestic_transfer_raw = 0;
            int64_t foreign_transfer_raw = 0;
            int64_t payout_raw = 0;
            uint64_t callback_us = 0;
            ReconciliationFailure reconciliation_failure = ReconciliationFailure::none;
            uint32_t reconciliation_creditor_count = 0;
            uint32_t reconciliation_destination_count = 0;
            uint32_t daily_max_creditor_count = 0;
            uint32_t daily_max_destination_count = 0;
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

        const char *StatusName(FixStatus status)
        {
            switch (status) {
            case FixStatus::paid: return "paid";
            case FixStatus::invalid_pair: return "invalid_pair";
            case FixStatus::batch_invalid: return "batch_invalid";
            case FixStatus::day_incomplete: return "day_incomplete";
            case FixStatus::day_summary: return "day_summary";
            case FixStatus::day_partial: return "day_partial";
            case FixStatus::recipient_identity_invalid: return "recipient_identity_invalid";
            case FixStatus::collection_failed: return "collection_failed";
            case FixStatus::no_eligible_savings: return "no_eligible_savings";
            case FixStatus::allocation_overflow: return "allocation_overflow";
            case FixStatus::allocation_invalid: return "allocation_invalid";
            case FixStatus::pop_balance_overflow: return "pop_balance_overflow";
            case FixStatus::pop_not_writable: return "pop_not_writable";
            case FixStatus::duplicate_pop: return "duplicate_pop";
            case FixStatus::pop_identity_limit: return "pop_identity_limit";
            case FixStatus::postcondition_failed: return "postcondition_failed";
            case FixStatus::conservation_failed: return "conservation_failed";
            case FixStatus::mutation_unavailable: return "mutation_unavailable";
            case FixStatus::mutation_precondition_changed: return "mutation_precondition_changed";
            case FixStatus::partial_mutation: return "partial_mutation";
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

        FixStatus PreflightFixStatus(PopInterestMutationStatus status)
        {
            switch (ClassifyPopInterestFailure(status)) {
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
            output_.open("interest_bug_fix.csv", std::ios::trunc);
            if (!output_) throw std::runtime_error("cannot open interest_bug_fix.csv in the game directory");
            output_ << "date_raw,country,status,flags,source_count,pop_count,paid_pop_count,"
                        "province_count,verified_pop_count,transfer_raw,domestic_transfer_raw,"
                        "foreign_transfer_raw,payout_raw,allocation_status,callback_us,"
                        "rejected_debtors,health_telemetry_result,value_telemetry_result,dropped_results,reconciliation_failure,"
                        "reconciliation_creditor_count,reconciliation_destination_count,daily_max_creditor_count,"
                          "daily_max_destination_count\n";
            output_.flush();
            if (!output_) throw std::runtime_error("cannot initialize interest_bug_fix.csv in the game directory");
            worker_ = std::thread([this] { WriteResults(); });
            try {
                AddEventHandler<smedley::events::DailyUpdateEvent>(
                    "interest_bug_fix.day", [this](smedley::events::DailyUpdateEvent &event) { OnDailyUpdate(event); });
                AddEventHandler<smedley::events::DailyInterestEvent>(
                    "interest_bug_fix.boundary", [this](smedley::events::DailyInterestEvent &event) { OnDailyInterest(event); });
            } catch (...) {
                RemoveEventHandler<smedley::events::DailyInterestEvent>("interest_bug_fix.boundary");
                RemoveEventHandler<smedley::events::DailyUpdateEvent>("interest_bug_fix.day");
                stop_.store(true, std::memory_order_release);
                worker_.join();
                throw;
            }
            logger().Info("enabled opt-in batched creditor POP interest distribution");
        }

        void OnUnload() override
        {
            RemoveEventHandler<smedley::events::DailyInterestEvent>("interest_bug_fix.boundary");
            RemoveEventHandler<smedley::events::DailyUpdateEvent>("interest_bug_fix.day");
            stop_.store(true, std::memory_order_release);
            if (worker_.joinable()) worker_.join();
            output_.flush();
        }

    private:
        void OnDailyUpdate(smedley::events::DailyUpdateEvent &)
        {
            if (disabled_) {
                ResetPendingInterest();
                return;
            }
            try {
                const GameStateRef game_state_ref = CurrentGameSession().game_state;
                if (!game_state_ref) return;
                int32_t date_raw = 0;
                if (!ReadCurrentDate(game_state_ref, &date_raw)) return;
                if (date_raw == finalized_date_raw_) return;
                if (batch_.started() && batch_.date_raw() != date_raw) {
                    ResetPendingInterest();
                    FixResult result{};
                    result.date_raw = batch_.date_raw();
                    std::memcpy(result.country_tag, "---", 4);
                    result.status = FixStatus::day_incomplete;
                    result.source_count = batch_.seen_count();
                    result.rejected_debtors = batch_.expected_debtors() - batch_.seen_count();
                    callback_started_ = std::chrono::steady_clock::now();
                    Publish(result);
                    batch_.Reset();
                }
                if (!batch_.started()) {
                    day_flags_ = 0;
                    daily_max_creditor_count_ = 0;
                    daily_max_destination_count_ = 0;
                    uint32_t country_count = 0;
                    if (!ReadCountryCount(game_state_ref, &country_count)
                        || !batch_.Begin(date_raw, country_count)) {
                        disabled_ = true;
                        logger().Failure("interest fix disabled because the country vector exceeds the daily batch bound");
                    }
                }
            } catch (...) {
                disabled_ = true;
                logger().Failure("interest fix disabled after a daily batch exception");
            }
        }

        void OnDailyInterest(smedley::events::DailyInterestEvent &event)
        {
            if (disabled_) {
                ResetPendingInterest();
                return;
            }
            DailyInterestAccess access = DailyInterestAccess::FromEvent(event);
            const GameStateRef game_state_ref = access.game_state();
            if (!game_state_ref || !batch_.started()) {
                ResetPendingInterest();
                return;
            }
            int32_t date_raw = 0;
            if (!ReadCurrentDate(game_state_ref, &date_raw)) {
                ResetPendingInterest();
                return;
            }
            if (event.GetPhase() == smedley::events::DailyInterestPhase::BEFORE) {
                ResetPendingInterest();
                pending_before_ = ReadCountryCreditors(access.country(), date_raw,
                    ResolveCountry, &game_state_ref);
                daily_max_creditor_count_ = (std::max)(daily_max_creditor_count_, pending_before_.creditor_count);
                daily_max_destination_count_ = (std::max)(daily_max_destination_count_, pending_before_.creditor_destinations);
                has_pending_before_ = true;
                return;
            }
            if (!has_pending_before_) {
                return;
            }
            ResetPendingInterest();
            callback_started_ = std::chrono::steady_clock::now();

            CountryEconomySnapshot after = ReadCountryCreditorBalances(pending_before_, access.country(),
                date_raw, ResolveCountry, &game_state_ref);
            DestinationTransferSummary transfer_summary{};
            const int32_t debtor_ordinal = after.country_ordinal > 0
                ? after.country_ordinal : pending_before_.country_ordinal;
            ReconciliationFailure reconciliation_failure = ReconciliationFailure::none;
            ComputeDestinationTransfers(pending_before_, after, &transfer_summary, &reconciliation_failure);
            if (reconciliation_failure == ReconciliationFailure::none
                && (pending_before_.date_raw != after.date_raw || after.date_raw != batch_.date_raw())) {
                reconciliation_failure = ReconciliationFailure::date_changed;
            } else if (reconciliation_failure == ReconciliationFailure::none
                && std::memcmp(pending_before_.country_tag, after.country_tag, 4) != 0) {
                reconciliation_failure = ReconciliationFailure::country_changed;
            }
            if (reconciliation_failure != ReconciliationFailure::none) {
                RejectPair(access, after, debtor_ordinal, FixStatus::invalid_pair, reconciliation_failure, pending_before_.flags,
                    pending_before_.creditor_count, pending_before_.creditor_destinations);
                return;
            }

            std::array<InterestTransfer, max_sample_creditor_destinations> transfers{};
            uint32_t transfer_count = 0;
            for (uint32_t index = 0; index < after.creditor_destinations; ++index) {
                if (transfer_summary.transfers_raw[index] <= 0) continue;
                InterestTransfer &transfer = transfers[transfer_count++];
                transfer.recipient_ordinal = after.destination_ordinals[index];
                std::memcpy(transfer.recipient_tag, &after.destination_keys[index], 4);
                transfer.transfer_raw = transfer_summary.transfers_raw[index];
            }
            const BatchAddStatus add = batch_.AddDebtor(
                debtor_ordinal, transfers.data(), transfer_count);
            if (add != BatchAddStatus::success) {
                RejectPair(access, after, debtor_ordinal, FixStatus::batch_invalid);
                return;
            }
            if (batch_.complete()) FinalizeDay(access);
        }

        void RejectPair(DailyInterestAccess &access, const CountryEconomySnapshot &sample, int32_t debtor_ordinal, FixStatus status,
                        ReconciliationFailure reconciliation_failure = ReconciliationFailure::none,
                        uint32_t before_flags = 0, uint32_t creditor_count = 0, uint32_t destination_count = 0)
        {
            ResetPendingInterest();
            batch_.RejectDebtor(debtor_ordinal);
            FixResult result{};
            result.date_raw = sample.date_raw;
            std::memcpy(result.country_tag, sample.country_tag, 4);
            result.status = status;
            result.flags = before_flags | sample.flags | INTEREST_RECONCILIATION_INVALID;
            result.reconciliation_failure = reconciliation_failure;
            result.reconciliation_creditor_count = creditor_count;
            result.reconciliation_destination_count = destination_count;
            Publish(result);
            if (batch_.complete()) {
                FinalizeDay(access);
            }
        }

        void FinalizeDay(DailyInterestAccess &access)
        {
            daily_paid_pops_.Reset();
            uint32_t recipient_failures = 0;
            uint64_t callback_us_total = 0;
            const int32_t date_raw = batch_.date_raw();
            const uint32_t rejected_debtors = batch_.rejected_debtors();
            summary_source_count_ = 0;
            summary_province_count_ = 0;
            summary_pop_count_ = 0;
            summary_paid_pop_count_ = 0;
            summary_verified_pop_count_ = 0;
            summary_transfer_raw_ = 0;
            summary_domestic_transfer_raw_ = 0;
            summary_foreign_transfer_raw_ = 0;
            summary_payout_raw_ = 0;
            for (uint32_t ordinal = 1; ordinal < max_batch_countries; ++ordinal) {
                const DailyRecipient &recipient = batch_.recipient(ordinal);
                if (!recipient.active) continue;
                if (recipient.source_count > (std::numeric_limits<uint32_t>::max)() - summary_source_count_
                    || !CanAdd(summary_transfer_raw_, recipient.transfer_raw)
                    || !CanAdd(summary_domestic_transfer_raw_, recipient.domestic_transfer_raw)
                    || !CanAdd(summary_foreign_transfer_raw_, recipient.foreign_transfer_raw)) {
                    FixResult overflow{};
                    overflow.date_raw = date_raw;
                    std::memcpy(overflow.country_tag, "---", 4);
                    overflow.status = FixStatus::conservation_failed;
                    overflow.flags = SAMPLE_SUM_OVERFLOW;
                    overflow.rejected_debtors = batch_.rejected_debtors();
                    callback_started_ = std::chrono::steady_clock::now();
                    Publish(overflow);
                    finalized_date_raw_ = date_raw;
                    batch_.Reset();
                    return;
                }
                summary_source_count_ += recipient.source_count;
                summary_transfer_raw_ += recipient.transfer_raw;
                summary_domestic_transfer_raw_ += recipient.domestic_transfer_raw;
                summary_foreign_transfer_raw_ += recipient.foreign_transfer_raw;
            }
            if (summary_transfer_raw_ > (std::numeric_limits<int64_t>::max)() / 1000) {
                FixResult overflow{};
                overflow.date_raw = date_raw;
                std::memcpy(overflow.country_tag, "---", 4);
                overflow.status = FixStatus::conservation_failed;
                overflow.flags = SAMPLE_SUM_OVERFLOW;
                overflow.rejected_debtors = batch_.rejected_debtors();
                callback_started_ = std::chrono::steady_clock::now();
                Publish(overflow);
                finalized_date_raw_ = date_raw;
                batch_.Reset();
                return;
            }
            for (uint32_t ordinal = 1; ordinal < max_batch_countries; ++ordinal) {
                const DailyRecipient &recipient = batch_.recipient(ordinal);
                if (!recipient.active) continue;
                callback_started_ = std::chrono::steady_clock::now();
                FixResult result{};
                result.date_raw = date_raw;
                std::memcpy(result.country_tag, recipient.tag, 4);
                result.source_count = recipient.source_count;
                result.transfer_raw = recipient.transfer_raw;
                result.domestic_transfer_raw = recipient.domestic_transfer_raw;
                result.foreign_transfer_raw = recipient.foreign_transfer_raw;
                PayRecipient(access, recipient, &result);
                Publish(result, result.status != FixStatus::paid);
                callback_us_total += result.callback_us;
                if (result.status == FixStatus::paid) {
                    summary_province_count_ += result.province_count;
                    summary_pop_count_ += result.pop_count;
                    summary_paid_pop_count_ += result.paid_pop_count;
                    summary_verified_pop_count_ += result.verified_pop_count;
                    summary_payout_raw_ += result.payout_raw;
                } else {
                    ++recipient_failures;
                }
                if (disabled_) break;
            }

            FixResult summary{};
            summary.date_raw = date_raw;
            std::memcpy(summary.country_tag, "---", 4);
            summary.status = rejected_debtors == 0 && recipient_failures == 0
                ? FixStatus::day_summary : FixStatus::day_partial;
            summary.flags = day_flags_;
            summary.source_count = summary_source_count_;
            summary.province_count = summary_province_count_;
            summary.pop_count = summary_pop_count_;
            summary.paid_pop_count = summary_paid_pop_count_;
            summary.verified_pop_count = summary_verified_pop_count_;
            summary.rejected_debtors = rejected_debtors + recipient_failures;
            summary.transfer_raw = summary_transfer_raw_;
            summary.domestic_transfer_raw = summary_domestic_transfer_raw_;
            summary.foreign_transfer_raw = summary_foreign_transfer_raw_;
            summary.payout_raw = summary_payout_raw_;
            summary.callback_us = callback_us_total;
            summary.daily_max_creditor_count = daily_max_creditor_count_;
            summary.daily_max_destination_count = daily_max_destination_count_;
            Publish(summary, true, true);
            finalized_date_raw_ = date_raw;
            batch_.Reset();
            summary_source_count_ = 0;
            summary_province_count_ = 0;
            summary_pop_count_ = 0;
            summary_paid_pop_count_ = 0;
            summary_verified_pop_count_ = 0;
            summary_transfer_raw_ = 0;
            summary_domestic_transfer_raw_ = 0;
            summary_foreign_transfer_raw_ = 0;
            summary_payout_raw_ = 0;
            day_flags_ = 0;
        }

        void PayRecipient(DailyInterestAccess &access,
                           const DailyRecipient &recipient, FixResult *result)
        {
            const GameStateRef game_state = access.game_state();
            const CountryRef country = smedley::game_state::ResolveCountry(game_state, recipient.ordinal);
            uint32_t collected = 0;
            CountryEconomySnapshot quality{};
            if (!CollectCountryPops(country, result->date_raw, ResolveProvince, &game_state,
                    candidates_.data(), candidates_.size(), max_sample_destination_provinces,
                    &collected, &quality)) {
                result->status = FixStatus::collection_failed;
                result->flags = quality.flags;
                result->province_count = quality.destination_province_attempts;
                result->pop_count = quality.destination_pop_attempts;
                return;
            }
            if (quality.country_ordinal != recipient.ordinal
                || std::memcmp(quality.country_tag, recipient.tag, 4) != 0) {
                result->status = FixStatus::recipient_identity_invalid;
                return;
            }
            result->province_count = quality.destination_province_attempts;
            result->pop_count = collected;
            candidate_count_ = collected;
            for (uint32_t index = 0; index < collected; ++index) {
                allocations_[index].savings_raw = candidates_[index].savings_raw;
            }
            result->allocation_status = AllocateInterest(result->transfer_raw,
                allocations_.data(), collected, order_scratch_.data(), order_scratch_.size());
            if (result->allocation_status != AllocationStatus::success) {
                result->status = AllocationFixStatus(result->allocation_status);
                return;
            }
            if (result->transfer_raw > (std::numeric_limits<int64_t>::max)() / 1000) {
                result->status = FixStatus::conservation_failed;
                return;
            }

            int64_t payout_total = 0;
            uint32_t eligible_pop_count = 0;
            for (uint32_t index = 0; index < collected; ++index) {
                const int64_t payout = allocations_[index].payout_raw;
                if (payout == 0) continue;
                if (daily_paid_pops_.Contains(candidates_[index].address.address())) {
                    result->status = FixStatus::duplicate_pop;
                    return;
                }
                const PopInterestMutationStatus preflight_status = PreparePopInterest(
                    access, candidates_[index].address, payout, &preflights_[index]);
                if (preflight_status != PopInterestMutationStatus::success) {
                    result->status = PreflightFixStatus(preflight_status);
                    if (IsUnsafePopInterestFailure(preflight_status)) disabled_ = true;
                    return;
                }
                if (!CanAdd(payout_total, payout)) {
                    result->status = FixStatus::conservation_failed;
                    return;
                }
                payout_total += payout;
                ++eligible_pop_count;
            }
            if (payout_total != result->transfer_raw * 1000) {
                result->status = FixStatus::conservation_failed;
                return;
            }
            result->payout_raw = payout_total;
            if (eligible_pop_count > daily_pop_set_capacity - daily_paid_pops_.size()) {
                result->status = FixStatus::pop_identity_limit;
                return;
            }
            for (uint32_t index = 0; index < collected; ++index) {
                if (allocations_[index].payout_raw == 0) continue;
                if (daily_paid_pops_.Insert(candidates_[index].address.address())
                    != PointerInsertStatus::inserted) {
                    result->status = FixStatus::duplicate_pop;
                    return;
                }
            }

            for (uint32_t index = 0; index < collected; ++index) {
                const int64_t payout = allocations_[index].payout_raw;
                if (payout == 0) continue;
                const PopInterestMutationStatus apply_status = ApplyPopInterest(
                    access, candidates_[index].address, payout, preflights_[index]);
                if (apply_status != PopInterestMutationStatus::success) {
                    const PopInterestFailureClass failure = ClassifyAppliedPopInterestFailure(
                        apply_status, result->paid_pop_count != 0);
                    switch (failure) {
                    case PopInterestFailureClass::balance: result->status = FixStatus::pop_balance_overflow; break;
                    case PopInterestFailureClass::not_writable: result->status = FixStatus::pop_not_writable; break;
                    case PopInterestFailureClass::unavailable: result->status = FixStatus::mutation_unavailable; break;
                    case PopInterestFailureClass::precondition_changed: result->status = FixStatus::mutation_precondition_changed; break;
                    case PopInterestFailureClass::postcondition_failed: result->status = FixStatus::postcondition_failed; break;
                    case PopInterestFailureClass::partial_mutation: result->status = FixStatus::partial_mutation; break;
                    }
                    if (IsUnsafeAppliedPopInterestFailure(apply_status, result->paid_pop_count != 0)) {
                        disabled_ = true;
                    }
                    return;
                }
                ++result->paid_pop_count;
                ++result->verified_pop_count;
            }
            result->status = FixStatus::paid;
        }

        static CountryRef ResolveCountry(const void *context, int32_t ordinal)
        {
            if (context == nullptr) return {};
            return smedley::game_state::ResolveCountry(*static_cast<const GameStateRef *>(context), ordinal);
        }

        static ProvinceRef ResolveProvince(const void *context, int32_t id)
        {
            if (context == nullptr) return {};
            return smedley::game_state::ResolveProvince(*static_cast<const GameStateRef *>(context), id);
        }

        void ResetPendingInterest() noexcept
        {
            has_pending_before_ = false;
        }

        void Publish(FixResult &result, bool emit_telemetry = true, bool preserve_callback_us = false)
        {
            if (!preserve_callback_us) {
                result.callback_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - callback_started_).count());
            }
            if (emit_telemetry) {
                const auto country = TelemetryStringField("country_tag", result.country_tag);
                const SmedleyTelemetryFieldV1 health[] = {
                    TelemetryStringField("status", StatusName(result.status)),
                    TelemetryIntField("flags", result.flags),
                    TelemetryIntField("source_count", result.source_count),
                    TelemetryIntField("province_count", result.province_count),
                    TelemetryIntField("pop_count", result.pop_count),
                    TelemetryIntField("verified_pop_count", result.verified_pop_count),
                    TelemetryIntField("callback_us", static_cast<int64_t>(result.callback_us)),
                };
                static_assert(1 + std::size(health) <= SMEDLEY_TELEMETRY_MAX_FIELDS);
                result.health_telemetry_result = telemetry_.Emit(
                    "interest.fix.health", "verified-runtime", result.date_raw, &country, 1, health, 7, true);
                const SmedleyTelemetryFieldV1 value[] = {
                    TelemetryIntField("transfer_raw", result.transfer_raw),
                    TelemetryIntField("payout_raw", result.payout_raw),
                    TelemetryIntField("domestic_transfer_raw", result.domestic_transfer_raw),
                    TelemetryIntField("foreign_transfer_raw", result.foreign_transfer_raw),
                };
                static_assert(1 + std::size(value) <= SMEDLEY_TELEMETRY_MAX_FIELDS);
                if (result.status == FixStatus::paid
                    || (result.status == FixStatus::day_summary && result.rejected_debtors == 0
                        && result.transfer_raw <= (std::numeric_limits<int64_t>::max)() / 1000
                        && result.payout_raw == result.transfer_raw * 1000)) {
                    result.value_telemetry_result = telemetry_.Emit(
                        "interest.fix.value", "verified-runtime", result.date_raw, &country, 1, value, 4, true);
                }
            }
            Enqueue(result);
        }

        void Enqueue(const FixResult &result) noexcept
        {
            if (!queue_.TryPush(result)) dropped_.fetch_add(1, std::memory_order_relaxed);
        }

        void WriteResults()
        {
            while (!stop_.load(std::memory_order_acquire) || !queue_.Empty()) {
                bool wrote = false;
                FixResult result{};
                while (queue_.TryPop(&result)) {
                    output_ << result.date_raw << ',' << result.country_tag << ',' << StatusName(result.status)
                            << ",0x" << std::hex << result.flags << std::dec << ',' << result.source_count << ','
                             << result.pop_count << ',' << result.paid_pop_count << ',' << result.province_count << ','
                             << result.verified_pop_count << ',' << result.transfer_raw << ','
                             << result.domestic_transfer_raw << ',' << result.foreign_transfer_raw << ','
                             << result.payout_raw << ','
                            << AllocationStatusName(result.allocation_status) << ',' << result.callback_us << ','
                            << result.rejected_debtors << ',' << result.health_telemetry_result << ','
                            << result.value_telemetry_result << ',' << dropped_.load(std::memory_order_relaxed) << ','
                             << ReconciliationFailureName(result.reconciliation_failure) << ','
                             << result.reconciliation_creditor_count << ','
                             << result.reconciliation_destination_count << ',' << result.daily_max_creditor_count << ','
                              << result.daily_max_destination_count << '\n';
                    wrote = true;
                }
                if (wrote) {
                    output_.flush();
                }
                else std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        std::ofstream output_;
        ResultQueue<result_queue_capacity> queue_{};
        DailyInterestBatch batch_{};
        CountryEconomySnapshot pending_before_{};
        bool has_pending_before_ = false;
        bool disabled_ = false;
        int32_t finalized_date_raw_ = 0;
        uint32_t candidate_count_ = 0;
        uint32_t summary_source_count_ = 0;
        uint32_t summary_province_count_ = 0;
        uint32_t summary_pop_count_ = 0;
        uint32_t summary_paid_pop_count_ = 0;
        uint32_t summary_verified_pop_count_ = 0;
        int64_t summary_transfer_raw_ = 0;
        int64_t summary_domestic_transfer_raw_ = 0;
        int64_t summary_foreign_transfer_raw_ = 0;
        int64_t summary_payout_raw_ = 0;
        uint32_t day_flags_ = 0;
        uint32_t daily_max_creditor_count_ = 0;
        uint32_t daily_max_destination_count_ = 0;
        DailyPopSet daily_paid_pops_{};
        std::array<PopCandidate, max_sample_pops> candidates_{};
        std::array<AllocationEntry, max_sample_pops> allocations_{};
        std::array<PopInterestPreflight, max_sample_pops> preflights_{};
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
    return new interest_bug_fix::InterestFix();
}
