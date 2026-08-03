#pragma once

#include <cstddef>
#include <cstdint>

namespace interest_bug_fix
{
    enum class AllocationStatus
    {
        success,
        no_payment,
        no_eligible_savings,
        invalid_input,
        overflow,
        scratch_too_small,
    };

    struct AllocationEntry
    {
        int64_t savings_raw = 0;
        int64_t payout_raw = 0;
        uint64_t remainder = 0;
    };

    AllocationStatus AllocateInterest(int64_t transfer_raw, AllocationEntry *entries,
                                      size_t entry_count, uint32_t *order_scratch,
                                      size_t scratch_count);
}
