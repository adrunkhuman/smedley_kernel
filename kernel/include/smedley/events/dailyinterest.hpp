#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "../event.hpp"

namespace smedley::memory {
    struct RawHook;
}

namespace smedley::v2 {
    class CCountry;
}

namespace smedley::events
{
    namespace detail
    {
        class DailyInterestHookBridge;
    }

    enum class DailyInterestPhase : uint32_t
    {
        BEFORE = 0,
        AFTER = 1,
    };

    class DailyInterestEvent : public Event
    {
        v2::CCountry *_country;
        DailyInterestPhase _phase;
        static constexpr uintptr_t hook_addr = 0x00108d3e;
        static constexpr uintptr_t function_addr = 0x00123c30;
        inline static uintptr_t hook_ret_addr = 0;
        inline static uintptr_t pay_daily_interest_addr = 0;
        inline static std::atomic<uint64_t> callback_failures_{0};
        static void HookTrampoline();
        struct TrustedHookTag {};
        DailyInterestEvent(v2::CCountry *country, DailyInterestPhase phase, TrustedHookTag);

        friend class detail::DailyInterestHookBridge;

    public:
        DailyInterestEvent(v2::CCountry *country, DailyInterestPhase phase);
        v2::CCountry *GetCountry();
        DailyInterestPhase GetPhase() const;
        static uint64_t ActiveDispatchGeneration() noexcept;
        static bool IsDispatchActive(uint64_t generation) noexcept;
        static bool IsCurrentDispatch(const DailyInterestEvent &event, uint64_t generation) noexcept;
        static uint64_t CallbackFailures() noexcept;
        static void RecordCallbackFailures(uint32_t failures) noexcept;
        static void InstallHook(const uint8_t *expected, size_t size, memory::RawHook *installed);
    };

#if defined(_M_IX86)
    static_assert(sizeof(DailyInterestEvent) == 16);
#endif
}
