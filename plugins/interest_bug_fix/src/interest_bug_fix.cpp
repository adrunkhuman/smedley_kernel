#include "interest_allocation.hpp"
#include "interest_mutation_status.hpp"
#include "telemetry_bridge.hpp"

#include <smedley/event_services_api.h>
#include <smedley/interest_pool_api.h>
#include <smedley/logging_api.h>
#define SMEDLEY_PLUGIN_BUILD
#include <smedley/plugin_abi.h>

#include <windows.h>
#include <shellapi.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <string_view>
#include <thread>

namespace interest_bug_fix
{
    namespace
    {
        constexpr uint32_t result_queue_capacity = 1024;
        constexpr int32_t unavailable_date_raw = -1;
        constexpr uint32_t metadata_date_unavailable = 1;
        constexpr uint32_t metadata_country_unavailable = 2;

        enum class FixStatus : uint32_t {
            initialized, paid, collection_failed, no_eligible_savings, allocation_overflow,
            allocation_invalid, mutation_unavailable, mutation_precondition_changed,
            partial_mutation, conservation_failed, callback_failed,
        };

        struct FixResult {
            int32_t date_raw = unavailable_date_raw;
            uint32_t country_index = 0;
            uint32_t metadata_flags = metadata_date_unavailable | metadata_country_unavailable;
            int32_t state_id = -1;
            FixStatus status = FixStatus::collection_failed;
            uint32_t flags = 0, state_count = 0, province_count = 0, pop_count = 0;
            uint32_t paid_pop_count = 0, verified_pop_count = 0;
            AllocationStatus allocation_status = AllocationStatus::success;
            int64_t state_pool_raw = 0, payout_raw = 0, discarded_raw = 0;
            SmedleyInterestPoolResult mutation_result = SMEDLEY_INTEREST_POOL_SUCCESS;
        };

        template <size_t Capacity>
        class ResultQueue {
        public:
            bool TryPush(const FixResult &result) noexcept {
                const uint32_t write = write_.load(std::memory_order_relaxed);
                const uint32_t next = (write + 1) % Capacity;
                if (next == read_.load(std::memory_order_acquire)) return false;
                results_[write] = result;
                write_.store(next, std::memory_order_release);
                return true;
            }
            bool TryPop(FixResult *result) noexcept {
                const uint32_t read = read_.load(std::memory_order_relaxed);
                if (read == write_.load(std::memory_order_acquire)) return false;
                *result = results_[read];
                read_.store((read + 1) % Capacity, std::memory_order_release);
                return true;
            }
            bool Empty() const noexcept {
                return read_.load(std::memory_order_relaxed) == write_.load(std::memory_order_acquire);
            }
        private:
            std::array<FixResult, Capacity> results_{};
            std::atomic<uint32_t> write_{0}, read_{0};
        };

        const char *StatusName(FixStatus status) noexcept {
            switch (status) {
            case FixStatus::initialized: return "initialized";
            case FixStatus::paid: return "paid";
            case FixStatus::collection_failed: return "collection_failed";
            case FixStatus::no_eligible_savings: return "no_eligible_savings";
            case FixStatus::allocation_overflow: return "allocation_overflow";
            case FixStatus::allocation_invalid: return "allocation_invalid";
            case FixStatus::mutation_unavailable: return "mutation_unavailable";
            case FixStatus::mutation_precondition_changed: return "mutation_precondition_changed";
            case FixStatus::partial_mutation: return "partial_mutation";
            case FixStatus::conservation_failed: return "conservation_failed";
            case FixStatus::callback_failed: return "callback_failed";
            }
            return "unknown";
        }
        const char *AllocationStatusName(AllocationStatus status) noexcept {
            switch (status) {
            case AllocationStatus::success: return "success";
            case AllocationStatus::no_payment: return "no_payment";
            case AllocationStatus::no_eligible_savings: return "no_eligible_savings";
            case AllocationStatus::invalid_input: return "invalid_input";
            case AllocationStatus::overflow: return "overflow";
            case AllocationStatus::scratch_too_small: return "scratch_too_small";
            }
            return "unknown";
        }
        FixStatus AllocationFixStatus(AllocationStatus status) noexcept {
            if (status == AllocationStatus::no_eligible_savings) return FixStatus::no_eligible_savings;
            if (status == AllocationStatus::overflow) return FixStatus::allocation_overflow;
            return FixStatus::allocation_invalid;
        }
        FixStatus MutationFixStatus(SmedleyInterestPoolResult status, bool partial) noexcept {
            switch (ClassifyAppliedPopInterestFailure(status, partial)) {
            case PopInterestFailureClass::unavailable: return FixStatus::mutation_unavailable;
            case PopInterestFailureClass::partial_mutation: return FixStatus::partial_mutation;
            default: return FixStatus::mutation_precondition_changed;
            }
        }
        bool DebugEnabled() {
            int count = 0;
            wchar_t **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
            if (arguments == nullptr) return false;
            bool enabled = false;
            for (int index = 1; index < count; ++index) {
                if (std::wstring_view(arguments[index]) == L"-smedley-interest-fix-debug=1") enabled = true;
            }
            LocalFree(arguments);
            return enabled;
        }

        class State final {
        public:
            bool Load() {
                if (!AcquireServices()) return false;
                debug_ = DebugEnabled();
                initialized_ = false;
                disabled_ = false;
                needs_cleanup_ = true;
                stop_.store(false, std::memory_order_release);
                if (debug_) {
                    output_.open("interest_bug_fix.csv", std::ios::trunc);
                    if (output_) {
                        output_ << "date_raw,country_index,metadata_flags,state_id,status,flags,state_count,province_count,"
                                   "pop_count,paid_pop_count,verified_pop_count,state_pool_raw,payout_raw,discarded_raw,"
                                   "allocation_status,mutation_result,dropped_results\n";
                    }
                }
                worker_ = std::thread([this] { WriteResults(); });
                Log(SMEDLEY_LOG_INFO, "interest C ABI services registered");
                return true;
            }
            void Unload() noexcept {
                if (registration_ != 0) event_api_.unregister(registration_);
                registration_ = 0;
                stop_.store(true, std::memory_order_release);
                if (worker_.joinable()) worker_.join();
                if (output_) output_.flush();
            }
            SmedleyEventServicesCallbackResult OnBankInterest(const SmedleyBankInterestEventV1 *event) noexcept {
                if (event == nullptr || event->struct_size != sizeof(*event)
                    || event->version != SMEDLEY_BANK_INTEREST_EVENT_VERSION_V1 || event->authority == 0) {
                    return SMEDLEY_EVENT_SERVICES_CALLBACK_DISABLE;
                }
                struct HandleScrubber {
                    State *state;
                    ~HandleScrubber() { state->ScrubAuthorityHandles(); }
                } scrubber{this};
                try {
                    if (event->phase == SMEDLEY_BANK_INTEREST_BEFORE && event->country_index == 0) Initialize(*event);
                    if (event->phase == SMEDLEY_BANK_INTEREST_AFTER && event->distributes_to_states != 0 && initialized_ && !disabled_) {
                        PayCountryPools(*event);
                    }
                    return SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE;
                } catch (...) {
                    disabled_ = true;
                    needs_cleanup_ = true;
                    Publish({unavailable_date_raw, event->country_index,
                        metadata_date_unavailable | metadata_country_unavailable, -1, FixStatus::callback_failed});
                    return SMEDLEY_EVENT_SERVICES_CALLBACK_DISABLE;
                }
            }
        private:
            bool AcquireServices() {
                const HMODULE kernel = GetModuleHandleW(L"smedley_kernel.dll");
                if (kernel == nullptr) return false;
                const auto get_events = reinterpret_cast<SmedleyGetEventServicesApiV1Fn>(
                    GetProcAddress(kernel, SMEDLEY_EVENT_SERVICES_GET_API_V1_SYMBOL));
                const auto get_interest = reinterpret_cast<SmedleyGetInterestPoolApiV1Fn>(
                    GetProcAddress(kernel, SMEDLEY_INTEREST_POOL_GET_API_V1_SYMBOL));
                const auto get_logging = reinterpret_cast<SmedleyGetLoggingApiV1Fn>(
                    GetProcAddress(kernel, SMEDLEY_LOGGING_GET_API_V1_SYMBOL));
                if (get_events == nullptr || get_interest == nullptr || get_logging == nullptr) return false;
                event_api_ = {sizeof(event_api_), SMEDLEY_EVENT_SERVICES_API_VERSION_V1};
                interest_api_ = {sizeof(interest_api_), SMEDLEY_INTEREST_POOL_API_VERSION_V1};
                logging_api_ = {sizeof(logging_api_), SMEDLEY_LOGGING_API_VERSION_V1};
                if (get_events(&event_api_) != SMEDLEY_EVENT_SERVICES_SUCCESS
                    || get_interest(&interest_api_) != SMEDLEY_INTEREST_POOL_SUCCESS
                    || get_logging(&logging_api_) != SMEDLEY_LOGGING_SUCCESS
                    || event_api_.register_bank_interest == nullptr || event_api_.unregister == nullptr
                    || interest_api_.collect == nullptr || interest_api_.prepare == nullptr
                    || interest_api_.apply == nullptr || interest_api_.discard == nullptr || logging_api_.write == nullptr) return false;
                return event_api_.register_bank_interest(&BankInterestCallback, this, &registration_)
                    == SMEDLEY_EVENT_SERVICES_SUCCESS;
            }
            static SmedleyEventServicesCallbackResult SMEDLEY_EVENT_SERVICES_CALL BankInterestCallback(
                void *context, const SmedleyBankInterestEventV1 *event) noexcept {
                auto *state = static_cast<State *>(context);
                return state == nullptr ? SMEDLEY_EVENT_SERVICES_CALLBACK_DISABLE : state->OnBankInterest(event);
            }
            void Initialize(const SmedleyBankInterestEventV1 &event) noexcept {
                SmedleyInterestInitializationV1 initialization{sizeof(initialization), 1};
                const auto result = interest_api_.discard(event.authority, &initialization);
                FixResult diagnostic{};
                diagnostic.country_index = event.country_index;
                diagnostic.state_count = initialization.state_count;
                diagnostic.flags = initialization.flags;
                diagnostic.discarded_raw = initialization.discarded_raw;
                diagnostic.mutation_result = result;
                diagnostic.status = result == SMEDLEY_INTEREST_POOL_SUCCESS ? FixStatus::initialized
                    : MutationFixStatus(result, initialization.cleared_state_count != 0);
                Publish(diagnostic);
                if (result == SMEDLEY_INTEREST_POOL_SUCCESS) {
                    initialized_ = true;
                    disabled_ = false;
                    needs_cleanup_ = false;
                } else {
                    initialized_ = false;
                    disabled_ = true;
                }
            }
            void PayCountryPools(const SmedleyBankInterestEventV1 &event) noexcept {
                uint32_t state_count = 0, pop_count = 0, flags = 0;
                auto result = interest_api_.collect(event.authority, states_.data(), static_cast<uint32_t>(states_.size()),
                    &state_count, nullptr, 0, &pop_count, &flags);
                if (result != SMEDLEY_INTEREST_POOL_SUCCESS) {
                    PublishFailure(event, result, flags);
                    return;
                }
                state_handles_ = state_count;
                bool has_interest = false;
                for (uint32_t index = 0; index < state_count; ++index) has_interest |= states_[index].interest_raw > 0;
                if (!has_interest) return;
                result = interest_api_.collect(event.authority, states_.data(), static_cast<uint32_t>(states_.size()), &state_count,
                    pops_.data(), static_cast<uint32_t>(pops_.size()), &pop_count, &flags);
                if (result != SMEDLEY_INTEREST_POOL_SUCCESS) {
                    PublishFailure(event, result, flags);
                    return;
                }
                state_handles_ = state_count;
                pop_handles_ = pop_count;
                result = interest_api_.prepare(event.authority, states_.data(), state_count);
                if (result != SMEDLEY_INTEREST_POOL_SUCCESS) {
                    needs_cleanup_ = true;
                    PublishFailure(event, result, flags);
                    return;
                }
                for (uint32_t index = 0; index < state_count && !disabled_; ++index) {
                    if (states_[index].interest_raw > 0) PayStatePool(event, states_[index]);
                }
            }
            void PayStatePool(const SmedleyBankInterestEventV1 &event, const SmedleyInterestStateSnapshotV1 &state) noexcept {
                FixResult diagnostic{};
                diagnostic.country_index = event.country_index;
                diagnostic.state_id = state.state_id;
                diagnostic.state_pool_raw = state.interest_raw;
                diagnostic.province_count = state.province_count;
                diagnostic.pop_count = state.pop_count;
                if (state.pop_count == 0 || state.first_pop > pops_.size() || state.pop_count > pops_.size() - state.first_pop) {
                    needs_cleanup_ = true;
                    diagnostic.status = FixStatus::collection_failed;
                    Publish(diagnostic);
                    return;
                }
                for (uint32_t index = 0; index < state.pop_count; ++index) {
                    allocations_[index] = {pops_[state.first_pop + index].savings_raw};
                }
                diagnostic.allocation_status = AllocateInterest(state.interest_raw, allocations_.data(), state.pop_count,
                    order_.data(), order_.size());
                if (diagnostic.allocation_status != AllocationStatus::success) {
                    needs_cleanup_ = true;
                    diagnostic.status = AllocationFixStatus(diagnostic.allocation_status);
                    Publish(diagnostic);
                    return;
                }
                int64_t total = 0;
                uint32_t payment_count = 0;
                for (uint32_t index = 0; index < state.pop_count; ++index) {
                    const auto amount = allocations_[index].payout_raw;
                    if (amount == 0) continue;
                    if (total > (std::numeric_limits<int64_t>::max)() - amount) {
                        needs_cleanup_ = true;
                        diagnostic.status = FixStatus::conservation_failed;
                        Publish(diagnostic);
                        return;
                    }
                    payouts_[payment_count] = {sizeof(SmedleyInterestPayoutV1), 1, pops_[state.first_pop + index].pop, amount};
                    total += amount;
                    ++payment_count;
                    payout_handles_ = payment_count;
                }
                if (state.interest_raw > (std::numeric_limits<int64_t>::max)() / 1000
                    || total != state.interest_raw * 1000 || payment_count == 0) {
                    needs_cleanup_ = true;
                    diagnostic.status = FixStatus::conservation_failed;
                    Publish(diagnostic);
                    return;
                }
                SmedleyInterestPayoutResultV1 payout_result{sizeof(payout_result), 1};
                const auto result = interest_api_.apply(event.authority, &state, payouts_.data(), payment_count, &payout_result);
                diagnostic.payout_raw = total;
                diagnostic.paid_pop_count = payout_result.write_count;
                diagnostic.verified_pop_count = payout_result.verified_count;
                diagnostic.mutation_result = result;
                diagnostic.status = result == SMEDLEY_INTEREST_POOL_SUCCESS ? FixStatus::paid
                    : MutationFixStatus(result, payout_result.write_count != 0);
                if (result != SMEDLEY_INTEREST_POOL_SUCCESS) {
                    needs_cleanup_ = true;
                    if (IsUnsafeAppliedPopInterestFailure(result, payout_result.write_count != 0)) disabled_ = true;
                }
                Publish(diagnostic);
            }
            void ScrubAuthorityHandles() noexcept {
                for (uint32_t index = 0; index < state_handles_; ++index) states_[index].state = 0;
                for (uint32_t index = 0; index < pop_handles_; ++index) pops_[index].pop = 0;
                for (uint32_t index = 0; index < payout_handles_; ++index) payouts_[index].pop = 0;
                state_handles_ = 0;
                pop_handles_ = 0;
                payout_handles_ = 0;
            }
            void PublishFailure(const SmedleyBankInterestEventV1 &event, SmedleyInterestPoolResult result, uint32_t flags) noexcept {
                needs_cleanup_ = true;
                FixResult diagnostic{};
                diagnostic.country_index = event.country_index;
                diagnostic.flags = flags;
                diagnostic.mutation_result = result;
                diagnostic.status = result == SMEDLEY_INTEREST_POOL_UNAVAILABLE ? FixStatus::collection_failed
                    : MutationFixStatus(result, false);
                Publish(diagnostic);
            }
            void Publish(const FixResult &result) noexcept {
                if (!queue_.TryPush(result)) dropped_.fetch_add(1, std::memory_order_relaxed);
            }
            void Log(SmedleyLogLevel level, const char *message) noexcept {
                static constexpr char component[] = "interest_bug_fix";
                logging_api_.write(level, component, sizeof(component) - 1, message,
                    static_cast<uint32_t>(std::strlen(message)));
            }
            void WriteResults() noexcept {
                while (!stop_.load(std::memory_order_acquire) || !queue_.Empty()) {
                    FixResult result{};
                    bool wrote = false;
                    while (queue_.TryPop(&result)) {
                        if (result.status != FixStatus::initialized && result.status != FixStatus::paid) {
                            Log(SMEDLEY_LOG_FAILURE, StatusName(result.status));
                        }
                        EmitTelemetry(result);
                        if (debug_ && output_) {
                            output_ << result.date_raw << ',' << result.country_index << ",0x" << std::hex << result.metadata_flags
                                    << std::dec << ',' << result.state_id << ',' << StatusName(result.status) << ",0x" << std::hex
                                    << result.flags << std::dec << ',' << result.state_count << ',' << result.province_count << ','
                                    << result.pop_count << ',' << result.paid_pop_count << ',' << result.verified_pop_count << ','
                                    << result.state_pool_raw << ',' << result.payout_raw << ',' << result.discarded_raw << ','
                                    << AllocationStatusName(result.allocation_status) << ',' << result.mutation_result << ','
                                    << dropped_.load(std::memory_order_relaxed) << '\n';
                            wrote = true;
                        }
                    }
                    if (wrote) output_.flush();
                    else Sleep(10);
                }
            }
            void EmitTelemetry(const FixResult &result) noexcept {
                const auto status = TelemetryStringField("status", StatusName(result.status));
                const auto country = TelemetryIntField("country_index", result.country_index);
                const SmedleyTelemetryFieldV1 payload[] = {
                    TelemetryIntField("state_id", result.state_id), TelemetryIntField("payout_raw", result.payout_raw),
                    TelemetryIntField("mutation_result", result.mutation_result),
                };
                telemetry_.Emit("interest.fix.health", "metadata-degraded", result.date_raw, false, &country, 1,
                    &status, 1, true);
                if (result.status == FixStatus::paid) {
                    telemetry_.Emit("interest.fix.value", "metadata-degraded", result.date_raw, false, &country, 1,
                        payload, 3, true);
                }
            }

            ResultQueue<result_queue_capacity> queue_{};
            std::array<SmedleyInterestStateSnapshotV1, SMEDLEY_INTEREST_POOL_MAX_STATES> states_{};
            std::array<SmedleyInterestPopSnapshotV1, SMEDLEY_INTEREST_POOL_MAX_POPS> pops_{};
            std::array<AllocationEntry, SMEDLEY_INTEREST_POOL_MAX_POPS> allocations_{};
            std::array<SmedleyInterestPayoutV1, SMEDLEY_INTEREST_POOL_MAX_POPS> payouts_{};
            std::array<uint32_t, SMEDLEY_INTEREST_POOL_MAX_POPS> order_{};
            SmedleyEventServicesApiV1 event_api_{};
            SmedleyInterestPoolApiV1 interest_api_{};
            SmedleyLoggingApiV1 logging_api_{};
            SmedleyEventServicesRegistration registration_ = 0;
            TelemetryBridge telemetry_{};
            std::ofstream output_;
            std::thread worker_;
            std::atomic<uint64_t> dropped_{0};
            std::atomic<bool> stop_{false};
            bool initialized_ = false, disabled_ = false, needs_cleanup_ = true, debug_ = false;
            uint32_t state_handles_ = 0, pop_handles_ = 0, payout_handles_ = 0;
        };

        State global_state;
    }
}

namespace
{
    struct PluginInstance { uint32_t loaded = 0; };
    SmedleyPluginResult SMEDLEY_PLUGIN_CALL Create(void *instance, uint32_t size) {
        if (instance == nullptr || size != sizeof(PluginInstance)) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
        new (instance) PluginInstance{};
        return SMEDLEY_PLUGIN_SUCCESS;
    }
    SmedleyPluginResult SMEDLEY_PLUGIN_CALL Load(void *instance) {
        if (instance == nullptr) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
        try {
            if (!interest_bug_fix::global_state.Load()) return SMEDLEY_PLUGIN_FAILURE;
            static_cast<PluginInstance *>(instance)->loaded = 1;
            return SMEDLEY_PLUGIN_SUCCESS;
        } catch (...) {
            interest_bug_fix::global_state.Unload();
            return SMEDLEY_PLUGIN_FAILURE;
        }
    }
    SmedleyPluginResult SMEDLEY_PLUGIN_CALL Unload(void *instance) {
        if (instance == nullptr) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
        if (static_cast<PluginInstance *>(instance)->loaded != 0) interest_bug_fix::global_state.Unload();
        return SMEDLEY_PLUGIN_SUCCESS;
    }
    void SMEDLEY_PLUGIN_CALL Destroy(void *instance) { if (instance != nullptr) static_cast<PluginInstance *>(instance)->~PluginInstance(); }
}

SMEDLEY_PLUGIN_EXPORT SmedleyPluginResult SMEDLEY_PLUGIN_CALL SmedleyPluginGetApiV1(SmedleyPluginApiV1 *api)
{
    if (api == nullptr || api->struct_size != sizeof(*api) || api->version != SMEDLEY_PLUGIN_ABI_VERSION_V1
        || api->reserved[0] || api->reserved[1] || api->reserved[2] || api->reserved[3]) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
    api->instance_size = sizeof(PluginInstance);
    api->instance_alignment = alignof(PluginInstance);
    api->create = &Create;
    api->load = &Load;
    api->unload = &Unload;
    api->destroy = &Destroy;
    return SMEDLEY_PLUGIN_SUCCESS;
}
