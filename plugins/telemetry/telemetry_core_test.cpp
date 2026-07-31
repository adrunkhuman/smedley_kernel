#include "telemetry_core.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <fstream>
#include <array>
#include <cstring>
#include <limits>
#include <new>

namespace telemetry = smedley::telemetry;
namespace fs = std::filesystem;

static_assert(sizeof(SmedleyTelemetryResult) == sizeof(uint32_t));
static_assert(sizeof(SmedleyTelemetryScalarType) == sizeof(uint32_t));

namespace
{
    SmedleyTelemetryFieldV1 Field(const char *key, SmedleyTelemetryScalarType type = SMEDLEY_TELEMETRY_NULL)
    {
        return {sizeof(SmedleyTelemetryFieldV1), SMEDLEY_TELEMETRY_ABI_VERSION_V1, key,
                static_cast<uint32_t>(std::strlen(key)), static_cast<uint32_t>(type), 0, {}};
    }

    SmedleyTelemetryRecordV1 Record(const SmedleyTelemetryFieldV1 *entities = nullptr, uint32_t entity_count = 0,
                                    const SmedleyTelemetryFieldV1 *payload = nullptr, uint32_t payload_count = 0)
    {
        return {sizeof(SmedleyTelemetryRecordV1), SMEDLEY_TELEMETRY_ABI_VERSION_V1, SMEDLEY_TELEMETRY_RECORD_HAS_GAME_DATE, 0,
            "event.test", 10, "lifecycle", 9, "v2game-3.04", 11, "verified-runtime", 16, 42, 0,
            entities, entity_count, payload, payload_count, {0, 0, 0, 0}};
    }
}

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

TEST(TelemetryAbiTest, FormatsTypedScalarsAndEscapesStrings)
{
    auto entity = Field("country_tag", SMEDLEY_TELEMETRY_UTF8_STRING);
    entity.value.string_value = {"D01", 3, 0};
    SmedleyTelemetryFieldV1 payload[] = {Field("nothing"), Field("enabled", SMEDLEY_TELEMETRY_BOOL),
        Field("count", SMEDLEY_TELEMETRY_INT64), Field("ratio", SMEDLEY_TELEMETRY_DOUBLE), Field("text", SMEDLEY_TELEMETRY_UTF8_STRING)};
    payload[1].value.bool_value = 1;
    payload[2].value.int64_value = -7;
    payload[3].value.double_value = 1.5;
    payload[4].value.string_value = {"quote\"", 6, 0};
    const auto record = Record(&entity, 1, payload, 5);
    std::string line;
    std::string error;
    ASSERT_TRUE(telemetry::FormatRecordV1(&record, "run-1", 7, "2026-07-31T12:34:56.789Z", 42, &line, &error)) << error;
    EXPECT_NE(line.find("\"entities\":{\"country_tag\":\"D01\"}"), std::string::npos);
    EXPECT_NE(line.find("\"payload\":{\"nothing\":null,\"enabled\":true,\"count\":-7,\"ratio\":1.500000,\"text\":\"quote\\\"\"}"), std::string::npos);
    EXPECT_FALSE(telemetry::FormatRecordV1(&record, "run-1", 0, "2026-07-31T12:34:56.789Z", 42, &line, &error));
}

TEST(TelemetryAbiTest, RejectsMalformedRecordsBeforeFormatting)
{
    auto record = Record();
    std::string error;
    EXPECT_TRUE(telemetry::ValidateRecordV1(&record, &error));
    record.version = 2;
    EXPECT_FALSE(telemetry::ValidateRecordV1(&record, &error));
    record = Record(); record.reserved = 1;
    EXPECT_FALSE(telemetry::ValidateRecordV1(&record, &error));
    record = Record(); record.struct_size = sizeof(record) - 1;
    EXPECT_FALSE(telemetry::ValidateRecordV1(&record, &error));
    record = Record(); record.struct_size = sizeof(record) + 1;
    EXPECT_FALSE(telemetry::ValidateRecordV1(&record, &error));
    record = Record(); record.flags = 2;
    EXPECT_FALSE(telemetry::ValidateRecordV1(&record, &error));
    record = Record(); record.payload_field_count = 1;
    EXPECT_FALSE(telemetry::ValidateRecordV1(&record, &error));
    record = Record(); record.payload_field_count = SMEDLEY_TELEMETRY_MAX_FIELDS + 1;
    EXPECT_FALSE(telemetry::ValidateRecordV1(&record, &error));
    auto invalid_utf8 = Field("text", SMEDLEY_TELEMETRY_UTF8_STRING);
    invalid_utf8.value.string_value = {"\xc0", 1, 0};
    record = Record(nullptr, 0, &invalid_utf8, 1);
    EXPECT_FALSE(telemetry::ValidateRecordV1(&record, &error));
    auto nonfinite = Field("ratio", SMEDLEY_TELEMETRY_DOUBLE);
    nonfinite.value.double_value = std::numeric_limits<double>::infinity();
    record = Record(nullptr, 0, &nonfinite, 1);
    EXPECT_FALSE(telemetry::ValidateRecordV1(&record, &error));
    SmedleyTelemetryFieldV1 duplicate[] = {Field("same"), Field("same")};
    record = Record(nullptr, 0, duplicate, 2);
    EXPECT_FALSE(telemetry::ValidateRecordV1(&record, &error));
    auto invalid_key = Field("not/key");
    record = Record(nullptr, 0, &invalid_key, 1);
    EXPECT_FALSE(telemetry::ValidateRecordV1(&record, &error));
    auto small_field = Field("small");
    small_field.struct_size = 0;
    record = Record(nullptr, 0, &small_field, 1);
    EXPECT_FALSE(telemetry::ValidateRecordV1(&record, &error));
    auto large_field = Field("large");
    large_field.struct_size = sizeof(large_field) + 1;
    record = Record(nullptr, 0, &large_field, 1);
    EXPECT_FALSE(telemetry::ValidateRecordV1(&record, &error));
    auto wrong_version_field = Field("version");
    wrong_version_field.version = 2;
    record = Record(nullptr, 0, &wrong_version_field, 1);
    EXPECT_FALSE(telemetry::ValidateRecordV1(&record, &error));
    std::string too_long(SMEDLEY_TELEMETRY_MAX_STRING_BYTES + 1, 'x');
    auto long_string = Field("text", SMEDLEY_TELEMETRY_UTF8_STRING);
    long_string.value.string_value = {too_long.data(), static_cast<uint32_t>(too_long.size()), 0};
    record = Record(nullptr, 0, &long_string, 1);
    EXPECT_FALSE(telemetry::ValidateRecordV1(&record, &error));
    std::array<SmedleyTelemetryFieldV1, 5> entities{Field("one"), Field("two"), Field("three"), Field("four"), Field("five")};
    std::array<SmedleyTelemetryFieldV1, 4> fields{Field("six"), Field("seven"), Field("eight"), Field("nine")};
    record = Record(entities.data(), static_cast<uint32_t>(entities.size()), fields.data(), static_cast<uint32_t>(fields.size()));
    EXPECT_FALSE(telemetry::ValidateRecordV1(&record, &error));
}

TEST(TelemetryAbiTest, BoundsFinalRecordAndDispatchesCanonicalResults)
{
    std::array<SmedleyTelemetryFieldV1, SMEDLEY_TELEMETRY_MAX_FIELDS> payload{};
    std::string long_value(SMEDLEY_TELEMETRY_MAX_STRING_BYTES, '"');
    for (size_t index = 0; index < payload.size(); ++index) {
        payload[index] = Field("value0", SMEDLEY_TELEMETRY_UTF8_STRING);
        payload[index].value.string_value = {long_value.data(), static_cast<uint32_t>(long_value.size()), 0};
    }
    const char *keys[] = {"value0", "value1", "value2", "value3", "value4", "value5", "value6", "value7"};
    for (size_t index = 0; index < payload.size(); ++index) payload[index].key = keys[index];
    auto oversized = Record(nullptr, 0, payload.data(), static_cast<uint32_t>(payload.size()));
    std::string line;
    std::string error;
    EXPECT_FALSE(telemetry::FormatRecordV1(&oversized, "run-1", 1, "2026-07-31T12:34:56.789Z", 1, &line, &error));

    telemetry::Config config; config.run_id = "run-1"; config.categories = {"lifecycle"};
    uint64_t sequence = 0;
    std::string dispatched;
    auto record = Record();
    EXPECT_EQ(telemetry::DispatchRecordV1(nullptr, &record, &sequence, [](std::string_view) { return true; }), SMEDLEY_TELEMETRY_UNAVAILABLE);
    EXPECT_EQ(telemetry::DispatchRecordV1(&config, &record, &sequence, [&](std::string_view line) { dispatched = line; return true; }), SMEDLEY_TELEMETRY_ACCEPTED);
    EXPECT_EQ(sequence, 1u);
    EXPECT_NE(dispatched.find("\"sequence\":1,"), std::string::npos);
    EXPECT_EQ(dispatched.find("18446744073709551615"), std::string::npos);
    EXPECT_EQ(telemetry::DispatchRecordV1(&config, &record, &sequence, [](std::string_view) { return false; }), SMEDLEY_TELEMETRY_DROPPED);
    record.category = "state"; record.category_length = 5;
    EXPECT_EQ(telemetry::DispatchRecordV1(&config, &record, &sequence, [](std::string_view) { return true; }), SMEDLEY_TELEMETRY_FILTERED);
    record = Record();
    EXPECT_EQ(telemetry::DispatchRecordV1(&config, &record, &sequence, [](std::string_view) -> bool { throw std::bad_alloc(); }), SMEDLEY_TELEMETRY_DROPPED);
    sequence = (std::numeric_limits<uint64_t>::max)();
    EXPECT_EQ(telemetry::DispatchRecordV1(&config, &record, &sequence, [](std::string_view) { return true; }), SMEDLEY_TELEMETRY_DROPPED);
    EXPECT_EQ(sequence, (std::numeric_limits<uint64_t>::max)());
}

TEST(TelemetryAbiTest, EncodesNullStringAsAnEmptyJsonString)
{
    auto field = Field("text", SMEDLEY_TELEMETRY_UTF8_STRING);
    field.value.string_value = {nullptr, 0, 0};
    const auto record = Record(nullptr, 0, &field, 1);
    std::string line;
    std::string error;
    ASSERT_TRUE(telemetry::FormatRecordV1(&record, "run-1", 1, "2026-07-31T12:34:56.789Z", 1, &line, &error)) << error;
    EXPECT_NE(line.find("\"text\":\"\""), std::string::npos);
}

TEST(TelemetryPublishTest, OrdersAcceptedRecordsAndAccountsForContentionGaps)
{
    const auto record = Record();
    std::string error;
    telemetry::PreparedRecordV1 first;
    telemetry::PreparedRecordV1 second;
    telemetry::PreparedRecordV1 third;
    ASSERT_TRUE(telemetry::PrepareRecordV1(&record, "run-1", "2026-07-31T12:34:56.789Z", 1, &first, &error)) << error;
    ASSERT_TRUE(telemetry::PrepareRecordV1(&record, "run-1", "2026-07-31T12:34:56.789Z", 2, &second, &error)) << error;
    ASSERT_TRUE(telemetry::PrepareRecordV1(&record, "run-1", "2026-07-31T12:34:56.789Z", 3, &third, &error)) << error;
    std::atomic<uint64_t> sequence{0};
    std::mutex mutex;
    std::atomic<uint64_t> dropped{0};
    std::vector<std::string> accepted;
    const auto enqueue = [&accepted](std::string_view line) { accepted.emplace_back(line); return true; };
    const auto mark_dropped = [&dropped] { dropped.fetch_add(1, std::memory_order_relaxed); };
    std::unique_lock<std::mutex> held(mutex);
    std::thread contender([&] {
        EXPECT_EQ(telemetry::PublishPreparedRecord(first, &sequence, &mutex, false, enqueue, mark_dropped), SMEDLEY_TELEMETRY_DROPPED);
    });
    contender.join();
    held.unlock();
    std::thread producer_a([&] {
        EXPECT_EQ(telemetry::PublishPreparedRecord(second, &sequence, &mutex, true, enqueue, mark_dropped), SMEDLEY_TELEMETRY_ACCEPTED);
    });
    std::thread producer_b([&] {
        EXPECT_EQ(telemetry::PublishPreparedRecord(third, &sequence, &mutex, true, enqueue, mark_dropped), SMEDLEY_TELEMETRY_ACCEPTED);
    });
    producer_a.join();
    producer_b.join();
    ASSERT_EQ(accepted.size(), 2u);
    EXPECT_TRUE((accepted[0].find("\"sequence\":2,") != std::string::npos && accepted[1].find("\"sequence\":3,") != std::string::npos)
                || (accepted[0].find("\"sequence\":3,") != std::string::npos && accepted[1].find("\"sequence\":2,") != std::string::npos));
    const auto first_sequence = accepted[0].find("\"sequence\":2,") != std::string::npos ? 2 : 3;
    const auto second_sequence = accepted[1].find("\"sequence\":2,") != std::string::npos ? 2 : 3;
    EXPECT_LT(first_sequence, second_sequence);
    EXPECT_EQ(dropped.load(std::memory_order_relaxed), 1u);
    EXPECT_EQ(sequence.load(std::memory_order_relaxed), 3u);
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
    telemetry::Config config{"run-1", path, {"lifecycle"}, {}, std::nullopt, std::nullopt, 1, 64, false};
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
                                                     L"-smedley-telemetry-country-tags=ENG,D01", L"-smedley-telemetry-start-date-raw=-5",
                                                    L"-smedley-telemetry-end-date-raw=10", L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0"}, &config, &error));
    EXPECT_EQ(config.categories, (std::vector<std::string>{"lifecycle", "state"}));
    EXPECT_EQ(config.sample_days, 3);
    EXPECT_EQ(config.queue_capacity, 128);
    EXPECT_EQ(config.country_tags, (std::vector<std::string>{"ENG", "D01"}));
    EXPECT_TRUE(telemetry::IsDateInRange(config, 0));
    EXPECT_FALSE(telemetry::IsDateInRange(config, 11));
    EXPECT_TRUE(telemetry::HasCountryTag(config, "ENG"));
    EXPECT_FALSE(telemetry::HasCountryTag(config, "USA"));

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
    EXPECT_FALSE(telemetry::ShouldSampleDate(267, 7, &last));
    EXPECT_TRUE(telemetry::ShouldSampleDate(268, 7, &last));
    EXPECT_TRUE(telemetry::ShouldSampleDate(90, 7, &last));
}

TEST(TelemetrySamplingTest, ObservesDateRegressionOncePerObservedTransition)
{
    std::optional<int> previous;
    int64_t delta = 0;
    EXPECT_FALSE(telemetry::ObserveDateRegression(100, &previous, &delta));
    EXPECT_FALSE(telemetry::ObserveDateRegression(100, &previous, &delta));
    EXPECT_FALSE(telemetry::ObserveDateRegression(124, &previous, &delta));
    const int minimum = (std::numeric_limits<int>::min)();
    EXPECT_TRUE(telemetry::ObserveDateRegression(minimum, &previous, &delta));
    EXPECT_EQ(delta, static_cast<int64_t>(minimum) - static_cast<int64_t>(124));
    EXPECT_FALSE(telemetry::ObserveDateRegression(minimum, &previous, &delta));
}
