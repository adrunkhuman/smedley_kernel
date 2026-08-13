#include "event_abi_runtime.hpp"

#include <smedley/game_state/runtime.hpp>

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>

namespace
{
    constexpr uint64_t kWriting = UINT64_C(1) << 62;
    constexpr uint64_t kActive = UINT64_C(1) << 63;
    constexpr uint64_t kDisabled = UINT64_C(1) << 61;
    constexpr uint64_t kRemoving = UINT64_C(1) << 60;
    constexpr uint64_t kReferenceMask = UINT32_MAX;

    struct DailyRegistration
    {
        std::atomic<uint64_t> control{0};
        std::atomic<SmedleyEventRegistration> handle{0};
        SmedleyDailyEventCallbackV1Fn callback = nullptr;
        void *context = nullptr;
    };

    std::array<DailyRegistration, SMEDLEY_EVENT_MAX_DAILY_REGISTRATIONS> daily_registrations;
    std::atomic<uint64_t> next_registration{1};
    std::atomic<uint32_t> active_registrations{0};
    thread_local SmedleyEventRegistration current_registration = 0;

    SmedleyEventRegistration NextRegistration()
    {
        auto handle = next_registration.fetch_add(1, std::memory_order_relaxed);
        if (handle != 0) return handle;
        return next_registration.fetch_add(1, std::memory_order_relaxed);
    }

    SmedleyEventResult SMEDLEY_EVENT_CALL RegisterDaily(
        SmedleyDailyEventCallbackV1Fn callback, void *context, SmedleyEventRegistration *registration)
    {
        if (callback == nullptr || registration == nullptr) return SMEDLEY_EVENT_INVALID_ARGUMENT;
        *registration = 0;
        for (auto &slot : daily_registrations) {
            uint64_t expected = 0;
            if (!slot.control.compare_exchange_strong(expected, kWriting, std::memory_order_acq_rel)) continue;
            const auto handle = NextRegistration();
            slot.handle.store(handle, std::memory_order_relaxed);
            slot.callback = callback;
            slot.context = context;
            *registration = handle;
            slot.control.store(kActive, std::memory_order_release);
            active_registrations.fetch_add(1, std::memory_order_release);
            return SMEDLEY_EVENT_SUCCESS;
        }
        return SMEDLEY_EVENT_CAPACITY;
    }

    SmedleyEventResult SMEDLEY_EVENT_CALL Unregister(SmedleyEventRegistration registration)
    {
        if (registration == 0) return SMEDLEY_EVENT_INVALID_ARGUMENT;
        if (registration == current_registration) return SMEDLEY_EVENT_BUSY;
        for (auto &slot : daily_registrations) {
            auto control = slot.control.load(std::memory_order_acquire);
            if ((control & (kActive | kDisabled)) == 0
                || slot.handle.load(std::memory_order_relaxed) != registration) {
                continue;
            }
            for (;;) {
                if ((control & (kActive | kDisabled)) == 0) return SMEDLEY_EVENT_BUSY;
                const auto removing = kRemoving | (control & kReferenceMask);
                if (slot.control.compare_exchange_weak(control, removing, std::memory_order_acq_rel)) {
                    if ((control & kActive) != 0) active_registrations.fetch_sub(1, std::memory_order_acq_rel);
                    break;
                }
            }
            while ((slot.control.load(std::memory_order_acquire) & kReferenceMask) != 0) SwitchToThread();
            slot.callback = nullptr;
            slot.context = nullptr;
            slot.handle.store(0, std::memory_order_relaxed);
            slot.control.store(0, std::memory_order_release);
            return SMEDLEY_EVENT_SUCCESS;
        }
        return SMEDLEY_EVENT_NOT_FOUND;
    }
}

SMEDLEY_EVENT_EXPORT SmedleyEventResult SMEDLEY_EVENT_CALL SmedleyGetEventApiV1(SmedleyEventApiV1 *api)
{
    if (api == nullptr || api->struct_size != sizeof(SmedleyEventApiV1)
        || api->version != SMEDLEY_EVENT_API_VERSION_V1) {
        return SMEDLEY_EVENT_INVALID_ARGUMENT;
    }
    for (const auto value : api->reserved) {
        if (value != 0) return SMEDLEY_EVENT_INVALID_ARGUMENT;
    }
    api->register_daily = &RegisterDaily;
    api->unregister = &Unregister;
    return SMEDLEY_EVENT_SUCCESS;
}

namespace smedley
{
    void DispatchDailyEventApi(const SmedleyDailyEventV1 &event) noexcept
    {
        if (active_registrations.load(std::memory_order_acquire) == 0) return;
        for (auto &slot : daily_registrations) {
            auto control = slot.control.load(std::memory_order_acquire);
            for (;;) {
                if ((control & kActive) == 0) break;
                if ((control & kReferenceMask) == kReferenceMask) break;
                if (slot.control.compare_exchange_weak(control, control + 1, std::memory_order_acq_rel)) {
                    const auto previous_registration = current_registration;
                    current_registration = slot.handle.load(std::memory_order_relaxed);
                    SmedleyEventCallbackResult result = SMEDLEY_EVENT_CALLBACK_DISABLE;
                    try {
                        result = slot.callback(slot.context, &event);
                    } catch (...) {
                    }
                    current_registration = previous_registration;
                    if (result != SMEDLEY_EVENT_CALLBACK_CONTINUE) {
                        auto active_control = slot.control.load(std::memory_order_acquire);
                        while ((active_control & kActive) != 0) {
                            const auto disabled = kDisabled | (active_control & kReferenceMask);
                            if (slot.control.compare_exchange_weak(
                                    active_control, disabled, std::memory_order_acq_rel)) {
                                active_registrations.fetch_sub(1, std::memory_order_acq_rel);
                                break;
                            }
                        }
                    }
                    slot.control.fetch_sub(1, std::memory_order_release);
                    break;
                }
            }
        }
    }

    void NotifyDailyEventApi(v2::CCountry *country) noexcept
    {
        if (active_registrations.load(std::memory_order_acquire) == 0 || country == nullptr) return;
        game_state::DailyUpdateSnapshot snapshot{};
        if (!game_state::ReadDailyUpdateSnapshot(
                game_state::CountryRef{static_cast<const void *>(country)}, &snapshot)) return;
        SmedleyDailyEventV1 event{};
        event.struct_size = sizeof(event);
        event.version = SMEDLEY_DAILY_EVENT_VERSION_V1;
        event.treasury_raw = snapshot.treasury_raw;
        event.game_date_raw = snapshot.date_raw;
        event.country_slot_count = snapshot.country_slot_count;
        event.ai_scheduler_entry_count = snapshot.ai_scheduler_entry_count;
        event.country_tag[0] = snapshot.country_tag.value[0];
        event.country_tag[1] = snapshot.country_tag.value[1];
        event.country_tag[2] = snapshot.country_tag.value[2];
        event.has_owned_province = snapshot.country_exists ? 1 : 0;
        event.human_control_present = snapshot.human_control_present ? 1 : 0;
        DispatchDailyEventApi(event);
    }
}

static_assert(sizeof(SmedleyDailyEventV1) == 56, "event ABI v1 layout changed");
static_assert(sizeof(SmedleyEventApiV1) == 32, "event API v1 layout changed");
static_assert(std::atomic<uint64_t>::is_always_lock_free, "daily hook registration requires lock-free atomics");
