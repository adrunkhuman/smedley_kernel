#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <smedley/event_services_api.h>

namespace smedley::events { class BankInterestEvent; }

namespace smedley::game_state
{
    template <size_t Capacity>
    struct TelemetryEntityIndex
    {
        struct Entry { uintptr_t address = 0; uint32_t id = 0; };

        std::array<Entry, Capacity> by_address{};
        std::array<uint32_t, Capacity + 1> by_id{};
        uint32_t next_id = 1;

        void reset() noexcept { by_address.fill({}); by_id.fill(0); next_id = 1; }

        uint32_t find_or_insert(uintptr_t address) noexcept
        {
            if (address == 0) return 0;
            uint32_t index = static_cast<uint32_t>((address >> 4) ^ (address >> 17)) % Capacity;
            for (uint32_t attempt = 0; attempt < Capacity; ++attempt, index = (index + 1) % Capacity) {
                auto &entry = by_address[index];
                if (entry.address == address) return entry.id;
                if (entry.address != 0) continue;
                if (next_id > Capacity) return 0;
                entry = {address, next_id};
                by_id[next_id] = index + 1;
                return next_id++;
            }
            return 0;
        }

        uintptr_t find(uint32_t id) const noexcept
        {
            const uint32_t index = id == 0 || id >= next_id ? 0 : by_id[id];
            return index == 0 ? 0 : by_address[index - 1].address;
        }
    };

    constexpr uint64_t TelemetryOpaqueEntityHandle(uint32_t session_id, uint32_t entity_id) noexcept
    {
        return session_id == 0 || entity_id == 0 ? 0
            : (static_cast<uint64_t>(session_id) << 32) | entity_id;
    }

    bool BindBankInterestGameServices(SmedleyBankInterestAuthority authority,
        events::BankInterestEvent &event) noexcept;
    void UnbindBankInterestGameServices(SmedleyBankInterestAuthority authority) noexcept;
}
