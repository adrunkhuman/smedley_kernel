#include "factory_consumption_hook.hpp"
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
        constexpr uintptr_t settlement_call_rva = 0x000845fd;
        constexpr uintptr_t first_pool_add_rva = 0x0008369d;
        constexpr uintptr_t second_pool_add_rva = 0x000836aa;
        constexpr uintptr_t goods_pool_add_rva = 0x0007dc20;
        constexpr uintptr_t settlement_rva = 0x00082ff0;
        constexpr uintptr_t loaded_goods_count_rva = 0x00e587f4;
        constexpr size_t queue_capacity = max_factory_flow_records + 1;
        constexpr std::array<uint8_t, 5> settlement_original{0xe8, 0xee, 0xe9, 0xff, 0xff};
        constexpr std::array<uint8_t, 5> first_original{0xe8, 0x7e, 0xa5, 0xff, 0xff};
        constexpr std::array<uint8_t, 5> second_original{0xe8, 0x71, 0xa5, 0xff, 0xff};

        struct GoodsPool
        {
            uint8_t prefix[8];
            uint8_t value_indices[64];
            const int64_t *values;
            const int64_t *values_end;
            const int64_t *values_capacity;
        };

        std::array<FactorySettlementHookRecord, queue_capacity> queue;
        std::atomic<uint32_t> queue_write{0};
        std::atomic<uint32_t> queue_read{0};
        std::atomic<uint64_t> queue_dropped{0};
        uintptr_t goods_pool_add = 0;
        uintptr_t settlement = 0;
        bool installed = false;
        bool poisoned = false;
        volatile LONG active = 0;

        bool DecodePool(const void *source, FactorySettlementHookRecord *record) noexcept
        {
            __try {
                const auto base = smedley::memory::Map::base_addr;
                const int32_t goods_count = *reinterpret_cast<const int32_t *>(base + loaded_goods_count_rva);
                if (source == nullptr || goods_count <= 0 || goods_count > 64) return false;
                const auto *pool = static_cast<const GoodsPool *>(source);
                const uintptr_t values = reinterpret_cast<uintptr_t>(pool->values);
                const uintptr_t values_end = reinterpret_cast<uintptr_t>(pool->values_end);
                if (values == 0 || values_end < values) return false;
                const uintptr_t bytes = values_end - values;
                if (bytes % sizeof(int64_t) != 0) return false;
                const size_t value_count = bytes / sizeof(int64_t);
                if (value_count == 0 || value_count > 255) return false;
                for (int32_t ordinal = 0; ordinal < goods_count; ++ordinal) {
                    const uint8_t index = pool->value_indices[ordinal];
                    if (index == 0) continue;
                    if (index >= value_count) return false;
                    record->quantity_raw[ordinal] = pool->values[index];
                }
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        void __cdecl CapturePool(const void *factory, const void *source, uint32_t pool) noexcept
        {
            if (InterlockedCompareExchange(&active, 0, 0) == 0) return;
            FactorySettlementHookRecord record{};
            if (factory == nullptr) {
                queue_dropped.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            record.factory = factory;
            record.pool = pool;
            if (!DecodePool(source, &record)) {
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
                mov eax, dword ptr [esp + 12]
                lea ecx, [eax + 28h]
                pushfd
                pushad
                push 0
                push ecx
                push eax
                call CapturePool
                add esp, 12
                popad
                popfd
                jmp dword ptr [settlement]
            }
        }

        __declspec(naked) void FirstPoolAddHook()
        {
            __asm {
                lea eax, [esi - 28h]
                pushfd
                pushad
                push 1
                push esi
                push eax
                call CapturePool
                add esp, 12
                popad
                popfd
                mov eax, dword ptr [esp + 4]
                lea ecx, [esi - 28h]
                pushfd
                pushad
                push 2
                push eax
                push ecx
                call CapturePool
                add esp, 12
                popad
                popfd
                jmp dword ptr [goods_pool_add]
            }
        }

        __declspec(naked) void SecondPoolAddHook()
        {
            __asm {
                mov eax, dword ptr [esp + 4]
                lea ecx, [esi - 28h]
                pushfd
                pushad
                push 3
                push eax
                push ecx
                call CapturePool
                add esp, 12
                popad
                popfd
                jmp dword ptr [goods_pool_add]
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
                *error = "factory consumption hook bytes do not match the supported executable";
                return false;
            }
            DWORD old_protection = 0;
            if (!VirtualProtect(reinterpret_cast<void *>(address), replacement.size(), PAGE_EXECUTE_READWRITE,
                    &old_protection)) {
                *error = "cannot make the factory consumption callsite writable";
                return false;
            }
            std::memcpy(reinterpret_cast<void *>(address), replacement.data(), replacement.size());
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void *>(address), replacement.size());
            DWORD ignored = 0;
            if (!VirtualProtect(reinterpret_cast<void *>(address), replacement.size(), old_protection, &ignored)) {
                std::memcpy(reinterpret_cast<void *>(address), expected.data(), expected.size());
                FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void *>(address), expected.size());
                (void)VirtualProtect(reinterpret_cast<void *>(address), expected.size(), old_protection, &ignored);
                *error = "cannot restore factory consumption callsite protection";
                return false;
            }
            return true;
        }
    }

    bool InstallFactoryConsumptionHook(std::string *error)
    {
        if (error == nullptr) return false;
        if (installed || poisoned) {
            *error = "factory consumption hook is already installed or has an unrecoverable patch state";
            return false;
        }
        const uintptr_t base = smedley::memory::Map::base_addr;
        if (base == 0) { *error = "game module is unavailable"; return false; }
        const uintptr_t first = base + first_pool_add_rva;
        const uintptr_t second = base + second_pool_add_rva;
        const uintptr_t settlement_call = base + settlement_call_rva;
        goods_pool_add = base + goods_pool_add_rva;
        settlement = base + settlement_rva;
        std::array<uint8_t, 5> settlement_hook{}, first_hook{}, second_hook{};
        if (!CallBytes(settlement_call, reinterpret_cast<const void *>(&SettlementHook), &settlement_hook)
            || !CallBytes(first, reinterpret_cast<const void *>(&FirstPoolAddHook), &first_hook)
            || !CallBytes(second, reinterpret_cast<const void *>(&SecondPoolAddHook), &second_hook)) {
            *error = "factory consumption hook target is out of range";
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
        if (!WriteBytes(settlement_call, settlement_original, settlement_hook, error)) {
            (void)release_threads();
            return false;
        }
        if (!WriteBytes(first, first_original, first_hook, error)) {
            std::string rollback_error;
            (void)WriteBytes(settlement_call, settlement_hook, settlement_original, &rollback_error);
            if (!rollback_error.empty()) {
                poisoned = true;
                installed = true;
                *error += "; rollback failed: " + rollback_error;
            }
            (void)release_threads();
            return false;
        }
        if (!WriteBytes(second, second_original, second_hook, error)) {
            std::string rollback_error;
            (void)WriteBytes(first, first_hook, first_original, &rollback_error);
            (void)WriteBytes(settlement_call, settlement_hook, settlement_original, &rollback_error);
            if (!rollback_error.empty()) {
                poisoned = true;
                installed = true;
                *error += "; rollback failed: " + rollback_error;
            }
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

    bool UninstallFactoryConsumptionHook(std::string *error)
    {
        if (error == nullptr) return false;
        if (!installed) return true;
        InterlockedExchange(&active, 0);
        const uintptr_t base = smedley::memory::Map::base_addr;
        const uintptr_t first = base + first_pool_add_rva;
        const uintptr_t second = base + second_pool_add_rva;
        const uintptr_t settlement_call = base + settlement_call_rva;
        std::array<uint8_t, 5> settlement_hook{}, first_hook{}, second_hook{};
        if (!CallBytes(settlement_call, reinterpret_cast<const void *>(&SettlementHook), &settlement_hook)
            || !CallBytes(first, reinterpret_cast<const void *>(&FirstPoolAddHook), &first_hook)
            || !CallBytes(second, reinterpret_cast<const void *>(&SecondPoolAddHook), &second_hook)) {
            *error = "factory consumption hook target is out of range";
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
        if (!WriteBytes(second, second_hook, second_original, error)) {
            (void)release_threads();
            return false;
        }
        if (!WriteBytes(first, first_hook, first_original, error)) {
            std::string rollback_error;
            (void)WriteBytes(second, second_original, second_hook, &rollback_error);
            if (!rollback_error.empty()) *error += "; rollback failed: " + rollback_error;
            (void)release_threads();
            return false;
        }
        if (!WriteBytes(settlement_call, settlement_hook, settlement_original, error)) {
            std::string rollback_error;
            (void)WriteBytes(first, first_original, first_hook, &rollback_error);
            (void)WriteBytes(second, second_original, second_hook, &rollback_error);
            if (!rollback_error.empty()) *error += "; rollback failed: " + rollback_error;
            (void)release_threads();
            return false;
        }
        if (!release_threads()) return false;
        installed = false;
        return true;
    }

    bool DrainFactoryConsumptionHook(FactorySettlementHookRecord *records, size_t capacity,
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
