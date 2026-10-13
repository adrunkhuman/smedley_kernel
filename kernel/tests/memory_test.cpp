#include "memory.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>

namespace
{
    using DetourFixtureFn = int(__cdecl *)(int);
    DetourFixtureFn detour_fixture_original = nullptr;

    __declspec(noinline) int __cdecl DetourFixture(int value)
    {
        volatile int preserved = value;
        return preserved + 1;
    }

    int __cdecl DetourFixtureReplacement(int value)
    {
        return detour_fixture_original(value) + 10;
    }

    void ExpectProtection(const void *address, DWORD expected)
    {
        MEMORY_BASIC_INFORMATION region{};
        ASSERT_EQ(VirtualQuery(address, &region, sizeof(region)), sizeof(region));
        EXPECT_EQ(region.Protect, expected);
    }

    TEST(MinHookAdapterTest, CallsRelocatedOriginalAndRestoresTarget)
    {
        ASSERT_EQ(DetourFixture(2), 3);
        ASSERT_TRUE(smedley::memory::InstallDetour(reinterpret_cast<uintptr_t>(&DetourFixture),
            reinterpret_cast<void *>(&DetourFixtureReplacement), reinterpret_cast<void **>(&detour_fixture_original)));
        ASSERT_NE(detour_fixture_original, nullptr);
        EXPECT_EQ(DetourFixture(2), 13);
        ASSERT_TRUE(smedley::memory::RemoveDetour(reinterpret_cast<uintptr_t>(&DetourFixture)));
        EXPECT_EQ(DetourFixture(2), 3);

        detour_fixture_original = nullptr;
        ASSERT_TRUE(smedley::memory::InstallDetour(reinterpret_cast<uintptr_t>(&DetourFixture),
            reinterpret_cast<void *>(&DetourFixtureReplacement), reinterpret_cast<void **>(&detour_fixture_original)));
        EXPECT_EQ(DetourFixture(2), 13);
        EXPECT_TRUE(smedley::memory::RemoveDetour(reinterpret_cast<uintptr_t>(&DetourFixture)));
    }

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

    TEST(RawHookTest, InstallsAndRestoresOnlyTheExpectedInstructions)
    {
        constexpr size_t page_size = 4096;
        const std::array<uint8_t, 8> original{{0x55, 0x8b, 0xec, 0x83, 0xec, 0x08, 0x53, 0x56}};
        auto *target = static_cast<uint8_t *>(VirtualAlloc(
            nullptr, page_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        ASSERT_NE(target, nullptr);
        std::memcpy(target, original.data(), original.size());
        DWORD old_protection;
        ASSERT_TRUE(VirtualProtect(target, page_size, PAGE_EXECUTE_READ, &old_protection));

        smedley::memory::RawHook hook;
        ASSERT_TRUE(smedley::memory::InstallRawHook(
            reinterpret_cast<uintptr_t>(target), reinterpret_cast<void *>(&DetourFixtureReplacement),
            original.data(), original.size(), &hook));
        EXPECT_EQ(hook.address, reinterpret_cast<uintptr_t>(target));
        EXPECT_EQ(hook.original, std::vector<uint8_t>(original.begin(), original.end()));
        EXPECT_EQ(target[0], 0xe9);
        EXPECT_TRUE(std::all_of(target + 5, target + original.size(), [](uint8_t byte) { return byte == 0x90; }));
        ExpectProtection(target, PAGE_EXECUTE_READ);

        ASSERT_TRUE(smedley::memory::RestoreRawHook(&hook));
        EXPECT_EQ(hook.address, 0u);
        EXPECT_EQ(std::memcmp(target, original.data(), original.size()), 0);
        ExpectProtection(target, PAGE_EXECUTE_READ);
        EXPECT_TRUE(VirtualFree(target, 0, MEM_RELEASE));
    }

    TEST(RawHookTest, RejectsMismatchedAndUnreadableTargetsWithoutWriting)
    {
        constexpr size_t page_size = 4096;
        const std::array<uint8_t, 5> original{{0x55, 0x8b, 0xec, 0x53, 0x56}};
        std::array<uint8_t, 5> wrong = original;
        wrong[0] = 0x90;
        auto *target = static_cast<uint8_t *>(VirtualAlloc(
            nullptr, page_size * 2, MEM_RESERVE, PAGE_NOACCESS));
        ASSERT_NE(target, nullptr);
        ASSERT_EQ(VirtualAlloc(target, page_size, MEM_COMMIT, PAGE_READWRITE), target);
        ASSERT_EQ(VirtualAlloc(target + page_size, page_size, MEM_COMMIT, PAGE_READWRITE), target + page_size);
        std::memcpy(target, original.data(), original.size());
        std::memcpy(target + page_size - 2, original.data(), original.size());
        DWORD old_protection;
        ASSERT_TRUE(VirtualProtect(target, page_size, PAGE_EXECUTE_READ, &old_protection));
        ASSERT_TRUE(VirtualProtect(target + page_size, page_size, PAGE_READONLY, &old_protection));

        smedley::memory::RawHook hook;
        EXPECT_FALSE(smedley::memory::InstallRawHook(
            reinterpret_cast<uintptr_t>(target), reinterpret_cast<void *>(&DetourFixtureReplacement),
            wrong.data(), wrong.size(), &hook));
        EXPECT_EQ(hook.address, 0u);
        EXPECT_EQ(std::memcmp(target, original.data(), original.size()), 0);
        EXPECT_TRUE(smedley::memory::MatchesReadableBytes(
            reinterpret_cast<uintptr_t>(target + page_size - 2), original.data(), original.size()));
        smedley::memory::RawHook boundary_hook;
        EXPECT_FALSE(smedley::memory::InstallRawHook(
            reinterpret_cast<uintptr_t>(target + page_size - 2),
            reinterpret_cast<void *>(&DetourFixtureReplacement), original.data(), original.size(), &boundary_hook));
        ExpectProtection(target, PAGE_EXECUTE_READ);
        ExpectProtection(target + page_size, PAGE_READONLY);
        EXPECT_TRUE(VirtualFree(target, 0, MEM_RELEASE));
    }

    TEST(RawHookTest, RefusesToRestoreOverUnknownInstructions)
    {
        constexpr size_t page_size = 4096;
        const std::array<uint8_t, 5> original{{0x55, 0x8b, 0xec, 0x53, 0x56}};
        const std::array<uint8_t, 5> unknown{{0xcc, 0xcc, 0xcc, 0xcc, 0xcc}};
        auto *target = static_cast<uint8_t *>(VirtualAlloc(
            nullptr, page_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        ASSERT_NE(target, nullptr);
        std::memcpy(target, original.data(), original.size());
        DWORD old_protection;
        ASSERT_TRUE(VirtualProtect(target, page_size, PAGE_EXECUTE_READ, &old_protection));

        smedley::memory::RawHook hook;
        ASSERT_TRUE(smedley::memory::InstallRawHook(
            reinterpret_cast<uintptr_t>(target), reinterpret_cast<void *>(&DetourFixtureReplacement),
            original.data(), original.size(), &hook));
        ASSERT_TRUE(VirtualProtect(target, page_size, PAGE_EXECUTE_READWRITE, &old_protection));
        std::memcpy(target, unknown.data(), unknown.size());
        ASSERT_TRUE(VirtualProtect(target, page_size, old_protection, &old_protection));
        EXPECT_FALSE(smedley::memory::RestoreRawHook(&hook));
        EXPECT_EQ(std::memcmp(target, unknown.data(), unknown.size()), 0);

        ASSERT_TRUE(VirtualProtect(target, page_size, PAGE_EXECUTE_READWRITE, &old_protection));
        std::memcpy(target, hook.replacement.data(), hook.replacement.size());
        ASSERT_TRUE(VirtualProtect(target, page_size, old_protection, &old_protection));
        ASSERT_TRUE(smedley::memory::RestoreRawHook(&hook));
        EXPECT_TRUE(VirtualFree(target, 0, MEM_RELEASE));
    }

    TEST(ThreadQuiescenceTest, DetectsAndResumesAThreadExecutingInTheTargetRange)
    {
        constexpr size_t page_size = 4096;
        const std::array<uint8_t, 17> loop_code{{
            0x8b, 0x44, 0x24, 0x04,             // mov eax, [esp+4]
            0xc7, 0x00, 0x01, 0x00, 0x00, 0x00, // mov dword ptr [eax], 1
            0x83, 0x78, 0x04, 0x00,             // cmp dword ptr [eax+4], 0
            0x74, 0xfa,                         // je -6
            0xc3,                               // ret
        }};
        auto *code = static_cast<uint8_t *>(VirtualAlloc(
            nullptr, page_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        ASSERT_NE(code, nullptr);
        std::memcpy(code, loop_code.data(), loop_code.size());
        DWORD old_protection;
        ASSERT_TRUE(VirtualProtect(code, page_size, PAGE_EXECUTE_READ, &old_protection));

        struct LoopState
        {
            volatile LONG ready = 0;
            volatile LONG stop = 0;
        };
        using LoopFn = void(__cdecl *)(LoopState *);
        const auto loop = reinterpret_cast<LoopFn>(code);
        LoopState state;
        std::thread worker([&] { loop(&state); });
        while (InterlockedCompareExchange(&state.ready, 0, 0) == 0) SwitchToThread();

        smedley::memory::ScopedThreadQuiescence quiescence;
        const bool ready = static_cast<bool>(quiescence);
        bool in_target = false;
        const bool inspected = ready
            && quiescence.AnyInstructionPointerIn(reinterpret_cast<uintptr_t>(code), loop_code.size(), &in_target);
        const bool released = quiescence.Release();
        InterlockedExchange(&state.stop, 1);
        worker.join();

        EXPECT_TRUE(ready) << (quiescence.error() == nullptr ? "" : quiescence.error());
        EXPECT_TRUE(inspected) << (quiescence.error() == nullptr ? "" : quiescence.error());
        EXPECT_TRUE(in_target);
        EXPECT_TRUE(released);
        EXPECT_TRUE(VirtualFree(code, 0, MEM_RELEASE));
    }

    TEST(RawHookTest, RepeatedlyQuiescesWorkersAndRollsBackAPartialInstallation)
    {
        constexpr size_t page_size = 4096;
        const std::array<uint8_t, 8> original{{0x55, 0x8b, 0xec, 0x83, 0xec, 0x08, 0x53, 0x56}};
        auto *target = static_cast<uint8_t *>(VirtualAlloc(
            nullptr, page_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        ASSERT_NE(target, nullptr);
        std::memcpy(target, original.data(), original.size());
        std::memcpy(target + 32, original.data(), original.size());
        DWORD old_protection;
        ASSERT_TRUE(VirtualProtect(target, page_size, PAGE_EXECUTE_READ, &old_protection));

        std::atomic<bool> stop{false};
        std::atomic<size_t> ready{0};
        std::array<std::thread, 3> workers{
            std::thread([&] { ready.fetch_add(1, std::memory_order_release); while (!stop.load(std::memory_order_acquire)) SwitchToThread(); }),
            std::thread([&] { ready.fetch_add(1, std::memory_order_release); while (!stop.load(std::memory_order_acquire)) SwitchToThread(); }),
            std::thread([&] { ready.fetch_add(1, std::memory_order_release); while (!stop.load(std::memory_order_acquire)) SwitchToThread(); }),
        };
        while (ready.load(std::memory_order_acquire) != workers.size()) SwitchToThread();
        bool operations_succeeded = true;
        for (size_t iteration = 0; iteration < 5; ++iteration) {
            smedley::memory::RawHook hook;
            operations_succeeded = smedley::memory::InstallRawHook(
                reinterpret_cast<uintptr_t>(target), reinterpret_cast<void *>(&DetourFixtureReplacement),
                original.data(), original.size(), &hook);
            if (!operations_succeeded) break;
            operations_succeeded = smedley::memory::RestoreRawHook(&hook);
            if (!operations_succeeded) break;
        }

        smedley::memory::RawHook first;
        operations_succeeded = operations_succeeded && smedley::memory::InstallRawHook(
            reinterpret_cast<uintptr_t>(target), reinterpret_cast<void *>(&DetourFixtureReplacement),
            original.data(), original.size(), &first);
        std::array<uint8_t, 8> wrong = original;
        wrong[0] = 0x90;
        smedley::memory::RawHook second;
        const bool second_installed = smedley::memory::InstallRawHook(
            reinterpret_cast<uintptr_t>(target + 32), reinterpret_cast<void *>(&DetourFixtureReplacement),
            wrong.data(), wrong.size(), &second);
        operations_succeeded = operations_succeeded && !second_installed
            && smedley::memory::RestoreRawHook(&first);

        stop.store(true, std::memory_order_release);
        for (auto &worker : workers) worker.join();
        EXPECT_TRUE(operations_succeeded);
        EXPECT_EQ(std::memcmp(target, original.data(), original.size()), 0);
        EXPECT_EQ(std::memcmp(target + 32, original.data(), original.size()), 0);
        ExpectProtection(target, PAGE_EXECUTE_READ);
        EXPECT_TRUE(VirtualFree(target, 0, MEM_RELEASE));
    }
}
