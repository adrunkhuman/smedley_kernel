#include "memory.hpp"

#include <gtest/gtest.h>

#include <array>

namespace
{
    TEST(CodePatchRegistryTest, AcceptsOriginalBeforePatchAndExactRegisteredReplacementAfterPatch)
    {
        std::array<uint8_t, 4> bytes{{0x55, 0x8b, 0xec, 0x83}};
        const std::array<uint8_t, 4> original = bytes;
        const std::array<uint8_t, 4> replacement{{0xe9, 0x01, 0x02, 0x03}};
        const uintptr_t address = reinterpret_cast<uintptr_t>(bytes.data());

        EXPECT_TRUE(smedley::memory::MatchesOriginalOrRegisteredCodePatch(address, original.data(), original.size()));
        EXPECT_TRUE(smedley::memory::RegisterCodePatch(address, original.data(), replacement.data(), replacement.size()));
        EXPECT_TRUE(smedley::memory::MatchesOriginalOrRegisteredCodePatch(address, original.data(), original.size()));
        bytes = replacement;
        EXPECT_TRUE(smedley::memory::MatchesOriginalOrRegisteredCodePatch(address, original.data(), original.size()));
        bytes = original;
        EXPECT_TRUE(smedley::memory::MatchesOriginalOrRegisteredCodePatch(address, original.data(), original.size()));
        EXPECT_TRUE(smedley::memory::UnregisterCodePatch(address, original.data(), replacement.data(), replacement.size()));
        EXPECT_TRUE(smedley::memory::MatchesOriginalOrRegisteredCodePatch(address, original.data(), original.size()));
    }

    TEST(CodePatchRegistryTest, RejectsMismatchedDuplicateAndConflictingPatches)
    {
        std::array<uint8_t, 3> bytes{{0xe9, 0x01, 0x02}};
        const std::array<uint8_t, 3> original{{0x55, 0x8b, 0xec}};
        const std::array<uint8_t, 3> replacement = bytes;
        const std::array<uint8_t, 3> other_original{{0x56, 0x8b, 0xf1}};
        const std::array<uint8_t, 3> other_replacement{{0xe9, 0x03, 0x04}};
        std::array<uint8_t, 3> other_bytes = replacement;
        const uintptr_t address = reinterpret_cast<uintptr_t>(bytes.data());

        EXPECT_TRUE(smedley::memory::RegisterCodePatch(address, original.data(), replacement.data(), replacement.size()));
        EXPECT_FALSE(smedley::memory::RegisterCodePatch(address, original.data(), replacement.data(), replacement.size()));
        EXPECT_FALSE(smedley::memory::RegisterCodePatch(address, other_original.data(), other_replacement.data(), other_replacement.size()));
        EXPECT_FALSE(smedley::memory::MatchesOriginalOrRegisteredCodePatch(address, other_original.data(), other_original.size()));
        EXPECT_FALSE(smedley::memory::MatchesOriginalOrRegisteredCodePatch(
            reinterpret_cast<uintptr_t>(other_bytes.data()), original.data(), original.size()));
        EXPECT_FALSE(smedley::memory::UnregisterCodePatch(address, original.data(), other_replacement.data(), other_replacement.size()));
        EXPECT_TRUE(smedley::memory::UnregisterCodePatch(address, original.data(), replacement.data(), replacement.size()));
    }

    TEST(CodePatchRegistryTest, BoundsRegistrationCapacityAndRejectsUnknownBytes)
    {
        std::array<std::array<uint8_t, 2>, 17> bytes{};
        const std::array<uint8_t, 2> original{{0x55, 0x8b}};
        const std::array<uint8_t, 2> replacement{{0xe9, 0x90}};
        for (size_t index = 0; index < bytes.size(); ++index) {
            bytes[index] = replacement;
            const uintptr_t address = reinterpret_cast<uintptr_t>(bytes[index].data());
            EXPECT_EQ(smedley::memory::RegisterCodePatch(address, original.data(), replacement.data(), replacement.size()),
                index < 16);
        }
        for (size_t index = 0; index < 16; ++index) {
            const uintptr_t address = reinterpret_cast<uintptr_t>(bytes[index].data());
            EXPECT_TRUE(smedley::memory::UnregisterCodePatch(address, original.data(), replacement.data(), replacement.size()));
        }

        bytes[0] = original;
        const uintptr_t first_address = reinterpret_cast<uintptr_t>(bytes[0].data());
        EXPECT_TRUE(smedley::memory::RegisterCodePatch(
            first_address, original.data(), replacement.data(), replacement.size()));
        EXPECT_TRUE(smedley::memory::UnregisterCodePatch(
            first_address, original.data(), replacement.data(), replacement.size()));
        bytes[0] = {{0x00, 0x00}};
        EXPECT_FALSE(smedley::memory::RegisterCodePatch(
            first_address, original.data(), replacement.data(), replacement.size()));
        EXPECT_FALSE(smedley::memory::RegisterCodePatch(0, original.data(), replacement.data(), replacement.size()));
    }

    TEST(CodePatchRegistryTest, RetainsOwnershipWhenActiveBytesBecomeUnknown)
    {
        std::array<uint8_t, 3> bytes{{0x55, 0x8b, 0xec}};
        const std::array<uint8_t, 3> original = bytes;
        const std::array<uint8_t, 3> replacement{{0xe9, 0x01, 0x02}};
        const std::array<uint8_t, 3> unknown{{0xcc, 0xcc, 0xcc}};
        const uintptr_t address = reinterpret_cast<uintptr_t>(bytes.data());

        ASSERT_TRUE(smedley::memory::RegisterCodePatch(
            address, original.data(), replacement.data(), replacement.size()));
        bytes = unknown;
        EXPECT_FALSE(smedley::memory::MatchesOriginalOrRegisteredCodePatch(
            address, original.data(), original.size()));
        EXPECT_FALSE(smedley::memory::UnregisterCodePatch(
            address, original.data(), replacement.data(), replacement.size()));

        bytes = replacement;
        EXPECT_TRUE(smedley::memory::MatchesOriginalOrRegisteredCodePatch(
            address, original.data(), original.size()));
        EXPECT_TRUE(smedley::memory::UnregisterCodePatch(
            address, original.data(), replacement.data(), replacement.size()));
    }
}
