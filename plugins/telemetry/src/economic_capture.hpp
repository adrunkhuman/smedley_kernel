#pragma once

#include "economic_capture_core.hpp"
#include <smedley/game_state/readers.hpp>

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
        const smedley::game_state::PopCandidate &population_candidate(size_t index) const { return candidates_[index]; }
        const smedley::game_state::PopDetailSnapshot &population_detail(size_t index) const { return population_details_[index]; }

    private:
        static smedley::game_state::ProvinceRef ResolveProvince(const void *context, int32_t id);
        std::array<smedley::game_state::PopCandidate, smedley::game_state::max_sample_pops> candidates_{};
        std::array<smedley::game_state::PopDetailSnapshot, smedley::game_state::max_sample_pops> population_details_{};
        std::array<int32_t, smedley::game_state::max_sample_pops> population_ids_{};
        PopulationCapture cached_population_{};
        bool population_cached_ = false;
    };
}
