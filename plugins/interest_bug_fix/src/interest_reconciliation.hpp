#pragma once

#include <smedley/game_state/readers.hpp>

#include <array>
#include <cstdint>

namespace interest_bug_fix
{
    constexpr uint32_t INTEREST_RECONCILIATION_INVALID = 1u << 24;

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
        std::array<int64_t, smedley::game_state::max_sample_creditor_destinations> transfers_raw{};
        uint32_t transfer_count = 0;
        int64_t transfer_raw = 0;
    };

    bool ComputeDestinationTransfers(const smedley::game_state::CountryEconomySnapshot &before,
                                       const smedley::game_state::CountryEconomySnapshot &after,
                                       DestinationTransferSummary *summary,
                                       ReconciliationFailure *failure = nullptr);
    const char *ReconciliationFailureName(ReconciliationFailure failure);
}
