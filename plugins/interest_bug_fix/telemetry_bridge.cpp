#include "telemetry_bridge.hpp"

#include <array>
#include <cstring>
#include <filesystem>

#include <windows.h>
#include <psapi.h>

namespace interest_bug_fix
{
    namespace
    {
        bool ModulePath(HMODULE module, std::filesystem::path *path)
        {
            std::array<wchar_t, 32768> buffer{};
            const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0 || length >= buffer.size()) return false;
            *path = std::filesystem::path(std::wstring(buffer.data(), length)).lexically_normal();
            return true;
        }

        bool SamePath(const std::filesystem::path &left, const std::filesystem::path &right)
        {
            const std::wstring left_value = left.native();
            const std::wstring right_value = right.native();
            return CompareStringOrdinal(left_value.data(), static_cast<int>(left_value.size()),
                right_value.data(), static_cast<int>(right_value.size()), TRUE) == CSTR_EQUAL;
        }
    }

    SmedleyTelemetryFieldV1 TelemetryIntField(const char *key, int64_t value)
    {
        SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key,
            static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_INT64, 0, {}};
        field.value.int64_value = value;
        return field;
    }

    SmedleyTelemetryFieldV1 TelemetryBoolField(const char *key, bool value)
    {
        SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key,
            static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_BOOL, 0, {}};
        field.value.bool_value = value ? 1u : 0u;
        return field;
    }

    SmedleyTelemetryFieldV1 TelemetryStringField(const char *key, const char *value)
    {
        SmedleyTelemetryFieldV1 field{sizeof(field), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key,
            static_cast<uint32_t>(std::strlen(key)), SMEDLEY_TELEMETRY_UTF8_STRING, 0, {}};
        field.value.string_value = {value, static_cast<uint32_t>(std::strlen(value)), 0};
        return field;
    }

    SmedleyTelemetryEmitV1Fn TelemetryBridge::Resolve()
    {
        if (emit_ != nullptr || reliable_emit_ != nullptr || resolution_attempted_) {
            return emit_;
        }
        resolution_attempted_ = true;
        HMODULE own_module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&TelemetryIntField), &own_module)) {
            return nullptr;
        }
        std::filesystem::path own_path;
        if (!ModulePath(own_module, &own_path)) return nullptr;
        const auto expected = (own_path.parent_path() / L"telemetry.dll").lexically_normal();

        std::array<HMODULE, 1024> modules{};
        DWORD required = 0;
        if (!EnumProcessModules(GetCurrentProcess(), modules.data(), static_cast<DWORD>(sizeof(modules)), &required)
            || required > sizeof(modules)) {
            return nullptr;
        }
        const size_t module_count = required / sizeof(HMODULE);
        for (size_t index = 0; index < module_count; ++index) {
            std::filesystem::path candidate;
            if (modules[index] == nullptr || !ModulePath(modules[index], &candidate) || !SamePath(expected, candidate)) continue;
            emit_ = reinterpret_cast<SmedleyTelemetryEmitV1Fn>(
                GetProcAddress(modules[index], SMEDLEY_TELEMETRY_EMIT_V1_SYMBOL));
            reliable_emit_ = reinterpret_cast<SmedleyTelemetryEmitV1Fn>(
                GetProcAddress(modules[index], SMEDLEY_TELEMETRY_EMIT_RELIABLE_V1_SYMBOL));
            break;
        }
        return emit_;
    }

    SmedleyTelemetryResult TelemetryBridge::Emit(const char *event_type, const char *quality, int32_t date_raw,
                                                  const SmedleyTelemetryFieldV1 *entities, uint32_t entity_count,
                                                  const SmedleyTelemetryFieldV1 *payload, uint32_t payload_count,
                                                  bool reliable)
    {
        SmedleyTelemetryEmitV1Fn emit = nullptr;
        try {
            emit = Resolve();
        } catch (...) {
            return SMEDLEY_TELEMETRY_UNAVAILABLE;
        }
        if (reliable && reliable_emit_ != nullptr) emit = reliable_emit_;
        if (emit == nullptr) return SMEDLEY_TELEMETRY_UNAVAILABLE;
        SmedleyTelemetryRecordV1 record{sizeof(record), SMEDLEY_TELEMETRY_ABI_VERSION_V1,
            SMEDLEY_TELEMETRY_RECORD_HAS_GAME_DATE, 0,
            event_type, static_cast<uint32_t>(std::strlen(event_type)), "state", 5,
            "v2game-3.04", 11, quality, static_cast<uint32_t>(std::strlen(quality)), date_raw, 0,
            entities, entity_count, payload, payload_count, {0, 0, 0, 0}};
        try {
            return emit(&record);
        } catch (...) {
            return SMEDLEY_TELEMETRY_DROPPED;
        }
    }
}
