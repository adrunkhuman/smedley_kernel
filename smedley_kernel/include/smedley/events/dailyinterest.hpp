#pragma once

#include <atomic>
#include <cstdint>

#include "../event.hpp"

namespace smedley::v2 {
    class CCountry;
}

namespace smedley::events
{
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

    public:
        DailyInterestEvent(v2::CCountry *country, DailyInterestPhase phase);
        v2::CCountry *GetCountry();
        DailyInterestPhase GetPhase() const;
        static uint64_t CallbackFailures() noexcept;
        static void RecordCallbackFailures(uint32_t failures) noexcept;
        static void InstallHook();
    };
}
