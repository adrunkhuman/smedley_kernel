#include "economic_telemetry_core.hpp"

#include <limits>
#include <string_view>

namespace interest_probe
{
    namespace
    {
        bool ParseInteger(std::wstring_view argument, std::wstring_view prefix, int32_t *value)
        {
            if (argument.rfind(prefix, 0) != 0 || argument.size() == prefix.size()) return false;
            size_t parsed = 0;
            try {
                const long long candidate = std::stoll(std::wstring(argument.substr(prefix.size())), &parsed);
                if (parsed != argument.size() - prefix.size()
                    || candidate < (std::numeric_limits<int32_t>::min)()
                    || candidate > (std::numeric_limits<int32_t>::max)()) return false;
                *value = static_cast<int32_t>(candidate);
                return true;
            } catch (...) {
                return false;
            }
        }

        bool ContainsStateCategory(std::wstring_view value)
        {
            size_t begin = 0;
            while (begin <= value.size()) {
                const size_t end = value.find(L',', begin);
                const size_t length = end == std::wstring_view::npos ? value.size() - begin : end - begin;
                if (value.substr(begin, length) == L"state") return true;
                if (end == std::wstring_view::npos) break;
                begin = end + 1;
            }
            return false;
        }
    }

    CaptureConfig ParseEconomicTelemetryArguments(const std::vector<std::wstring> &arguments)
    {
        CaptureConfig config{};
        for (const auto &argument : arguments) {
            constexpr std::wstring_view categories_prefix = L"-smedley-telemetry-categories=";
            if (argument.rfind(categories_prefix, 0) == 0) {
                config.enabled = ContainsStateCategory(std::wstring_view(argument).substr(categories_prefix.size()));
                continue;
            }
            int32_t value = 0;
            if (ParseInteger(argument, L"-smedley-telemetry-sample-days=", &value)) {
                if (value >= 1 && value <= 365) config.sample_days = value;
            } else if (ParseInteger(argument, L"-smedley-telemetry-start-date-raw=", &value)) {
                config.start_date_raw = value;
            } else if (ParseInteger(argument, L"-smedley-telemetry-end-date-raw=", &value)) {
                config.end_date_raw = value;
            }
        }
        return config;
    }

    bool ShouldCaptureEconomicDate(int32_t date, const CaptureConfig &config,
                                   std::optional<int32_t> *last_observed_date,
                                   std::optional<int32_t> *last_sampled_date)
    {
        if ((config.start_date_raw && date < *config.start_date_raw)
            || (config.end_date_raw && date > *config.end_date_raw)) return false;
        if (*last_observed_date && date < **last_observed_date) last_sampled_date->reset();
        *last_observed_date = date;
        if (*last_sampled_date
            && static_cast<int64_t>(date) - **last_sampled_date < static_cast<int64_t>(config.sample_days) * 24) {
            return false;
        }
        *last_sampled_date = date;
        return true;
    }

    void AddEconomicValue(int64_t value, int64_t *total, uint32_t *flags,
                          uint32_t overflow_flag)
    {
        if ((value > 0 && *total > (std::numeric_limits<int64_t>::max)() - value)
            || (value < 0 && *total < (std::numeric_limits<int64_t>::min)() - value)) {
            *flags |= overflow_flag;
            return;
        }
        *total += value;
    }

    int64_t UtilizationBasisPoints(uint32_t value, uint32_t limit)
    {
        if (limit == 0) return 0;
        return static_cast<int64_t>(value) * 10000 / limit;
    }
}
