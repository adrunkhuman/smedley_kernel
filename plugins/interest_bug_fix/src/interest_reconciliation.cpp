#include "interest_reconciliation.hpp"

#include <limits>

namespace interest_bug_fix
{
    namespace
    {
        bool Fail(ReconciliationFailure value, ReconciliationFailure *failure)
        {
            if (failure != nullptr) *failure = value;
            return false;
        }
    }

    bool ComputeDestinationTransfers(const DestinationInterestSnapshot &before,
                                       const DestinationInterestSnapshot &after,
                                      DestinationTransferSummary *summary, ReconciliationFailure *failure)
    {
        if (failure != nullptr) *failure = ReconciliationFailure::none;
        if (summary == nullptr) return Fail(ReconciliationFailure::output_invalid, failure);
        *summary = {};
        if ((before.flags & INTEREST_RECONCILIATION_INVALID) != 0
            || (after.flags & INTEREST_RECONCILIATION_INVALID) != 0) {
            return Fail(ReconciliationFailure::destination_limit, failure);
        }
        if (before.flags != 0) return Fail(ReconciliationFailure::before_flags, failure);
        if (after.flags != 0) return Fail(ReconciliationFailure::after_flags, failure);
        if (before.creditor_destinations != after.creditor_destinations) {
            return Fail(ReconciliationFailure::destination_count_changed, failure);
        }
        if (before.creditor_destinations > max_reconciliation_destinations) {
            return Fail(ReconciliationFailure::destination_limit, failure);
        }
        bool overflow = false;
        for (uint32_t index = 0; index < before.creditor_destinations; ++index) {
            if (before.destination_ordinals[index] != after.destination_ordinals[index]
                || before.destination_keys[index] != after.destination_keys[index]) {
                return Fail(ReconciliationFailure::destination_identity_changed, failure);
            }
            const int64_t before_value = before.destination_bank_interests_raw[index];
            const int64_t after_value = after.destination_bank_interests_raw[index];
            if (before_value < 0 || after_value < before_value) {
                return Fail(ReconciliationFailure::destination_balance_invalid, failure);
            }
            const int64_t transfer = after_value - before_value;
            summary->transfers_raw[index] = transfer;
            if (transfer == 0) continue;
            if ((transfer > 0 && summary->transfer_raw > (std::numeric_limits<int64_t>::max)() - transfer)
                || (transfer < 0 && summary->transfer_raw < (std::numeric_limits<int64_t>::min)() - transfer)) {
                overflow = true;
            } else {
                summary->transfer_raw += transfer;
            }
            ++summary->transfer_count;
        }
        if (before.destination_bank_interest_raw < 0
            || after.destination_bank_interest_raw < before.destination_bank_interest_raw) {
            return Fail(ReconciliationFailure::aggregate_balance_invalid, failure);
        }
        const int64_t aggregate_delta = after.destination_bank_interest_raw - before.destination_bank_interest_raw;
        if (overflow) return Fail(ReconciliationFailure::transfer_overflow, failure);
        if (aggregate_delta != summary->transfer_raw) return Fail(ReconciliationFailure::aggregate_delta_mismatch, failure);
        return true;
    }

    const char *ReconciliationFailureName(ReconciliationFailure failure)
    {
        switch (failure) {
        case ReconciliationFailure::none: return "none";
        case ReconciliationFailure::output_invalid: return "output_invalid";
        case ReconciliationFailure::before_flags: return "before_flags";
        case ReconciliationFailure::after_flags: return "after_flags";
        case ReconciliationFailure::destination_count_changed: return "destination_count_changed";
        case ReconciliationFailure::destination_limit: return "destination_limit";
        case ReconciliationFailure::destination_identity_changed: return "destination_identity_changed";
        case ReconciliationFailure::destination_balance_invalid: return "destination_balance_invalid";
        case ReconciliationFailure::transfer_overflow: return "transfer_overflow";
        case ReconciliationFailure::aggregate_balance_invalid: return "aggregate_balance_invalid";
        case ReconciliationFailure::aggregate_delta_mismatch: return "aggregate_delta_mismatch";
        case ReconciliationFailure::date_changed: return "date_changed";
        case ReconciliationFailure::country_changed: return "country_changed";
        }
        return "unknown";
    }

}
