#pragma once

#include "economic_capture_core.hpp"
#include "../interest_bug_fix/economic_state.hpp"

#include <smedley/v2/gamestate.hpp>

#include <array>
#include <cstdint>

namespace telemetry_plugin
{
    struct PopulationCapture
    {
        int32_t date_raw = 0;
        uint32_t flags = 0;
        uint32_t country_count = 0;
        uint32_t province_count = 0;
        uint32_t pop_count = 0;
        uint64_t collection_us = 0;

        bool complete() const { return flags == 0; }
    };

    class EconomicCapture
    {
    public:
        EconomicSnapshot Collect(
            const smedley::v2::CCurrentGameState *game_state, int32_t date);
        PopulationCapture CollectPopulation(
            const smedley::v2::CCurrentGameState *game_state, int32_t date);
        void InvalidatePopulationCache() { population_cached_ = false; }
        const interest_bug_fix::PopCandidate &population_candidate(size_t index) const { return candidates_[index]; }
        const interest_bug_fix::PopDetailSnapshot &population_detail(size_t index) const { return population_details_[index]; }

    private:
        static const void *ResolveProvince(const void *context, int32_t id);
        std::array<interest_bug_fix::PopCandidate, interest_bug_fix::max_sample_pops> candidates_{};
        std::array<interest_bug_fix::PopDetailSnapshot, interest_bug_fix::max_sample_pops> population_details_{};
        PopulationCapture cached_population_{};
        bool population_cached_ = false;
    };
}
