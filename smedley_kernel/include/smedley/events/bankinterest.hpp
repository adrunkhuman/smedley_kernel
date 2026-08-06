#pragma once

#include <atomic>
#include <cstdint>

#include "../event.hpp"

namespace smedley::v2 {
    class CBank;
}

namespace smedley::events
{
    namespace detail
    {
        class BankInterestHookBridge;
    }

    enum class BankInterestPhase : uint32_t
    {
        BEFORE = 0,
        AFTER = 1,
    };

    class BankInterestEvent : public Event
    {
        v2::CBank *bank_;
        BankInterestPhase phase_;
        uint32_t country_index_;
        static constexpr uintptr_t hook_addr = 0x00285f0f;
        static constexpr uintptr_t function_addr = 0x000f5bf0;
        inline static uintptr_t hook_ret_addr = 0;
        inline static uintptr_t distribute_interest_addr = 0;
        inline static std::atomic<uint64_t> callback_failures_{0};
        static void HookTrampoline();
        struct TrustedHookTag {};
        BankInterestEvent(v2::CBank *bank, BankInterestPhase phase, uint32_t country_index,
                          bool distributes_to_states, TrustedHookTag);

        friend class detail::BankInterestHookBridge;

    public:
        BankInterestEvent(v2::CBank *bank, BankInterestPhase phase, uint32_t country_index = 0);
        v2::CBank *GetBank() const noexcept;
        BankInterestPhase GetPhase() const noexcept;
        uint32_t GetCountryIndex() const noexcept;
        bool DistributesToStates() const noexcept;
        static uint64_t ActiveDispatchGeneration() noexcept;
        static bool IsDispatchActive(uint64_t generation) noexcept;
        static bool IsCurrentDispatch(const BankInterestEvent &event, uint64_t generation) noexcept;
        static uint64_t CallbackFailures() noexcept;
        static void RecordCallbackFailures(uint32_t failures) noexcept;
        static void InstallHook();
    };

#if defined(_M_IX86)
    static_assert(sizeof(BankInterestEvent) == 20);
#endif
}
