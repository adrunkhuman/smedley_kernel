#include "memory.hpp"
#include <algorithm>
#include <cstdint>
#include <array>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <MinHook.h>
#include <TlHelp32.h>

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
        std::mutex hook_mutex;
        std::mutex raw_hook_mutex;
        bool minhook_initialized = false;

        constexpr size_t max_thread_snapshot = 256;

        struct ThreadSnapshot
        {
            std::array<DWORD, max_thread_snapshot> ids{};
            size_t count = 0;
        };

        enum class ExecutableWriteResult
        {
            success,
            clean_failure,
            indeterminate,
        };

        bool IsReadableProtection(DWORD protection) noexcept
        {
            if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
            switch (protection & 0xff) {
            case PAGE_READONLY:
            case PAGE_READWRITE:
            case PAGE_WRITECOPY:
            case PAGE_EXECUTE:
            case PAGE_EXECUTE_READ:
            case PAGE_EXECUTE_READWRITE:
            case PAGE_EXECUTE_WRITECOPY:
                return true;
            default:
                return false;
            }
        }

        bool CopyBytes(void *destination, const void *source, size_t size) noexcept
        {
            __try {
                std::memcpy(destination, source, size);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool CaptureProcessThreads(DWORD process_id, DWORD current_thread_id,
                                   ThreadSnapshot *result) noexcept
        {
            if (result == nullptr) return false;
            *result = {};
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot == INVALID_HANDLE_VALUE) return false;
            THREADENTRY32 entry{sizeof(entry)};
            SetLastError(ERROR_SUCCESS);
            BOOL found = Thread32First(snapshot, &entry);
            while (found) {
                if (entry.th32OwnerProcessID == process_id && entry.th32ThreadID != current_thread_id) {
                    if (result->count == result->ids.size()) {
                        CloseHandle(snapshot);
                        return false;
                    }
                    result->ids[result->count++] = entry.th32ThreadID;
                }
                entry.dwSize = sizeof(entry);
                SetLastError(ERROR_SUCCESS);
                found = Thread32Next(snapshot, &entry);
            }
            const DWORD enumeration_error = GetLastError();
            CloseHandle(snapshot);
            return enumeration_error == ERROR_NO_MORE_FILES;
        }

        bool IsSingleReadableRegion(uintptr_t address, size_t size) noexcept
        {
            MEMORY_BASIC_INFORMATION region{};
            if (VirtualQuery(reinterpret_cast<const void *>(address), &region, sizeof(region)) != sizeof(region)
                || region.State != MEM_COMMIT || !IsReadableProtection(region.Protect)) return false;
            const uintptr_t region_base = reinterpret_cast<uintptr_t>(region.BaseAddress);
            if (region.RegionSize > (std::numeric_limits<uintptr_t>::max)() - region_base) return false;
            const uintptr_t region_end = region_base + region.RegionSize;
            return address >= region_base && size <= region_end - address;
        }

        ExecutableWriteResult WriteExecutableBytes(uintptr_t address, const uint8_t *expected,
                                                    const uint8_t *replacement, size_t size) noexcept
        {
            if (!IsSingleReadableRegion(address, size)
                || !MatchesReadableBytes(address, expected, size)) return ExecutableWriteResult::clean_failure;
            auto *target = reinterpret_cast<void *>(address);
            DWORD old_protection;
            if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &old_protection)) {
                return ExecutableWriteResult::clean_failure;
            }

            const bool wrote = CopyBytes(target, replacement, size);
            const bool flushed = wrote && FlushInstructionCache(GetCurrentProcess(), target, size) != FALSE;
            DWORD ignored;
            if (flushed && VirtualProtect(target, size, old_protection, &ignored)) {
                return ExecutableWriteResult::success;
            }

            const bool restored_bytes = CopyBytes(target, expected, size)
                && FlushInstructionCache(GetCurrentProcess(), target, size) != FALSE;
            const bool restored_protection = VirtualProtect(target, size, old_protection, &ignored) != FALSE;
            return restored_bytes && restored_protection
                ? ExecutableWriteResult::clean_failure
                : ExecutableWriteResult::indeterminate;
        }

        [[noreturn]] void AbortIndeterminateCodePatch() noexcept
        {
            TerminateProcess(GetCurrentProcess(), ERROR_OPERATION_ABORTED);
            std::terminate();
        }

        bool EnsureMinHookInitialized()
        {
            if (minhook_initialized) return true;
            const MH_STATUS status = MH_Initialize();
            if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return false;
            minhook_initialized = true;
            return true;
        }

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

    void InstallHeapHook(const uint8_t *expected, size_t size, RawHook *installed)
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
            if (!InstallRawHook(Map::base_addr + 0x006babee, trampoline, expected, size, installed)) {
                throw std::runtime_error("could not install heap hook");
            }
            // Retain this process-lifetime allocation: rollback cannot prove that no thread is still traversing it.
        } catch (...) {
            DWORD ignored;
            VirtualProtect(trampoline, sizeof(trampoline_template), old_protect, &ignored);
            VirtualFree(trampoline, 0, MEM_RELEASE);
            throw;
        }
    }

    ScopedThreadQuiescence::ScopedThreadQuiescence()
    {
        const DWORD process_id = GetCurrentProcessId();
        const DWORD current_thread_id = GetCurrentThreadId();
        for (size_t attempt = 0; attempt < 8; ++attempt) {
            ThreadSnapshot snapshot;
            if (!CaptureProcessThreads(process_id, current_thread_id, &snapshot)) {
                error_ = "could not enumerate threads before patching code";
                if (!Release()) AbortIndeterminateCodePatch();
                return;
            }
            for (size_t snapshot_index = 0; snapshot_index < snapshot.count; ++snapshot_index) {
                const DWORD thread_id = snapshot.ids[snapshot_index];
                bool already_suspended = false;
                if (!ContainsLiveSuspendedThread(thread_id, &already_suspended)) {
                    if (!Release()) AbortIndeterminateCodePatch();
                    return;
                }
                if (already_suspended) continue;
                if (thread_count_ == threads_.size()) {
                    error_ = "too many game threads to quiesce before patching code";
                    if (!Release()) AbortIndeterminateCodePatch();
                    return;
                }
                HANDLE thread = OpenThread(SYNCHRONIZE | THREAD_SUSPEND_RESUME | THREAD_QUERY_LIMITED_INFORMATION
                                               | THREAD_GET_CONTEXT,
                                           FALSE, thread_id);
                if (thread == nullptr) {
                    if (GetLastError() != ERROR_INVALID_PARAMETER) {
                        error_ = "could not open a game thread before patching code";
                        if (!Release()) AbortIndeterminateCodePatch();
                        return;
                    }
                    continue;
                }
                if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
                    CloseHandle(thread);
                    error_ = "could not suspend a game thread before patching code";
                    if (!Release()) AbortIndeterminateCodePatch();
                    return;
                }
                threads_[thread_count_] = thread;
                thread_ids_[thread_count_] = thread_id;
                ++thread_count_;
                ++suspended_count_;
            }

            ThreadSnapshot verification;
            if (!CaptureProcessThreads(process_id, current_thread_id, &verification)) {
                error_ = "could not verify suspended threads before patching code";
                if (!Release()) AbortIndeterminateCodePatch();
                return;
            }
            bool stable = true;
            for (size_t snapshot_index = 0; snapshot_index < verification.count; ++snapshot_index) {
                bool found = false;
                if (!ContainsLiveSuspendedThread(verification.ids[snapshot_index], &found)) {
                    if (!Release()) AbortIndeterminateCodePatch();
                    return;
                }
                if (!found) {
                    stable = false;
                    break;
                }
            }
            if (stable) {
                ready_ = true;
                return;
            }
        }
        error_ = "game threads did not stabilize before patching code";
        if (!Release()) AbortIndeterminateCodePatch();
    }

    ScopedThreadQuiescence::~ScopedThreadQuiescence()
    {
        if (!Release()) {
            TerminateProcess(GetCurrentProcess(), ERROR_OPERATION_ABORTED);
        }
    }

    bool ScopedThreadQuiescence::AnyInstructionPointerIn(uintptr_t address, size_t size, bool *found) noexcept
    {
        size_t count = 0;
        if (!InstructionPointerCountIn(address, size, &count)) return false;
        if (found == nullptr) {
            error_ = "invalid suspended-thread instruction result";
            return false;
        }
        *found = count != 0;
        return true;
    }

    bool ScopedThreadQuiescence::InstructionPointerCountIn(uintptr_t address, size_t size, size_t *count) noexcept
    {
        if (!ready_ || count == nullptr || size == 0 || address > UINTPTR_MAX - size) {
            error_ = "invalid suspended-thread instruction range";
            return false;
        }
        *count = 0;
        for (size_t index = 0; index < suspended_count_; ++index) {
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL;
            if (!GetThreadContext(threads_[index], &context)) {
                if (WaitForSingleObject(threads_[index], 0) == WAIT_OBJECT_0) continue;
                error_ = "could not inspect a suspended game thread before patching code";
                return false;
            }
#if defined(_M_IX86)
            const uintptr_t instruction_pointer = context.Eip;
#else
#error Raw hooks require the Win32 x86 build.
#endif
            if (instruction_pointer >= address && instruction_pointer < address + size) ++*count;
        }
        return true;
    }

    bool ScopedThreadQuiescence::Release() noexcept
    {
        const size_t old_suspended_count = suspended_count_;
        const size_t old_thread_count = thread_count_;
        size_t failed_count = 0;
        for (size_t index = 0; index < old_suspended_count; ++index) {
            const HANDLE thread = threads_[index];
            if (ResumeThread(thread) != static_cast<DWORD>(-1)
                || WaitForSingleObject(thread, 0) == WAIT_OBJECT_0) {
                CloseHandle(thread);
            } else {
                threads_[failed_count++] = thread;
                thread_ids_[failed_count - 1] = thread_ids_[index];
            }
        }
        for (size_t index = old_suspended_count; index < old_thread_count; ++index) CloseHandle(threads_[index]);
        thread_count_ = failed_count;
        suspended_count_ = failed_count;
        ready_ = false;
        if (failed_count != 0) error_ = "could not resume a game thread after patching code";
        return failed_count == 0;
    }

    void ScopedThreadQuiescence::CloseThreads() noexcept
    {
        for (size_t index = 0; index < thread_count_; ++index) CloseHandle(threads_[index]);
        thread_count_ = 0;
        suspended_count_ = 0;
    }

    bool ScopedThreadQuiescence::ContainsLiveSuspendedThread(DWORD thread_id, bool *found) noexcept
    {
        if (found == nullptr) return false;
        *found = false;
        size_t index = 0;
        while (index < thread_count_) {
            if (thread_ids_[index] != thread_id) {
                ++index;
                continue;
            }
            const DWORD wait = WaitForSingleObject(threads_[index], 0);
            if (wait == WAIT_TIMEOUT) {
                *found = true;
                return true;
            }
            if (wait != WAIT_OBJECT_0) {
                error_ = "could not verify a suspended game thread before patching code";
                return false;
            }
            CloseHandle(threads_[index]);
            --thread_count_;
            --suspended_count_;
            threads_[index] = threads_[thread_count_];
            thread_ids_[index] = thread_ids_[thread_count_];
        }
        return true;
    }

    bool MatchesReadableBytes(uintptr_t address, const void *expected, size_t size) noexcept
    {
        if (address == 0 || expected == nullptr || size == 0
            || size > (std::numeric_limits<uintptr_t>::max)() - address) return false;
        const uintptr_t end = address + size;
        uintptr_t cursor = address;
        while (cursor < end) {
            MEMORY_BASIC_INFORMATION region{};
            if (VirtualQuery(reinterpret_cast<const void *>(cursor), &region, sizeof(region)) != sizeof(region)
                || region.State != MEM_COMMIT || !IsReadableProtection(region.Protect)) return false;
            const uintptr_t region_base = reinterpret_cast<uintptr_t>(region.BaseAddress);
            if (region.RegionSize > (std::numeric_limits<uintptr_t>::max)() - region_base) return false;
            const uintptr_t region_end = region_base + region.RegionSize;
            if (region_end <= cursor) return false;
            cursor = (std::min)(region_end, end);
        }
        __try {
            return std::memcmp(reinterpret_cast<const void *>(address), expected, size) == 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool InstallRawHook(uintptr_t address, void *destination, const uint8_t *expected,
                        size_t size, RawHook *installed)
    {
        if (address == 0 || destination == nullptr || expected == nullptr || installed == nullptr
            || installed->address != 0 || !installed->original.empty() || !installed->replacement.empty()
            || size < 5 || size > max_registered_code_patch_bytes) return false;
        const int64_t displacement = static_cast<int64_t>(reinterpret_cast<uintptr_t>(destination))
            - static_cast<int64_t>(address) - 5;
        if (displacement < (std::numeric_limits<int32_t>::min)()
            || displacement > (std::numeric_limits<int32_t>::max)()) return false;

        RawHook candidate;
        candidate.address = address;
        candidate.original.assign(expected, expected + size);
        candidate.replacement.assign(size, 0x90);
        candidate.replacement[0] = 0xe9;
        const int32_t relative_jump = static_cast<int32_t>(displacement);
        std::memcpy(candidate.replacement.data() + 1, &relative_jump, sizeof(relative_jump));

        std::lock_guard lock(raw_hook_mutex);
        ScopedThreadQuiescence quiescence;
        bool instruction_pointer_in_target = false;
        if (!quiescence
            || !quiescence.AnyInstructionPointerIn(address, size, &instruction_pointer_in_target)
            || instruction_pointer_in_target) return false;
        const ExecutableWriteResult result = WriteExecutableBytes(
            address, candidate.original.data(), candidate.replacement.data(), size);
        if (result == ExecutableWriteResult::indeterminate) AbortIndeterminateCodePatch();
        if (result != ExecutableWriteResult::success) return false;
        if (!quiescence.Release()) AbortIndeterminateCodePatch();
        *installed = std::move(candidate);
        return true;
    }

    bool RestoreRawHook(RawHook *hook) noexcept
    {
        if (hook == nullptr || hook->address == 0 || hook->original.size() < 5
            || hook->original.size() != hook->replacement.size()) return false;
        std::lock_guard lock(raw_hook_mutex);
        ScopedThreadQuiescence quiescence;
        bool instruction_pointer_in_target = false;
        if (!quiescence
            || !quiescence.AnyInstructionPointerIn(
                hook->address, hook->replacement.size(), &instruction_pointer_in_target)
            || instruction_pointer_in_target) return false;
        const ExecutableWriteResult result = WriteExecutableBytes(
            hook->address, hook->replacement.data(), hook->original.data(), hook->original.size());
        if (result == ExecutableWriteResult::indeterminate) AbortIndeterminateCodePatch();
        if (result != ExecutableWriteResult::success) return false;
        if (!quiescence.Release()) AbortIndeterminateCodePatch();
        *hook = {};
        return true;
    }

    bool InstallDetour(uintptr_t addr, void *detour, void **original)
    {
        if (addr == 0 || detour == nullptr || original == nullptr) return false;
        std::lock_guard lock(hook_mutex);
        if (!EnsureMinHookInitialized()) throw std::runtime_error("could not initialize MinHook");
        auto *target = reinterpret_cast<void *>(addr);
        if (MH_CreateHook(target, detour, original) != MH_OK) return false;
        if (MH_EnableHook(target) == MH_OK) return true;
        MH_RemoveHook(target);
        *original = nullptr;
        return false;
    }

    bool RemoveDetour(uintptr_t addr) noexcept
    {
        if (addr == 0) return false;
        std::lock_guard lock(hook_mutex);
        auto *target = reinterpret_cast<void *>(addr);
        const MH_STATUS disabled = MH_DisableHook(target);
        if (disabled != MH_OK && disabled != MH_ERROR_DISABLED) return false;
        return MH_RemoveHook(target) == MH_OK;
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
