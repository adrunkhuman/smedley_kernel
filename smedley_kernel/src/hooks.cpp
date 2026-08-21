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
        if (!memory::MatchesReadableBytes(address, expected, Size)) {
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

        std::vector<memory::RawHook> installed;
        installed.reserve(5);

        try {
            installed.emplace_back();
            memory::InstallHeapHook(heap, sizeof(heap), &installed.back());
            installed.emplace_back();
            ConsoleCmdManagerInitEvent::InstallHook(console, sizeof(console), &installed.back());
            installed.emplace_back();
            DailyUpdateEvent::InstallHook(daily, sizeof(daily), &installed.back());
            installed.emplace_back();
            DailyInterestEvent::InstallHook(daily_interest, sizeof(daily_interest), &installed.back());
            installed.emplace_back();
            BankInterestEvent::InstallHook(bank_interest, sizeof(bank_interest), &installed.back());
        } catch (...) {
            const std::exception_ptr failure = std::current_exception();
            bool restored = true;
            for (auto hook = installed.rbegin(); hook != installed.rend(); ++hook) {
                if (hook->address != 0) restored = memory::RestoreRawHook(&*hook) && restored;
            }
            if (!restored) throw std::runtime_error("hook installation failed and rollback was incomplete");
            std::rethrow_exception(failure);
        }
    }

}
