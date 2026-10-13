#pragma once

namespace smedley
{
    /** Validates and retains the current process executable identity once. */
    bool ValidateCurrentExecutableIdentity();
    /** Returns true only after successful validation of the supported executable. */
    bool IsCurrentExecutableSupported() noexcept;
}
