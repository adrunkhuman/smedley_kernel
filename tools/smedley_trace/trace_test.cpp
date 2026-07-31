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
