#include "telemetry_core.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <fstream>
#include <limits>

namespace telemetry = smedley::telemetry;
namespace fs = std::filesystem;

TEST(TelemetryJsonTest, EscapesControlsAndPreservesUnicode)
{
    const std::string input = std::string("quote \" slash \\ tab \t newline \nOmega ") + "\xce\xa9" + std::string(1, '\x01');
    EXPECT_EQ(telemetry::EscapeJson(input), std::string("quote \\\" slash \\\\ tab \\t newline \\nOmega ") + "\xce\xa9" + "\\u0001");
}

TEST(TelemetryJsonTest, FormatsSchemaV1EnvelopeWithNullDate)
{
    telemetry::Envelope envelope{"run-1", 7, "2026-07-31T12:34:56.789Z", 42, std::nullopt,
                                 "session.started", "lifecycle", "v2game-3.04", "launcher-verified", "{}", "{\"enabled\":true}"};
    EXPECT_EQ(telemetry::FormatEnvelope(envelope),
              "{\"schema\":\"smedley.telemetry\",\"schema_version\":1,\"run_id\":\"run-1\",\"sequence\":7,"
              "\"wall_time_utc\":\"2026-07-31T12:34:56.789Z\",\"monotonic_us\":42,\"game_date_raw\":null,"
              "\"event_type\":\"session.started\",\"category\":\"lifecycle\",\"mapping_id\":\"v2game-3.04\","
              "\"quality\":\"launcher-verified\",\"entities\":{},\"payload\":{\"enabled\":true}}");
}

TEST(TelemetryQueueTest, IsBoundedAndAccountsForDrops)
{
    telemetry::BoundedQueue queue(2);
    EXPECT_TRUE(queue.TryPush("one"));
    EXPECT_TRUE(queue.TryPush("two"));
    EXPECT_FALSE(queue.TryPush("three"));
    const auto stats = queue.stats();
    EXPECT_EQ(stats.accepted, 2u);
    EXPECT_EQ(stats.dropped, 1u);
    EXPECT_EQ(stats.high_water, 2u);
}

TEST(TelemetryWriterTest, WritesCompleteNewlineDelimitedRecords)
{
    const fs::path path = fs::temp_directory_path() / (L"smedley telemetry " + std::to_wstring(GetTickCount64()) + L".jsonl");
    telemetry::Config config{"run-1", path, {"lifecycle"}, 1, 64, false};
    telemetry::Writer writer(config);
    std::string error;
    ASSERT_TRUE(writer.Start(&error)) << error;
    ASSERT_TRUE(writer.TryWrite("{\"line\":1}"));
    ASSERT_TRUE(writer.TryWrite("{\"line\":2}"));
    telemetry::QueueStats final_stats;
    writer.Stop([&](const telemetry::QueueStats &stats) {
        final_stats = stats;
        return "{\"summary\":true}";
    });
    EXPECT_EQ(final_stats.accepted, 2u);
    EXPECT_EQ(final_stats.written, 2u);

    std::ifstream input(path, std::ios::binary);
    std::string contents((std::istreambuf_iterator<char>(input)), {});
    EXPECT_EQ(contents, "{\"line\":1}\n{\"line\":2}\n{\"summary\":true}\n");
    std::error_code ignored;
    fs::remove(path, ignored);
}

TEST(TelemetryConfigTest, ParsesAndRejectsMalformedLaunchArguments)
{
    telemetry::Config config;
    std::string error;
    ASSERT_TRUE(telemetry::ParseLaunchArguments({L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
                                                  L"-smedley-telemetry-categories=lifecycle,state", L"-smedley-telemetry-sample-days=3",
                                                   L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0"}, &config, &error));
    EXPECT_EQ(config.categories, (std::vector<std::string>{"lifecycle", "state"}));
    EXPECT_EQ(config.sample_days, 3);
    EXPECT_EQ(config.queue_capacity, 128);

    EXPECT_FALSE(telemetry::ParseLaunchArguments({L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
                                                   L"-smedley-telemetry-categories=state", L"-smedley-telemetry-sample-days=0",
                                                   L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0"}, &config, &error));
}

TEST(TelemetryTimingTest, ConvertsLongUptimeWithoutMultiplicationOverflow)
{
    EXPECT_EQ(telemetry::QpcToMicroseconds(15'000'000, 10'000'000), 1'500'000u);
    EXPECT_EQ(telemetry::QpcToMicroseconds((std::numeric_limits<uint64_t>::max)(), 1), (std::numeric_limits<uint64_t>::max)());
}

TEST(TelemetrySamplingTest, SkipsUnavailableAndResetsOnDateRegression)
{
    std::optional<int> last;
    EXPECT_FALSE(telemetry::ShouldSampleDate(std::nullopt, 7, &last));
    EXPECT_TRUE(telemetry::ShouldSampleDate(100, 7, &last));
    EXPECT_TRUE(telemetry::ShouldSampleDate(100, 7, &last));
    EXPECT_FALSE(telemetry::ShouldSampleDate(105, 7, &last));
    EXPECT_TRUE(telemetry::ShouldSampleDate(90, 7, &last));
}
