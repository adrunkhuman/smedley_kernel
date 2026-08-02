#include "interest_allocation.hpp"

#include <algorithm>
#include <limits>

namespace interest_bug_fix
{
    namespace
    {
        constexpr int64_t pop_money_scale = 1000;

        bool MultiplyDivide(uint64_t multiplicand, uint64_t multiplier, uint64_t divisor,
                            uint64_t *quotient, uint64_t *remainder)
        {
            if (divisor == 0 || multiplier > divisor || quotient == nullptr || remainder == nullptr) return false;
            const uint64_t add_quotient = multiplicand / divisor;
            const uint64_t add_remainder = multiplicand % divisor;
            uint64_t result_quotient = 0;
            uint64_t result_remainder = 0;
            int bit = 63;
            while (bit > 0 && ((multiplier >> bit) & 1u) == 0) --bit;
            for (; bit >= 0; --bit) {
                if (result_quotient > (std::numeric_limits<uint64_t>::max)() / 2) return false;
                result_quotient *= 2;
                uint64_t carry = 0;
                if (result_remainder >= divisor - result_remainder) {
                    result_remainder -= divisor - result_remainder;
                    carry = 1;
                } else {
                    result_remainder *= 2;
                }
                if (((multiplier >> bit) & 1u) != 0) {
                    if (result_quotient > (std::numeric_limits<uint64_t>::max)() - add_quotient) return false;
                    result_quotient += add_quotient;
                    if (add_remainder != 0) {
                        if (result_remainder >= divisor - add_remainder) {
                            result_remainder -= divisor - add_remainder;
                            ++carry;
                        } else {
                            result_remainder += add_remainder;
                        }
                    }
                }
                if (result_quotient > (std::numeric_limits<uint64_t>::max)() - carry) return false;
                result_quotient += carry;
            }
            *quotient = result_quotient;
            *remainder = result_remainder;
            return true;
        }
    }

    AllocationStatus AllocateInterest(int64_t transfer_raw, AllocationEntry *entries,
                                      size_t entry_count, uint32_t *order_scratch,
                                      size_t scratch_count)
    {
        if (entries == nullptr && entry_count != 0) return AllocationStatus::invalid_input;
        for (size_t index = 0; index < entry_count; ++index) {
            entries[index].payout_raw = 0;
            entries[index].remainder = 0;
        }
        if (transfer_raw <= 0) return AllocationStatus::no_payment;
        if (entry_count == 0) return AllocationStatus::no_eligible_savings;
        if (transfer_raw > (std::numeric_limits<int64_t>::max)() / pop_money_scale) {
            return AllocationStatus::overflow;
        }
        if (entry_count > (std::numeric_limits<uint32_t>::max)()) return AllocationStatus::overflow;
        const int64_t payout_total = transfer_raw * pop_money_scale;

        int64_t savings_total = 0;
        size_t eligible_count = 0;
        for (size_t index = 0; index < entry_count; ++index) {
            const int64_t savings = entries[index].savings_raw;
            if (savings <= 0) continue;
            if (savings_total > (std::numeric_limits<int64_t>::max)() - savings) {
                return AllocationStatus::overflow;
            }
            savings_total += savings;
            ++eligible_count;
        }
        if (eligible_count == 0) return AllocationStatus::no_eligible_savings;
        if (order_scratch == nullptr || scratch_count < eligible_count) return AllocationStatus::scratch_too_small;

        int64_t allocated = 0;
        size_t order_count = 0;
        for (size_t index = 0; index < entry_count; ++index) {
            const int64_t savings = entries[index].savings_raw;
            if (savings <= 0) continue;
            uint64_t quotient = 0;
            uint64_t remainder = 0;
            if (payout_total <= (std::numeric_limits<int64_t>::max)() / savings) {
                const int64_t product = payout_total * savings;
                quotient = static_cast<uint64_t>(product / savings_total);
                remainder = static_cast<uint64_t>(product % savings_total);
            } else {
                if (!MultiplyDivide(static_cast<uint64_t>(payout_total), static_cast<uint64_t>(savings),
                        static_cast<uint64_t>(savings_total), &quotient, &remainder)
                    || quotient > static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())) {
                    return AllocationStatus::overflow;
                }
            }
            entries[index].payout_raw = static_cast<int64_t>(quotient);
            entries[index].remainder = remainder;
            allocated += entries[index].payout_raw;
            order_scratch[order_count++] = static_cast<uint32_t>(index);
        }

        const int64_t remainder_units = payout_total - allocated;
        if (remainder_units < 0 || static_cast<uint64_t>(remainder_units) > order_count) {
            for (size_t index = 0; index < entry_count; ++index) entries[index].payout_raw = 0;
            return AllocationStatus::overflow;
        }
        const auto remainder_order = [entries](uint32_t left, uint32_t right) {
            if (entries[left].remainder != entries[right].remainder) {
                return entries[left].remainder > entries[right].remainder;
            }
            return left < right;
        };
        if (remainder_units != 0 && static_cast<size_t>(remainder_units) != order_count) {
            std::nth_element(order_scratch, order_scratch + remainder_units,
                order_scratch + order_count, remainder_order);
        }
        for (int64_t index = 0; index < remainder_units; ++index) {
            ++entries[order_scratch[index]].payout_raw;
        }
        return AllocationStatus::success;
    }
}
