#include <smedley/executable_identity.hpp>

#include <gtest/gtest.h>

namespace smedley
{
    TEST(ExecutableIdentityTest, RejectsTheHostTestExecutableAndRetainsTheVerdict)
    {
        EXPECT_FALSE(ValidateCurrentExecutableIdentity());
        EXPECT_FALSE(ValidateCurrentExecutableIdentity());
        EXPECT_FALSE(IsCurrentExecutableSupported());
    }
}
