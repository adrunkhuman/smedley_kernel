#include "interest_batch.hpp"

#include <cstring>
#include <limits>

namespace interest_bug_fix
{
    namespace
    {
        bool CanAdd(int64_t value, int64_t amount)
        {
            return amount >= 0 && value <= (std::numeric_limits<int64_t>::max)() - amount;
        }
    }

    PointerInsertStatus DailyPopSet::Insert(uintptr_t address)
    {
        if (address == 0) return PointerInsertStatus::duplicate;
        constexpr size_t mask = daily_pop_set_capacity - 1;
        size_t slot = static_cast<size_t>((address >> 4) * uintptr_t{2654435761u}) & mask;
        for (size_t attempt = 0; attempt < daily_pop_set_capacity; ++attempt) {
            if (generations_[slot] == generation_ && entries_[slot] == address) {
                return PointerInsertStatus::duplicate;
            }
            if (generations_[slot] != generation_) {
                entries_[slot] = address;
                generations_[slot] = generation_;
                ++size_;
                return PointerInsertStatus::inserted;
            }
            slot = (slot + 1) & mask;
        }
        return PointerInsertStatus::full;
    }

    bool DailyPopSet::Contains(uintptr_t address) const
    {
        if (address == 0) return false;
        constexpr size_t mask = daily_pop_set_capacity - 1;
        size_t slot = static_cast<size_t>((address >> 4) * uintptr_t{2654435761u}) & mask;
        for (size_t attempt = 0; attempt < daily_pop_set_capacity; ++attempt) {
            if (generations_[slot] != generation_) return false;
            if (entries_[slot] == address) return true;
            slot = (slot + 1) & mask;
        }
        return false;
    }

    void DailyPopSet::Reset()
    {
        ++generation_;
        if (generation_ == 0) {
            generations_.fill(0);
            generation_ = 1;
        }
        size_ = 0;
    }

    bool DailyInterestBatch::Begin(int32_t date_raw, uint32_t country_count)
    {
        Reset();
        if (country_count < 2 || country_count > max_batch_countries) return false;
        date_raw_ = date_raw;
        country_count_ = country_count;
        expected_debtors_ = country_count - 1;
        started_ = true;
        return true;
    }

    BatchAddStatus DailyInterestBatch::AddDebtor(int32_t debtor_ordinal,
                                                   const InterestTransfer *transfers,
                                                   uint32_t transfer_count)
    {
        if (!started_) return BatchAddStatus::not_started;
        if (debtor_ordinal <= 0 || static_cast<uint32_t>(debtor_ordinal) >= country_count_) {
            return BatchAddStatus::invalid_debtor;
        }
        if (seen_debtors_[debtor_ordinal]) return BatchAddStatus::duplicate_debtor;
        if (transfer_count != 0 && transfers == nullptr) {
            return BatchAddStatus::invalid_amount;
        }

        std::array<bool, max_batch_countries> touched{};
        for (uint32_t index = 0; index < transfer_count; ++index) {
            const InterestTransfer &transfer = transfers[index];
            if (transfer.recipient_ordinal <= 0
                || static_cast<uint32_t>(transfer.recipient_ordinal) >= country_count_) {
                return BatchAddStatus::invalid_recipient;
            }
            if (transfer.transfer_raw <= 0) return BatchAddStatus::invalid_amount;
            if (touched[transfer.recipient_ordinal]) return BatchAddStatus::duplicate_recipient;
            touched[transfer.recipient_ordinal] = true;

            const DailyRecipient &recipient = recipients_[transfer.recipient_ordinal];
            if (recipient.active && std::memcmp(recipient.tag, transfer.recipient_tag, 4) != 0) {
                return BatchAddStatus::identity_mismatch;
            }
            if (!CanAdd(recipient.transfer_raw, transfer.transfer_raw)
                || (transfer.recipient_ordinal == debtor_ordinal
                    && !CanAdd(recipient.domestic_transfer_raw, transfer.transfer_raw))
                || (transfer.recipient_ordinal != debtor_ordinal
                    && !CanAdd(recipient.foreign_transfer_raw, transfer.transfer_raw))) {
                return BatchAddStatus::overflow;
            }
        }
        for (uint32_t index = 0; index < transfer_count; ++index) {
            const InterestTransfer &transfer = transfers[index];
            DailyRecipient &recipient = recipients_[transfer.recipient_ordinal];
            if (!recipient.active) {
                recipient.active = true;
                recipient.ordinal = transfer.recipient_ordinal;
                std::memcpy(recipient.tag, transfer.recipient_tag, 4);
            }
            recipient.transfer_raw += transfer.transfer_raw;
            if (transfer.recipient_ordinal == debtor_ordinal) {
                recipient.domestic_transfer_raw += transfer.transfer_raw;
            } else {
                recipient.foreign_transfer_raw += transfer.transfer_raw;
            }
            ++recipient.source_count;
        }
        seen_debtors_[debtor_ordinal] = true;
        ++seen_count_;
        return BatchAddStatus::success;
    }

    bool DailyInterestBatch::RejectDebtor(int32_t debtor_ordinal)
    {
        if (!started_ || debtor_ordinal <= 0
            || static_cast<uint32_t>(debtor_ordinal) >= country_count_
            || seen_debtors_[debtor_ordinal]) {
            return false;
        }
        seen_debtors_[debtor_ordinal] = true;
        ++seen_count_;
        ++rejected_debtors_;
        return true;
    }

    void DailyInterestBatch::Reset()
    {
        date_raw_ = 0;
        country_count_ = 0;
        expected_debtors_ = 0;
        seen_count_ = 0;
        rejected_debtors_ = 0;
        started_ = false;
        seen_debtors_.fill(false);
        recipients_.fill({});
    }
}
