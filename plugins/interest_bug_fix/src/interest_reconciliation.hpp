#pragma once

#include <array>
#include <cstdint>

namespace interest_bug_fix
{
    constexpr uint32_t INTEREST_RECONCILIATION_INVALID = 1u << 24;
    constexpr uint32_t max_reconciliation_destinations = 512;

    struct DestinationInterestSnapshot
    {
        uint32_t flags = 0;
        uint32_t creditor_destinations = 0;
        int64_t destination_bank_interest_raw = 0;
        std::array<uint32_t, max_reconciliation_destinations> destination_keys{};
        std::array<int32_t, max_reconciliation_destinations> destination_ordinals{};
        std::array<int64_t, max_reconciliation_destinations> destination_bank_interests_raw{};
    };

    enum class ReconciliationFailure : uint8_t
    {
        none,
        output_invalid,
        before_flags,
        after_flags,
        destination_count_changed,
        destination_limit,
        destination_identity_changed,
        destination_balance_invalid,
        transfer_overflow,
        aggregate_balance_invalid,
        aggregate_delta_mismatch,
        date_changed,
        country_changed,
    };

    struct DestinationTransferSummary
    {
        std::array<int64_t, max_reconciliation_destinations> transfers_raw{};
        uint32_t transfer_count = 0;
        int64_t transfer_raw = 0;
    };

    bool ComputeDestinationTransfers(const DestinationInterestSnapshot &before,
                                       const DestinationInterestSnapshot &after,
                                       DestinationTransferSummary *summary,
                                       ReconciliationFailure *failure = nullptr);
    const char *ReconciliationFailureName(ReconciliationFailure failure);
}
