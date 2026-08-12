#pragma once

#define NOMINMAX

#include "apimacros.hpp"
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
    struct SMEDLEY_API Map
    {
        /// @brief Base address of v2game.exe.
        static uintptr_t base_addr;
        /// @brief Heap used by v2game.exe for dynamic allocation.
        static HANDLE game_heap;

        static void Init();
    };

    /**
     * Patches instructions at the specified address.
     * @param addr Address of the instructions to patch.
     * @param instr Replacement byte sequence.
     * @param n Number of bytes to write.
     */
    void Patch(uintptr_t addr, uint8_t *instr, int n);
    void InstallHeapHook();

    /** Writes a raw relative jump over caller-verified complete instructions. */
    bool Hook(uintptr_t addr, void *jmp, int n, std::vector<uint8_t> *old_instr);
    /** Restores bytes retained by Hook and flushes the instruction cache. */
    bool RestoreHook(uintptr_t addr, const std::vector<uint8_t> &instructions) noexcept;
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
