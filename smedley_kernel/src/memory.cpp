#include "memory.hpp"
#include <cstdint>
#include <array>
#include <mutex>
#include <stdexcept>

namespace smedley::memory
{
    namespace
    {
        constexpr size_t max_registered_code_patches = 16;

        struct RegisteredCodePatch
        {
            uintptr_t address = 0;
            std::array<uint8_t, max_registered_code_patch_bytes> original{};
            std::array<uint8_t, max_registered_code_patch_bytes> replacement{};
            size_t size = 0;
        };

        std::array<RegisteredCodePatch, max_registered_code_patches> registered_code_patches{};
        std::mutex registered_code_patches_mutex;

        bool ValidCodePatchArguments(uintptr_t address, const uint8_t *original,
                                     const uint8_t *replacement, size_t size)
        {
            return address != 0 && original != nullptr && replacement != nullptr
                && size != 0 && size <= max_registered_code_patch_bytes;
        }

        bool Matches(const RegisteredCodePatch &patch, uintptr_t address, const uint8_t *original,
                     const uint8_t *replacement, size_t size)
        {
            return patch.address == address && patch.size == size
                && std::memcmp(patch.original.data(), original, size) == 0
                && std::memcmp(patch.replacement.data(), replacement, size) == 0;
        }
    }

    uintptr_t Map::base_addr = NULL;
    HANDLE Map::game_heap = NULL;

    void __stdcall HeapInitHook(HANDLE heap)
    {
        Map::game_heap = heap;
    }

    void Map::Init()
    {
        Map::base_addr = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    }

    void InstallHeapHook()
    {
        constexpr uint8_t trampoline_template[] = {
            0x50, // Push EAX.
            0x50, // Push EAX.
            0xe8, 0x90, 0x90, 0x90, 0x90, // Call the heap hook.
            0x58, // Pop EAX.

            0xa3, 0xe8, 0x02, 0xb2, 0x00, // Store EAX in the heap-handle global.
            0x8b, 0xc1, // Move ECX to EAX.
            0xc3, // Return.
        };

        auto *trampoline = static_cast<uint8_t *>(VirtualAlloc(
            nullptr, sizeof(trampoline_template), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (trampoline == nullptr) throw std::runtime_error("could not allocate heap-hook trampoline");
        uintptr_t trampoline_jmp = (reinterpret_cast<uintptr_t>(HeapInitHook) + 9) - reinterpret_cast<uintptr_t>((trampoline + sizeof(trampoline_template)));

        std::copy(std::begin(trampoline_template), std::end(trampoline_template), trampoline);
        *reinterpret_cast<uintptr_t *>(trampoline + 3) = trampoline_jmp;
        *reinterpret_cast<uintptr_t *>(trampoline + 9) = Map::base_addr + 0x00b202e8;
        DWORD old_protect;
        if (!VirtualProtect(trampoline, sizeof(trampoline_template), PAGE_EXECUTE_READ, &old_protect)) {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            throw std::runtime_error("could not make heap-hook trampoline executable");
        }
        if (!FlushInstructionCache(GetCurrentProcess(), trampoline, sizeof(trampoline_template))) {
            DWORD ignored;
            VirtualProtect(trampoline, sizeof(trampoline_template), old_protect, &ignored);
            VirtualFree(trampoline, 0, MEM_RELEASE);
            throw std::runtime_error("could not flush the heap-hook trampoline instruction cache");
        }
        try {
            Hook(Map::base_addr + 0x006babee, trampoline, 8, nullptr);
        } catch (...) {
            DWORD ignored;
            VirtualProtect(trampoline, sizeof(trampoline_template), old_protect, &ignored);
            VirtualFree(trampoline, 0, MEM_RELEASE);
            throw;
        }
    }

    void Patch(uintptr_t addr, uint8_t *instr, int n)
    {
        DWORD old_protect;
        LPVOID lpv_addr = reinterpret_cast<LPVOID>(addr);

        VirtualProtect(lpv_addr, n, PAGE_EXECUTE_READWRITE, &old_protect);
        memcpy(lpv_addr, instr, n);
        VirtualProtect(lpv_addr, n, old_protect, &old_protect);
    }

    bool Hook(uintptr_t addr, void *jmp, int n, std::vector<uint8_t> *old_instr)
    {
        DWORD old_protect, offset;
        LPVOID lpv_addr = reinterpret_cast<LPVOID>(addr);
        uint8_t *bytes_addr = reinterpret_cast<uint8_t *>(addr);

        if (n < 5) {
            return false;
        }

        offset = reinterpret_cast<uintptr_t>(jmp) - addr - 5;
        const std::vector<uint8_t> original(bytes_addr, bytes_addr + n);

        if (!VirtualProtect(lpv_addr, n, PAGE_EXECUTE_READWRITE, &old_protect)) {
            throw std::runtime_error("could not make hook target writable");
        }

        if (old_instr != nullptr) {
            *old_instr = original;
        }

        std::memset(bytes_addr + 1, 0x90, n - 1); // Fill the remaining bytes with NOPs.
        *bytes_addr = 0xe9;
        *reinterpret_cast<DWORD *>(bytes_addr + 1) = offset;

        if (!FlushInstructionCache(GetCurrentProcess(), lpv_addr, n)) {
            std::copy(original.begin(), original.end(), bytes_addr);
            FlushInstructionCache(GetCurrentProcess(), lpv_addr, n);
            DWORD ignored;
            VirtualProtect(lpv_addr, n, old_protect, &ignored);
            throw std::runtime_error("could not flush the hook instruction cache");
        }
        DWORD ignored;
        if (!VirtualProtect(lpv_addr, n, old_protect, &ignored)) {
            std::copy(original.begin(), original.end(), bytes_addr);
            FlushInstructionCache(GetCurrentProcess(), lpv_addr, n);
            VirtualProtect(lpv_addr, n, old_protect, &ignored);
            throw std::runtime_error("could not restore hook target protection");
        }

        return true;
    }

    bool RestoreHook(uintptr_t addr, const std::vector<uint8_t> &instructions) noexcept
    {
        if (instructions.empty()) return false;
        auto *target = reinterpret_cast<void *>(addr);
        DWORD old_protect;
        if (!VirtualProtect(target, instructions.size(), PAGE_EXECUTE_READWRITE, &old_protect)) return false;
        std::memcpy(target, instructions.data(), instructions.size());
        const bool flushed = FlushInstructionCache(GetCurrentProcess(), target, instructions.size()) != FALSE;
        DWORD ignored;
        const bool restored = VirtualProtect(target, instructions.size(), old_protect, &ignored) != FALSE;
        return flushed && restored;
    }

    bool RegisterCodePatch(uintptr_t address, const uint8_t *original, const uint8_t *replacement, size_t size)
    {
        if (!ValidCodePatchArguments(address, original, replacement, size)) return false;
        const auto *current = reinterpret_cast<const void *>(address);
        if (std::memcmp(current, original, size) != 0 && std::memcmp(current, replacement, size) != 0) return false;
        std::lock_guard lock(registered_code_patches_mutex);
        for (const auto &patch : registered_code_patches) {
            if (patch.address == address) return false;
        }
        for (auto &patch : registered_code_patches) {
            if (patch.address != 0) continue;
            patch.address = address;
            patch.size = size;
            std::memcpy(patch.original.data(), original, size);
            std::memcpy(patch.replacement.data(), replacement, size);
            return true;
        }
        return false;
    }

    bool UnregisterCodePatch(uintptr_t address, const uint8_t *original, const uint8_t *replacement, size_t size)
    {
        if (!ValidCodePatchArguments(address, original, replacement, size)) return false;
        std::lock_guard lock(registered_code_patches_mutex);
        for (auto &patch : registered_code_patches) {
            if (!Matches(patch, address, original, replacement, size)) continue;
            const auto *current = reinterpret_cast<const void *>(address);
            if (std::memcmp(current, original, size) != 0 && std::memcmp(current, replacement, size) != 0) return false;
            patch = {};
            return true;
        }
        return false;
    }

    bool MatchesOriginalOrRegisteredCodePatch(uintptr_t address, const uint8_t *original, size_t size)
    {
        if (address == 0 || original == nullptr || size == 0 || size > max_registered_code_patch_bytes) return false;
        if (std::memcmp(reinterpret_cast<const void *>(address), original, size) == 0) return true;
        std::lock_guard lock(registered_code_patches_mutex);
        for (const auto &patch : registered_code_patches) {
            if (patch.address != address || patch.size != size
                || std::memcmp(patch.original.data(), original, size) != 0) continue;
            return std::memcmp(reinterpret_cast<const void *>(address), patch.replacement.data(), size) == 0;
        }
        return false;
    }
}
