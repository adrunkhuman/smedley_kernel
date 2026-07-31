#include "eventregistry.hpp"
#include "events/dailyinterest.hpp"
#include "memory.hpp"

#include <stdexcept>

using namespace smedley;

void DailyInterestEventHook(v2::CCountry *country, uint32_t phase)
{
    const auto boundary = phase == 0 ? events::DailyInterestPhase::BEFORE : events::DailyInterestPhase::AFTER;
    events::DailyInterestEvent event(country, boundary);
    events::DailyInterestEvent::RecordCallbackFailures(
        smedley::EventRegistry<events::DailyInterestEvent>::NotifyContained(event));
}

namespace smedley::events
{
    void __declspec(naked) DailyInterestEvent::HookTrampoline()
    {
        __asm {
            pushfd
            pushad
            push 0
            push ebx
            call DailyInterestEventHook
            add esp, 8
            popad
            popfd

            call dword ptr [pay_daily_interest_addr]

            pushfd
            pushad
            push 1
            push ebx
            call DailyInterestEventHook
            add esp, 8
            popad
            popfd

            jmp hook_ret_addr
        }
    }

    DailyInterestEvent::DailyInterestEvent(v2::CCountry *country, DailyInterestPhase phase) : Event(false),
        _country(country), _phase(phase)
    {
    }

    v2::CCountry *DailyInterestEvent::GetCountry()
    {
        return _country;
    }

    DailyInterestPhase DailyInterestEvent::GetPhase() const
    {
        return _phase;
    }

    uint64_t DailyInterestEvent::CallbackFailures() noexcept
    {
        return callback_failures_.load(std::memory_order_relaxed);
    }

    void DailyInterestEvent::RecordCallbackFailures(uint32_t failures) noexcept
    {
        callback_failures_.fetch_add(failures, std::memory_order_relaxed);
    }

    void DailyInterestEvent::InstallHook()
    {
        hook_ret_addr = memory::Map::base_addr + hook_addr + 5;
        pay_daily_interest_addr = memory::Map::base_addr + function_addr;
        if (!memory::Hook(memory::Map::base_addr + hook_addr, HookTrampoline, 5, nullptr)) {
            throw std::runtime_error("could not install daily interest boundary hook");
        }
    }
}
