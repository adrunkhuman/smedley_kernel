#include "interest_mutation_status.hpp"

namespace interest_bug_fix
{
    PopInterestFailureClass ClassifyPopInterestFailure(SmedleyInterestPoolResult status)
    {
        if (status == SMEDLEY_INTEREST_POOL_UNAVAILABLE) return PopInterestFailureClass::unavailable;
        if (status == SMEDLEY_INTEREST_POOL_PARTIAL_MUTATION) return PopInterestFailureClass::partial_mutation;
        return PopInterestFailureClass::precondition_changed;
    }

    bool IsUnsafePopInterestFailure(SmedleyInterestPoolResult status)
    {
        const auto failure = ClassifyPopInterestFailure(status);
        return failure == PopInterestFailureClass::unavailable
            || failure == PopInterestFailureClass::postcondition_failed;
    }

    PopInterestFailureClass ClassifyAppliedPopInterestFailure(
        SmedleyInterestPoolResult status, bool prior_mutation)
    {
        if (prior_mutation) return PopInterestFailureClass::partial_mutation;
        return ClassifyPopInterestFailure(status);
    }

    bool IsUnsafeAppliedPopInterestFailure(
        SmedleyInterestPoolResult status, bool prior_mutation)
    {
        const auto failure = ClassifyAppliedPopInterestFailure(status, prior_mutation);
        return failure == PopInterestFailureClass::unavailable
            || failure == PopInterestFailureClass::postcondition_failed
            || failure == PopInterestFailureClass::partial_mutation;
    }
}
