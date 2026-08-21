#include "eventregistry.hpp"
#include "events/dailyinterest.hpp"
#include "memory.hpp"

#include <stdexcept>

using namespace smedley;

namespace smedley::events
{
    namespace
    {
        std::atomic<uint64_t> next_dispatch_generation{1};
        thread_local uint64_t active_dispatch_generation = 0;
        thread_local const DailyInterestEvent *active_dispatch_event = nullptr;
        void DailyInterestEventHook(v2::CCountry *country, uint32_t phase);

        class DispatchScope final
        {
        public:
            explicit DispatchScope(const DailyInterestEvent &event) noexcept
                : previous_generation_(active_dispatch_generation), previous_event_(active_dispatch_event)
            {
                generation_ = next_dispatch_generation.fetch_add(1, std::memory_order_relaxed);
                if (generation_ == 0) generation_ = next_dispatch_generation.fetch_add(1, std::memory_order_relaxed);
                active_dispatch_generation = generation_;
                active_dispatch_event = &event;
            }

            ~DispatchScope()
            {
                active_dispatch_generation = previous_generation_;
                active_dispatch_event = previous_event_;
            }

            uint64_t generation() const noexcept { return generation_; }

        private:
            uint64_t generation_ = 0;
            uint64_t previous_generation_ = 0;
            const DailyInterestEvent *previous_event_ = nullptr;
        };
    }

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

    DailyInterestEvent::DailyInterestEvent(v2::CCountry *country, DailyInterestPhase phase, TrustedHookTag) : Event(false),
        _country(country), _phase(phase)
    {
    }

    uint64_t DailyInterestEvent::ActiveDispatchGeneration() noexcept
    {
        return active_dispatch_generation;
    }

    bool DailyInterestEvent::IsDispatchActive(uint64_t generation) noexcept
    {
        return generation != 0 && active_dispatch_generation == generation;
    }

    bool DailyInterestEvent::IsCurrentDispatch(const DailyInterestEvent &event, uint64_t generation) noexcept
    {
        return IsDispatchActive(generation) && active_dispatch_event == &event;
    }

    uint64_t DailyInterestEvent::CallbackFailures() noexcept
    {
        return callback_failures_.load(std::memory_order_relaxed);
    }

    void DailyInterestEvent::RecordCallbackFailures(uint32_t failures) noexcept
    {
        callback_failures_.fetch_add(failures, std::memory_order_relaxed);
    }

    void DailyInterestEvent::InstallHook(const uint8_t *expected, size_t size,
                                          memory::RawHook *installed)
    {
        hook_ret_addr = memory::Map::base_addr + hook_addr + 5;
        pay_daily_interest_addr = memory::Map::base_addr + function_addr;
        if (!memory::InstallRawHook(memory::Map::base_addr + hook_addr, HookTrampoline,
                                    expected, size, installed)) {
            throw std::runtime_error("could not install daily interest boundary hook");
        }
    }
    namespace detail
    {
        class DailyInterestHookBridge
        {
        public:
            static void Dispatch(v2::CCountry *country, uint32_t phase)
            {
                const auto boundary = phase == 0 ? DailyInterestPhase::BEFORE : DailyInterestPhase::AFTER;
                DailyInterestEvent event(country, boundary, DailyInterestEvent::TrustedHookTag{});
                DispatchScope scope(event);
                DailyInterestEvent::RecordCallbackFailures(EventRegistry<DailyInterestEvent>::NotifyContained(event));
            }
        };
    }

    namespace
    {
        void DailyInterestEventHook(v2::CCountry *country, uint32_t phase)
        {
            detail::DailyInterestHookBridge::Dispatch(country, phase);
        }
    }
}
