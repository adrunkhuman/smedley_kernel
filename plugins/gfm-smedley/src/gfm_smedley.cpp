#include "province_reconciliation.hpp"

#include <smedley/event_api.h>
#include <smedley/logging_api.h>
#include <smedley/province_modifier_reconciliation_api.h>
#define SMEDLEY_PLUGIN_BUILD
#include <smedley/plugin_abi.h>

#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <string_view>
#include <vector>

namespace gfm_smedley
{
    namespace
    {
        constexpr std::array<unsigned char, 32> pinned_gfm_smedley_lf_hash{
            0x71, 0x08, 0xc5, 0xeb, 0x90, 0xef, 0x4f, 0xc2,
            0xb8, 0x58, 0xa6, 0x4e, 0xd2, 0x41, 0x11, 0x20,
            0x6f, 0x3f, 0x0f, 0xeb, 0x02, 0xe7, 0xdc, 0x61,
            0x94, 0x8a, 0x61, 0x32, 0xfd, 0x9d, 0x96, 0x96,
        };
        constexpr std::array<unsigned char, 32> pinned_gfm_smedley_crlf_hash{
            0x34, 0x70, 0x9c, 0xb3, 0xae, 0x8c, 0x40, 0xcb,
            0xa4, 0x41, 0xa0, 0x5c, 0x6c, 0xba, 0x57, 0x95,
            0x2e, 0x84, 0xd5, 0xc6, 0xa2, 0x34, 0x46, 0x48,
            0x79, 0xe1, 0x42, 0x54, 0xdc, 0x15, 0x46, 0xc0,
        };
        constexpr wchar_t gfm_smedley_argument[] = L"-mod=mod/GFMSmedley.mod";
        constexpr char component[] = "gfm-smedley";

        bool HashMatches(const std::filesystem::path &path)
        {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            std::vector<unsigned char> hash_object;
            auto cleanup = [&] {
                if (hash != nullptr) BCryptDestroyHash(hash);
                if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
            };
            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
            DWORD object_size = 0;
            DWORD copied = 0;
            if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size),
                    sizeof(object_size), &copied, 0) < 0) {
                cleanup();
                return false;
            }
            hash_object.resize(object_size);
            if (BCryptCreateHash(algorithm, &hash, hash_object.data(), object_size, nullptr, 0, 0) < 0) {
                cleanup();
                return false;
            }
            std::ifstream input(path, std::ios::binary);
            std::array<char, 64 * 1024> buffer{};
            while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
                if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                        static_cast<ULONG>(input.gcount()), 0) < 0) {
                    cleanup();
                    return false;
                }
            }
            std::array<unsigned char, 32> actual{};
            const bool matches = input.eof()
                && BCryptFinishHash(hash, actual.data(), actual.size(), 0) >= 0
                && (actual == pinned_gfm_smedley_lf_hash || actual == pinned_gfm_smedley_crlf_hash);
            cleanup();
            return matches;
        }

        bool PinnedGfmSmedleyInstalled()
        {
            std::array<wchar_t, 32768> executable{};
            const DWORD length = GetModuleFileNameW(nullptr, executable.data(), executable.size());
            if (length == 0 || length == executable.size()) return false;
            const auto game_dir = std::filesystem::path(std::wstring_view(executable.data(), length)).parent_path();
            return HashMatches(game_dir / L"mod" / L"GFM-smedley" / L"events" / L"V2ME Cleanup (GFM).txt");
        }

        struct LaunchOptions
        {
            bool gfm_smedley = false;
        };

        LaunchOptions ReadLaunchOptions()
        {
            LaunchOptions options{};
            int count = 0;
            wchar_t **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
            if (arguments == nullptr) return options;
            for (int index = 1; index < count; ++index) {
                const std::wstring_view argument(arguments[index]);
                if (argument == gfm_smedley_argument) options.gfm_smedley = true;
            }
            LocalFree(arguments);
            return options;
        }

        void SetKey(SmedleyReconciliationKeyV1 *key, const char *value) noexcept
        {
            *key = {};
            key->struct_size = sizeof(*key);
            key->version = 1;
            key->bytes = static_cast<uint32_t>(std::strlen(value));
            std::memcpy(key->value, value, key->bytes + 1);
        }

        class State final
        {
        public:
            bool Load()
            {
                const auto options = ReadLaunchOptions();
                if (!options.gfm_smedley || !PinnedGfmSmedleyInstalled() || !AcquireServices()) return false;
                SetKey(&modifier_keys_[0], "slave_trade_decadence_1");
                SetKey(&modifier_keys_[1], "slave_trade_decadence_2");
                SetKey(&modifier_keys_[2], "slave_trade_decadence_3");
                SetKey(&modifier_keys_[3], "slave_trade_decadence_4");
                SetKey(&owner_option_key_, "no_slavery");
                if (event_api_.register_daily(&DailyCallback, this, &daily_registration_) != SMEDLEY_EVENT_SUCCESS) {
                    return false;
                }
                if (reconciliation_api_.register_callback(
                        &ReconciliationCallback, this, &reconciliation_registration_)
                    != SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_SUCCESS) {
                    event_api_.unregister(daily_registration_);
                    daily_registration_ = 0;
                    return false;
                }
                enabled_ = true;
                Log(SMEDLEY_LOG_INFO, "pinned GFM-smedley reconciliation registered");
                return true;
            }

            void Unload() noexcept
            {
                if (reconciliation_registration_ != 0) {
                    reconciliation_api_.unregister_callback(reconciliation_registration_);
                }
                if (daily_registration_ != 0) event_api_.unregister(daily_registration_);
                reconciliation_registration_ = 0;
                daily_registration_ = 0;
                enabled_ = false;
            }

            SmedleyEventCallbackResult OnDaily(const SmedleyDailyEventV1 *event) noexcept
            {
                if (!enabled_ || event == nullptr || event->struct_size != sizeof(*event)
                    || event->version != SMEDLEY_DAILY_EVENT_VERSION_V1) return SMEDLEY_EVENT_CALLBACK_DISABLE;
                if (ReconciliationDue(event->game_date_raw, &schedule_)) due_ = true;
                return SMEDLEY_EVENT_CALLBACK_CONTINUE;
            }

            SmedleyProvinceModifierReconciliationCallbackResult OnReconciliation(
                const SmedleyProvinceModifierReconciliationEventV1 *event) noexcept
            {
                if (!enabled_ || event == nullptr || event->struct_size != sizeof(*event)
                    || event->version != SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_EVENT_VERSION_V1
                    || event->authority == 0) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALLBACK_DISABLE;
                }
                if (!due_) return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALLBACK_CONTINUE;
                const auto status = Reconcile(event->authority);
                if (status == SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_DEFERRED) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALLBACK_CONTINUE;
                }
                if (status != SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_SUCCESS) {
                    TerminateProcess(GetCurrentProcess(), ERROR_OPERATION_ABORTED);
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALLBACK_DISABLE;
                }
                due_ = false;
                return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALLBACK_CONTINUE;
            }

        private:
            bool AcquireServices()
            {
                const HMODULE kernel = GetModuleHandleW(L"smedley_kernel.dll");
                if (kernel == nullptr) return false;
                const auto get_events = reinterpret_cast<SmedleyGetEventApiV1Fn>(
                    GetProcAddress(kernel, SMEDLEY_EVENT_GET_API_V1_SYMBOL));
                const auto get_reconciliation = reinterpret_cast<SmedleyGetProvinceModifierReconciliationApiV1Fn>(
                    GetProcAddress(kernel, SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_GET_API_V1_SYMBOL));
                const auto get_logging = reinterpret_cast<SmedleyGetLoggingApiV1Fn>(
                    GetProcAddress(kernel, SMEDLEY_LOGGING_GET_API_V1_SYMBOL));
                if (get_events == nullptr || get_reconciliation == nullptr || get_logging == nullptr) return false;
                event_api_ = {sizeof(event_api_), SMEDLEY_EVENT_API_VERSION_V1};
                reconciliation_api_ = {sizeof(reconciliation_api_),
                    SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_API_VERSION_V1};
                logging_api_ = {sizeof(logging_api_), SMEDLEY_LOGGING_API_VERSION_V1};
                return get_events(&event_api_) == SMEDLEY_EVENT_SUCCESS
                    && get_reconciliation(&reconciliation_api_) == SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_SUCCESS
                    && get_logging(&logging_api_) == SMEDLEY_LOGGING_SUCCESS
                    && event_api_.register_daily != nullptr && event_api_.unregister != nullptr
                    && reconciliation_api_.register_callback != nullptr
                    && reconciliation_api_.unregister_callback != nullptr
                    && reconciliation_api_.collect != nullptr && reconciliation_api_.apply != nullptr
                    && logging_api_.write != nullptr;
            }

            SmedleyProvinceModifierReconciliationResult Reconcile(
                SmedleyProvinceModifierReconciliationAuthority authority) noexcept
            {
                uint32_t province_count = 0;
                uint32_t country_count = 0;
                auto status = reconciliation_api_.collect(authority, modifier_keys_.data(), modifier_keys_.size(),
                    &owner_option_key_, service_provinces_.data(), service_provinces_.size(), &province_count,
                    service_countries_.data(), service_countries_.size(), &country_count);
                if (status != SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_SUCCESS) return status;
                for (uint32_t index = 0; index < province_count; ++index) {
                    const auto &source = service_provinces_[index];
                    if (source.struct_size != sizeof(source) || source.version != 1) {
                        return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
                    }
                    policy_provinces_[index] = {source.province, source.owner, source.present_modifier_mask};
                }
                for (uint32_t index = 0; index < country_count; ++index) {
                    const auto &source = service_countries_[index];
                    if (source.struct_size != sizeof(source) || source.version != 1) {
                        return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE;
                    }
                    policy_countries_[index] = {source.country, source.owner_option_selected};
                }
                uint32_t removal_count = 0;
                if (PlanSlaveTradeReconciliation(policy_provinces_.data(), province_count,
                        policy_countries_.data(), country_count, policy_removals_.data(),
                        policy_removals_.size(), &removal_count) != ReconciliationPlanStatus::success) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_PRECONDITION_FAILED;
                }
                if (removal_count == 0) return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_SUCCESS;
                uint32_t output = 0;
                for (uint32_t province_index = 0; province_index < province_count; ++province_index) {
                    const auto &province = service_provinces_[province_index];
                    if (output >= removal_count || province.province != policy_removals_[output].province) continue;
                    auto &request = service_removals_[output++];
                    request = {};
                    request.struct_size = sizeof(request);
                    request.version = 1;
                    request.province = province.province;
                    request.expected_owner = province.owner;
                    request.expected_present_modifier_mask = province.present_modifier_mask;
                    request.remove_modifier_mask = policy_removals_[output - 1].modifier_mask;
                    request.expected_owner_option_selected = 1;
                }
                if (output != removal_count) return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_PRECONDITION_FAILED;
                SmedleyReconciliationBatchResultV1 result{sizeof(result), 1};
                status = reconciliation_api_.apply(authority, service_removals_.data(), removal_count, &result);
                if (status == SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_SUCCESS
                    && (result.write_count != result.verified_count || result.failed_index != removal_count)) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_PARTIAL_MUTATION;
                }
                return status;
            }

            static SmedleyEventCallbackResult SMEDLEY_EVENT_CALL DailyCallback(
                void *context, const SmedleyDailyEventV1 *event) noexcept
            {
                try {
                    return context == nullptr ? SMEDLEY_EVENT_CALLBACK_DISABLE
                        : static_cast<State *>(context)->OnDaily(event);
                } catch (...) {
                    return SMEDLEY_EVENT_CALLBACK_DISABLE;
                }
            }

            static SmedleyProvinceModifierReconciliationCallbackResult
            SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALL ReconciliationCallback(
                void *context, const SmedleyProvinceModifierReconciliationEventV1 *event) noexcept
            {
                try {
                    return context == nullptr ? SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALLBACK_DISABLE
                        : static_cast<State *>(context)->OnReconciliation(event);
                } catch (...) {
                    return SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALLBACK_DISABLE;
                }
            }

            void Log(SmedleyLogLevel level, const char *message) noexcept
            {
                if (logging_api_.write != nullptr) {
                    logging_api_.write(level, component, sizeof(component) - 1,
                        message, static_cast<uint32_t>(std::strlen(message)));
                }
            }

            SmedleyEventApiV1 event_api_{};
            SmedleyProvinceModifierReconciliationApiV1 reconciliation_api_{};
            SmedleyLoggingApiV1 logging_api_{};
            SmedleyEventRegistration daily_registration_ = 0;
            SmedleyProvinceModifierReconciliationRegistration reconciliation_registration_ = 0;
            ReconciliationSchedule schedule_{};
            bool enabled_ = false;
            bool due_ = true;
            std::array<SmedleyReconciliationKeyV1, 4> modifier_keys_{};
            SmedleyReconciliationKeyV1 owner_option_key_{};
            std::array<SmedleyReconciliationProvinceSnapshotV1,
                SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_PROVINCES> service_provinces_{};
            std::array<SmedleyReconciliationCountrySnapshotV1,
                SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_COUNTRIES> service_countries_{};
            std::array<ProvinceModifierSnapshot,
                SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_PROVINCES> policy_provinces_{};
            std::array<CountryPolicySnapshot,
                SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_COUNTRIES> policy_countries_{};
            std::array<ProvinceModifierRemoval,
                SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_PROVINCES> policy_removals_{};
            std::array<SmedleyReconciliationRemovalV1,
                SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_PROVINCES> service_removals_{};
        };

        static_assert(sizeof(State) <= SMEDLEY_PLUGIN_MAX_INSTANCE_BYTES, "GFM-smedley state exceeds host storage");

        SmedleyPluginResult SMEDLEY_PLUGIN_CALL Create(void *instance, uint32_t size)
        {
            if (instance == nullptr || size != sizeof(State)) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
            new (instance) State();
            return SMEDLEY_PLUGIN_SUCCESS;
        }

        SmedleyPluginResult SMEDLEY_PLUGIN_CALL Load(void *instance)
        {
            if (instance == nullptr) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
            try {
                return static_cast<State *>(instance)->Load() ? SMEDLEY_PLUGIN_SUCCESS : SMEDLEY_PLUGIN_FAILURE;
            } catch (...) {
                return SMEDLEY_PLUGIN_FAILURE;
            }
        }

        SmedleyPluginResult SMEDLEY_PLUGIN_CALL Unload(void *instance)
        {
            if (instance == nullptr) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
            static_cast<State *>(instance)->Unload();
            return SMEDLEY_PLUGIN_SUCCESS;
        }

        void SMEDLEY_PLUGIN_CALL Destroy(void *instance)
        {
            if (instance != nullptr) static_cast<State *>(instance)->~State();
        }
    }
}

SMEDLEY_PLUGIN_EXPORT SmedleyPluginResult SMEDLEY_PLUGIN_CALL SmedleyPluginGetApiV1(SmedleyPluginApiV1 *api)
{
    if (api == nullptr || api->struct_size != sizeof(*api) || api->version != SMEDLEY_PLUGIN_ABI_VERSION_V1) {
        return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
    }
    for (const uint32_t value : api->reserved) {
        if (value != 0) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
    }
    api->instance_size = sizeof(gfm_smedley::State);
    api->instance_alignment = alignof(gfm_smedley::State);
    api->create = &gfm_smedley::Create;
    api->load = &gfm_smedley::Load;
    api->unload = &gfm_smedley::Unload;
    api->destroy = &gfm_smedley::Destroy;
    return SMEDLEY_PLUGIN_SUCCESS;
}
