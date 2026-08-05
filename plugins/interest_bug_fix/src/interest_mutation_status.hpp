#pragma once

#include <smedley/game_state/runtime.hpp>

namespace interest_bug_fix
{
    enum class PopInterestFailureClass
    {
        balance,
        not_writable,
        unavailable,
        precondition_changed,
        postcondition_failed,
        partial_mutation,
    };

    PopInterestFailureClass ClassifyPopInterestFailure(smedley::game_state::PopInterestMutationStatus status);
    PopInterestFailureClass ClassifyAppliedPopInterestFailure(
        smedley::game_state::PopInterestMutationStatus status, bool prior_mutation);
    bool IsUnsafePopInterestFailure(smedley::game_state::PopInterestMutationStatus status);
    bool IsUnsafeAppliedPopInterestFailure(
        smedley::game_state::PopInterestMutationStatus status, bool prior_mutation);
}
