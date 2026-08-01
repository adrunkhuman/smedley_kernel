#include "economic_capture.hpp"

#include <algorithm>
#include <chrono>

namespace telemetry_plugin
{
    using namespace interest_bug_fix;

    EconomicSnapshot EconomicCapture::Collect(
        const smedley::v2::CCurrentGameState *game_state, int32_t date)
    {
        const auto started = std::chrono::steady_clock::now();
        EconomicSnapshot snapshot{};
        snapshot.date_raw = date;
        auto finish_timing = [&] {
            snapshot.collection_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started).count());
        };
        const size_t slots = game_state->country_count();
        if (slots == 0 || slots > static_cast<size_t>(max_world_countries) + 1) {
            snapshot.snapshot_flags |= SNAPSHOT_COUNTRY_LIMIT;
            finish_timing();
            return snapshot;
        }

        uint32_t candidate_count = 0;
        for (size_t ordinal = 1; ordinal < slots; ++ordinal) {
            const auto *country = game_state->country(static_cast<int32_t>(ordinal));
            const Sample credit_quality = CollectSample(country, date);
            constexpr uint32_t credit_flag_mask = SAMPLE_SUM_OVERFLOW | SAMPLE_CREDITOR_VECTOR_INVALID
                | SAMPLE_CREDITOR_UNREADABLE | SAMPLE_CREDITOR_TAG_INVALID;
            snapshot.credit_flags |= credit_quality.flags & credit_flag_mask;
            Sample quality{};
            uint32_t collected = 0;
            const uint32_t province_remaining = snapshot.province_count >= max_sample_destination_provinces
                ? 0 : max_sample_destination_provinces - snapshot.province_count;
            if (!CollectCountryPops(country, date, ResolveProvince, game_state,
                    candidates_.data() + candidate_count, candidates_.size() - candidate_count,
                    province_remaining, &collected, &quality)) {
                snapshot.snapshot_flags |= SNAPSHOT_COLLECTION_FAILED;
                snapshot.collection_flags |= quality.flags;
                snapshot.state_count += quality.states_walked;
                snapshot.province_count += quality.destination_province_attempts;
                snapshot.pop_count += quality.destination_pop_attempts;
                break;
            }
            ++snapshot.country_count;
            snapshot.state_count += quality.states_walked;
            snapshot.province_count += quality.destination_province_attempts;
            snapshot.pop_count += collected;
            snapshot.creditor_count += credit_quality.creditor_count;
            snapshot.creditors_was_paid += credit_quality.creditors_was_paid;
            if (credit_quality.creditor_count != 0) ++snapshot.countries_with_creditors;
            if (quality.treasury_raw < 0) ++snapshot.countries_with_negative_treasury;
            AddEconomicValue(quality.treasury_raw, &snapshot.treasury_observed_raw, &snapshot.snapshot_flags);
            AddEconomicValue(quality.bank_interest_raw, &snapshot.bank_interest_accumulator_raw, &snapshot.snapshot_flags);
            AddEconomicValue(quality.state_savings_raw, &snapshot.state_savings_candidate_raw, &snapshot.snapshot_flags);
            AddEconomicValue(quality.state_interest_raw, &snapshot.state_interest_candidate_raw, &snapshot.snapshot_flags);
            AddEconomicValue(credit_quality.creditor_interest_raw,
                &snapshot.creditor_interest_candidate_raw, &snapshot.credit_flags, SAMPLE_SUM_OVERFLOW);
            AddEconomicValue(credit_quality.creditor_debt_raw,
                &snapshot.creditor_debt_candidate_raw, &snapshot.credit_flags, SAMPLE_SUM_OVERFLOW);
            candidate_count += collected;
        }

        if (snapshot.complete()) {
            std::sort(candidates_.begin(), candidates_.begin() + candidate_count,
                [](const PopCandidate &left, const PopCandidate &right) {
                    return reinterpret_cast<uintptr_t>(left.address) < reinterpret_cast<uintptr_t>(right.address);
                });
            for (uint32_t index = 1; index < candidate_count; ++index) {
                if (candidates_[index - 1].address == candidates_[index].address) {
                    snapshot.snapshot_flags |= SNAPSHOT_DUPLICATE_POP;
                    break;
                }
            }
        }
        if (snapshot.complete()) {
            for (uint32_t index = 0; index < candidate_count; ++index) {
                PopMoneySnapshot pop{};
                if (!ReadPopMoneySnapshot(candidates_[index].address, &pop)
                    || pop.savings_raw != candidates_[index].savings_raw) {
                    snapshot.snapshot_flags |= SNAPSHOT_POP_UNREADABLE;
                    break;
                }
                if (pop.money_raw > 0) ++snapshot.positive_money_pops;
                if (pop.savings_raw > 0) ++snapshot.positive_savings_pops;
                AddEconomicValue(pop.money_raw, &snapshot.pop_money_observed_raw, &snapshot.snapshot_flags);
                AddEconomicValue(pop.savings_raw, &snapshot.pop_savings_observed_raw, &snapshot.snapshot_flags);
            }
        }
        finish_timing();
        return snapshot;
    }

    const void *EconomicCapture::ResolveProvince(const void *context, int32_t id)
    {
        return static_cast<const smedley::v2::CCurrentGameState *>(context)->province(id);
    }
}
