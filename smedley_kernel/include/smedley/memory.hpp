#pragma once

#define NOMINMAX

#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <windows.h>
#include <memoryapi.h>

namespace smedley::memory
{

    /**
     * Stores process-wide memory state, including the game-module base address
     * and heap handle. These values should not change after initialization.
     */
    struct Map
    {
        /// @brief Base address of v2game.exe.
        static uintptr_t base_addr;
        /// @brief Heap used by v2game.exe for dynamic allocation.
        static HANDLE game_heap;

        static void Init();
    };

    struct RawHook
    {
        uintptr_t address = 0;
        std::vector<uint8_t> original;
        std::vector<uint8_t> replacement;
    };

    class ScopedThreadQuiescence
    {
    public:
        ScopedThreadQuiescence();
        ~ScopedThreadQuiescence();

        ScopedThreadQuiescence(const ScopedThreadQuiescence &) = delete;
        ScopedThreadQuiescence &operator=(const ScopedThreadQuiescence &) = delete;

        explicit operator bool() const noexcept { return ready_; }
        const char *error() const noexcept { return error_; }
        bool AnyInstructionPointerIn(uintptr_t address, size_t size, bool *found) noexcept;
        bool InstructionPointerCountIn(uintptr_t address, size_t size, size_t *count) noexcept;
        bool Release() noexcept;

    private:
        void CloseThreads() noexcept;
        bool ContainsLiveSuspendedThread(DWORD thread_id, bool *found) noexcept;

        static constexpr size_t max_threads = 256;
        std::array<HANDLE, max_threads> threads_{};
        std::array<DWORD, max_threads> thread_ids_{};
        size_t thread_count_ = 0;
        size_t suspended_count_ = 0;
        bool ready_ = false;
        const char *error_ = nullptr;
    };

    bool MatchesReadableBytes(uintptr_t address, const void *expected, size_t size) noexcept;
    bool InstallRawHook(uintptr_t address, void *destination, const uint8_t *expected,
                        size_t size, RawHook *installed);
    bool RestoreRawHook(RawHook *hook) noexcept;
    void InstallHeapHook(const uint8_t *expected, size_t size, RawHook *installed);
    /** Installs a serialized MinHook function-entry detour and returns its callable original trampoline. */
    bool InstallDetour(uintptr_t addr, void *detour, void **original);
    /** Disables and removes an installed MinHook detour, restoring the function entry. */
    bool RemoveDetour(uintptr_t addr) noexcept;

    constexpr size_t max_registered_code_patch_bytes = 32;

    // Registers an exact patch while the site still contains either its original or replacement bytes.
    bool RegisterCodePatch(uintptr_t address, const uint8_t *original, const uint8_t *replacement, size_t size);
    // Removes only the matching patch after the site contains either registered byte sequence.
    bool UnregisterCodePatch(uintptr_t address, const uint8_t *original, const uint8_t *replacement, size_t size);
    // Accepts an original sequence or an exact registered replacement for that sequence.
    bool MatchesOriginalOrRegisteredCodePatch(uintptr_t address, const uint8_t *original, size_t size);

}
