#include "campaign_telemetry.hpp"
#include "campaign_save_selection.hpp"
#include "benchmark_controller.hpp"
#include "campaign_launch_arguments.hpp"

#include <gtest/gtest.h>

#include <deque>
#include <limits>

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
    int fallback_calls = 0;

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

    SmedleyTelemetryResult SMEDLEY_TELEMETRY_CALL CaptureFallback(const SmedleyTelemetryRecordV1 *)
    {
        ++fallback_calls;
        return SMEDLEY_TELEMETRY_DROPPED;
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
            fallback_calls = 0;
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

TEST_F(CampaignTelemetryTest, PrefersReliableEmitterAndRetainsFallbackCompatibility)
{
    campaign_runner::CampaignTelemetry telemetry(&CaptureFallback, &Capture);
    EXPECT_EQ(telemetry.SaveSelectionRequested(), SMEDLEY_TELEMETRY_ACCEPTED);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(fallback_calls, 0);

    campaign_runner::CampaignTelemetry fallback(&CaptureFallback, nullptr);
    EXPECT_EQ(fallback.SaveSelectionRequested(), SMEDLEY_TELEMETRY_DROPPED);
    EXPECT_EQ(fallback_calls, 1);
}

TEST(BenchmarkControllerTest, CompletesOnlyAtTheExactTargetOnce)
{
    campaign_runner::BenchmarkController controller;
    const char *error = nullptr;
    ASSERT_TRUE(controller.Begin(100, 2, std::nullopt, 600, 10, &error)) << error;
    EXPECT_EQ(controller.target_date_raw(), 148);
    EXPECT_EQ(controller.Observe({true, 124, 0, true, 11}).action, campaign_runner::BenchmarkAction::Continue);
    EXPECT_EQ(controller.Observe({true, 148, 0, true, 12}).action, campaign_runner::BenchmarkAction::Complete);
    EXPECT_EQ(controller.Observe({true, 148, 0, true, 13}).action, campaign_runner::BenchmarkAction::Continue);
}

TEST(CampaignLaunchArgumentsTest, IgnoresOtherSmedleyArgumentsAndRejectsBenchmarkDuplicates)
{
    campaign_runner::CampaignLaunchArguments arguments;
    std::string error;
    ASSERT_TRUE(campaign_runner::ParseCampaignLaunchArguments({L"v2game.exe", L"-smedley-save=C:\\save.v2",
        L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl", L"-smedley-telemetry-categories=lifecycle",
        L"-smedley-run-days=365", L"-smedley-run-timeout-seconds=600"}, &arguments, &error)) << error;
    EXPECT_EQ(arguments.run_condition.days, 365);
    EXPECT_EQ(arguments.run_condition.timeout_seconds, 600);
    EXPECT_FALSE(campaign_runner::ParseCampaignLaunchArguments({L"v2game.exe", L"-smedley-save=C:\\save.v2", L"-smedley-run-days=1", L"-smedley-run-days=2"}, &arguments, &error));
    EXPECT_FALSE(campaign_runner::ParseCampaignLaunchArguments({L"v2game.exe", L"-smedley-save=C:\\save.v2", L"-smedley-run-until-date-raw"}, &arguments, &error));
}

TEST(CampaignLaunchArgumentsTest, ParsesQuitAfterRunOnlyWithOneBoundedRunTarget)
{
    campaign_runner::CampaignLaunchArguments arguments;
    std::string error;
    ASSERT_TRUE(campaign_runner::ParseCampaignLaunchArguments({L"v2game.exe", L"-smedley-save=C:\\save.v2",
        L"-smedley-run-days=1", L"-smedley-quit-after-run"}, &arguments, &error)) << error;
    EXPECT_TRUE(arguments.quit_after_run);

    EXPECT_FALSE(campaign_runner::ParseCampaignLaunchArguments({L"v2game.exe", L"-smedley-save=C:\\save.v2",
        L"-smedley-quit-after-run"}, &arguments, &error));
    EXPECT_FALSE(campaign_runner::ParseCampaignLaunchArguments({L"v2game.exe", L"-smedley-save=C:\\save.v2",
        L"-smedley-run-days=1", L"-smedley-quit-after-run", L"-smedley-quit-after-run"}, &arguments, &error));
    EXPECT_FALSE(campaign_runner::ParseCampaignLaunchArguments({L"v2game.exe", L"-smedley-save=C:\\save.v2",
        L"-smedley-run-days=1", L"-smedley-quit-after-run=true"}, &arguments, &error));
}

TEST(BenchmarkControllerTest, RejectsInvalidTargetMathAndDetectsFailures)
{
    campaign_runner::BenchmarkController controller;
    const char *error = nullptr;
    EXPECT_FALSE(controller.Begin((std::numeric_limits<int>::max)(), 1, std::nullopt, 600, 0, &error));
    EXPECT_FALSE(controller.active());
    EXPECT_FALSE(controller.Begin(100, std::nullopt, 101, 600, 0, nullptr));
    EXPECT_FALSE(controller.active());
    EXPECT_FALSE(controller.Begin(100, std::nullopt, 101, 600, 0, &error));
    ASSERT_TRUE(controller.Begin(100, std::nullopt, 124, 1, 10, &error));
    EXPECT_STREQ(controller.Observe({true, 125, 0, true, 11}).reason, "date_overshoot");

    ASSERT_TRUE(controller.Begin(100, 1, std::nullopt, 1, 10, &error));
    EXPECT_STREQ(controller.Observe({true, 100, 0, true, 1000010}).reason, "timeout");
    ASSERT_TRUE(controller.Begin(100, 1, std::nullopt, 600, 10, &error));
    EXPECT_STREQ(controller.Observe({true, 99, 0, true, 11}).reason, "date_regressed");
    ASSERT_TRUE(controller.Begin(100, 1, std::nullopt, 600, 10, &error));
    EXPECT_STREQ(controller.Observe({true, 100, 1, true, 11}).reason, "unexpected_pause");
    ASSERT_TRUE(controller.Begin(100, 1, std::nullopt, 600, 10, &error));
    EXPECT_STREQ(controller.Observe({true, 100, 0, false, 11}).reason, "observer_invariant_failed");
    ASSERT_TRUE(controller.Begin(100, 1, std::nullopt, 600, 10, &error));
    EXPECT_STREQ(controller.Observe({false, std::nullopt, -1, true, 11}).reason, "idler_unavailable");
    ASSERT_TRUE(controller.Begin(100, 1, std::nullopt, 600, 10, &error));
    EXPECT_STREQ(controller.Observe({true, 100, 2, true, 11}).reason, "invalid_pause_state");
}

TEST_F(CampaignTelemetryTest, EmitsTypedBenchmarkRecords)
{
    campaign_runner::CampaignTelemetry telemetry(&Capture);
    EXPECT_EQ(telemetry.BenchmarkStarted(100, 124, 1, 600), SMEDLEY_TELEMETRY_ACCEPTED);
    EXPECT_EQ(event_type, "benchmark.started");
    EXPECT_EQ(payload.size(), 4u);
    EXPECT_EQ(telemetry.BenchmarkResources(124, 40, 100, 120, 200, 230, 150), SMEDLEY_TELEMETRY_ACCEPTED);
    EXPECT_EQ(event_type, "benchmark.resources");
    ASSERT_EQ(payload.size(), 6u);
    EXPECT_EQ(payload[0].key, "process_cpu_us");
    EXPECT_EQ(payload[0].int_value, 40);
    EXPECT_EQ(payload[5].key, "process_peak_working_set_bytes");
    EXPECT_EQ(payload[5].int_value, 150);
    EXPECT_EQ(telemetry.BenchmarkCompleted(100, 124, 124, 1, 50), SMEDLEY_TELEMETRY_ACCEPTED);
    EXPECT_EQ(event_type, "benchmark.completed");
    EXPECT_EQ(payload.size(), 7u);
    EXPECT_EQ(telemetry.BenchmarkFailed(100, 124, 125, 50, "date_overshoot", true), SMEDLEY_TELEMETRY_FILTERED);

    campaign_runner::CampaignTelemetry partial(&Capture);
    EXPECT_EQ(partial.BenchmarkResources(std::nullopt, 0, std::nullopt, std::nullopt,
                                         std::nullopt, std::nullopt, std::nullopt),
              SMEDLEY_TELEMETRY_ACCEPTED);
    ASSERT_EQ(payload.size(), 1u);
    EXPECT_EQ(payload[0].key, "process_cpu_us");
}

TEST_F(CampaignTelemetryTest, SelectsOnlyAnEmptyOrMatchingSaveBasename)
{
    EXPECT_TRUE(campaign_runner::CanSelectRequestedSave("campaign.v2", ""));
    EXPECT_TRUE(campaign_runner::CanSelectRequestedSave("campaign.v2", "campaign.v2"));
    EXPECT_TRUE(campaign_runner::CanSelectRequestedSave("campaign.v2", "CAMPAIGN.V2"));
    EXPECT_FALSE(campaign_runner::CanSelectRequestedSave("campaign.v2", "other.v2"));
    EXPECT_FALSE(campaign_runner::CanSelectRequestedSave("", ""));
}
