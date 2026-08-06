#include "hooks.hpp"
#include "events/console.hpp"
#include "events/bankinterest.hpp"
#include "events/dailyupdate.hpp"
#include "events/dailyinterest.hpp"
#include "memory.hpp"

#include <cstring>
#include <exception>
#include <stdexcept>
#include <vector>

namespace smedley
{
    template <size_t Size>
    void RequireSignature(uintptr_t rva, const uint8_t (&expected)[Size])
    {
        const auto address = memory::Map::base_addr + rva;
        if (std::memcmp(reinterpret_cast<const void *>(address), expected, Size) != 0) {
            throw std::runtime_error("unsupported v2game.exe hook signature");
        }
    }

    void InstallHooks()
    {
        using namespace events;

        constexpr uint8_t console[] = {0x5f, 0x5e, 0x5b, 0x8b, 0xe5};
        constexpr uint8_t daily[] = {0x53, 0x8b, 0x5d, 0x08, 0x8a, 0x83, 0xbc, 0x15, 0x00, 0x00};
        constexpr uint8_t daily_interest[] = {0xe8, 0xed, 0xae, 0x01, 0x00};
        constexpr uint8_t bank_interest[] = {0xe8, 0xdc, 0xfc, 0xe6, 0xff};
        uint8_t heap[] = {0xa3, 0, 0, 0, 0, 0x8b, 0xc1, 0xc3};
        const auto game_heap = memory::Map::base_addr + 0xb202e8;
        std::memcpy(heap + 1, &game_heap, sizeof(uint32_t));

        RequireSignature(0x00023a43, console);
        RequireSignature(0x001085ae, daily);
        RequireSignature(0x00108d3e, daily_interest);
        RequireSignature(0x00285f0f, bank_interest);
        RequireSignature(0x006babee, heap);

        struct HookBackup
        {
            uintptr_t address;
            std::vector<uint8_t> instructions;
        };
        std::vector<HookBackup> installed;
        installed.reserve(5);
        auto prepare = [&installed](uintptr_t rva, const auto &expected) {
            installed.push_back({memory::Map::base_addr + rva,
                std::vector<uint8_t>(std::begin(expected), std::end(expected))});
        };

        try {
            prepare(0x006babee, heap);
            memory::InstallHeapHook();
            prepare(0x00023a43, console);
            ConsoleCmdManagerInitEvent::InstallHook();
            prepare(0x001085ae, daily);
            DailyUpdateEvent::InstallHook();
            prepare(0x00108d3e, daily_interest);
            DailyInterestEvent::InstallHook();
            prepare(0x00285f0f, bank_interest);
            BankInterestEvent::InstallHook();
        } catch (...) {
            const std::exception_ptr failure = std::current_exception();
            bool restored = true;
            for (auto hook = installed.rbegin(); hook != installed.rend(); ++hook) {
                restored = memory::RestoreHook(hook->address, hook->instructions) && restored;
            }
            if (!restored) throw std::runtime_error("hook installation failed and rollback was incomplete");
            std::rethrow_exception(failure);
        }
    }

}
