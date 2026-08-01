#pragma once

#include "economic_capture_core.hpp"
#include "../interest_bug_fix/economic_state.hpp"

#include <smedley/v2/gamestate.hpp>

#include <array>
#include <cstdint>

namespace telemetry_plugin
{
    class EconomicCapture
    {
    public:
        EconomicSnapshot Collect(
            const smedley::v2::CCurrentGameState *game_state, int32_t date);

    private:
        static const void *ResolveProvince(const void *context, int32_t id);
        std::array<interest_bug_fix::PopCandidate, interest_bug_fix::max_sample_pops> candidates_{};
    };
}
