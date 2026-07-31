#pragma once

#include <optional>
#include <string>
#include <vector>

namespace campaign_runner
{
    struct CampaignRunCondition
    {
        std::optional<int> days;
        std::optional<int> target_date_raw;
        int timeout_seconds = 600;

        bool requested() const { return days.has_value() || target_date_raw.has_value(); }
    };

    struct CampaignLaunchArguments
    {
        std::optional<std::wstring> save;
        std::optional<std::wstring> view_tag;
        bool observe = false;
        int speed = 5;
        bool speed_requested = false;
        bool start_paused = false;
        CampaignRunCondition run_condition;
    };

    bool ParseCampaignLaunchArguments(const std::vector<std::wstring> &arguments,
                                      CampaignLaunchArguments *result, std::string *error);
}
