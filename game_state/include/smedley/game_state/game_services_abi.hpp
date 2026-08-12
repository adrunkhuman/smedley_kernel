#pragma once

#include <cstdint>

#include <smedley/event_services_api.h>

namespace smedley::events { class BankInterestEvent; }

namespace smedley::game_state
{
    bool BindBankInterestGameServices(SmedleyBankInterestAuthority authority,
        events::BankInterestEvent &event) noexcept;
    void UnbindBankInterestGameServices(SmedleyBankInterestAuthority authority) noexcept;
}
