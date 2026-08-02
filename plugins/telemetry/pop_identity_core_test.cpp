#include "pop_identity_core.hpp"

#include <gtest/gtest.h>

#include <array>

TEST(PopIdentityCoreTest, ReconcilesObservedIdentityChanges)
{
    const std::array<telemetry_plugin::PopIdentityState, 3> previous{{
        {1, 10, 2, 100, 1}, {2, 11, 3, 200, 1}, {4, 12, 4, 300, 1}}};
    const std::array<telemetry_plugin::PopIdentityState, 3> current{{
        {1, 10, 2, 110, 1}, {2, 13, 5, 200, 2}, {3, 14, 6, 50, 2}}};
    std::array<telemetry_plugin::PopIdentityChange, 4> changes{};
    size_t change_count = 0;
    telemetry_plugin::PopIdentityDiff diff{};

    ASSERT_TRUE(telemetry_plugin::DiffPopIdentities(previous.data(), previous.size(),
        current.data(), current.size(), changes.data(), changes.size(), &change_count, &diff));
    ASSERT_EQ(change_count, 3u);
    EXPECT_EQ(diff.unchanged, 1u);
    EXPECT_EQ(diff.scope_changed, 1u);
    EXPECT_EQ(diff.appeared, 1u);
    EXPECT_EQ(diff.disappeared, 1u);
    EXPECT_EQ(changes[0].kind, telemetry_plugin::PopObservationKind::ScopeChanged);
    EXPECT_EQ(changes[1].kind, telemetry_plugin::PopObservationKind::Appeared);
    EXPECT_EQ(changes[2].kind, telemetry_plugin::PopObservationKind::Disappeared);
}

TEST(PopIdentityCoreTest, RejectsInvalidSequencesAndShortOutput)
{
    const std::array<telemetry_plugin::PopIdentityState, 2> duplicate{{
        {1, 10, 2, 100, 1}, {1, 11, 3, 200, 1}}};
    const std::array<telemetry_plugin::PopIdentityState, 1> current{{{2, 10, 2, 100, 1}}};
    telemetry_plugin::PopIdentityChange change{};
    size_t count = 0;
    telemetry_plugin::PopIdentityDiff diff{};
    EXPECT_FALSE(telemetry_plugin::DiffPopIdentities(duplicate.data(), duplicate.size(),
        current.data(), current.size(), &change, 1, &count, &diff));

    const std::array<telemetry_plugin::PopIdentityState, 1> previous{{{1, 10, 2, 100, 1}}};
    EXPECT_FALSE(telemetry_plugin::DiffPopIdentities(previous.data(), previous.size(),
        current.data(), current.size(), &change, 1, &count, &diff));
}
