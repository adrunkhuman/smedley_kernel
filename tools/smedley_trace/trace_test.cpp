#include "trace.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <fstream>

namespace trace = smedley::trace;
namespace fs = std::filesystem;

namespace
{
    class TraceTest : public testing::Test
    {
    protected:
        fs::path root;

        void SetUp() override
        {
            root = fs::temp_directory_path() / (L"smedley trace test " + std::to_wstring(GetTickCount64()));
            fs::create_directories(root);
        }

        void TearDown() override
        {
            std::error_code ignored;
            fs::remove_all(root, ignored);
        }

        fs::path Path(const wchar_t *name, const wchar_t *extension = L".jsonl") const
        {
            return root / (std::wstring(name) + extension);
        }

        fs::path Path(const std::wstring &name) const
        {
            return root / (name + L".jsonl");
        }

        void Write(const fs::path &path, const std::string &contents)
        {
            std::ofstream output(path, std::ios::binary);
            ASSERT_TRUE(output);
            output << contents;
        }

        std::string Line(uint64_t sequence, int date = 12, const char *run = "run-1") const
        {
            return std::string("{\"schema\":\"smedley.telemetry\",\"schema_version\":1,\"run_id\":\"") + run
                + "\",\"sequence\":" + std::to_string(sequence)
                + ",\"wall_time_utc\":\"2024-02-29T12:34:56.789Z\",\"monotonic_us\":" + std::to_string(sequence * 10)
                + ",\"game_date_raw\":" + std::to_string(date)
                + ",\"event_type\":\"country.daily\",\"category\":\"state\",\"mapping_id\":\"v2game-3.04\",\"quality\":\"provisional\",\"entities\":{\"country_tag\":\"ENG\"},\"payload\":{\"treasury_raw\":32768,\"treasury\":1.0}}";
        }
    };
}

TEST_F(TraceTest, AcceptsValidFinalEnvelopeWithoutNewline)
{
    const auto path = Path(L"complete");
    Write(path, Line(1));
    trace::Summary summary;
    std::string error;
    ASSERT_TRUE(trace::Read(path, {}, &summary, nullptr, 0, &error)) << error;
    EXPECT_EQ(summary.records, 1u);
    EXPECT_TRUE(summary.warning.empty());
}

TEST_F(TraceTest, AcceptsCrlfRecords)
{
    const auto path = Path(L"crlf");
    Write(path, Line(1) + "\r\n" + Line(2) + "\r\n");
    trace::Summary summary;
    std::string error;
    ASSERT_TRUE(trace::Read(path, {}, &summary, nullptr, 0, &error)) << error;
    EXPECT_EQ(summary.records, 2u);
}

TEST_F(TraceTest, WarnsOnlyForUnexpectedFinalEof)
{
    const auto path = Path(L"partial");
    Write(path, Line(1) + "\n{\"schema\":");
    trace::Summary summary;
    std::string error;
    ASSERT_TRUE(trace::Read(path, {}, &summary, nullptr, 0, &error)) << error;
    EXPECT_EQ(summary.records, 1u);
    EXPECT_EQ(summary.warning, "incomplete final line ignored");
}

TEST_F(TraceTest, DistinguishesTruncatedUtf8FromInvalidStringControls)
{
    const auto path = Path(L"utf8 tail");
    Write(path, Line(1) + "\n{\"value\":\"\xe2");
    trace::Summary summary;
    std::string error;
    ASSERT_TRUE(trace::Read(path, {}, &summary, nullptr, 0, &error)) << error;
    EXPECT_FALSE(summary.warning.empty());

    Write(path, Line(1) + "\n{\"value\":\"bad\x01tail\"}");
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    EXPECT_NE(error.find("control"), std::string::npos);
}

TEST_F(TraceTest, RejectsBadEnvelopeAfterAValidRecordWithoutNewline)
{
    const auto path = Path(L"complete bad tail");
    Write(path, Line(1) + "\n{}");
    trace::Summary summary;
    std::string error;
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    EXPECT_NE(error.find("envelope"), std::string::npos);
}

TEST_F(TraceTest, RejectsCompleteBadFinalRecords)
{
    for (const std::string record : {std::string("{}"), std::string("{]"), std::string("{\"schema\":\"x\"}" )}) {
        const auto path = Path(L"bad" + std::to_wstring(record.size()));
        Write(path, record);
        trace::Summary summary;
        std::string error;
        EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    }
}

TEST_F(TraceTest, RejectsEmptyPartialOnlyAndEmbeddedCarriageReturn)
{
    for (const std::string contents : {std::string{}, std::string("{\"schema\":"), Line(1) + "\rX\n"}) {
        const auto path = Path(L"invalid" + std::to_wstring(contents.size()));
        Write(path, contents);
        trace::Summary summary;
        std::string error;
        EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    }
}

TEST_F(TraceTest, CountsInitialGapsAndRejectsBadSequenceOrRun)
{
    const auto gaps = Path(L"gaps");
    Write(gaps, Line(3) + "\n" + Line(5) + "\n");
    trace::Summary summary;
    std::string error;
    ASSERT_TRUE(trace::Read(gaps, {}, &summary, nullptr, 0, &error)) << error;
    EXPECT_EQ(summary.gaps, 3u);

    const auto invalid = Path(L"sequence");
    Write(invalid, Line(2) + "\n" + Line(1) + "\n");
    EXPECT_FALSE(trace::Read(invalid, {}, &summary, nullptr, 0, &error));
    Write(invalid, Line(1) + "\n" + Line(2, 12, "other") + "\n");
    EXPECT_FALSE(trace::Read(invalid, {}, &summary, nullptr, 0, &error));
}

TEST_F(TraceTest, RejectsIntermediateMonotonicRegression)
{
    auto second = Line(2);
    second.replace(second.find("\"monotonic_us\":20"), std::string("\"monotonic_us\":20").size(), "\"monotonic_us\":5");
    const auto path = Path(L"monotonic regression");
    Write(path, Line(1) + "\n" + second + "\n" + Line(3) + "\n");
    trace::Summary summary;
    std::string error;
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    EXPECT_NE(error.find("monotonic_us"), std::string::npos);
}

TEST_F(TraceTest, RejectsInvalidCalendarRunIdAndSequenceZero)
{
    auto invalid = Line(1);
    invalid.replace(invalid.find("2024-02-29"), 10, "2023-02-29");
    const auto path = Path(L"schema");
    Write(path, invalid);
    trace::Summary summary;
    std::string error;
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    Write(path, Line(0));
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    Write(path, Line(1, 12, "bad_tag"));
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
}

TEST_F(TraceTest, ExportsCsvTransactionallyAndNeutralizesFormulaText)
{
    const auto input = Path(L"input");
    const auto output = Path(L"output", L".csv");
    Write(input, Line(1, 12, "-run"));
    std::string error;
    ASSERT_TRUE(trace::ExportCountryCsv(input, output, false, &error)) << error;
    std::ifstream csv(output, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(csv)), {});
    EXPECT_NE(contents.find("\"'-run\""), std::string::npos);
    Write(output, "preserve");
    EXPECT_FALSE(trace::ExportCountryCsv(input, output, false, &error));
    std::ifstream preserved(output, std::ios::binary);
    EXPECT_EQ(std::string((std::istreambuf_iterator<char>(preserved)), {}), "preserve");
}

TEST_F(TraceTest, DateRegressionMakesThroughputUnavailable)
{
    const auto path = Path(L"regression");
    Write(path, Line(1, 20) + "\n" + Line(2, 10) + "\n");
    trace::Summary summary;
    std::string error;
    ASSERT_TRUE(trace::Read(path, {}, &summary, nullptr, 0, &error)) << error;
    EXPECT_TRUE(summary.date_regressed);
    EXPECT_NE(trace::FormatSummary(summary).find("game_days_per_sec=unavailable"), std::string::npos);
}

TEST_F(TraceTest, ConvertsRawDateUnitsToGameDays)
{
    const auto path = Path(L"date units");
    Write(path, Line(1, 100) + "\n" + Line(2, 124) + "\n");
    trace::Summary summary;
    std::string error;
    ASSERT_TRUE(trace::Read(path, {}, &summary, nullptr, 0, &error)) << error;
    const auto formatted = trace::FormatSummary(summary);
    EXPECT_NE(formatted.find("game_date_span_days=1.000000"), std::string::npos);
    EXPECT_NE(formatted.find("game_days_per_sec=100000"), std::string::npos);
}

TEST_F(TraceTest, SummarizesAndComparesCompletedBenchmarks)
{
    const auto benchmark = [](uint64_t sequence, uint64_t monotonic, int date, const char *event, const char *payload, const char *run) {
        const char *quality = std::string_view(event) == "benchmark.resources" ? "verified-current" : "provisional";
        return std::string("{\"schema\":\"smedley.telemetry\",\"schema_version\":1,\"run_id\":\"") + run
            + "\",\"sequence\":" + std::to_string(sequence) + ",\"wall_time_utc\":\"2024-02-29T12:34:56.789Z\",\"monotonic_us\":" + std::to_string(monotonic)
            + ",\"game_date_raw\":" + std::to_string(date) + ",\"event_type\":\"" + event + "\",\"category\":\"lifecycle\",\"mapping_id\":\"v2game-3.04\",\"quality\":\"" + quality + "\",\"entities\":{},\"payload\":" + payload + "}";
    };
    const auto left = Path(L"benchmark left");
    const auto right = Path(L"benchmark right");
    Write(left, benchmark(1, 10, 100, "benchmark.started", "{\"start_date_raw\":100,\"target_date_raw\":124,\"requested_days\":1,\"timeout_seconds\":600}", "left")
        + "\n" + benchmark(2, 109, 124, "benchmark.resources", "{\"process_cpu_us\":90,\"working_set_start_bytes\":1000,\"working_set_end_bytes\":1200,\"private_bytes_start\":2000,\"private_bytes_end\":2200,\"process_peak_working_set_bytes\":1300}", "left")
        + "\n" + benchmark(3, 110, 124, "benchmark.completed", "{\"start_date_raw\":100,\"target_date_raw\":124,\"actual_date_raw\":124,\"game_days\":1,\"elapsed_us\":100,\"overshoot_raw\":0,\"paused\":true}", "left"));
    Write(right, benchmark(1, 10, 100, "benchmark.started", "{\"start_date_raw\":100,\"target_date_raw\":124,\"requested_days\":1,\"timeout_seconds\":600}", "right")
        + "\n" + benchmark(2, 209, 124, "benchmark.resources", "{\"process_cpu_us\":180,\"working_set_start_bytes\":1000,\"working_set_end_bytes\":1400,\"private_bytes_start\":2000,\"private_bytes_end\":2400,\"process_peak_working_set_bytes\":1500}", "right")
        + "\n" + benchmark(3, 210, 124, "benchmark.completed", "{\"start_date_raw\":100,\"target_date_raw\":124,\"actual_date_raw\":124,\"game_days\":1,\"elapsed_us\":200,\"overshoot_raw\":0,\"paused\":true}", "right"));
    trace::Summary left_summary, right_summary;
    std::string error;
    ASSERT_TRUE(trace::Read(left, {}, &left_summary, nullptr, 0, &error)) << error;
    ASSERT_TRUE(trace::Read(right, {}, &right_summary, nullptr, 0, &error)) << error;
    EXPECT_NE(trace::FormatSummary(left_summary).find("benchmark_status=completed"), std::string::npos);
    EXPECT_NE(trace::FormatSummary(left_summary).find("benchmark_process_cpu_us=90"), std::string::npos);
    EXPECT_NE(trace::FormatSummary(left_summary).find("benchmark_working_set_delta_bytes=200"), std::string::npos);
    EXPECT_NE(trace::FormatCompare(left_summary, right_summary).find("benchmark_elapsed_us 100 | 200 | 100"), std::string::npos);
    EXPECT_NE(trace::FormatCompare(left_summary, right_summary).find("benchmark_process_cpu_us 90 | 180 | 90"), std::string::npos);
}

TEST_F(TraceTest, ValidatesBenchmarkResourceSchemaAndOrder)
{
    const auto record = [](uint64_t sequence, const char *date, const char *event, const char *payload, const char *quality) {
        return std::string("{\"schema\":\"smedley.telemetry\",\"schema_version\":1,\"run_id\":\"run-1\",\"sequence\":")
            + std::to_string(sequence) + ",\"wall_time_utc\":\"2024-02-29T12:34:56.789Z\",\"monotonic_us\":"
            + std::to_string(sequence) + ",\"game_date_raw\":" + date + ",\"event_type\":\"" + event
            + "\",\"category\":\"lifecycle\",\"mapping_id\":\"v2game-3.04\",\"quality\":\"" + quality
            + "\",\"entities\":{},\"payload\":" + payload + "}";
    };
    const auto started = record(1, "100", "benchmark.started",
        "{\"start_date_raw\":100,\"target_date_raw\":124,\"requested_days\":1,\"timeout_seconds\":600}", "provisional");
    const auto resources = record(2, "124", "benchmark.resources",
        "{\"process_cpu_us\":10,\"working_set_start_bytes\":100,\"working_set_end_bytes\":120,\"process_peak_working_set_bytes\":130}", "verified-current");
    const auto completed = record(3, "124", "benchmark.completed",
        "{\"start_date_raw\":100,\"target_date_raw\":124,\"actual_date_raw\":124,\"game_days\":1,\"elapsed_us\":10,\"overshoot_raw\":0,\"paused\":true}", "provisional");
    const auto path = Path(L"benchmark resources");
    trace::Summary summary;
    std::string error;

    Write(path, started + "\n" + resources + "\n" + completed);
    ASSERT_TRUE(trace::Read(path, {}, &summary, nullptr, 0, &error)) << error;
    EXPECT_EQ(summary.benchmark.process_cpu_us, 10u);
    EXPECT_EQ(summary.benchmark.working_set_end_bytes, 120u);

    Write(path, resources);
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    Write(path, started + "\n" + resources + "\n" + record(3, "124", "benchmark.resources", "{\"process_cpu_us\":11}", "verified-current"));
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    Write(path, started + "\n" + completed + "\n" + record(4, "124", "benchmark.resources", "{\"process_cpu_us\":11}", "verified-current"));
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    Write(path, started + "\n" + record(2, "124", "benchmark.resources", "{\"process_cpu_us\":-1}", "verified-current"));
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    Write(path, started + "\n" + record(2, "124", "benchmark.resources", "{\"unknown\":1}", "verified-current"));
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    Write(path, started + "\n" + record(2, "123", "benchmark.resources", "{\"process_cpu_us\":1}", "verified-current") + "\n" + completed);
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    Write(path, started + "\n" + record(2, "null", "benchmark.resources", "{\"process_cpu_us\":1}", "verified-current") + "\n"
        + record(3, "100", "benchmark.failed", "{\"start_date_raw\":100,\"target_date_raw\":124,\"actual_date_raw\":100,\"elapsed_us\":600000000,\"reason\":\"timeout\",\"paused\":true}", "provisional"));
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    Write(path, started + "\n" + record(2, "100", "benchmark.resources", "{\"process_cpu_us\":1}", "verified-current") + "\n"
        + record(3, "null", "benchmark.failed", "{\"start_date_raw\":100,\"target_date_raw\":124,\"elapsed_us\":1,\"reason\":\"idler_unavailable\"}", "provisional"));
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
}

TEST_F(TraceTest, VerifiesExpectedBenchmarkOutcomeAndTelemetryHealth)
{
    trace::Summary summary;
    summary.run_id = "run-1";
    summary.benchmark.status = "completed";
    summary.benchmark.start_date_raw = 100;
    summary.benchmark.target_date_raw = 124;
    summary.benchmark.actual_date_raw = 124;
    summary.benchmark.game_days = 1;
    summary.benchmark.elapsed_us = 50;
    summary.benchmark.paused = true;
    summary.progress_seen = true;
    summary.progress["dropped"] = {trace::JsonKind::Number, "0"};
    summary.progress["write_failed"] = {trace::JsonKind::Boolean, "false"};
    std::string error;
    EXPECT_TRUE(trace::IsBenchmarkFailureReason("timeout"));
    EXPECT_FALSE(trace::IsBenchmarkFailureReason("unknown"));
    EXPECT_TRUE(trace::VerifyBenchmark(summary, {trace::BenchmarkStatus::Completed, 1, {}}, &error)) << error;
    EXPECT_NE(trace::FormatBenchmarkVerification(summary).find("benchmark_verified status=completed"), std::string::npos);
    EXPECT_FALSE(trace::VerifyBenchmark(summary, {trace::BenchmarkStatus::Completed, 2, {}}, &error));
    summary.gaps = 1;
    EXPECT_FALSE(trace::VerifyBenchmark(summary, {trace::BenchmarkStatus::Completed, 1, {}}, &error));
    summary.gaps = 0;
    summary.progress["dropped"].text = "1";
    EXPECT_FALSE(trace::VerifyBenchmark(summary, {trace::BenchmarkStatus::Completed, 1, {}}, &error));
    summary.progress["dropped"].text = "0";
    summary.progress["write_failed"].text = "true";
    EXPECT_FALSE(trace::VerifyBenchmark(summary, {trace::BenchmarkStatus::Completed, 1, {}}, &error));

    summary.progress.clear();
    summary.progress_seen = false;
    summary.benchmark.status = "failed";
    summary.benchmark.target_date_raw = 100;
    summary.benchmark.actual_date_raw = 100;
    summary.benchmark.game_days.reset();
    summary.benchmark.reason = "invalid_target";
    EXPECT_TRUE(trace::VerifyBenchmark(summary, {trace::BenchmarkStatus::Failed, std::nullopt, "invalid_target"}, &error)) << error;
    EXPECT_FALSE(trace::VerifyBenchmark(summary, {trace::BenchmarkStatus::Failed, std::nullopt, "timeout"}, &error));
}

TEST_F(TraceTest, RejectsIncompleteOrSemanticallyImpossibleBenchmarkAssertions)
{
    const auto record = [](uint64_t sequence, int date, const char *event, const char *payload) {
        return std::string("{\"schema\":\"smedley.telemetry\",\"schema_version\":1,\"run_id\":\"run-1\",\"sequence\":")
            + std::to_string(sequence) + ",\"wall_time_utc\":\"2024-02-29T12:34:56.789Z\",\"monotonic_us\":"
            + std::to_string(sequence) + ",\"game_date_raw\":" + std::to_string(date) + ",\"event_type\":\"" + event
            + "\",\"category\":\"lifecycle\",\"mapping_id\":\"v2game-3.04\",\"quality\":\"provisional\",\"entities\":{},\"payload\":" + payload + "}";
    };
    const auto started = record(1, 100, "benchmark.started",
        "{\"start_date_raw\":100,\"target_date_raw\":124,\"requested_days\":1,\"timeout_seconds\":2}");
    const auto path = Path(L"benchmark assertion malformed");
    trace::Summary summary;
    std::string error;

    Write(path, record(1, 100, "benchmark.failed",
        "{\"start_date_raw\":100,\"target_date_raw\":124,\"actual_date_raw\":100,\"elapsed_us\":1,\"reason\":\"invalid_target\",\"paused\":true}"));
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    Write(path, started + "\n" + record(2, 124, "benchmark.failed",
        "{\"start_date_raw\":100,\"target_date_raw\":124,\"actual_date_raw\":124,\"elapsed_us\":2000000,\"reason\":\"date_overshoot\",\"paused\":true}"));
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    Write(path, started + "\n" + record(2, 100, "benchmark.failed",
        "{\"start_date_raw\":100,\"target_date_raw\":124,\"actual_date_raw\":100,\"elapsed_us\":1999999,\"reason\":\"timeout\",\"paused\":true}"));
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));

    Write(path, started + "\n" + record(2, 124, "benchmark.completed",
        "{\"start_date_raw\":100,\"target_date_raw\":124,\"actual_date_raw\":124,\"game_days\":1,\"elapsed_us\":1,\"overshoot_raw\":0,\"paused\":true}"));
    ASSERT_TRUE(trace::Read(path, {}, &summary, nullptr, 0, &error)) << error;
    EXPECT_FALSE(trace::VerifyBenchmark(summary, {trace::BenchmarkStatus::Completed, 1, {}}, &error));
    Write(path, started + "\n" + record(2, 124, "benchmark.completed",
        "{\"start_date_raw\":100,\"target_date_raw\":124,\"actual_date_raw\":124,\"game_days\":1,\"elapsed_us\":1,\"overshoot_raw\":0,\"paused\":true}") + "\n{\"");
    ASSERT_TRUE(trace::Read(path, {}, &summary, nullptr, 0, &error)) << error;
    EXPECT_FALSE(trace::VerifyBenchmark(summary, {trace::BenchmarkStatus::Completed, 1, {}}, &error));
}

TEST_F(TraceTest, AcceptsProducerCompatibleBenchmarkFailureShapes)
{
    const auto record = [](uint64_t sequence, const char *date, const char *event, const std::string &payload) {
        return std::string("{\"schema\":\"smedley.telemetry\",\"schema_version\":1,\"run_id\":\"run-1\",\"sequence\":")
            + std::to_string(sequence) + ",\"wall_time_utc\":\"2024-02-29T12:34:56.789Z\",\"monotonic_us\":"
            + std::to_string(sequence) + ",\"game_date_raw\":" + date + ",\"event_type\":\"" + event
            + "\",\"category\":\"lifecycle\",\"mapping_id\":\"v2game-3.04\",\"quality\":\"provisional\",\"entities\":{},\"payload\":" + payload + "}";
    };
    const auto started = record(1, "100", "benchmark.started",
        "{\"start_date_raw\":100,\"target_date_raw\":124,\"requested_days\":1,\"timeout_seconds\":1}");
    struct FailureCase { const char *reason; const char *date; const char *fields; int64_t elapsed; };
    const FailureCase cases[] = {
        {"timeout", "100", ",\"actual_date_raw\":100,\"paused\":true", 1000000},
        {"date_overshoot", "125", ",\"actual_date_raw\":125,\"paused\":true", 1},
        {"idler_unavailable", "null", "", 1},
        {"invalid_pause_state", "100", ",\"actual_date_raw\":100,\"paused\":true", 1},
        {"pause_failed", "100", ",\"actual_date_raw\":100,\"paused\":false", 1},
        {"observer_invariant_failed", "100", ",\"actual_date_raw\":100,\"paused\":true", 1},
        {"date_regressed", "99", ",\"actual_date_raw\":99,\"paused\":true", 1},
        {"unexpected_pause", "100", ",\"actual_date_raw\":100,\"paused\":true", 1},
        {"timer_unavailable", "100", ",\"actual_date_raw\":100,\"paused\":true", 1},
    };
    const auto path = Path(L"benchmark failure shapes");
    for (const auto &test : cases) {
        const std::string payload = "{\"start_date_raw\":100,\"target_date_raw\":124,\"elapsed_us\":"
            + std::to_string(test.elapsed) + ",\"reason\":\"" + test.reason + "\"" + test.fields + "}";
        Write(path, started + "\n" + record(2, test.date, "benchmark.failed", payload));
        trace::Summary summary;
        std::string error;
        EXPECT_TRUE(trace::Read(path, {}, &summary, nullptr, 0, &error)) << test.reason << ": " << error;
    }
    Write(path, record(1, "100", "benchmark.failed",
        "{\"start_date_raw\":100,\"target_date_raw\":100,\"actual_date_raw\":100,\"elapsed_us\":1,\"reason\":\"invalid_target\",\"paused\":true}"));
    trace::Summary summary;
    std::string error;
    EXPECT_TRUE(trace::Read(path, {}, &summary, nullptr, 0, &error)) << error;
}

TEST_F(TraceTest, RejectsInvalidBenchmarkCompletionAndReportsFailedBenchmark)
{
    const auto path = Path(L"benchmark failed");
    Write(path, "{\"schema\":\"smedley.telemetry\",\"schema_version\":1,\"run_id\":\"run-1\",\"sequence\":1,\"wall_time_utc\":\"2024-02-29T12:34:56.789Z\",\"monotonic_us\":1,\"game_date_raw\":100,\"event_type\":\"benchmark.started\",\"category\":\"lifecycle\",\"mapping_id\":\"v2game-3.04\",\"quality\":\"provisional\",\"entities\":{},\"payload\":{\"start_date_raw\":100,\"target_date_raw\":124,\"requested_days\":1,\"timeout_seconds\":600}}\n{\"schema\":\"smedley.telemetry\",\"schema_version\":1,\"run_id\":\"run-1\",\"sequence\":2,\"wall_time_utc\":\"2024-02-29T12:34:56.789Z\",\"monotonic_us\":600000001,\"game_date_raw\":100,\"event_type\":\"benchmark.failed\",\"category\":\"lifecycle\",\"mapping_id\":\"v2game-3.04\",\"quality\":\"provisional\",\"entities\":{},\"payload\":{\"start_date_raw\":100,\"target_date_raw\":124,\"actual_date_raw\":100,\"elapsed_us\":600000000,\"reason\":\"timeout\",\"paused\":true}}");
    trace::Summary summary;
    std::string error;
    ASSERT_TRUE(trace::Read(path, {}, &summary, nullptr, 0, &error)) << error;
    EXPECT_NE(trace::FormatSummary(summary).find("benchmark_status=failed"), std::string::npos);
    auto invalid = fs::path(path);
    Write(invalid, "{\"schema\":\"smedley.telemetry\",\"schema_version\":1,\"run_id\":\"run-1\",\"sequence\":1,\"wall_time_utc\":\"2024-02-29T12:34:56.789Z\",\"monotonic_us\":10,\"game_date_raw\":125,\"event_type\":\"benchmark.completed\",\"category\":\"lifecycle\",\"mapping_id\":\"v2game-3.04\",\"quality\":\"provisional\",\"entities\":{},\"payload\":{\"start_date_raw\":100,\"target_date_raw\":124,\"actual_date_raw\":125,\"game_days\":1,\"elapsed_us\":10,\"overshoot_raw\":0,\"paused\":true}}");
    EXPECT_FALSE(trace::Read(invalid, {}, &summary, nullptr, 0, &error));
}

TEST_F(TraceTest, RejectsBenchmarkLifecycleOrderAndAllowsStandaloneInvalidTarget)
{
    const auto record = [](uint64_t sequence, const char *event, const char *payload) {
        return std::string("{\"schema\":\"smedley.telemetry\",\"schema_version\":1,\"run_id\":\"run-1\",\"sequence\":") + std::to_string(sequence)
            + ",\"wall_time_utc\":\"2024-02-29T12:34:56.789Z\",\"monotonic_us\":" + std::to_string(sequence)
            + ",\"game_date_raw\":100,\"event_type\":\"" + event + "\",\"category\":\"lifecycle\",\"mapping_id\":\"v2game-3.04\",\"quality\":\"provisional\",\"entities\":{},\"payload\":" + payload + "}";
    };
    const auto started = record(1, "benchmark.started", "{\"start_date_raw\":100,\"target_date_raw\":124,\"requested_days\":1,\"timeout_seconds\":600}");
    const auto completed = record(2, "benchmark.completed", "{\"start_date_raw\":100,\"target_date_raw\":124,\"actual_date_raw\":124,\"game_days\":1,\"elapsed_us\":1,\"overshoot_raw\":0,\"paused\":true}");
    const auto path = Path(L"benchmark order");
    trace::Summary summary;
    std::string error;
    Write(path, started + "\n" + started);
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    Write(path, completed);
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    Write(path, started + "\n" + completed + "\n" + completed);
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    Write(path, record(1, "benchmark.failed", "{\"start_date_raw\":100,\"target_date_raw\":100,\"actual_date_raw\":100,\"elapsed_us\":1,\"reason\":\"invalid_target\",\"paused\":true}"));
    ASSERT_TRUE(trace::Read(path, {}, &summary, nullptr, 0, &error)) << error;
    EXPECT_EQ(summary.benchmark.status, "failed");
}

TEST_F(TraceTest, RejectsMisalignedBenchmarkStartAndUnpairedFailureDate)
{
    const auto record = [](const char *event, int date, const char *payload) {
        return std::string("{\"schema\":\"smedley.telemetry\",\"schema_version\":1,\"run_id\":\"run-1\",\"sequence\":1,\"wall_time_utc\":\"2024-02-29T12:34:56.789Z\",\"monotonic_us\":1,\"game_date_raw\":")
            + std::to_string(date) + ",\"event_type\":\"" + event
            + "\",\"category\":\"lifecycle\",\"mapping_id\":\"v2game-3.04\",\"quality\":\"provisional\",\"entities\":{},\"payload\":" + payload + "}";
    };
    const auto path = Path(L"benchmark malformed fields");
    trace::Summary summary;
    std::string error;
    Write(path, record("benchmark.started", 100,
        "{\"start_date_raw\":100,\"target_date_raw\":125,\"requested_days\":1,\"timeout_seconds\":600}"));
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
    Write(path, record("benchmark.failed", 100,
        "{\"start_date_raw\":100,\"target_date_raw\":100,\"elapsed_us\":1,\"reason\":\"invalid_target\",\"paused\":true}"));
    EXPECT_FALSE(trace::Read(path, {}, &summary, nullptr, 0, &error));
}

TEST_F(TraceTest, RejectsExportsThatSplitBenchmarkLifecycle)
{
    const auto input = Path(L"benchmark export input");
    const auto output = root / L"benchmark export.jsonl";
    Write(input, "{\"schema\":\"smedley.telemetry\",\"schema_version\":1,\"run_id\":\"run-1\",\"sequence\":1,\"wall_time_utc\":\"2024-02-29T12:34:56.789Z\",\"monotonic_us\":1,\"game_date_raw\":100,\"event_type\":\"benchmark.started\",\"category\":\"lifecycle\",\"mapping_id\":\"v2game-3.04\",\"quality\":\"provisional\",\"entities\":{},\"payload\":{\"start_date_raw\":100,\"target_date_raw\":124,\"requested_days\":1,\"timeout_seconds\":600}}\n{\"schema\":\"smedley.telemetry\",\"schema_version\":1,\"run_id\":\"run-1\",\"sequence\":2,\"wall_time_utc\":\"2024-02-29T12:34:56.789Z\",\"monotonic_us\":2,\"game_date_raw\":124,\"event_type\":\"benchmark.completed\",\"category\":\"lifecycle\",\"mapping_id\":\"v2game-3.04\",\"quality\":\"provisional\",\"entities\":{},\"payload\":{\"start_date_raw\":100,\"target_date_raw\":124,\"actual_date_raw\":124,\"game_days\":1,\"elapsed_us\":1,\"overshoot_raw\":0,\"paused\":true}}");
    std::string error;
    EXPECT_FALSE(trace::ExportTrace(input, output, {"benchmark.completed", {}, {}}, false, &error));
    EXPECT_EQ(error, "benchmark lifecycle events cannot be split by a trace export filter");
    EXPECT_FALSE(trace::ExportTrace(input, output, {"benchmark.resources", {}, {}}, false, &error));
    EXPECT_FALSE(fs::exists(output));
}
