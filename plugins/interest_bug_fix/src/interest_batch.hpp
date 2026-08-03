#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace interest_bug_fix
{
    constexpr uint32_t max_batch_countries = 512;
    constexpr size_t daily_pop_set_capacity = 262144;

    enum class PointerInsertStatus
    {
        inserted,
        duplicate,
        full,
    };

    class DailyPopSet
    {
    public:
        PointerInsertStatus Insert(uintptr_t address);
        bool Contains(uintptr_t address) const;
        void Reset();
        size_t size() const { return size_; }

    private:
        std::array<uintptr_t, daily_pop_set_capacity> entries_{};
        size_t size_ = 0;
    };

    struct InterestTransfer
    {
        int32_t recipient_ordinal = 0;
        char recipient_tag[4]{};
        int64_t transfer_raw = 0;
    };

    struct DailyRecipient
    {
        int32_t ordinal = 0;
        char tag[4]{};
        int64_t transfer_raw = 0;
        int64_t domestic_transfer_raw = 0;
        int64_t foreign_transfer_raw = 0;
        uint32_t source_count = 0;
        bool active = false;
    };

    enum class BatchAddStatus
    {
        success,
        not_started,
        invalid_debtor,
        duplicate_debtor,
        invalid_recipient,
        duplicate_recipient,
        identity_mismatch,
        invalid_amount,
        overflow,
    };

    class DailyInterestBatch
    {
    public:
        bool Begin(int32_t date_raw, uint32_t country_count);
        BatchAddStatus AddDebtor(int32_t debtor_ordinal, const InterestTransfer *transfers,
                                 uint32_t transfer_count, int64_t private_sink_raw);
        bool RejectDebtor(int32_t debtor_ordinal);
        void Reset();

        bool started() const { return started_; }
        bool complete() const { return started_ && seen_count_ == expected_debtors_; }
        int32_t date_raw() const { return date_raw_; }
        uint32_t expected_debtors() const { return expected_debtors_; }
        uint32_t seen_count() const { return seen_count_; }
        uint32_t rejected_debtors() const { return rejected_debtors_; }
        int64_t private_sink_raw() const { return private_sink_raw_; }
        const DailyRecipient &recipient(uint32_t ordinal) const { return recipients_[ordinal]; }

    private:
        int32_t date_raw_ = 0;
        uint32_t country_count_ = 0;
        uint32_t expected_debtors_ = 0;
        uint32_t seen_count_ = 0;
        uint32_t rejected_debtors_ = 0;
        int64_t private_sink_raw_ = 0;
        bool started_ = false;
        std::array<bool, max_batch_countries> seen_debtors_{};
        std::array<DailyRecipient, max_batch_countries> recipients_{};
    };
}
