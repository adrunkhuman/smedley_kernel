#include "pop_cash_flow_hook.hpp"
#include "hook_patch.hpp"

#include <smedley/memory.hpp>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace telemetry_plugin
{
    namespace
    {
        constexpr uintptr_t give_money_rva = 0x0055a5f0;
        constexpr size_t slot_capacity = 131072;
        constexpr std::array<uint8_t, 10> original_bytes{
            0x55, 0x8b, 0xec, 0x83, 0xb8, 0x84, 0x01, 0x00, 0x00, 0x00};

        struct Slot
        {
            PopCashFlowHookRecord record;
            bool used = false;
        };

        struct Buffer
        {
            std::array<Slot, slot_capacity> slots{};
            std::array<uint32_t, slot_capacity> used_indices{};
            uint32_t used_count = 0;
            PopCashFlowHookStats stats{};
        };

        std::unique_ptr<Buffer> buffers[2];
        uint32_t active_buffer = 0;
        std::atomic_flag buffer_lock = ATOMIC_FLAG_INIT;
        uintptr_t give_money_trampoline = 0;
        bool installed = false;
        bool poisoned = false;
        volatile LONG active = 0;
        volatile LONG in_flight = 0;

        class Lock final
        {
        public:
            Lock()
            {
                while (buffer_lock.test_and_set(std::memory_order_acquire)) YieldProcessor();
            }
            ~Lock() { buffer_lock.clear(std::memory_order_release); }
        };

        bool AddChecked(int64_t value, int64_t *sum)
        {
            if ((value > 0 && *sum > (std::numeric_limits<int64_t>::max)() - value)
                || (value < 0 && *sum < (std::numeric_limits<int64_t>::min)() - value)) return false;
            *sum += value;
            return true;
        }

        void __cdecl CapturePopCashFlow(const void *pop, int32_t index, int64_t amount,
                                         int64_t before, int64_t after) noexcept
        {
            if (InterlockedCompareExchange(&active, 0, 0) == 0) return;
            Lock lock;
            auto &buffer = *buffers[active_buffer];
            ++buffer.stats.calls;
            if (pop == nullptr || index < 0 || index >= static_cast<int32_t>(pop_cash_flow_component_count)) {
                ++buffer.stats.invalid_index;
                return;
            }
            if ((after >= before && before > (std::numeric_limits<int64_t>::max)() - (after - before))
                || (after < before && before < (std::numeric_limits<int64_t>::min)() - (after - before))) {
                ++buffer.stats.overflow;
                return;
            }
            const int64_t money_delta = after - before;
            size_t slot_index = (reinterpret_cast<uintptr_t>(pop) >> 4) & (slot_capacity - 1);
            Slot *slot = nullptr;
            for (size_t attempt = 0; attempt < slot_capacity; ++attempt) {
                auto &candidate = buffer.slots[slot_index];
                if (!candidate.used) {
                    if (buffer.used_count == buffer.used_indices.size()) break;
                    candidate.used = true;
                    candidate.record.pop = pop;
                    buffer.used_indices[buffer.used_count++] = static_cast<uint32_t>(slot_index);
                    slot = &candidate;
                    break;
                }
                if (candidate.record.pop == pop) {
                    slot = &candidate;
                    break;
                }
                slot_index = (slot_index + 1) & (slot_capacity - 1);
            }
            if (slot == nullptr) {
                ++buffer.stats.table_full;
                return;
            }
            if (!AddChecked(amount, &slot->record.posted_raw[static_cast<size_t>(index)])
                || !AddChecked(money_delta, &slot->record.money_delta_raw[static_cast<size_t>(index)])) {
                ++buffer.stats.overflow;
                return;
            }
            ++slot->record.call_count;
            if (amount != money_delta) ++slot->record.clamped_call_count;
        }

        __declspec(naked) void GiveMoneyHook()
        {
            __asm {
                lock inc dword ptr [in_flight]
                push ebp
                mov ebp, esp
                sub esp, 16
                mov dword ptr [ebp - 16], eax
                mov dword ptr [ebp - 12], esi
                mov ecx, dword ptr [eax + 180h]
                mov dword ptr [ebp - 8], ecx
                mov ecx, dword ptr [eax + 184h]
                mov dword ptr [ebp - 4], ecx
                push dword ptr [ebp + 0ch]
                push dword ptr [ebp + 8]
                call dword ptr [give_money_trampoline]
                pushfd
                pushad
                mov eax, dword ptr [ebp - 16]
                push dword ptr [eax + 184h]
                push dword ptr [eax + 180h]
                push dword ptr [ebp - 4]
                push dword ptr [ebp - 8]
                push dword ptr [ebp + 0ch]
                push dword ptr [ebp + 8]
                push dword ptr [ebp - 12]
                push eax
                call CapturePopCashFlow
                add esp, 32
                popad
                lock dec dword ptr [in_flight]
                popfd
                mov esp, ebp
                pop ebp
                ret 8
            }
        }

        bool JumpBytes(uintptr_t source, const void *target, std::array<uint8_t, 10> *bytes)
        {
            const intptr_t displacement = reinterpret_cast<intptr_t>(target) - static_cast<intptr_t>(source + 5);
            if (displacement < (std::numeric_limits<int32_t>::min)()
                || displacement > (std::numeric_limits<int32_t>::max)()) return false;
            bytes->fill(0x90);
            (*bytes)[0] = 0xe9;
            const int32_t relative = static_cast<int32_t>(displacement);
            std::memcpy(bytes->data() + 1, &relative, sizeof(relative));
            return true;
        }

        bool CurrentModuleRange(uintptr_t *base, size_t *size, std::string *error)
        {
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                    | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&GiveMoneyHook), &module)) {
                *error = "cannot identify telemetry code while draining the POP cash-flow hook";
                return false;
            }
            const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(module);
            const auto *nt = dos->e_magic == IMAGE_DOS_SIGNATURE
                ? reinterpret_cast<const IMAGE_NT_HEADERS *>(
                    reinterpret_cast<uintptr_t>(module) + dos->e_lfanew)
                : nullptr;
            if (nt == nullptr || nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.SizeOfImage == 0) {
                *error = "telemetry module headers are invalid while draining the POP cash-flow hook";
                return false;
            }
            *base = reinterpret_cast<uintptr_t>(module);
            *size = nt->OptionalHeader.SizeOfImage;
            return true;
        }

        bool WaitForTelemetryThreads(std::string *error)
        {
            uintptr_t module_base = 0;
            size_t module_size = 0;
            if (!CurrentModuleRange(&module_base, &module_size, error)) return false;
            const ULONGLONG deadline = GetTickCount64() + 5000;
            while (true) {
                ScopedThreadQuiescence quiescence(error);
                if (!quiescence) return false;
                bool thread_in_module = false;
                if (!quiescence.AnyInstructionPointerIn(
                        module_base, module_size, &thread_in_module, error)) return false;
                std::string resume_error;
                if (!quiescence.Release(&resume_error)) {
                    *error = resume_error;
                    return false;
                }
                if (!thread_in_module) return true;
                if (GetTickCount64() >= deadline) {
                    *error = "telemetry threads did not leave the module after removing the POP cash-flow hook";
                    return false;
                }
                Sleep(0);
            }
        }

        bool WriteBytes(uintptr_t address, const uint8_t *expected, const uint8_t *replacement,
                        size_t size, std::string *error)
        {
            if (std::memcmp(reinterpret_cast<const void *>(address), expected, size) != 0) {
                *error = "POP cash-flow hook bytes do not match the supported executable";
                return false;
            }
            DWORD old_protection = 0;
            if (!VirtualProtect(reinterpret_cast<void *>(address), size, PAGE_EXECUTE_READWRITE, &old_protection)) {
                *error = "cannot make the POP cash-flow hook writable";
                return false;
            }
            std::memcpy(reinterpret_cast<void *>(address), replacement, size);
            const bool flushed = FlushInstructionCache(
                GetCurrentProcess(), reinterpret_cast<const void *>(address), size) != FALSE;
            DWORD ignored = 0;
            if (!flushed || !VirtualProtect(reinterpret_cast<void *>(address), size, old_protection, &ignored)) {
                std::memcpy(reinterpret_cast<void *>(address), expected, size);
                const bool rollback_flushed = FlushInstructionCache(
                    GetCurrentProcess(), reinterpret_cast<const void *>(address), size) != FALSE;
                DWORD rollback_ignored = 0;
                const bool rollback_protected = VirtualProtect(
                    reinterpret_cast<void *>(address), size, old_protection, &rollback_ignored) != FALSE;
                if (!rollback_flushed || !rollback_protected) {
                    TerminateProcess(GetCurrentProcess(), ERROR_OPERATION_ABORTED);
                }
                *error = flushed ? "cannot restore POP cash-flow hook protection; bytes rolled back"
                                 : "cannot flush the POP cash-flow hook; bytes rolled back";
                return false;
            }
            return true;
        }

        bool BuildTrampoline(uintptr_t target, std::string *error)
        {
            auto *memory = static_cast<uint8_t *>(VirtualAlloc(nullptr, 15, MEM_COMMIT | MEM_RESERVE,
                PAGE_EXECUTE_READWRITE));
            if (memory == nullptr) { *error = "cannot allocate the POP cash-flow trampoline"; return false; }
            std::memcpy(memory, original_bytes.data(), original_bytes.size());
            memory[10] = 0xe9;
            const intptr_t displacement = static_cast<intptr_t>(target + 10)
                - reinterpret_cast<intptr_t>(memory + 15);
            if (displacement < (std::numeric_limits<int32_t>::min)()
                || displacement > (std::numeric_limits<int32_t>::max)()) {
                VirtualFree(memory, 0, MEM_RELEASE);
                *error = "POP cash-flow trampoline target is out of range";
                return false;
            }
            const int32_t relative = static_cast<int32_t>(displacement);
            std::memcpy(memory + 11, &relative, sizeof(relative));
            if (!FlushInstructionCache(GetCurrentProcess(), memory, 15)) {
                VirtualFree(memory, 0, MEM_RELEASE);
                *error = "cannot flush the POP cash-flow trampoline";
                return false;
            }
            DWORD old_protection = 0;
            if (!VirtualProtect(memory, 15, PAGE_EXECUTE_READ, &old_protection)) {
                VirtualFree(memory, 0, MEM_RELEASE);
                *error = "cannot protect the POP cash-flow trampoline";
                return false;
            }
            give_money_trampoline = reinterpret_cast<uintptr_t>(memory);
            return true;
        }

    }

    bool InstallPopCashFlowHook(std::string *error)
    {
        if (error == nullptr) return false;
        if (installed || poisoned) {
            *error = "POP cash-flow hook is already installed or poisoned";
            return false;
        }
        const uintptr_t target = smedley::memory::Map::base_addr + give_money_rva;
        if (smedley::memory::Map::base_addr == 0) { *error = "game module is unavailable"; return false; }
        buffers[0] = std::unique_ptr<Buffer>(new (std::nothrow) Buffer{});
        buffers[1] = std::unique_ptr<Buffer>(new (std::nothrow) Buffer{});
        if (!buffers[0] || !buffers[1]) { *error = "cannot allocate POP cash-flow buffers"; return false; }
        std::array<uint8_t, 10> hook{};
        if (!JumpBytes(target, reinterpret_cast<const void *>(&GiveMoneyHook), &hook)
            || !BuildTrampoline(target, error)) {
            if (error->empty()) *error = "POP cash-flow hook target is out of range";
            buffers[0].reset();
            buffers[1].reset();
            return false;
        }
        ScopedThreadQuiescence quiescence(error);
        if (!quiescence) return false;
        bool thread_in_patch = false;
        if (!quiescence.AnyInstructionPointerIn(target, original_bytes.size(), &thread_in_patch, error)
            || thread_in_patch) {
            if (thread_in_patch) *error = "a game thread is executing the POP cash-flow patch site";
            return false;
        }
        if (!WriteBytes(target, original_bytes.data(), hook.data(), hook.size(), error)) {
            std::string ignored;
            (void)quiescence.Release(&ignored);
            return false;
        }
        std::string resume_error;
        if (!quiescence.Release(&resume_error)) {
            poisoned = true;
            installed = true;
            *error += "; " + resume_error;
            return false;
        }
        active_buffer = 0;
        installed = true;
        InterlockedExchange(&active, 1);
        return true;
    }

    bool UninstallPopCashFlowHook(std::string *error)
    {
        if (error == nullptr) return false;
        if (!installed) return true;
        InterlockedExchange(&active, 0);
        const uintptr_t target = smedley::memory::Map::base_addr + give_money_rva;
        std::array<uint8_t, 10> hook{};
        if (!JumpBytes(target, reinterpret_cast<const void *>(&GiveMoneyHook), &hook)) {
            *error = "POP cash-flow hook target is out of range";
            return false;
        }
        ScopedThreadQuiescence quiescence(error);
        if (!quiescence) return false;
        bool thread_in_patch = false;
        if (!quiescence.AnyInstructionPointerIn(target + 1, original_bytes.size() - 1, &thread_in_patch, error)
            || thread_in_patch) {
            if (thread_in_patch) *error = "a game thread is executing the POP cash-flow patch site";
            return false;
        }
        if (!WriteBytes(target, hook.data(), original_bytes.data(), hook.size(), error)) {
            std::string ignored;
            (void)quiescence.Release(&ignored);
            return false;
        }
        std::string resume_error;
        if (!quiescence.Release(&resume_error)) {
            poisoned = true;
            *error += "; " + resume_error;
            return false;
        }
        const ULONGLONG drain_deadline = GetTickCount64() + 5000;
        while (InterlockedCompareExchange(&in_flight, 0, 0) != 0) {
            if (GetTickCount64() >= drain_deadline) {
                poisoned = true;
                *error = "POP cash-flow hook invocations did not drain after uninstall";
                TerminateProcess(GetCurrentProcess(), ERROR_OPERATION_ABORTED);
                return false;
            }
            Sleep(0);
        }
        if (!WaitForTelemetryThreads(error)) {
            poisoned = true;
            TerminateProcess(GetCurrentProcess(), ERROR_OPERATION_ABORTED);
            return false;
        }
        if (give_money_trampoline != 0) {
            VirtualFree(reinterpret_cast<void *>(give_money_trampoline), 0, MEM_RELEASE);
            give_money_trampoline = 0;
        }
        buffers[0].reset();
        buffers[1].reset();
        installed = false;
        return true;
    }

    bool DrainPopCashFlowHook(PopCashFlowHookRecord *records, size_t capacity,
                              uint32_t *count, PopCashFlowHookStats *stats)
    {
        if (records == nullptr || count == nullptr || stats == nullptr || !installed) return false;
        uint32_t drained_buffer = 0;
        {
            Lock lock;
            drained_buffer = active_buffer;
            active_buffer ^= 1;
        }
        auto &buffer = *buffers[drained_buffer];
        *count = 0;
        *stats = buffer.stats;
        for (uint32_t index = 0; index < buffer.used_count; ++index) {
            auto &slot = buffer.slots[buffer.used_indices[index]];
            if (*count < capacity) records[(*count)++] = slot.record;
            else ++stats->output_overflow;
            slot = {};
        }
        buffer.used_count = 0;
        buffer.stats = {};
        return true;
    }
}
