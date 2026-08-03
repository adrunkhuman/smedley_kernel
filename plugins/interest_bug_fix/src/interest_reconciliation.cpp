#include "interest_reconciliation.hpp"

#include <limits>

namespace interest_bug_fix
{
    bool ComputeDestinationTransfers(const smedley::game_state::CountryEconomySnapshot &before,
                                     const smedley::game_state::CountryEconomySnapshot &after,
                                     DestinationTransferSummary *summary)
    {
        if (summary == nullptr) return false;
        *summary = {};
        if (before.flags != 0 || after.flags != 0
            || before.creditor_destinations != after.creditor_destinations
            || before.creditor_destinations > smedley::game_state::max_sample_creditor_destinations) {
            return false;
        }
        bool overflow = false;
        for (uint32_t index = 0; index < before.creditor_destinations; ++index) {
            if (before.destination_ordinals[index] != after.destination_ordinals[index]
                || before.destination_keys[index] != after.destination_keys[index]) {
                return false;
            }
            const int64_t before_value = before.destination_bank_interests_raw[index];
            const int64_t after_value = after.destination_bank_interests_raw[index];
            if (before_value < 0 || after_value < before_value) {
                return false;
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
            return false;
        }
        const int64_t aggregate_delta = after.destination_bank_interest_raw - before.destination_bank_interest_raw;
        if (overflow || aggregate_delta != summary->transfer_raw) {
            return false;
        }
        return true;
    }

    bool TreasuryLossCoversTransfer(int64_t before_treasury, int64_t after_treasury, int64_t transfer)
    {
        return transfer >= 0
            && before_treasury >= (std::numeric_limits<int64_t>::min)() + transfer
            && after_treasury <= before_treasury - transfer;
    }

    bool ComputeTreasuryResidual(int64_t before_treasury, int64_t after_treasury,
                                 int64_t transfer, int64_t *residual)
    {
        if (residual == nullptr || !TreasuryLossCoversTransfer(before_treasury, after_treasury, transfer)) {
            return false;
        }
        const int64_t after_transfer = before_treasury - transfer;
        if (after_treasury < 0
            && after_transfer > (std::numeric_limits<int64_t>::max)() + after_treasury) {
            return false;
        }
        *residual = after_transfer - after_treasury;
        return true;
    }
}
