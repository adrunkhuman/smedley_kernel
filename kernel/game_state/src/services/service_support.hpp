#pragma once

#include <cstdint>
#include <mutex>
#include <thread>

#include <smedley/campaign_runtime_api.h>
#include <smedley/game_state/runtime.hpp>

namespace smedley::game_state::services
{
    extern std::recursive_mutex metadata_mutex;
    extern std::thread::id service_owner_thread;

    bool IsServiceOwnerThread();
    void CopyObserverCountry(const ObserverCountrySnapshot &from, SmedleyObserverCountrySnapshotV1 *to);
    SmedleyCampaignRuntimeResult CampaignSessionStatus(SmedleyCampaignSession handle);
    void RetireCampaignAutomations(SmedleyCampaignSession session);
    void RefreshCampaignAutomationEpoch(SmedleyCampaignSession session, uint64_t epoch);

    template <typename Record>
    bool ValidRecord(const Record *record, uint32_t version)
    {
        if (record == nullptr || record->struct_size != sizeof(Record) || record->version != version) return false;
        for (const auto value : record->reserved) if (value != 0) return false;
        return true;
    }
}
