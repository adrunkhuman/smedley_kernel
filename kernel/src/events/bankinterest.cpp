#include "eventregistry.hpp"
#include "event_services_runtime.hpp"
#include "events/bankinterest.hpp"
#include "memory.hpp"

#include <stdexcept>

using namespace smedley;

namespace smedley::events
{
    namespace
    {
        std::atomic<uint64_t> next_dispatch_generation{1};
        thread_local uint64_t active_dispatch_generation = 0;
        thread_local const BankInterestEvent *active_dispatch_event = nullptr;
        void BankInterestEventHook(
            v2::CBank *bank, uint32_t country_index, uint32_t phase, uint32_t distributes_to_states);

        class DispatchScope final
        {
        public:
            explicit DispatchScope(const BankInterestEvent &event) noexcept
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

        private:
            uint64_t generation_ = 0;
            uint64_t previous_generation_ = 0;
            const BankInterestEvent *previous_event_ = nullptr;
        };
    }

    void __declspec(naked) BankInterestEvent::HookTrampoline()
    {
        __asm {
            push edi
            push dword ptr [edi + 14h]
            push dword ptr [edi + 10h]
            push dword ptr [edi + 24h]
            push dword ptr [edi + 20h]
            pushfd
            pushad
            cmp dword ptr [esp + 4], 0
            jne skip_before
            mov eax, dword ptr [esp + 52]
            mov ecx, dword ptr [esp + 4]
            push 0
            push 0
            push ecx
            push eax
            call BankInterestEventHook
            add esp, 16
        skip_before:
            popad
            popfd

            call dword ptr [distribute_interest_addr]

            pushfd
            pushad
            mov eax, dword ptr [esp + 36]
            mov edx, dword ptr [esp + 40]
            test edx, edx
            jl skip_after
            jg dispatch_after
            test eax, eax
            je skip_after
        dispatch_after:
            xor ecx, ecx
            mov eax, dword ptr [esp + 44]
            mov edx, dword ptr [esp + 48]
            test edx, edx
            jl emit_after
            jg state_distribution
            cmp eax, 33
            jbe emit_after
        state_distribution:
            mov ecx, 1
        emit_after:
            mov eax, dword ptr [esp + 52]
            mov edx, dword ptr [esp + 4]
            push ecx
            push 1
            push edx
            push eax
            call BankInterestEventHook
            add esp, 16
        skip_after:
            popad
            popfd
            lea esp, [esp + 20]
            jmp hook_ret_addr
        }
    }

    BankInterestEvent::BankInterestEvent(v2::CBank *bank, BankInterestPhase phase, uint32_t country_index)
        : Event(false), bank_(bank), phase_(phase), country_index_(country_index)
    {
    }

    BankInterestEvent::BankInterestEvent(
        v2::CBank *bank, BankInterestPhase phase, uint32_t country_index,
        bool distributes_to_states, TrustedHookTag)
        : Event(false), bank_(bank), phase_(phase),
          country_index_(country_index | (distributes_to_states ? 0u : 0x80000000u))
    {
    }

    v2::CBank *BankInterestEvent::GetBank() const noexcept { return bank_; }
    BankInterestPhase BankInterestEvent::GetPhase() const noexcept { return phase_; }
    uint32_t BankInterestEvent::GetCountryIndex() const noexcept { return country_index_ & 0x7fffffffu; }
    bool BankInterestEvent::DistributesToStates() const noexcept { return (country_index_ & 0x80000000u) == 0; }
    uint64_t BankInterestEvent::ActiveDispatchGeneration() noexcept { return active_dispatch_generation; }
    bool BankInterestEvent::IsDispatchActive(uint64_t generation) noexcept
    {
        return generation != 0 && active_dispatch_generation == generation;
    }
    bool BankInterestEvent::IsCurrentDispatch(const BankInterestEvent &event, uint64_t generation) noexcept
    {
        return IsDispatchActive(generation) && active_dispatch_event == &event;
    }
    uint64_t BankInterestEvent::CallbackFailures() noexcept
    {
        return callback_failures_.load(std::memory_order_relaxed);
    }
    void BankInterestEvent::RecordCallbackFailures(uint32_t failures) noexcept
    {
        callback_failures_.fetch_add(failures, std::memory_order_relaxed);
    }
    void BankInterestEvent::InstallHook(const uint8_t *expected, size_t size,
                                         memory::RawHook *installed)
    {
        hook_ret_addr = memory::Map::base_addr + hook_addr + 5;
        distribute_interest_addr = memory::Map::base_addr + function_addr;
        if (!memory::InstallRawHook(memory::Map::base_addr + hook_addr, HookTrampoline,
                                    expected, size, installed)) {
            throw std::runtime_error("could not install bank interest boundary hook");
        }
    }

    namespace detail
    {
        class BankInterestHookBridge
        {
        public:
            static void Dispatch(v2::CBank *bank, uint32_t country_index, uint32_t phase,
                                 uint32_t distributes_to_states)
            {
                const auto boundary = phase == 0 ? BankInterestPhase::BEFORE : BankInterestPhase::AFTER;
                BankInterestEvent event(bank, boundary, country_index,
                    distributes_to_states != 0, BankInterestEvent::TrustedHookTag{});
                DispatchScope scope(event);
                BankInterestEvent::RecordCallbackFailures(EventRegistry<BankInterestEvent>::NotifyContained(event));
                smedley::DispatchBankInterestEventServices(event);
            }
        };
    }

    namespace
    {
        void BankInterestEventHook(
            v2::CBank *bank, uint32_t country_index, uint32_t phase, uint32_t distributes_to_states)
        {
            detail::BankInterestHookBridge::Dispatch(bank, country_index, phase, distributes_to_states);
        }
    }
}
