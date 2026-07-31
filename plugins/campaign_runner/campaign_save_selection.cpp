#include "campaign_save_selection.hpp"

#include <string>

#include <windows.h>

namespace campaign_runner
{
    bool CanSelectRequestedSave(std::string_view requested_basename, std::string_view existing_basename)
    {
        if (requested_basename.empty() || existing_basename.size() != requested_basename.size()) {
            return !requested_basename.empty() && existing_basename.empty();
        }
        const std::wstring requested(requested_basename.begin(), requested_basename.end());
        const std::wstring existing(existing_basename.begin(), existing_basename.end());
        return CompareStringOrdinal(requested.data(), static_cast<int>(requested.size()),
                                    existing.data(), static_cast<int>(existing.size()), TRUE) == CSTR_EQUAL;
    }
}
