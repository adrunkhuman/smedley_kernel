#include "campaign_launch_arguments.hpp"

#include <limits>

namespace campaign_runner
{
    bool ParseCampaignLaunchArguments(const std::vector<std::wstring> &arguments,
                                      CampaignLaunchArguments *result, std::string *error)
    {
        if (result == nullptr || error == nullptr) return false;
        *result = {};
        bool timeout_requested = false;
        auto fail = [&](const char *message) { *error = message; return false; };
        auto malformed = [](const std::wstring &argument, const wchar_t *name) {
            return argument.rfind(name, 0) == 0;
        };
        for (size_t index = 1; index < arguments.size(); ++index) {
            const auto &argument = arguments[index];
            if (argument.rfind(L"-smedley-save=", 0) == 0) {
                if (result->save || argument.size() == 14) return fail("-smedley-save requires one non-empty value");
                result->save = argument.substr(14);
            } else if (malformed(argument, L"-smedley-save")) return fail("malformed -smedley-save argument");
            else if (argument.rfind(L"-smedley-view-tag=", 0) == 0) {
                if (result->view_tag || argument.size() == 18) return fail("-smedley-view-tag requires one non-empty value");
                result->view_tag = argument.substr(18);
            } else if (malformed(argument, L"-smedley-view-tag")) return fail("malformed -smedley-view-tag argument");
            else if (argument.rfind(L"-smedley-speed=", 0) == 0) {
                const auto value = argument.substr(15);
                if (result->speed_requested || value.size() != 1 || value[0] < L'1' || value[0] > L'5') return fail("-smedley-speed must appear once with a value from 1 through 5");
                result->speed = value[0] - L'0';
                result->speed_requested = true;
            } else if (malformed(argument, L"-smedley-speed")) return fail("malformed -smedley-speed argument");
            else if (argument == L"-smedley-observe") {
                if (result->observe) return fail("-smedley-observe must not be repeated");
                result->observe = true;
            } else if (malformed(argument, L"-smedley-observe")) return fail("malformed -smedley-observe argument");
            else if (argument == L"-smedley-start-paused") {
                if (result->start_paused) return fail("-smedley-start-paused must not be repeated");
                result->start_paused = true;
            } else if (malformed(argument, L"-smedley-start-paused")) return fail("malformed -smedley-start-paused argument");
            else {
                const bool days = argument == L"-smedley-run-days" || argument.rfind(L"-smedley-run-days=", 0) == 0;
                const bool target = argument == L"-smedley-run-until-date-raw" || argument.rfind(L"-smedley-run-until-date-raw=", 0) == 0;
                const bool timeout = argument == L"-smedley-run-timeout-seconds" || argument.rfind(L"-smedley-run-timeout-seconds=", 0) == 0;
                if (!days && !target && !timeout) continue;
                const size_t prefix = days ? 18 : target ? 28 : 29;
                if (argument.size() <= prefix || argument[prefix - 1] != L'=') return fail("malformed benchmark run argument");
                size_t used = 0;
                long long value = 0;
                try { value = std::stoll(argument.substr(prefix), &used); } catch (...) { return fail("benchmark run arguments must be full signed integers"); }
                if (used != argument.size() - prefix || value < (std::numeric_limits<int>::min)() || value > (std::numeric_limits<int>::max)()) return fail("benchmark run arguments must be full signed integers");
                if (days) {
                    if (result->run_condition.days || value < 1 || value > 1000000) return fail("-smedley-run-days must appear once with a value from 1 through 1000000");
                    result->run_condition.days = static_cast<int>(value);
                } else if (target) {
                    if (result->run_condition.target_date_raw) return fail("-smedley-run-until-date-raw must appear once with an int32 value");
                    result->run_condition.target_date_raw = static_cast<int>(value);
                } else {
                    if (timeout_requested || value < 1 || value > 86400) return fail("-smedley-run-timeout-seconds must appear once with a value from 1 through 86400");
                    result->run_condition.timeout_seconds = static_cast<int>(value);
                    timeout_requested = true;
                }
            }
        }
        if ((result->speed_requested || result->start_paused || result->run_condition.requested()) && !result->save) return fail("campaign run controls require -smedley-save");
        if (result->observe && !result->save) return fail("-smedley-observe requires -smedley-save");
        if (result->view_tag && !result->observe) return fail("-smedley-view-tag requires -smedley-observe");
        if (result->observe && result->start_paused) return fail("observer mode cannot start paused because its watchdog requires advancement");
        if (result->run_condition.days && result->run_condition.target_date_raw) return fail("-smedley-run-days and -smedley-run-until-date-raw are mutually exclusive");
        if (result->run_condition.requested() && (result->start_paused || result->view_tag)) return fail("benchmark target runs require unpaused start and do not support -smedley-view-tag");
        return true;
    }
}
