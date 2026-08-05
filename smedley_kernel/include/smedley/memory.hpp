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

    /**
     * Hooks a function by writing a jump at the specified address.
     * @param addr Address at which to install the hook.
     * @param jmp Address of the naked jump function.
     * @param n Number of bytes to write. Bytes after the jump become NOPs.
     * @return Whether the requested patch length is valid.
     */
    bool Hook(uintptr_t addr, void *jmp, int n, std::vector<uint8_t> *old_instr);
    bool RestoreHook(uintptr_t addr, const std::vector<uint8_t> &instructions) noexcept;

    constexpr size_t max_registered_code_patch_bytes = 32;

    // Registers an exact patch while the site still contains either its original or replacement bytes.
    bool RegisterCodePatch(uintptr_t address, const uint8_t *original, const uint8_t *replacement, size_t size);
    // Removes only the matching patch after the site contains either registered byte sequence.
    bool UnregisterCodePatch(uintptr_t address, const uint8_t *original, const uint8_t *replacement, size_t size);
    // Accepts an original sequence or an exact registered replacement for that sequence.
    bool MatchesOriginalOrRegisteredCodePatch(uintptr_t address, const uint8_t *original, size_t size);

    /**
     * Hooks a function at its prologue and generates a trampoline that calls fn.
     * @param addr Location of the prologue.
     * @param fn Hook callback.
     * @param n Number of bytes to write.
     */
    template <typename... Types>
    bool HookPrologue(uintptr_t addr, void(__stdcall *fn)(Types... args), int n)
    {
        DWORD old_protect, call_addr, ret_addr;
        uint8_t *buf, *trampoline;
        int pos, trampoline_size;

        if (n < 5) {
            return false;
        }

        VirtualProtect(addr, n, PAGE_EXECUTE_READWRITE, &old_protect);
        buf = new uint8_t[n];
        std::memcpy(buf, addr, n);
        VirtualProtect(addr, n, old_protect, &old_protect);

        if (buf[0] != 0x55 || buf[1] != 0x8b || buf[2] != 0xec) {
            throw std::runtime_error("expected function prologue");
        }

        /*
         * n: bytes copied from the source.
         * i * 4: bytes emitted for each argument.
         * 10: bytes required for the call and jump.
         */
        trampoline_size = n + (sizeof...(args) * 4) + 10;
        trampoline = new uint8_t[trampoline_size];

        call_addr = (DWORD) fn - ((DWORD) trampoline + trampoline_size - 5 - (n - 3));
        ret_addr = ((DWORD) addr + n) - ((DWORD) trampoline + trampoline_size);

        trampoline[0] = buf[0];
        trampoline[1] = buf[1];
        trampoline[2] = buf[2];

        pos = 3;
        for (int i = 0; i < sizeof...(args); i++, pos += 4) {
            uint8_t offset = 0x8 + (i * 0x4);
            // mov esi, [ebp + offset]
            trampoline[pos] = 0x8b;
            trampoline[pos + 1] = 0x75;
            trampoline[pos + 2] = offset;
            // push esi
            trampoline[pos + 3] = 0x56;
        }
        trampoline[pos++] = 0xe8;
        // Encode the relative call displacement in little-endian order.
        trampoline[pos++] = call_addr & 0xff;
        trampoline[pos++] = (call_addr >> 0x8) & 0xff;
        trampoline[pos++] = (call_addr >> 0x10) & 0xff;
        trampoline[pos++] = (call_addr >> 0x18) & 0xff;

        for (int i = 3; i < n; i++, pos++) {
            trampoline[pos] = buf[i];
        }
        delete[] buf;

        trampoline[pos++] = 0xe9;
        trampoline[pos++] = ret_addr & 0xff;
        trampoline[pos++] = (ret_addr >> 0x8) & 0xff;
        trampoline[pos++] = (ret_addr >> 0x10) & 0xff;
        trampoline[pos++] = (ret_addr >> 0x18) & 0xff;

        VirtualProtect(trampoline, trampoline_size, PAGE_EXECUTE_READWRITE, &old_protect);
        return Hook(addr, trampoline, n, nullptr);

    }

    /**
     * Hooks a function at its epilogue and generates a trampoline that calls fn.
     * @param addr Location of the epilogue.
     * @param fn Hook callback.
     * @param n Number of bytes to write.
     * @param preserve Registers to restore after fn returns.
     */
    template <typename... Types>
    bool HookEpilogue(uintptr_t addr, void(__stdcall *fn)(Types... args), int n, const std::vector<int> &preserve = std::vector<int>())
    {
        DWORD old_protect, call_addr;
        uint8_t *buf, *trampoline;
        constexpr uint8_t pattern[] = {0x8b, 0xe5, 0x5d};
        int pos, trampoline_size, epilogue_start;

        if (n < 5) {
            return false;
        }

        VirtualProtect(addr, n, PAGE_EXECUTE_READWRITE, &old_protect);
        buf = new uint8_t[n];
        std::memcpy(buf, addr, n);
        VirtualProtect(addr, n, old_protect, &old_protect);

        epilogue_start = -1;
        for (int i = 0; i + 2 < n; i++) {
            if (buf[i] == pattern[0] && buf[i + 1] == pattern[1] && buf[i + 2] == pattern[2]) {
                epilogue_start = i;
                break;
            }
        }

        if (epilogue_start == -1) {
            throw std::runtime_error("expected function epilogue");
        }

        pos = 0;
        trampoline_size = n + (sizeof...(args) * 4) + 5 + (preserve.size() * 2);
        trampoline = new uint8_t[trampoline_size];

        call_addr = (DWORD) fn - ((DWORD) trampoline + trampoline_size - (n - epilogue_start));

        for (int i = 0; i < epilogue_start; i++, pos++) {
            trampoline[pos] = buf[i];
        }

        // Preserve registers across the callback.
        for (int reg : preserve) {
            trampoline[pos++] = 0x50 + reg;
        }

        for (int i = 0; i < sizeof...(args); i++, pos += 4) {
            uint8_t offset = 0x8 + (i * 0x4);
            // mov esi, [ebp + offset]
            trampoline[pos] = 0x8b;
            trampoline[pos + 1] = 0x75;
            trampoline[pos + 2] = offset;
            // push esi
            trampoline[pos + 3] = 0x56;
        }
        trampoline[pos++] = 0xe8;
        trampoline[pos++] = call_addr & 0xff;
        trampoline[pos++] = (call_addr >> 0x8) & 0xff;
        trampoline[pos++] = (call_addr >> 0x10) & 0xff;
        trampoline[pos++] = (call_addr >> 0x18) & 0xff;

        for (int i = 0; i < n - epilogue_start; i++, pos++) {
            trampoline[pos] = buf[i + epilogue_start];
        }
        delete[] buf;

        // Restore the preserved registers.
        for (int reg : preserve) {
            trampoline[pos++] = 0x58 + reg;
        }

        VirtualProtect(trampoline, trampoline_size, PAGE_EXECUTE_READWRITE, &old_protect);
        return Hook(addr, trampoline, n, nullptr);
    }

}
