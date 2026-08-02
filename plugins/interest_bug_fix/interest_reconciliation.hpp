#pragma once

#include "../game_state/readers.hpp"

#include <array>
#include <cstdint>

namespace interest_bug_fix
{
    constexpr uint32_t INTEREST_RECONCILIATION_INVALID = 1u << 24;

    struct DestinationTransferSummary
    {
        std::array<int64_t, smedley::game_state::max_sample_creditor_destinations> transfers_raw{};
        uint32_t transfer_count = 0;
        int64_t transfer_raw = 0;
    };

    bool ComputeDestinationTransfers(const smedley::game_state::CountryEconomySnapshot &before,
                                     const smedley::game_state::CountryEconomySnapshot &after,
                                     DestinationTransferSummary *summary);
    bool TreasuryLossCoversTransfer(int64_t before_treasury, int64_t after_treasury, int64_t transfer);
    bool ComputeTreasuryResidual(int64_t before_treasury, int64_t after_treasury,
                                 int64_t transfer, int64_t *residual);
}
