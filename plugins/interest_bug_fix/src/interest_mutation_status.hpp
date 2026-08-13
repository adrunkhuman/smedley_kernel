#pragma once

#include <smedley/interest_pool_api.h>

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

    PopInterestFailureClass ClassifyPopInterestFailure(SmedleyInterestPoolResult status);
    PopInterestFailureClass ClassifyAppliedPopInterestFailure(
        SmedleyInterestPoolResult status, bool prior_mutation);
    bool IsUnsafePopInterestFailure(SmedleyInterestPoolResult status);
    bool IsUnsafeAppliedPopInterestFailure(
        SmedleyInterestPoolResult status, bool prior_mutation);
}
