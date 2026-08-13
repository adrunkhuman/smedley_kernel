#include "event_services_runtime.hpp"

#include <smedley/events/bankinterest.hpp>
#include <smedley/game_state/game_services_abi.hpp>

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

    template <typename Callback>
    struct RegistrationSlot
    {
        std::atomic<uint64_t> control{0};
        std::atomic<SmedleyEventServicesRegistration> handle{0};
        Callback callback = nullptr;
        void *context = nullptr;
    };

    using BankSlot = RegistrationSlot<SmedleyBankInterestCallbackV1Fn>;
    using ConsoleSlot = RegistrationSlot<SmedleyCampaignConsoleCallbackV1Fn>;

    std::array<BankSlot, SMEDLEY_EVENT_SERVICES_MAX_BANK_INTEREST_REGISTRATIONS> bank_slots;
    std::array<ConsoleSlot, SMEDLEY_EVENT_SERVICES_MAX_CAMPAIGN_CONSOLE_REGISTRATIONS> console_slots;
    std::atomic<uint64_t> next_registration{1};
    std::atomic<uint64_t> next_authority{1};
    std::atomic<uint32_t> active_bank_slots{0};
    std::atomic<uint32_t> active_console_slots{0};
    thread_local SmedleyEventServicesRegistration current_registration = 0;

    struct BankAuthorityScope
    {
        SmedleyBankInterestAuthority authority = 0;
        uint32_t phase = 0;
        uint32_t country_index = 0;

        BankAuthorityScope(uint32_t callback_phase, uint32_t callback_country_index) noexcept
            : authority(next_authority.fetch_add(1, std::memory_order_relaxed)),
              phase(callback_phase), country_index(callback_country_index)
        {
            if (authority == 0) authority = next_authority.fetch_add(1, std::memory_order_relaxed);
            previous_ = active_authority_;
            active_authority_ = this;
        }

        ~BankAuthorityScope() { active_authority_ = previous_; }

        static const BankAuthorityScope *Active() noexcept { return active_authority_; }

    private:
        const BankAuthorityScope *previous_ = nullptr;
        static thread_local const BankAuthorityScope *active_authority_;
    };

    thread_local const BankAuthorityScope *BankAuthorityScope::active_authority_ = nullptr;

    SmedleyEventServicesRegistration NextRegistration() noexcept
    {
        auto handle = next_registration.fetch_add(1, std::memory_order_relaxed);
        if (handle != 0) return handle;
        return next_registration.fetch_add(1, std::memory_order_relaxed);
    }

    template <typename Slot, size_t Size, typename Callback>
    SmedleyEventServicesResult Register(std::array<Slot, Size> &slots, std::atomic<uint32_t> &active,
        Callback callback, void *context, SmedleyEventServicesRegistration *registration)
    {
        if (callback == nullptr || registration == nullptr) return SMEDLEY_EVENT_SERVICES_INVALID_ARGUMENT;
        *registration = 0;
        for (auto &slot : slots) {
            uint64_t expected = 0;
            if (!slot.control.compare_exchange_strong(expected, kWriting, std::memory_order_acq_rel)) continue;
            const auto handle = NextRegistration();
            slot.handle.store(handle, std::memory_order_relaxed);
            slot.callback = callback;
            slot.context = context;
            *registration = handle;
            slot.control.store(kActive, std::memory_order_release);
            active.fetch_add(1, std::memory_order_release);
            return SMEDLEY_EVENT_SERVICES_SUCCESS;
        }
        return SMEDLEY_EVENT_SERVICES_CAPACITY;
    }

    template <typename Slot, size_t Size>
    SmedleyEventServicesResult UnregisterFrom(std::array<Slot, Size> &slots,
        std::atomic<uint32_t> &active, SmedleyEventServicesRegistration registration)
    {
        for (auto &slot : slots) {
            auto control = slot.control.load(std::memory_order_acquire);
            if ((control & (kActive | kDisabled)) == 0
                || slot.handle.load(std::memory_order_relaxed) != registration) continue;
            for (;;) {
                if ((control & (kActive | kDisabled)) == 0) return SMEDLEY_EVENT_SERVICES_BUSY;
                const auto removing = kRemoving | (control & kReferenceMask);
                if (slot.control.compare_exchange_weak(control, removing, std::memory_order_acq_rel)) {
                    if ((control & kActive) != 0) active.fetch_sub(1, std::memory_order_acq_rel);
                    break;
                }
            }
            while ((slot.control.load(std::memory_order_acquire) & kReferenceMask) != 0) SwitchToThread();
            slot.callback = nullptr;
            slot.context = nullptr;
            slot.handle.store(0, std::memory_order_relaxed);
            slot.control.store(0, std::memory_order_release);
            return SMEDLEY_EVENT_SERVICES_SUCCESS;
        }
        return SMEDLEY_EVENT_SERVICES_NOT_FOUND;
    }

    SmedleyEventServicesResult SMEDLEY_EVENT_SERVICES_CALL RegisterBankInterest(
        SmedleyBankInterestCallbackV1Fn callback, void *context, SmedleyEventServicesRegistration *registration)
    {
        return Register(bank_slots, active_bank_slots, callback, context, registration);
    }

    SmedleyEventServicesResult SMEDLEY_EVENT_SERVICES_CALL RegisterCampaignConsole(
        SmedleyCampaignConsoleCallbackV1Fn callback, void *context, SmedleyEventServicesRegistration *registration)
    {
        return Register(console_slots, active_console_slots, callback, context, registration);
    }

    SmedleyEventServicesResult SMEDLEY_EVENT_SERVICES_CALL Unregister(
        SmedleyEventServicesRegistration registration)
    {
        if (registration == 0) return SMEDLEY_EVENT_SERVICES_INVALID_ARGUMENT;
        if (registration == current_registration) return SMEDLEY_EVENT_SERVICES_BUSY;
        const auto bank_result = UnregisterFrom(bank_slots, active_bank_slots, registration);
        if (bank_result != SMEDLEY_EVENT_SERVICES_NOT_FOUND) return bank_result;
        return UnregisterFrom(console_slots, active_console_slots, registration);
    }

    template <typename Slot>
    bool Acquire(Slot &slot) noexcept
    {
        auto control = slot.control.load(std::memory_order_acquire);
        for (;;) {
            if ((control & kActive) == 0 || (control & kReferenceMask) == kReferenceMask) return false;
            if (slot.control.compare_exchange_weak(control, control + 1, std::memory_order_acq_rel)) return true;
        }
    }

    template <typename Slot>
    void DisableIfRequested(Slot &slot, std::atomic<uint32_t> &active,
        SmedleyEventServicesCallbackResult callback_result) noexcept
    {
        if (callback_result == SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE) return;
        auto control = slot.control.load(std::memory_order_acquire);
        while ((control & kActive) != 0) {
            const auto disabled = kDisabled | (control & kReferenceMask);
            if (slot.control.compare_exchange_weak(control, disabled, std::memory_order_acq_rel)) {
                active.fetch_sub(1, std::memory_order_acq_rel);
                break;
            }
        }
    }
}

SMEDLEY_EVENT_SERVICES_EXPORT SmedleyEventServicesResult SMEDLEY_EVENT_SERVICES_CALL
SmedleyGetEventServicesApiV1(SmedleyEventServicesApiV1 *api)
{
    if (api == nullptr || api->struct_size != sizeof(SmedleyEventServicesApiV1)
        || api->version != SMEDLEY_EVENT_SERVICES_API_VERSION_V1) {
        return SMEDLEY_EVENT_SERVICES_INVALID_ARGUMENT;
    }
    for (const auto reserved : api->reserved) {
        if (reserved != 0) return SMEDLEY_EVENT_SERVICES_INVALID_ARGUMENT;
    }
    api->register_bank_interest = &RegisterBankInterest;
    api->register_campaign_console = &RegisterCampaignConsole;
    api->unregister = &Unregister;
    return SMEDLEY_EVENT_SERVICES_SUCCESS;
}

namespace smedley
{
    void DispatchBankInterestEventServices(
        uint32_t phase, uint32_t country_index, bool distributes_to_states) noexcept
    {
        if (active_bank_slots.load(std::memory_order_acquire) == 0) return;
        for (auto &slot : bank_slots) {
            if (!Acquire(slot)) continue;
            BankAuthorityScope authority(phase, country_index);
            SmedleyBankInterestEventV1 event{};
            event.struct_size = sizeof(event);
            event.version = SMEDLEY_BANK_INTEREST_EVENT_VERSION_V1;
            event.authority = authority.authority;
            event.phase = phase;
            event.country_index = country_index;
            event.distributes_to_states = distributes_to_states ? 1 : 0;
            const auto previous_registration = current_registration;
            current_registration = slot.handle.load(std::memory_order_relaxed);
            SmedleyEventServicesCallbackResult result = SMEDLEY_EVENT_SERVICES_CALLBACK_DISABLE;
            try { result = slot.callback(slot.context, &event); } catch (...) {}
            current_registration = previous_registration;
            DisableIfRequested(slot, active_bank_slots, result);
            slot.control.fetch_sub(1, std::memory_order_release);
        }
    }

    void DispatchBankInterestEventServices(events::BankInterestEvent &bank_event) noexcept
    {
        if (active_bank_slots.load(std::memory_order_acquire) == 0) return;
        for (auto &slot : bank_slots) {
            if (!Acquire(slot)) continue;
            BankAuthorityScope authority(static_cast<uint32_t>(bank_event.GetPhase()), bank_event.GetCountryIndex());
            SmedleyBankInterestEventV1 event{};
            event.struct_size = sizeof(event);
            event.version = SMEDLEY_BANK_INTEREST_EVENT_VERSION_V1;
            event.authority = authority.authority;
            event.phase = static_cast<uint32_t>(bank_event.GetPhase());
            event.country_index = bank_event.GetCountryIndex();
            event.distributes_to_states = bank_event.DistributesToStates() ? 1 : 0;
            const auto previous_registration = current_registration;
            current_registration = slot.handle.load(std::memory_order_relaxed);
            SmedleyEventServicesCallbackResult result = SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE;
            if (game_state::BindBankInterestGameServices(authority.authority, bank_event)) {
                try {
                    result = slot.callback(slot.context, &event);
                } catch (...) {
                    result = SMEDLEY_EVENT_SERVICES_CALLBACK_DISABLE;
                }
                game_state::UnbindBankInterestGameServices(authority.authority);
            }
            current_registration = previous_registration;
            DisableIfRequested(slot, active_bank_slots, result);
            slot.control.fetch_sub(1, std::memory_order_release);
        }
    }

    bool DispatchCampaignConsoleEventServices(const SmedleyCampaignConsoleInputV1 &input,
        SmedleyCampaignConsoleResultV1 *result) noexcept
    {
        if (result == nullptr || active_console_slots.load(std::memory_order_acquire) == 0) return false;
        bool handled = false;
        for (auto &slot : console_slots) {
            if (!Acquire(slot)) continue;
            SmedleyCampaignConsoleResultV1 candidate{};
            const auto previous_registration = current_registration;
            current_registration = slot.handle.load(std::memory_order_relaxed);
            SmedleyEventServicesCallbackResult callback_result = SMEDLEY_EVENT_SERVICES_CALLBACK_DISABLE;
            try {
                callback_result = slot.callback(slot.context, &input, &candidate);
            } catch (...) {
            }
            current_registration = previous_registration;
            const bool valid = candidate.handled == 0
                || (candidate.handled == 1 && candidate.struct_size == sizeof(candidate)
                    && candidate.version == SMEDLEY_CAMPAIGN_CONSOLE_RESULT_VERSION_V1
                    && candidate.reserved[0] == 0 && candidate.reserved[1] == 0 && candidate.reserved[2] == 0
                    && candidate.success <= 1
                    && candidate.message_bytes <= SMEDLEY_CAMPAIGN_CONSOLE_MAX_RESULT_BYTES);
            if (!valid) callback_result = SMEDLEY_EVENT_SERVICES_CALLBACK_DISABLE;
            if (!handled && valid && candidate.handled != 0) {
                *result = candidate;
                handled = true;
            }
            DisableIfRequested(slot, active_console_slots, callback_result);
            slot.control.fetch_sub(1, std::memory_order_release);
        }
        return handled;
    }

    bool IsBankInterestAuthorityActive(SmedleyBankInterestAuthority authority,
        uint32_t phase, uint32_t country_index) noexcept
    {
        const auto *active = BankAuthorityScope::Active();
        return active != nullptr && active->authority == authority && active->phase == phase
            && active->country_index == country_index;
    }
}

static_assert(sizeof(SmedleyBankInterestEventV1) == 48, "bank interest ABI v1 layout changed");
static_assert(sizeof(SmedleyCampaignConsoleInputV1) == 160, "campaign console input ABI v1 layout changed");
static_assert(sizeof(SmedleyCampaignConsoleResultV1) == 160, "campaign console result ABI v1 layout changed");
static_assert(sizeof(SmedleyEventServicesApiV1) == 32, "event services API v1 layout changed");
static_assert(std::atomic<uint64_t>::is_always_lock_free, "event services require lock-free atomics");
