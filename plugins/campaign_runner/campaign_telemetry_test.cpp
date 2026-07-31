#include "campaign_telemetry.hpp"
#include "campaign_save_selection.hpp"

#include <gtest/gtest.h>

#include <deque>

namespace
{
    struct CapturedField
    {
        std::string key;
        uint32_t type = 0;
        int64_t int_value = 0;
        uint32_t bool_value = 0;
        std::string string_value;
    };

    std::string event_type;
    std::vector<CapturedField> entities;
    std::vector<CapturedField> payload;
    std::deque<SmedleyTelemetryResult> results;
    int calls = 0;

    CapturedField CopyField(const SmedleyTelemetryFieldV1 &field)
    {
        CapturedField copy{std::string(field.key, field.key_length), field.type};
        if (field.type == SMEDLEY_TELEMETRY_INT64) {
            copy.int_value = field.value.int64_value;
        } else if (field.type == SMEDLEY_TELEMETRY_BOOL) {
            copy.bool_value = field.value.bool_value;
        } else if (field.type == SMEDLEY_TELEMETRY_UTF8_STRING) {
            copy.string_value.assign(field.value.string_value.data, field.value.string_value.length);
        }
        return copy;
    }

    SmedleyTelemetryResult SMEDLEY_TELEMETRY_CALL Capture(const SmedleyTelemetryRecordV1 *record)
    {
        event_type.assign(record->event_type, record->event_type_length);
        entities.clear();
        payload.clear();
        for (uint32_t index = 0; index < record->entity_field_count; ++index) entities.push_back(CopyField(record->entity_fields[index]));
        for (uint32_t index = 0; index < record->payload_field_count; ++index) payload.push_back(CopyField(record->payload_fields[index]));
        ++calls;
        const auto result = results.empty() ? SMEDLEY_TELEMETRY_ACCEPTED : results.front();
        if (!results.empty()) results.pop_front();
        return result;
    }

    class CampaignTelemetryTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            event_type.clear();
            entities.clear();
            payload.clear();
            results.clear();
            calls = 0;
        }
    };
}

TEST_F(CampaignTelemetryTest, BuildsEveryTypedLifecycleContract)
{
    campaign_runner::CampaignTelemetry telemetry(&Capture);
    EXPECT_EQ(telemetry.SaveSelectionRequested(), SMEDLEY_TELEMETRY_ACCEPTED);
    EXPECT_EQ(event_type, "campaign.save_selection_requested");
    ASSERT_EQ(payload.size(), 1u);
    EXPECT_EQ(payload[0].key, "source");
    EXPECT_EQ(payload[0].type, SMEDLEY_TELEMETRY_UTF8_STRING);
    EXPECT_EQ(payload[0].string_value, "campaign_runner");

    EXPECT_EQ(telemetry.SaveLoadCompleted(), SMEDLEY_TELEMETRY_ACCEPTED);
    EXPECT_EQ(event_type, "campaign.save_load_completed");
    EXPECT_TRUE(entities.empty());
    EXPECT_TRUE(payload.empty());

    EXPECT_EQ(telemetry.Entered(true, 5, false), SMEDLEY_TELEMETRY_ACCEPTED);
    EXPECT_EQ(event_type, "campaign.entered");
    ASSERT_EQ(payload.size(), 3u);
    EXPECT_EQ(payload[0].key, "observer_requested");
    EXPECT_EQ(payload[0].type, SMEDLEY_TELEMETRY_BOOL);
    EXPECT_EQ(payload[0].bool_value, 1u);
    EXPECT_EQ(payload[1].key, "requested_speed");
    EXPECT_EQ(payload[1].type, SMEDLEY_TELEMETRY_INT64);
    EXPECT_EQ(payload[1].int_value, 5);
    EXPECT_EQ(payload[2].key, "requested_paused");
    EXPECT_EQ(payload[2].bool_value, 0u);

    EXPECT_EQ(telemetry.ObserverConfigured("D01"), SMEDLEY_TELEMETRY_ACCEPTED);
    EXPECT_EQ(event_type, "observer.configured");
    ASSERT_EQ(entities.size(), 1u);
    EXPECT_EQ(entities[0].key, "viewing_country");
    EXPECT_EQ(entities[0].type, SMEDLEY_TELEMETRY_UTF8_STRING);
    EXPECT_EQ(entities[0].string_value, "D01");
    ASSERT_EQ(payload.size(), 2u);
    EXPECT_EQ(payload[0].key, "full_ai_control");
    EXPECT_EQ(payload[0].bool_value, 1u);
    EXPECT_EQ(payload[1].key, "full_map_visibility");
    EXPECT_EQ(payload[1].bool_value, 1u);

    EXPECT_EQ(telemetry.SpeedConfigured(3, 3, 3), SMEDLEY_TELEMETRY_ACCEPTED);
    EXPECT_EQ(event_type, "speed.configured");
    ASSERT_EQ(payload.size(), 3u);
    EXPECT_EQ(payload[0].int_value, 3);
    EXPECT_EQ(payload[1].int_value, 3);
    EXPECT_EQ(payload[2].int_value, 3);

    EXPECT_EQ(telemetry.PauseConfigured(true, false, false), SMEDLEY_TELEMETRY_ACCEPTED);
    EXPECT_EQ(event_type, "pause.configured");
    ASSERT_EQ(payload.size(), 3u);
    EXPECT_EQ(payload[0].type, SMEDLEY_TELEMETRY_BOOL);
    EXPECT_EQ(payload[0].bool_value, 1u);
    EXPECT_EQ(payload[1].bool_value, 0u);
    EXPECT_EQ(payload[2].bool_value, 0u);
}

TEST_F(CampaignTelemetryTest, RetriesUnavailableAndTreatsOtherResultsAsTerminal)
{
    campaign_runner::CampaignTelemetry telemetry(&Capture);
    results = {SMEDLEY_TELEMETRY_UNAVAILABLE, SMEDLEY_TELEMETRY_ACCEPTED};
    EXPECT_EQ(telemetry.SaveSelectionRequested(), SMEDLEY_TELEMETRY_UNAVAILABLE);
    EXPECT_EQ(telemetry.SaveSelectionRequested(), SMEDLEY_TELEMETRY_ACCEPTED);
    EXPECT_EQ(calls, 2);

    campaign_runner::CampaignTelemetry dropped(&Capture);
    results = {SMEDLEY_TELEMETRY_DROPPED};
    EXPECT_EQ(dropped.SaveLoadCompleted(), SMEDLEY_TELEMETRY_DROPPED);
    EXPECT_EQ(dropped.SaveLoadCompleted(), SMEDLEY_TELEMETRY_FILTERED);
    campaign_runner::CampaignTelemetry filtered(&Capture);
    results = {SMEDLEY_TELEMETRY_FILTERED};
    EXPECT_EQ(filtered.Entered(false, 1, false), SMEDLEY_TELEMETRY_FILTERED);
    EXPECT_EQ(filtered.Entered(false, 1, false), SMEDLEY_TELEMETRY_FILTERED);
    campaign_runner::CampaignTelemetry invalid(&Capture);
    results = {SMEDLEY_TELEMETRY_INVALID};
    EXPECT_EQ(invalid.SpeedConfigured(1, 1, 1), SMEDLEY_TELEMETRY_INVALID);
    EXPECT_EQ(invalid.SpeedConfigured(1, 1, 1), SMEDLEY_TELEMETRY_FILTERED);
}

TEST_F(CampaignTelemetryTest, ChoosesOnlyTheExactSiblingTelemetryPath)
{
    const std::wstring runner = L"C:\\Game\\plugins\\campaign_runner.dll";
    EXPECT_FALSE(campaign_runner::IsSiblingTelemetryPath(runner, L"C:\\Elsewhere\\telemetry.dll"));
    EXPECT_TRUE(campaign_runner::IsSiblingTelemetryPath(runner, L"c:\\game\\plugins\\TELEMETRY.dll"));
}

TEST_F(CampaignTelemetryTest, SelectsOnlyAnEmptyOrMatchingSaveBasename)
{
    EXPECT_TRUE(campaign_runner::CanSelectRequestedSave("campaign.v2", ""));
    EXPECT_TRUE(campaign_runner::CanSelectRequestedSave("campaign.v2", "campaign.v2"));
    EXPECT_TRUE(campaign_runner::CanSelectRequestedSave("campaign.v2", "CAMPAIGN.V2"));
    EXPECT_FALSE(campaign_runner::CanSelectRequestedSave("campaign.v2", "other.v2"));
    EXPECT_FALSE(campaign_runner::CanSelectRequestedSave("", ""));
}
