#include "factory_sales_hook.hpp"
#include "hook_patch.hpp"

#include <smedley/memory.hpp>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstring>
#include <limits>

namespace telemetry_plugin
{
    namespace
    {
        constexpr uintptr_t settlement_call_rva = 0x00088710;
        constexpr uintptr_t settlement_rva = 0x000f4b30;
        constexpr size_t queue_capacity = max_factory_sales_records + 1;
        constexpr std::array<uint8_t, 5> settlement_original{0xe8, 0x1b, 0xc4, 0x06, 0x00};

        std::array<FactorySalesHookRecord, queue_capacity> queue;
        std::atomic<uint32_t> queue_write{0};
        std::atomic<uint32_t> queue_read{0};
        std::atomic<uint64_t> queue_dropped{0};
        uintptr_t settlement = 0;
        bool installed = false;
        bool poisoned = false;
        volatile LONG active = 0;

        void __cdecl CaptureSettlement(const void *factory, const uint8_t *call_stack) noexcept
        {
            if (InterlockedCompareExchange(&active, 0, 0) == 0) return;
            FactorySalesHookRecord record{};
            if (factory == nullptr || call_stack == nullptr) {
                queue_dropped.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            __try {
                record.factory = factory;
                std::memcpy(&record.proceeds_raw, call_stack + 0x0c, sizeof(int64_t));
                const auto *factory_bytes = static_cast<const uint8_t *>(factory);
                std::memcpy(&record.produced_raw, factory_bytes + 0x00d8, sizeof(int64_t));
                std::memcpy(&record.opening_inventory_raw, call_stack + 0x00c4, sizeof(int64_t));
                std::memcpy(&record.closing_inventory_raw, factory_bytes + 0x01f8, sizeof(int64_t));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                queue_dropped.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            const uint32_t write = queue_write.load(std::memory_order_relaxed);
            const uint32_t next = (write + 1) % queue_capacity;
            if (next == queue_read.load(std::memory_order_acquire)) {
                queue_dropped.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            queue[write] = record;
            queue_write.store(next, std::memory_order_release);
        }

        __declspec(naked) void SettlementHook()
        {
            __asm {
                mov eax, dword ptr [esp + 4]
                mov edx, esp
                pushfd
                pushad
                push edx
                push eax
                call CaptureSettlement
                add esp, 8
                popad
                popfd
                jmp dword ptr [settlement]
            }
        }

        bool CallBytes(uintptr_t callsite, const void *target, std::array<uint8_t, 5> *bytes)
        {
            const intptr_t displacement = reinterpret_cast<intptr_t>(target) - static_cast<intptr_t>(callsite + 5);
            if (displacement < (std::numeric_limits<int32_t>::min)()
                || displacement > (std::numeric_limits<int32_t>::max)()) return false;
            (*bytes)[0] = 0xe8;
            const int32_t relative = static_cast<int32_t>(displacement);
            std::memcpy(bytes->data() + 1, &relative, sizeof(relative));
            return true;
        }

        bool WriteBytes(uintptr_t address, const std::array<uint8_t, 5> &expected,
                        const std::array<uint8_t, 5> &replacement, std::string *error)
        {
            if (std::memcmp(reinterpret_cast<const void *>(address), expected.data(), expected.size()) != 0) {
                *error = "factory sales hook bytes do not match the supported executable";
                return false;
            }
            DWORD old_protection = 0;
            if (!VirtualProtect(reinterpret_cast<void *>(address), replacement.size(), PAGE_EXECUTE_READWRITE,
                    &old_protection)) {
                *error = "cannot make the factory sales callsite writable";
                return false;
            }
            std::memcpy(reinterpret_cast<void *>(address), replacement.data(), replacement.size());
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void *>(address), replacement.size());
            DWORD ignored = 0;
            if (!VirtualProtect(reinterpret_cast<void *>(address), replacement.size(), old_protection, &ignored)) {
                std::memcpy(reinterpret_cast<void *>(address), expected.data(), expected.size());
                FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void *>(address), expected.size());
                (void)VirtualProtect(reinterpret_cast<void *>(address), expected.size(), old_protection, &ignored);
                *error = "cannot restore factory sales callsite protection";
                return false;
            }
            return true;
        }
    }

    bool InstallFactorySalesHook(std::string *error)
    {
        if (error == nullptr) return false;
        if (installed || poisoned) {
            *error = "factory sales hook is already installed or has an unrecoverable patch state";
            return false;
        }
        const uintptr_t base = smedley::memory::Map::base_addr;
        if (base == 0) { *error = "game module is unavailable"; return false; }
        const uintptr_t callsite = base + settlement_call_rva;
        settlement = base + settlement_rva;
        std::array<uint8_t, 5> hook{};
        if (!CallBytes(callsite, reinterpret_cast<const void *>(&SettlementHook), &hook)) {
            *error = "factory sales hook target is out of range";
            return false;
        }
        ScopedThreadQuiescence quiescence(error);
        if (!quiescence) return false;
        const auto release_threads = [&] {
            std::string resume_error;
            if (quiescence.Release(&resume_error)) return true;
            poisoned = true;
            *error += "; " + resume_error;
            return false;
        };
        if (!WriteBytes(callsite, settlement_original, hook, error)) {
            (void)release_threads();
            return false;
        }
        queue_write.store(0, std::memory_order_relaxed);
        queue_read.store(0, std::memory_order_relaxed);
        queue_dropped.store(0, std::memory_order_relaxed);
        installed = true;
        if (!release_threads()) return false;
        InterlockedExchange(&active, 1);
        return true;
    }

    bool UninstallFactorySalesHook(std::string *error)
    {
        if (error == nullptr) return false;
        if (!installed) return true;
        InterlockedExchange(&active, 0);
        const uintptr_t callsite = smedley::memory::Map::base_addr + settlement_call_rva;
        std::array<uint8_t, 5> hook{};
        if (!CallBytes(callsite, reinterpret_cast<const void *>(&SettlementHook), &hook)) {
            *error = "factory sales hook target is out of range";
            return false;
        }
        ScopedThreadQuiescence quiescence(error);
        if (!quiescence) return false;
        if (!WriteBytes(callsite, hook, settlement_original, error)) {
            std::string resume_error;
            (void)quiescence.Release(&resume_error);
            return false;
        }
        std::string resume_error;
        if (!quiescence.Release(&resume_error)) {
            poisoned = true;
            *error = resume_error;
            return false;
        }
        installed = false;
        return true;
    }

    bool DrainFactorySalesHook(FactorySalesHookRecord *records, size_t capacity,
                               uint32_t *count, uint64_t *dropped)
    {
        if (records == nullptr || count == nullptr || dropped == nullptr) return false;
        *count = 0;
        *dropped = queue_dropped.exchange(0, std::memory_order_acq_rel);
        uint32_t read = queue_read.load(std::memory_order_relaxed);
        const uint32_t write = queue_write.load(std::memory_order_acquire);
        while (read != write) {
            if (*count >= capacity) ++*dropped;
            else records[(*count)++] = queue[read];
            read = (read + 1) % queue_capacity;
        }
        queue_read.store(read, std::memory_order_release);
        return true;
    }
}
