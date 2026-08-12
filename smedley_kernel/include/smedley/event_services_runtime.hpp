#pragma once

#include <smedley/event_services_api.h>

namespace smedley
{
    void DispatchBankInterestEventServices(
        uint32_t phase, uint32_t country_index, bool distributes_to_states) noexcept;
    bool DispatchCampaignConsoleEventServices(const SmedleyCampaignConsoleInputV1 &input,
        SmedleyCampaignConsoleResultV1 *result) noexcept;
    bool IsBankInterestAuthorityActive(SmedleyBankInterestAuthority authority,
        uint32_t phase, uint32_t country_index) noexcept;
}
