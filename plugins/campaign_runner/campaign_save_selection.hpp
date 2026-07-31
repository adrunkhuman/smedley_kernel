#pragma once

#include <string_view>

namespace campaign_runner
{
    bool CanSelectRequestedSave(std::string_view requested_basename, std::string_view existing_basename);
}
