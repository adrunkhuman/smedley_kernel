#include "telemetry_core.hpp"
#include "economic_capture_core.hpp"
#include <smedley/telemetry_registry.hpp>

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
        const auto result = telemetry::PublishPreparedRecord(first, &sequence, &mutex, false, enqueue, mark_dropped);
        EXPECT_EQ(result.status, SMEDLEY_TELEMETRY_DROPPED);
        EXPECT_GT(result.formatted_bytes, 0u);
    });
    contender.join();
    held.unlock();
    std::thread producer_a([&] {
        EXPECT_EQ(telemetry::PublishPreparedRecord(second, &sequence, &mutex, true, enqueue, mark_dropped).status, SMEDLEY_TELEMETRY_ACCEPTED);
    });
    std::thread producer_b([&] {
        EXPECT_EQ(telemetry::PublishPreparedRecord(third, &sequence, &mutex, true, enqueue, mark_dropped).status, SMEDLEY_TELEMETRY_ACCEPTED);
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

TEST(TelemetryPublishTest, ReportsFinalizedBytesForAcceptedAndDroppedRecords)
{
    const auto record = Record();
    telemetry::PreparedRecordV1 prepared;
    std::string error;
    ASSERT_TRUE(telemetry::PrepareRecordV1(&record, "run-1", "2026-07-31T12:34:56.789Z", 1, &prepared, &error)) << error;
    std::atomic<uint64_t> sequence{0};
    std::mutex mutex;
    const auto accepted = telemetry::PublishPreparedRecord(prepared, &sequence, &mutex, false,
        [](std::string_view) { return true; }, [] {});
    EXPECT_EQ(accepted.status, SMEDLEY_TELEMETRY_ACCEPTED);
    EXPECT_GT(accepted.formatted_bytes, 0u);
    const auto dropped = telemetry::PublishPreparedRecord(prepared, &sequence, &mutex, false,
        [](std::string_view) { return false; }, [] {});
    EXPECT_EQ(dropped.status, SMEDLEY_TELEMETRY_DROPPED);
    EXPECT_GT(dropped.formatted_bytes, 0u);
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

    telemetry::BoundedQueue reliable_queue(1);
    EXPECT_TRUE(reliable_queue.Push("one"));
    EXPECT_FALSE(reliable_queue.Push("two"));
    EXPECT_EQ(reliable_queue.stats().dropped, 1u);
}

TEST(TelemetryQueueTest, ReservesCapacityForReliableRecords)
{
    telemetry::BoundedQueue queue(4);
    EXPECT_TRUE(queue.TryPush("one", 2));
    EXPECT_TRUE(queue.TryPush("two", 2));
    EXPECT_FALSE(queue.TryPush("detail", 2));
    EXPECT_TRUE(queue.Push("lifecycle-one"));
    EXPECT_TRUE(queue.Push("lifecycle-two"));
    EXPECT_FALSE(queue.Push("overflow"));
}

TEST(TelemetryWriterTest, WritesCompleteNewlineDelimitedRecords)
{
    const fs::path path = fs::temp_directory_path() / (L"smedley telemetry " + std::to_wstring(GetTickCount64()) + L".jsonl");
    telemetry::Config config{"run-1", path, {"lifecycle"}, {}, std::nullopt, std::nullopt, 1, 64, false};
    telemetry::Writer writer(config);
    std::string error;
    ASSERT_TRUE(writer.Start(&error)) << error;
    ASSERT_TRUE(writer.TryWrite("{\"line\":1}"));
    ASSERT_TRUE(writer.WriteReliable("{\"line\":2}"));
    telemetry::QueueStats final_stats;
    EXPECT_TRUE(writer.Stop([&](const telemetry::QueueStats &stats) {
        final_stats = stats;
        return "{\"summary\":true}";
    }));
    EXPECT_EQ(final_stats.accepted, 2u);
    EXPECT_EQ(final_stats.written, 2u);

    std::ifstream input(path, std::ios::binary);
    std::string contents((std::istreambuf_iterator<char>(input)), {});
    EXPECT_EQ(contents, "{\"line\":1}\n{\"line\":2}\n{\"summary\":true}\n");
    EXPECT_FALSE(writer.TryWrite("{\"late\":true}"));
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

TEST(TelemetryConfigTest, RequiresExplicitGoldRateForCountryEconomy)
{
    const std::vector<std::wstring> base = {
        L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
        L"-smedley-telemetry-categories=lifecycle,state", L"-smedley-telemetry-sample-days=1",
        L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0",
        L"-smedley-telemetry-capture=country.economy|monthly|totals,components,per_capita|ENG|||",
    };
    telemetry::Config config;
    std::string error;
    EXPECT_FALSE(telemetry::ParseLaunchArguments(base, &config, &error));
    EXPECT_EQ(error, "country.economy requires -smedley-telemetry-gold-to-cash-rate");

    auto configured = base;
    configured.push_back(L"-smedley-telemetry-gold-to-cash-rate=0.5");
    config = {};
    ASSERT_TRUE(telemetry::ParseLaunchArguments(configured, &config, &error)) << error;
    ASSERT_TRUE(config.gold_to_cash_rate);
    EXPECT_DOUBLE_EQ(*config.gold_to_cash_rate, 0.5);
    ASSERT_EQ(config.capture_rules.size(), 1u);
    EXPECT_EQ(config.capture_rules[0].family, "country.economy");

    configured.back() = L"-smedley-telemetry-gold-to-cash-rate=1000.1";
    config = {};
    EXPECT_FALSE(telemetry::ParseLaunchArguments(configured, &config, &error));
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

TEST(TelemetrySamplingTest, DecodesVictoriaFixedCalendar)
{
    const auto start = telemetry::DecodeClausewitzDate(59'883'384);
    ASSERT_TRUE(start);
    EXPECT_EQ(start->year, 1836);
    EXPECT_EQ(start->month, 1);
    EXPECT_EQ(start->day, 2);
    EXPECT_EQ(start->hour, 0);

    const auto no_leap_day = telemetry::DecodeClausewitzDate(59'884'752);
    ASSERT_TRUE(no_leap_day);
    EXPECT_EQ(no_leap_day->year, 1836);
    EXPECT_EQ(no_leap_day->month, 2);
    EXPECT_EQ(no_leap_day->day, 28);
    const auto march = telemetry::DecodeClausewitzDate(59'884'776);
    ASSERT_TRUE(march);
    EXPECT_EQ(march->month, 3);
    EXPECT_EQ(march->day, 1);
}

TEST(TelemetrySamplingTest, AppliesIndependentCalendarCadences)
{
    telemetry::CaptureRule daily{"country.daily", telemetry::CaptureCadence::Daily};
    telemetry::ScheduleState daily_state;
    EXPECT_TRUE(telemetry::ShouldCaptureDate(59'883'384, daily, &daily_state));
    EXPECT_TRUE(telemetry::ShouldCaptureDate(59'883'384, daily, &daily_state));
    EXPECT_TRUE(telemetry::ShouldCaptureDate(59'883'408, daily, &daily_state));

    telemetry::CaptureRule weekly{"country.daily", telemetry::CaptureCadence::Weekly};
    telemetry::ScheduleState weekly_state;
    EXPECT_TRUE(telemetry::ShouldCaptureDate(59'883'384, weekly, &weekly_state));
    EXPECT_FALSE(telemetry::ShouldCaptureDate(59'883'528, weekly, &weekly_state));
    EXPECT_TRUE(telemetry::ShouldCaptureDate(59'883'552, weekly, &weekly_state));

    telemetry::CaptureRule monthly{"world.daily", telemetry::CaptureCadence::Monthly};
    telemetry::ScheduleState monthly_state;
    EXPECT_TRUE(telemetry::ShouldCaptureDate(59'883'384, monthly, &monthly_state));
    EXPECT_FALSE(telemetry::ShouldCaptureDate(59'884'080, monthly, &monthly_state));
    EXPECT_TRUE(telemetry::ShouldCaptureDate(59'884'104, monthly, &monthly_state));

    telemetry::CaptureRule yearly{"world.economy", telemetry::CaptureCadence::Yearly};
    telemetry::ScheduleState yearly_state;
    EXPECT_TRUE(telemetry::ShouldCaptureDate(59'883'384, yearly, &yearly_state));
    EXPECT_FALSE(telemetry::ShouldCaptureDate(59'892'096, yearly, &yearly_state));
    EXPECT_TRUE(telemetry::ShouldCaptureDate(59'892'120, yearly, &yearly_state));
    EXPECT_TRUE(telemetry::ShouldCaptureDate(59'883'384, yearly, &yearly_state));
}

TEST(TelemetryQueueTest, DoesNotDoubleCountQueueRejectionFromPreparedPublication)
{
    const auto record = Record();
    telemetry::PreparedRecordV1 prepared;
    std::string error;
    ASSERT_TRUE(telemetry::PrepareRecordV1(&record, "run-1", "2026-07-31T12:34:56.789Z", 1, &prepared, &error)) << error;
    telemetry::BoundedQueue queue(1);
    ASSERT_TRUE(queue.TryPush("full"));
    const auto before = queue.stats().dropped;
    std::atomic<uint64_t> sequence{0};
    std::mutex mutex;
    const auto result = telemetry::PublishPreparedRecord(prepared, &sequence, &mutex, false,
        [&queue](std::string_view line) { return queue.TryPush(line); }, [] {});
    EXPECT_EQ(result.status, SMEDLEY_TELEMETRY_DROPPED);
    EXPECT_EQ(queue.stats().dropped, before + 1);
}

TEST(TelemetryRegistryTest, CoversUniqueFamiliesAndPlansOnlyConfiguredHooks)
{
    size_t count = 0;
    const auto *families = telemetry::MetricFamilies(&count);
    ASSERT_EQ(count, 22u);
    for (size_t index = 0; index < count; ++index) {
        ASSERT_NE(telemetry::FindMetricFamily(families[index].id), nullptr);
        EXPECT_GT(families[index].field_count, 0u);
        EXPECT_GT(families[index].event_count, 0u);
        for (size_t field = 0; field < families[index].field_count; ++field) {
            EXPECT_FALSE(families[index].fields[field].empty());
            for (size_t other = 0; other < field; ++other) EXPECT_NE(families[index].fields[field], families[index].fields[other]);
        }
        for (size_t event = 0; event < families[index].event_count; ++event) {
            EXPECT_FALSE(families[index].events[event].id.empty());
            EXPECT_FALSE(families[index].events[event].entity_schema.empty());
            EXPECT_FALSE(families[index].events[event].payload_schema.empty());
            EXPECT_TRUE(telemetry::MetricFamilyEmitsEvent(families[index], families[index].events[event].id));
            EXPECT_EQ(telemetry::FindMetricFamilyForEvent(families[index].events[event].id), &families[index]);
            EXPECT_EQ(telemetry::FindMetricEvent(families[index].events[event].id), &families[index].events[event]);
            EXPECT_NE(families[index].events[event].entity_schema, "entity");
            EXPECT_NE(families[index].events[event].payload_schema, "payload");
        }
        EXPECT_FALSE(telemetry::MetricFamilyEmitsEvent(families[index], std::string(families[index].id) + ".fake"));
        for (size_t other = 0; other < index; ++other) EXPECT_NE(families[index].id, families[other].id);
    }
    const auto lifecycle_only = telemetry::BuildRuntimePlan(nullptr, 0);
    EXPECT_FALSE(lifecycle_only.open_observation);
    const std::string_view factory_fields[] = {"sales"};
    const telemetry::MetricRuleSelection selected[] = {{"state.factory", factory_fields, 1}, {"pop.cashflow", nullptr, 0}};
    const auto plan = telemetry::BuildRuntimePlan(selected, 2);
    EXPECT_TRUE(plan.open_observation);
    EXPECT_TRUE(plan.install_factory_sales_hook);
    EXPECT_TRUE(plan.install_pop_cashflow_hook);
    EXPECT_FALSE(plan.install_factory_flow_hook);
    const std::string_view production_fields[] = {"production"};
    const telemetry::MetricRuleSelection no_hooks[] = {{"state.factory", production_fields, 1}};
    const auto no_hook_plan = telemetry::BuildRuntimePlan(no_hooks, 1);
    EXPECT_FALSE(no_hook_plan.install_factory_sales_hook);
    EXPECT_FALSE(no_hook_plan.install_factory_flow_hook);
    EXPECT_FALSE(no_hook_plan.install_artisan_flow_hook);
    EXPECT_FALSE(no_hook_plan.install_pop_cashflow_hook);
}

TEST(TelemetryRegistryTest, DefinesCanonicalRepresentativeAndStrictExporterSchemas)
{
    struct ExpectedSchema { std::string_view event, entities, payload; };
    constexpr ExpectedSchema schemas[] = {
        {"world.daily", "-", "country_slot_count?,ai_scheduler_entry_count?,human_control_present?"},
        {"world.economy.health", "-", "complete,snapshot_flags,collection_flags,credit_flags,country_count,state_count,province_count,pop_count"},
        {"world.military", "-", "ongoing_war_count_candidate"},
        {"country.daily", "country_tag", "treasury_raw?,treasury?"},
        {"country.metrics.power", "country_tag", "prestige_candidate_raw,infamy_candidate_raw,ranking_candidate,military_ranking_candidate,industrial_ranking_candidate,prestige_ranking_candidate"},
        {"country.economy.component", "country_tag,component", "nominal_value_added,real_value_added"},
        {"country.military", "country_tag", "unit_count_candidate?,mobilized_candidate?,scheduled_mobilization_count_candidate?,leadership_candidate_raw?,military_ranking_candidate?"},
        {"country.diplomacy.status", "country_tag", "substate_candidate,vassal_candidate,overlord_tag_candidate,sphere_leader_tag_candidate"},
        {"province.daily", "province_id", "owner_tag_candidate?,controller_tag_candidate?,colonial_level_candidate?,life_rating_candidate?,infrastructure_candidate_raw?"},
        {"province.production", "province_id", "building_slot_count_candidate?,construction_count_candidate?"},
        {"pop.economy", "province_id_candidate,pop_type_id_candidate,pop_id", "money_raw?,savings_raw?,interest_cash_flow_raw?,total_cash_flow_raw?"},
        {"pop.demographics", "province_id_candidate,pop_type_id_candidate,pop_id", "size_candidate?,employed_candidate?,consciousness_candidate_raw?,militancy_candidate_raw?,literacy_candidate_raw?"},
        {"pop.identity", "province_id_candidate,pop_type_id_candidate,pop_id", "pop_type_tag_candidate?,culture_tag_candidate?,religion_tag_candidate?"},
        {"pop.needs", "province_id_candidate,pop_type_id_candidate,pop_id", "life_satisfaction_candidate_raw?,everyday_satisfaction_candidate_raw?,luxury_satisfaction_candidate_raw?"},
        {"pop.aggregate", "country_tag,province_id_candidate,pop_type_id_candidate", "pop_count?,size_candidate?,employed_candidate?,money_raw?,savings_raw?"},
        {"pop.lifecycle.summary", "-", "opening_seen,opening_pop_count,closing_pop_count,observed_appeared_count,observed_disappeared_count,scope_changed_count,unchanged_count,complete"},
        {"pop.cashflow.component", "country_tag,province_id,pop_type_id_candidate,pop_id,cash_flow_index,component", "posted_raw,money_delta_raw"},
        {"pop.cashflow.aggregate.summary", "country_tag,pop_type_id_candidate", "opening_pop_count,closing_pop_count,opening_money_seen,reconciled"},
        {"state.factory.production", "country_tag,state_id,factory_type", "output_raw,output_good_ordinal,output_good,base_output_raw"},
        {"state.factory.input.flow.summary", "country_tag,state_id,factory_type", "post_consumption_seen,pre_purchase_seen,primary_delivery_seen,secondary_delivery_seen,settlement_count"},
        {"state.factory.input.flow", "country_tag,state_id,factory_type,good_ordinal", "post_consumption_raw,pre_purchase_raw,delivered_primary_raw,delivered_secondary_raw"},
        {"state.factory.sales.summary", "country_tag,state_id,factory_type", "settlement_seen,settlement_count,complete"},
        {"state.factory.sales.quantity", "country_tag,state_id,factory_type", "output_good_ordinal,opening_inventory_raw,produced_raw,sold_raw,closing_inventory_raw"},
        {"state.factory.sales.revenue", "country_tag,state_id,factory_type", "proceeds_raw"},
        {"world.market.price", "good_ordinal", "price_raw,last_price_raw"},
        {"province.rgo.identity", "country_tag,province_id", "production_type,output_good_ordinal,output_good"},
        {"province.rgo.production", "country_tag,province_id", "base_output_per_size_raw,base_size_raw_candidate,base_size_raw,output_efficiency_raw,throughput_raw,gross_output_raw"},
        {"province.rgo.finance", "country_tag,province_id", "income_raw"},
        {"province.rgo.sales.summary", "country_tag,province_id", "settlement_seen,opening_inventory_seen,complete"},
        {"province.rgo.sales.quantity", "country_tag,province_id", "output_good_ordinal,opening_inventory_raw,produced_raw,sold_raw,closing_inventory_raw"},
        {"province.rgo.sales.revenue", "country_tag,province_id", "proceeds_raw,percent_sold_domestic_raw,percent_sold_export_raw"},
        {"pop.artisan.identity", "country_tag,province_id,pop_id", "production_type,output_good_ordinal,output_good"},
        {"pop.artisan.production", "country_tag,province_id,pop_id", "base_output_raw,current_producing_raw,gross_output_raw"},
        {"pop.artisan.input", "country_tag,province_id,pop_id,good_ordinal", "stockpile_raw,need_raw"},
        {"pop.artisan.input.flow.summary", "country_tag,province_id,pop_id", "post_consumption_seen,pre_purchase_seen,primary_delivery_seen,secondary_delivery_seen,settlement_count"},
        {"pop.artisan.input.flow", "country_tag,province_id,pop_id,good_ordinal", "post_consumption_raw,pre_purchase_raw,delivered_primary_raw,delivered_secondary_raw"},
        {"pop.artisan.sales.summary", "country_tag,province_id,pop_id", "settlement_seen,opening_inventory_seen,complete"},
        {"pop.artisan.sales.quantity", "country_tag,province_id,pop_id", "output_good_ordinal,opening_inventory_raw,produced_raw,sold_raw,closing_inventory_raw"},
        {"pop.artisan.sales.revenue", "country_tag,province_id,pop_id", "proceeds_raw,percent_sold_domestic_raw,percent_sold_export_raw"},
    };
    for (const auto &expected : schemas) {
        const auto *event = telemetry::FindMetricEvent(expected.event);
        ASSERT_NE(event, nullptr) << expected.event;
        EXPECT_EQ(event->entity_schema, expected.entities) << expected.event;
        EXPECT_EQ(event->payload_schema, expected.payload) << expected.event;
    }
}

TEST(TelemetryRegistryTest, EnforcesRequiredOptionalAndUnexpectedSchemaFields)
{
    const auto *event = telemetry::FindMetricEvent("country.daily");
    ASSERT_NE(event, nullptr);
    const std::string_view entities[] = {"country_tag"};
    const std::string_view complete_payload[] = {"treasury_raw", "treasury"};
    const std::string_view partial_payload[] = {"treasury_raw"};
    const std::string_view unexpected_payload[] = {"treasury_raw", "other"};
    EXPECT_TRUE(telemetry::MetricEventMatchesSchema(*event, entities, 1, complete_payload, 2));
    EXPECT_TRUE(telemetry::MetricEventMatchesSchema(*event, entities, 1, partial_payload, 1));
    EXPECT_TRUE(telemetry::MetricEventMatchesSchema(*event, entities, 1, nullptr, 0));
    EXPECT_FALSE(telemetry::MetricEventMatchesSchema(*event, nullptr, 0, complete_payload, 2));
    EXPECT_FALSE(telemetry::MetricEventMatchesSchema(*event, entities, 1, unexpected_payload, 2));
}

TEST(TelemetryRegistryTest, CentralizesValidationAndAdmission)
{
    const auto *cashflow = telemetry::FindMetricFamily("pop.cashflow");
    ASSERT_NE(cashflow, nullptr);
    EXPECT_EQ(telemetry::MetricFamilyValidate(*cashflow, telemetry::CaptureCadence::FixedDays, 2, nullptr, 0, 1, 0),
              telemetry::MetricValidationError::DailyCadenceRequired);
    EXPECT_EQ(telemetry::MetricValidationErrorMessage(telemetry::MetricValidationError::DailyCadenceRequired),
              "capture requires daily cadence");
    const auto *factory = telemetry::FindMetricFamily("state.factory");
    ASSERT_NE(factory, nullptr);
    EXPECT_EQ(telemetry::MetricFamilyAdmission(*factory, 0, 0), telemetry::MetricAdmission::BestEffort);
    EXPECT_EQ(telemetry::MetricFamilyAdmission(*factory, 1, 0), telemetry::MetricAdmission::Reliable);
    EXPECT_EQ(telemetry::MetricFamilyAdmission(*factory, 16, 0), telemetry::MetricAdmission::Reliable);
    EXPECT_EQ(telemetry::MetricFamilyAdmission(*factory, 17, 0), telemetry::MetricAdmission::BestEffort);
    const auto *lifecycle = telemetry::FindMetricFamily("pop.lifecycle");
    ASSERT_NE(lifecycle, nullptr);
    EXPECT_EQ(telemetry::MetricFamilyAdmission(*lifecycle, 0, 0), telemetry::MetricAdmission::Reliable);
    EXPECT_EQ(telemetry::MetricFamilyAdmission(*lifecycle, 17, 0), telemetry::MetricAdmission::Reliable);
    EXPECT_EQ(telemetry::FindMetricFamilyForEvent("state.factory.production.fake"), nullptr);
    EXPECT_EQ(telemetry::FindMetricEvent("state.factory.production.fake"), nullptr);
}

TEST(TelemetryRegistryTest, PlansHooksForConfiguredFieldsOnly)
{
    const std::string_view production[] = {"production"};
    const std::string_view flows[] = {"flows"};
    const telemetry::MetricRuleSelection factory_no_hooks[] = {{"state.factory", production, 1}};
    const telemetry::MetricRuleSelection factory_all_fields[] = {{"state.factory", nullptr, 0}};
    const telemetry::MetricRuleSelection artisan_flows[] = {{"pop.artisan", flows, 1}};
    const telemetry::MetricRuleSelection country_economy[] = {{"country.economy", nullptr, 0}};
    const auto no_hooks = telemetry::BuildRuntimePlan(factory_no_hooks, 1);
    EXPECT_FALSE(no_hooks.install_factory_sales_hook);
    EXPECT_FALSE(no_hooks.install_factory_flow_hook);
    EXPECT_FALSE(no_hooks.install_artisan_flow_hook);
    const auto all_factory = telemetry::BuildRuntimePlan(factory_all_fields, 1);
    EXPECT_TRUE(all_factory.install_factory_sales_hook);
    EXPECT_TRUE(all_factory.install_factory_flow_hook);
    const auto artisan = telemetry::BuildRuntimePlan(artisan_flows, 1);
    EXPECT_TRUE(artisan.install_artisan_flow_hook);
    EXPECT_FALSE(artisan.install_factory_flow_hook);
    const auto economy = telemetry::BuildRuntimePlan(country_economy, 1);
    EXPECT_TRUE(economy.install_factory_flow_hook);
    EXPECT_FALSE(economy.install_factory_sales_hook);
}

TEST(TelemetryConfigTest, ParsesExplicitCaptureRules)
{
    telemetry::Config config;
    std::string error;
    ASSERT_TRUE(telemetry::ParseLaunchArguments({L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
        L"-smedley-telemetry-categories=lifecycle,state", L"-smedley-telemetry-sample-days=3",
        L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0",
        L"-smedley-telemetry-capture=country.daily|daily|treasury_raw|ENG,D01||59883384|"}, &config, &error)) << error;
    ASSERT_EQ(config.capture_rules.size(), 1u);
    EXPECT_EQ(config.capture_rules[0].family, "country.daily");
    EXPECT_EQ(config.capture_rules[0].cadence, telemetry::CaptureCadence::Daily);
    EXPECT_EQ(config.capture_rules[0].fields, (std::vector<std::string>{"treasury_raw"}));
    EXPECT_EQ(config.capture_rules[0].country_tags, (std::vector<std::string>{"ENG", "D01"}));
    EXPECT_EQ(config.capture_rules[0].start_date_raw, 59'883'384);
}

TEST(TelemetryConfigTest, AcceptsAggregatePopCapture)
{
    telemetry::Config config;
    std::string error;
    ASSERT_TRUE(telemetry::ParseLaunchArguments({
        L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
        L"-smedley-telemetry-categories=lifecycle,state", L"-smedley-telemetry-sample-days=1",
        L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0",
        L"-smedley-telemetry-capture=pop.aggregate|monthly|pop_count,size_candidate,money_raw||549|59883360|60759216"},
        &config, &error)) << error;
    ASSERT_EQ(config.capture_rules.size(), 1u);
    EXPECT_EQ(config.capture_rules[0].family, "pop.aggregate");
    EXPECT_EQ(config.capture_rules[0].cadence, telemetry::CaptureCadence::Monthly);
    EXPECT_EQ(config.capture_rules[0].fields.size(), 3u);
    ASSERT_EQ(config.capture_rules[0].province_ids.size(), 1u);
    EXPECT_EQ(config.capture_rules[0].province_ids[0], 549);
}

TEST(TelemetryConfigTest, ValidatesDailyPopCashFlowCapture)
{
    const std::vector<std::wstring> base{
        L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
        L"-smedley-telemetry-categories=state", L"-smedley-telemetry-sample-days=1",
        L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0"};
    auto arguments = base;
    arguments.push_back(L"-smedley-telemetry-capture=pop.cashflow|daily|summary,account,components|PRU|||");
    arguments.push_back(L"-smedley-telemetry-capture=pop.cashflow.aggregate|daily|summary,account,components||||");
    telemetry::Config config;
    std::string error;
    ASSERT_TRUE(telemetry::ParseLaunchArguments(arguments, &config, &error)) << error;
    ASSERT_EQ(config.capture_rules.size(), 2u);

    config = {};
    arguments = base;
    arguments.push_back(L"-smedley-telemetry-capture=pop.cashflow|monthly|summary|PRU|||");
    EXPECT_FALSE(telemetry::ParseLaunchArguments(arguments, &config, &error));
    EXPECT_EQ(error, "capture requires daily cadence");

    config = {};
    arguments = base;
    arguments.push_back(L"-smedley-telemetry-capture=pop.cashflow|daily|summary||||");
    EXPECT_FALSE(telemetry::ParseLaunchArguments(arguments, &config, &error));
    EXPECT_EQ(error, "capture requires a country or province filter");
}

TEST(TelemetryConfigTest, RejectsCaptureRulesWithoutStateCategory)
{
    telemetry::Config config;
    std::string error;
    EXPECT_FALSE(telemetry::ParseLaunchArguments({
        L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
        L"-smedley-telemetry-categories=lifecycle", L"-smedley-telemetry-sample-days=1",
        L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0",
        L"-smedley-telemetry-capture=pop.aggregate|daily|||||"}, &config, &error));
    EXPECT_EQ(error, "telemetry capture rules require the state category");
}

TEST(TelemetryConfigTest, AcceptsCountryMetricGroups)
{
    telemetry::Config config;
    std::string error;
    ASSERT_TRUE(telemetry::ParseLaunchArguments({
        L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
        L"-smedley-telemetry-categories=state", L"-smedley-telemetry-sample-days=1",
        L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0",
        L"-smedley-telemetry-capture=country.metrics|monthly|power,politics|PRU|||"}, &config, &error)) << error;
    ASSERT_EQ(config.capture_rules.size(), 1u);
    EXPECT_EQ(config.capture_rules[0].family, "country.metrics");
    EXPECT_EQ(config.capture_rules[0].country_tags, (std::vector<std::string>{"PRU"}));
}

TEST(TelemetryConfigTest, AcceptsProvinceProductionCandidates)
{
    telemetry::Config config;
    std::string error;
    ASSERT_TRUE(telemetry::ParseLaunchArguments({
        L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
        L"-smedley-telemetry-categories=state", L"-smedley-telemetry-sample-days=1",
        L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0",
        L"-smedley-telemetry-capture=province.production|weekly|building_slot_count_candidate,construction_count_candidate||549||"},
        &config, &error)) << error;
    ASSERT_EQ(config.capture_rules.size(), 1u);
    EXPECT_EQ(config.capture_rules[0].family, "province.production");
    EXPECT_EQ(config.capture_rules[0].province_ids, (std::vector<int32_t>{549}));
}

TEST(TelemetryConfigTest, AcceptsMilitaryAggregateFamilies)
{
    telemetry::Config config;
    std::string error;
    ASSERT_TRUE(telemetry::ParseLaunchArguments({
        L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
        L"-smedley-telemetry-categories=state", L"-smedley-telemetry-sample-days=1",
        L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0",
        L"-smedley-telemetry-capture=country.military|monthly|unit_count_candidate,mobilized_candidate|PRU|||",
        L"-smedley-telemetry-capture=world.military|yearly|ongoing_war_count_candidate||||"}, &config, &error)) << error;
    ASSERT_EQ(config.capture_rules.size(), 2u);
    EXPECT_EQ(config.capture_rules[0].family, "country.military");
    EXPECT_EQ(config.capture_rules[1].family, "world.military");
}

TEST(TelemetryConfigTest, AcceptsDiplomacyGroups)
{
    telemetry::Config config;
    std::string error;
    ASSERT_TRUE(telemetry::ParseLaunchArguments({
        L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
        L"-smedley-telemetry-categories=state", L"-smedley-telemetry-sample-days=1",
        L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0",
        L"-smedley-telemetry-capture=country.diplomacy|monthly|status,relations|PRU|||"},
        &config, &error)) << error;
    ASSERT_EQ(config.capture_rules.size(), 1u);
    EXPECT_EQ(config.capture_rules[0].family, "country.diplomacy");
    EXPECT_EQ(config.capture_rules[0].country_tags, (std::vector<std::string>{"PRU"}));
}

TEST(TelemetryConfigTest, AcceptsStateFactoryGroups)
{
    telemetry::Config config;
    std::string error;
    ASSERT_TRUE(telemetry::ParseLaunchArguments({
        L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
        L"-smedley-telemetry-categories=state", L"-smedley-telemetry-sample-days=1",
        L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0",
        L"-smedley-telemetry-capture=state.factory|daily|identity,employment,production,finance,inputs,flows,sales|PRU|||"},
        &config, &error)) << error;
    ASSERT_EQ(config.capture_rules.size(), 1u);
    EXPECT_EQ(config.capture_rules[0].family, "state.factory");
    EXPECT_EQ(config.capture_rules[0].country_tags, (std::vector<std::string>{"PRU"}));
}

TEST(TelemetryConfigTest, RejectsProducerSalesOutsideDailyCadence)
{
    telemetry::Config config;
    std::string error;
    EXPECT_FALSE(telemetry::ParseLaunchArguments({
        L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
        L"-smedley-telemetry-categories=state", L"-smedley-telemetry-sample-days=1",
        L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0",
        L"-smedley-telemetry-capture=province.rgo|monthly|sales|PRU|||"},
        &config, &error));
    EXPECT_EQ(error, "capture requires daily cadence");

    error.clear();
    config = {};
    EXPECT_FALSE(telemetry::ParseLaunchArguments({
        L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
        L"-smedley-telemetry-categories=state", L"-smedley-telemetry-sample-days=1",
        L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0",
        L"-smedley-telemetry-capture=pop.artisan|weekly||PRU|||"},
        &config, &error));
    EXPECT_EQ(error, "capture requires daily cadence");
}

TEST(TelemetryConfigTest, AcceptsWorldMarketGroups)
{
    telemetry::Config config;
    std::string error;
    ASSERT_TRUE(telemetry::ParseLaunchArguments({
        L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
        L"-smedley-telemetry-categories=state", L"-smedley-telemetry-sample-days=1",
        L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0",
        L"-smedley-telemetry-capture=world.market|daily|price,supply,demand,sales||||"},
        &config, &error)) << error;
    ASSERT_EQ(config.capture_rules.size(), 1u);
    EXPECT_EQ(config.capture_rules[0].family, "world.market");
}

TEST(TelemetryConfigTest, AcceptsProvinceRgoGroups)
{
    telemetry::Config config;
    std::string error;
    ASSERT_TRUE(telemetry::ParseLaunchArguments({
        L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
        L"-smedley-telemetry-categories=state", L"-smedley-telemetry-sample-days=1",
        L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0",
        L"-smedley-telemetry-capture=province.rgo|daily|identity,employment,production,finance,modifiers,sales|PRU|549,687||"},
        &config, &error)) << error;
    ASSERT_EQ(config.capture_rules.size(), 1u);
    EXPECT_EQ(config.capture_rules[0].family, "province.rgo");
    EXPECT_EQ(config.capture_rules[0].country_tags, (std::vector<std::string>{"PRU"}));
    EXPECT_EQ(config.capture_rules[0].province_ids, (std::vector<int32_t>{549, 687}));
}

TEST(TelemetryConfigTest, AcceptsCountryScopedArtisanAndPopulationGroups)
{
    telemetry::Config config;
    std::string error;
    ASSERT_TRUE(telemetry::ParseLaunchArguments({
        L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
        L"-smedley-telemetry-categories=state", L"-smedley-telemetry-sample-days=1",
        L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0",
        L"-smedley-telemetry-capture=pop.artisan|daily|identity,production,inputs,finance,flows,sales|FRA|||",
        L"-smedley-telemetry-capture=pop.economy|daily|money_raw|FRA|||",
        L"-smedley-telemetry-capture=pop.demographics|daily|size_candidate|FRA|||",
        L"-smedley-telemetry-capture=pop.identity|daily|pop_type_tag_candidate,culture_tag_candidate,religion_tag_candidate|FRA|||",
        L"-smedley-telemetry-capture=pop.needs|daily|life_satisfaction_candidate_raw,everyday_satisfaction_candidate_raw,luxury_satisfaction_candidate_raw|FRA|||",
        L"-smedley-telemetry-capture=pop.lifecycle|daily|summary,appeared,disappeared,scope_changed|FRA|||",
        L"-smedley-telemetry-capture=pop.aggregate|daily|size_candidate|FRA|||"},
        &config, &error)) << error;
    ASSERT_EQ(config.capture_rules.size(), 7u);
    for (const auto &rule : config.capture_rules) {
        EXPECT_EQ(rule.country_tags, (std::vector<std::string>{"FRA"}));
    }
}

TEST(TelemetryConfigTest, RejectsNondailyPopLifecycleCapture)
{
    telemetry::Config config;
    std::string error;
    EXPECT_FALSE(telemetry::ParseLaunchArguments({
        L"-smedley-run-id=run-1", L"-smedley-telemetry-output=C:\\trace.jsonl",
        L"-smedley-telemetry-categories=state", L"-smedley-telemetry-sample-days=1",
        L"-smedley-telemetry-queue-capacity=128", L"-smedley-telemetry-overwrite=0",
        L"-smedley-telemetry-capture=pop.lifecycle|monthly|summary||||"}, &config, &error));
    EXPECT_EQ(error, "capture requires daily cadence");
}

TEST(TelemetryConfigTest, RequiresPopulationOwnerOnlyForCountryFiltersAndArtisans)
{
    EXPECT_FALSE(telemetry::PopulationOwnerRequired("pop.economy", 0));
    EXPECT_FALSE(telemetry::PopulationOwnerRequired("pop.demographics", 0));
    EXPECT_TRUE(telemetry::PopulationOwnerRequired("pop.economy", 1));
    EXPECT_TRUE(telemetry::PopulationOwnerRequired("pop.artisan", 0));
}

TEST(EconomicCaptureTest, DetectsSignedAggregationOverflow)
{
    uint32_t flags = 0;
    int64_t total = (std::numeric_limits<int64_t>::max)();
    telemetry_plugin::AddEconomicValue(1, &total, &flags);
    EXPECT_EQ(total, (std::numeric_limits<int64_t>::max)());
    EXPECT_NE(flags & telemetry_plugin::SNAPSHOT_SUM_OVERFLOW, 0u);

    flags = 0;
    total = (std::numeric_limits<int64_t>::min)();
    telemetry_plugin::AddEconomicValue(-1, &total, &flags);
    EXPECT_EQ(total, (std::numeric_limits<int64_t>::min)());
    EXPECT_NE(flags & telemetry_plugin::SNAPSHOT_SUM_OVERFLOW, 0u);
    EXPECT_EQ(telemetry_plugin::UtilizationBasisPoints(20000, 100000), 2000);
}
