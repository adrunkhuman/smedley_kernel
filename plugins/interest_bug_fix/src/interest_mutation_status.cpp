#include "interest_mutation_status.hpp"

namespace interest_bug_fix
{
    PopInterestFailureClass ClassifyPopInterestFailure(smedley::game_state::PopInterestMutationStatus status)
    {
        using smedley::game_state::PopInterestMutationStatus;
        if (status == PopInterestMutationStatus::balance_unreadable
            || status == PopInterestMutationStatus::balance_overflow) return PopInterestFailureClass::balance;
        if (status == PopInterestMutationStatus::not_writable) return PopInterestFailureClass::not_writable;
        if (status == PopInterestMutationStatus::signature_mismatch
            || status == PopInterestMutationStatus::unavailable
            || status == PopInterestMutationStatus::invalid_context
            || status == PopInterestMutationStatus::invalid_phase
            || status == PopInterestMutationStatus::invalid_thread) return PopInterestFailureClass::unavailable;
        if (status == PopInterestMutationStatus::postcondition_failed) return PopInterestFailureClass::postcondition_failed;
        return PopInterestFailureClass::precondition_changed;
    }

    bool IsUnsafePopInterestFailure(smedley::game_state::PopInterestMutationStatus status)
    {
        const auto failure = ClassifyPopInterestFailure(status);
        return failure == PopInterestFailureClass::unavailable
            || failure == PopInterestFailureClass::postcondition_failed;
    }

    PopInterestFailureClass ClassifyAppliedPopInterestFailure(
        smedley::game_state::PopInterestMutationStatus status, bool prior_mutation)
    {
        if (prior_mutation) return PopInterestFailureClass::partial_mutation;
        return ClassifyPopInterestFailure(status);
    }

    bool IsUnsafeAppliedPopInterestFailure(
        smedley::game_state::PopInterestMutationStatus status, bool prior_mutation)
    {
        const auto failure = ClassifyAppliedPopInterestFailure(status, prior_mutation);
        return failure == PopInterestFailureClass::unavailable
            || failure == PopInterestFailureClass::postcondition_failed
            || failure == PopInterestFailureClass::partial_mutation;
    }
}
