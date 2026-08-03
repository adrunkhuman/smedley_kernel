#include "trace.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <fstream>
#include <string_view>

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

        std::string LifecycleLine(uint64_t sequence, uint64_t monotonic, const char *event, const char *run = "run-1") const
        {
            std::string entities = "{}", payload = "{}";
            const std::string_view type(event);
            if (type == "campaign.save_selection_requested") payload = "{\"source\":\"campaign_runner\"}";
            else if (type == "campaign.entered") payload = "{\"observer_requested\":true,\"requested_speed\":5,\"requested_paused\":false}";
            else if (type == "observer.configured") {
                entities = "{\"viewing_country\":\"ENG\"}";
                payload = "{\"full_ai_control\":true,\"full_map_visibility\":true}";
            }
            return std::string("{\"schema\":\"smedley.telemetry\",\"schema_version\":1,\"run_id\":\"") + run
                + "\",\"sequence\":" + std::to_string(sequence)
                + ",\"wall_time_utc\":\"2024-02-29T12:34:56.789Z\",\"monotonic_us\":" + std::to_string(monotonic)
                + ",\"game_date_raw\":null,\"event_type\":\"" + event
                + "\",\"category\":\"lifecycle\",\"mapping_id\":\"v2game-3.04\",\"quality\":\"verified-runtime\",\"entities\":"
                + entities + ",\"payload\":" + payload + "}";
        }

        std::string StateLine(uint64_t sequence, int date, const std::string &event,
                              const std::string &entities, const std::string &payload) const
        {
            return std::string("{\"schema\":\"smedley.telemetry\",\"schema_version\":1,\"run_id\":\"run-1\",\"sequence\":")
                + std::to_string(sequence) + ",\"wall_time_utc\":\"2024-02-29T12:34:56.789Z\",\"monotonic_us\":"
                + std::to_string(sequence * 10) + ",\"game_date_raw\":" + std::to_string(date)
                + ",\"event_type\":\"" + event + "\",\"category\":\"state\",\"mapping_id\":\"v2game-3.04\","
                + "\"quality\":\"provisional\",\"entities\":" + entities + ",\"payload\":" + payload + "}";
        }

        std::string HealthLine(uint64_t sequence, const std::string &event,
                               const std::string &entities, const std::string &payload) const
        {
            return std::string("{\"schema\":\"smedley.telemetry\",\"schema_version\":1,\"run_id\":\"run-1\",\"sequence\":")
                + std::to_string(sequence) + ",\"wall_time_utc\":\"2024-02-29T12:34:56.789Z\",\"monotonic_us\":"
                + std::to_string(sequence * 10) + ",\"game_date_raw\":null,\"event_type\":\"" + event
                + "\",\"category\":\"lifecycle\",\"mapping_id\":\"v2game-3.04\",\"quality\":\"verified-current\","
                + "\"entities\":" + entities + ",\"payload\":" + payload + "}";
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

TEST_F(TraceTest, ExportsDirectFactoryValueAddedWithoutFinanceOrInputSnapshots)
{
    const auto input = Path(L"factory-value-added");
    const auto output = Path(L"factory-value-added", L".csv");
    const std::string factory = "{\"country_tag\":\"BEL\",\"state_id\":836,\"factory_type\":\"cement_factory\"}";
    const std::string input_good = "{\"country_tag\":\"BEL\",\"state_id\":836,\"factory_type\":\"cement_factory\",\"good_ordinal\":0}";
    const std::string price_input = "{\"good_ordinal\":0}";
    const std::string price_output = "{\"good_ordinal\":1}";
    std::string trace_text;
    auto append = [&](const std::string &line) { trace_text += line + '\n'; };
    append(StateLine(1, 24, "world.market.price", price_input, "{\"price_raw\":65536}"));
    append(StateLine(2, 24, "world.market.price", price_output, "{\"price_raw\":163840}"));
    append(StateLine(3, 24, "state.factory.production", factory, "{\"output_raw\":131072,\"output_good_ordinal\":1}"));
    append(StateLine(4, 24, "state.factory.input.flow.summary", factory,
        "{\"post_consumption_seen\":true,\"pre_purchase_seen\":true,\"primary_delivery_seen\":true,\"secondary_delivery_seen\":true,\"settlement_count\":1}"));
    append(StateLine(5, 24, "state.factory.input.flow", input_good,
        "{\"post_consumption_raw\":262144,\"pre_purchase_raw\":262144,\"delivered_primary_raw\":65536,\"delivered_secondary_raw\":0}"));
    append(StateLine(6, 48, "world.market.price", price_input, "{\"price_raw\":65536}"));
    append(StateLine(7, 48, "world.market.price", price_output, "{\"price_raw\":163840}"));
    append(StateLine(8, 48, "state.factory.production", factory, "{\"output_raw\":131072,\"output_good_ordinal\":1}"));
    const auto unterminated = Path(L"factory-value-added-unterminated");
    const auto unterminated_output = Path(L"factory-value-added-unterminated", L".csv");
    Write(unterminated, trace_text);
    std::string error;
    EXPECT_FALSE(trace::ExportFactoryValueAddedCsv(unterminated, unterminated_output, "BEL", false, &error));
    EXPECT_EQ(error, "factory value added requires terminal factory, market, and writer health summaries");
    append(StateLine(9, 48, "state.factory.input.flow.summary", factory,
        "{\"post_consumption_seen\":true,\"pre_purchase_seen\":true,\"primary_delivery_seen\":true,\"secondary_delivery_seen\":true,\"settlement_count\":1}"));
    append(StateLine(10, 48, "state.factory.input.flow", input_good,
        "{\"post_consumption_raw\":98304,\"pre_purchase_raw\":98304,\"delivered_primary_raw\":163840,\"delivered_secondary_raw\":0}"));
    append(StateLine(11, 72, "world.market.price", price_input, "{\"price_raw\":65536}"));
    append(StateLine(12, 72, "world.market.price", price_output, "{\"price_raw\":163840}"));
    append(StateLine(13, 72, "state.factory.production", factory,
        "{\"output_raw\":131072,\"output_good_ordinal\":1}"));
    append(StateLine(14, 72, "state.factory.input.flow.summary", factory,
        "{\"post_consumption_seen\":true,\"pre_purchase_seen\":false,"
        "\"primary_delivery_seen\":false,\"secondary_delivery_seen\":false,\"settlement_count\":0}"));
    append(StateLine(15, 72, "state.factory.input.flow", input_good,
        "{\"post_consumption_raw\":32768,\"pre_purchase_raw\":0,"
        "\"delivered_primary_raw\":0,\"delivered_secondary_raw\":0}"));
    append(HealthLine(16, "telemetry.family.summary", "{\"family\":\"state.factory\"}", "{\"dropped\":0,\"invalid\":0}"));
    append(HealthLine(17, "telemetry.family.summary", "{\"family\":\"world.market\"}", "{\"dropped\":0,\"invalid\":0}"));
    append(HealthLine(18, "telemetry.summary", "{}", "{\"dropped\":0,\"write_failed\":false}"));
    Write(input, trace_text);

    error.clear();
    ASSERT_TRUE(trace::ExportFactoryValueAddedCsv(input, output, "BEL", false, &error)) << error;
    std::ifstream csv(output, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(csv)), {});
    EXPECT_NE(contents.find("\"run-1\",24,48,\"BEL\",1,20.000000000,14.000000000,6.000000000,\"verified-runtime\""), std::string::npos);
}

TEST_F(TraceTest, RejectsIncompleteFactoryValueAddedIntervals)
{
    const auto input = Path(L"factory-value-added-incomplete");
    const auto output = Path(L"factory-value-added-incomplete", L".csv");
    const std::string factory = "{\"country_tag\":\"BEL\",\"state_id\":836,\"factory_type\":\"cement_factory\"}";
    const std::string price_input = "{\"good_ordinal\":0}";
    const std::string price_output = "{\"good_ordinal\":1}";
    std::string trace_text;
    auto append = [&](const std::string &line) { trace_text += line + '\n'; };
    append(StateLine(1, 24, "world.market.price", price_input, "{\"price_raw\":65536}"));
    append(StateLine(2, 24, "world.market.price", price_output, "{\"price_raw\":163840}"));
    append(StateLine(3, 24, "state.factory.production", factory, "{\"output_raw\":131072,\"output_good_ordinal\":1}"));
    append(StateLine(4, 48, "world.market.price", price_input, "{\"price_raw\":65536}"));
    append(StateLine(5, 48, "world.market.price", price_output, "{\"price_raw\":163840}"));
    append(StateLine(6, 48, "state.factory.production", factory, "{\"output_raw\":131072,\"output_good_ordinal\":1}"));
    append(HealthLine(7, "telemetry.family.summary", "{\"family\":\"state.factory\"}", "{\"dropped\":0,\"invalid\":0}"));
    append(HealthLine(8, "telemetry.family.summary", "{\"family\":\"world.market\"}", "{\"dropped\":0,\"invalid\":0}"));
    append(HealthLine(9, "telemetry.summary", "{}", "{\"dropped\":0,\"write_failed\":false}"));
    Write(input, trace_text);

    std::string error;
    EXPECT_FALSE(trace::ExportFactoryValueAddedCsv(input, output, "BEL", false, &error));
    EXPECT_EQ(error, "factory snapshot is missing production or flow-summary evidence at date 24");
    EXPECT_FALSE(fs::exists(output));
}

TEST_F(TraceTest, ExportsStrictProducerSalesAccounts)
{
    const auto input = Path(L"producer-sales");
    const auto output = Path(L"producer-sales", L".csv");
    const std::string factory =
        "{\"country_tag\":\"FRA\",\"state_id\":1,\"factory_type\":\"cement_factory\"}";
    const std::string rgo = "{\"country_tag\":\"FRA\",\"province_id\":2}";
    const std::string artisan = "{\"country_tag\":\"FRA\",\"province_id\":3,\"pop_id\":4}";
    uint64_t sequence = 0;
    std::string trace_text;
    auto append = [&](const std::string &line) { trace_text += line + '\n'; };
    append(StateLine(++sequence, 48, "state.factory.sales.summary", factory,
        "{\"settlement_seen\":true,\"settlement_count\":1,\"complete\":true}"));
    append(StateLine(++sequence, 48, "state.factory.sales.quantity", factory,
        "{\"output_good_ordinal\":1,\"opening_inventory_raw\":100,\"produced_raw\":50,"
        "\"sold_raw\":120,\"closing_inventory_raw\":30}"));
    append(StateLine(++sequence, 48, "state.factory.sales.revenue", factory,
        "{\"proceeds_raw\":9000}"));
    for (const auto &[family, entities] : std::vector<std::pair<std::string, std::string>>{
             {"province.rgo", rgo}, {"pop.artisan", artisan}}) {
        append(StateLine(++sequence, 48, family + ".sales.summary", entities,
            "{\"settlement_seen\":true,\"opening_inventory_seen\":true,\"complete\":true}"));
        append(StateLine(++sequence, 48, family + ".sales.quantity", entities,
            "{\"output_good_ordinal\":2,\"opening_inventory_raw\":40,\"produced_raw\":20,"
            "\"sold_raw\":45,\"closing_inventory_raw\":15}"));
        append(StateLine(++sequence, 48, family + ".sales.revenue", entities,
            "{\"proceeds_raw\":7000,\"percent_sold_domestic_raw\":24576,"
            "\"percent_sold_export_raw\":16384}"));
    }
    for (const char *family : {"state.factory", "province.rgo", "pop.artisan"}) {
        append(HealthLine(++sequence, "telemetry.family.summary",
            std::string("{\"family\":\"") + family + "\"}", "{\"dropped\":0,\"invalid\":0}"));
    }
    append(HealthLine(++sequence, "telemetry.summary", "{}", "{\"dropped\":0,\"write_failed\":false}"));
    Write(input, trace_text);

    std::string error;
    ASSERT_TRUE(trace::ExportProducerSalesCsv(input, output, "FRA", false, &error)) << error;
    std::ifstream csv(output, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(csv)), {});
    EXPECT_NE(contents.find("\"state.factory\",\"FRA\",1,,,\"cement_factory\",1,100,50,120,30,9000,,,\"provisional\""),
        std::string::npos);
    EXPECT_NE(contents.find("\"province.rgo\",\"FRA\",,2,,\"\",2,40,20,45,15,7000,24576,16384,\"provisional\""),
        std::string::npos);

    auto unreconciled = trace_text;
    const auto sold = unreconciled.find("\"sold_raw\":120");
    ASSERT_NE(sold, std::string::npos);
    unreconciled.replace(sold, std::string("\"sold_raw\":120").size(), "\"sold_raw\":121");
    const auto invalid = Path(L"producer-sales-invalid");
    Write(invalid, unreconciled);
    EXPECT_FALSE(trace::ExportProducerSalesCsv(invalid, Path(L"producer-sales-invalid", L".csv"),
        "FRA", false, &error));
    EXPECT_NE(error.find("unreconciled"), std::string::npos);

    auto orphan = trace_text;
    const auto revenue = orphan.find("state.factory.sales.revenue");
    ASSERT_NE(revenue, std::string::npos);
    orphan.replace(revenue, std::string("state.factory.sales.revenue").size(), "state.factory.finance");
    const auto orphan_trace = Path(L"producer-sales-orphan");
    Write(orphan_trace, orphan);
    const auto orphan_output = Path(L"producer-sales-orphan", L".csv");
    EXPECT_FALSE(trace::ExportProducerSalesCsv(orphan_trace, orphan_output, "FRA", false, &error));
    EXPECT_EQ(error, "producer sales summary and detail records are inconsistent");
    EXPECT_FALSE(fs::exists(orphan_output));

    auto revenue_only = trace_text;
    const auto quantity = revenue_only.find("state.factory.sales.quantity");
    ASSERT_NE(quantity, std::string::npos);
    revenue_only.replace(quantity, std::string("state.factory.sales.quantity").size(), "state.factory.production");
    const auto revenue_only_trace = Path(L"producer-sales-revenue-only");
    Write(revenue_only_trace, revenue_only);
    EXPECT_FALSE(trace::ExportProducerSalesCsv(revenue_only_trace,
        Path(L"producer-sales-revenue-only", L".csv"), "FRA", false, &error));
    EXPECT_EQ(error, "producer sales summary and detail records are inconsistent");

    auto incomplete_with_details = trace_text;
    const auto complete = incomplete_with_details.find("\"complete\":true");
    ASSERT_NE(complete, std::string::npos);
    incomplete_with_details.replace(complete, std::string("\"complete\":true").size(), "\"complete\":false");
    const auto incomplete_trace = Path(L"producer-sales-incomplete-details");
    Write(incomplete_trace, incomplete_with_details);
    EXPECT_FALSE(trace::ExportProducerSalesCsv(incomplete_trace,
        Path(L"producer-sales-incomplete-details", L".csv"), "FRA", false, &error));
    EXPECT_EQ(error, "producer sales summary and detail records are inconsistent");
}

TEST_F(TraceTest, ExportsStrictPopCashFlowAccounts)
{
    const auto input = Path(L"pop-cashflow");
    const auto output = Path(L"pop-cashflow", L".csv");
    const std::string entities = "{\"country_tag\":\"FRA\",\"pop_type_id_candidate\":2}";
    std::string trace_text;
    trace_text += HealthLine(1, "telemetry.capture.rule", "{\"family\":\"pop.cashflow.aggregate\"}",
        "{\"cadence\":\"daily\",\"all_fields\":false,\"country_filter_count\":1,"
        "\"province_filter_count\":0,\"bounded_dates\":false}") + '\n';
    trace_text += HealthLine(2, "telemetry.capture.field",
        "{\"family\":\"pop.cashflow.aggregate\",\"field\":\"summary\"}", "{}") + '\n';
    trace_text += HealthLine(3, "telemetry.capture.field",
        "{\"family\":\"pop.cashflow.aggregate\",\"field\":\"account\"}", "{}") + '\n';
    trace_text += HealthLine(4, "telemetry.capture.field",
        "{\"family\":\"pop.cashflow.aggregate\",\"field\":\"components\"}", "{}") + '\n';
    trace_text += StateLine(5, 48, "pop.cashflow.aggregate.summary", entities,
        "{\"opening_pop_count\":10,\"closing_pop_count\":11,"
        "\"opening_money_seen\":true,\"reconciled\":true}") + '\n';
    trace_text += StateLine(6, 48, "pop.cashflow.aggregate.account", entities,
        "{\"opening_money_raw\":1000,\"closing_money_raw\":1150,"
        "\"money_delta_raw\":150,\"residual_raw\":0}") + '\n';
    trace_text += StateLine(7, 48, "pop.cashflow.aggregate.component",
        "{\"country_tag\":\"FRA\",\"pop_type_id_candidate\":2,"
        "\"cash_flow_index\":2,\"component\":\"salary\"}",
        "{\"posted_raw\":200,\"money_delta_raw\":150}") + '\n';
    trace_text += StateLine(8, 48, "pop.cashflow.country.summary", "{\"country_tag\":\"FRA\"}",
        "{\"opening_pop_count\":10,\"closing_pop_count\":11,"
        "\"opening_money_seen\":true,\"reconciled\":true}") + '\n';
    trace_text += StateLine(9, 48, "pop.cashflow.country.account", "{\"country_tag\":\"FRA\"}",
        "{\"opening_money_raw\":1000,\"closing_money_raw\":1150,"
        "\"money_delta_raw\":150,\"residual_raw\":0}") + '\n';
    trace_text += StateLine(10, 48, "pop.cashflow.country.component",
        "{\"country_tag\":\"FRA\",\"cash_flow_index\":2,\"component\":\"salary\"}",
        "{\"posted_raw\":200,\"money_delta_raw\":150}") + '\n';
    trace_text += HealthLine(11, "telemetry.family.summary",
        "{\"family\":\"pop.cashflow.aggregate\"}", "{\"dropped\":0,\"invalid\":0}") + '\n';
    trace_text += HealthLine(12, "telemetry.summary", "{}",
        "{\"dropped\":0,\"write_failed\":false}") + '\n';
    Write(input, trace_text);

    std::string error;
    ASSERT_TRUE(trace::ExportPopCashFlowCsv(input, output, "FRA", false, &error)) << error;
    std::ifstream csv(output, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(csv)), {});
    EXPECT_NE(contents.find("\"FRA\",2,10,11,1000,1150,150,0,true"), std::string::npos);
    EXPECT_NE(contents.find("\"FRA\",-1,10,11,1000,1150,150,0,true"), std::string::npos);
    EXPECT_NE(contents.find(",200,150,"), std::string::npos);

    auto invalid = trace_text;
    const auto residual = invalid.find("\"residual_raw\":0");
    ASSERT_NE(residual, std::string::npos);
    invalid.replace(residual, std::string("\"residual_raw\":0").size(), "\"residual_raw\":1");
    const auto invalid_trace = Path(L"pop-cashflow-invalid");
    Write(invalid_trace, invalid);
    EXPECT_FALSE(trace::ExportPopCashFlowCsv(invalid_trace,
        Path(L"pop-cashflow-invalid", L".csv"), "FRA", false, &error));
    EXPECT_NE(error.find("does not reconcile"), std::string::npos);
}

TEST_F(TraceTest, ExportsStrictPopStockAndLifecycle)
{
    const auto input = Path(L"pop-stock-lifecycle");
    const auto output = Path(L"pop-stock-lifecycle", L".csv");
    uint64_t sequence = 0;
    std::string trace_text;
    const auto append = [&](const std::string &line) { trace_text += line + '\n'; };
    const auto capture = [&](const std::string &family, const std::vector<std::string> &fields) {
        append(HealthLine(++sequence, "telemetry.capture.rule", "{\"family\":\"" + family + "\"}",
            "{\"cadence\":\"daily\",\"all_fields\":false,\"country_filter_count\":0,"
            "\"province_filter_count\":0,\"bounded_dates\":false}"));
        for (const auto &field : fields) {
            append(HealthLine(++sequence, "telemetry.capture.field",
                "{\"family\":\"" + family + "\",\"field\":\"" + field + "\"}", "{}"));
        }
    };
    capture("pop.aggregate", {"pop_count", "size_candidate", "employed_candidate"});
    capture("pop.lifecycle", {"summary", "appeared", "disappeared", "scope_changed"});
    append(StateLine(++sequence, 24, "pop.aggregate",
        "{\"country_tag\":\"FRA\",\"province_id_candidate\":1,\"pop_type_id_candidate\":2}",
        "{\"pop_count\":2,\"size_candidate\":100,\"employed_candidate\":80}"));
    append(StateLine(++sequence, 24, "pop.aggregate",
        "{\"country_tag\":\"BEL\",\"province_id_candidate\":2,\"pop_type_id_candidate\":3}",
        "{\"pop_count\":1,\"size_candidate\":50,\"employed_candidate\":40}"));
    append(StateLine(++sequence, 24, "pop.lifecycle.summary", "{}",
        "{\"opening_seen\":false,\"opening_pop_count\":0,\"closing_pop_count\":3,"
        "\"observed_appeared_count\":0,\"observed_disappeared_count\":0,"
        "\"scope_changed_count\":0,\"unchanged_count\":0,\"complete\":false}"));
    append(StateLine(++sequence, 48, "pop.aggregate",
        "{\"country_tag\":\"FRA\",\"province_id_candidate\":1,\"pop_type_id_candidate\":2}",
        "{\"pop_count\":2,\"size_candidate\":110,\"employed_candidate\":85}"));
    append(StateLine(++sequence, 48, "pop.aggregate",
        "{\"country_tag\":\"BEL\",\"province_id_candidate\":2,\"pop_type_id_candidate\":3}",
        "{\"pop_count\":1,\"size_candidate\":45,\"employed_candidate\":38}"));
    append(StateLine(++sequence, 48, "pop.lifecycle.summary", "{}",
        "{\"opening_seen\":true,\"opening_pop_count\":3,\"closing_pop_count\":3,"
        "\"observed_appeared_count\":1,\"observed_disappeared_count\":1,"
        "\"scope_changed_count\":1,\"unchanged_count\":1,\"complete\":true}"));
    append(StateLine(++sequence, 48, "pop.lifecycle.observed_appeared",
        "{\"country_tag_candidate\":\"FRA\",\"province_id_candidate\":1,"
        "\"pop_type_id_candidate\":2,\"pop_id\":11}", "{\"size_candidate\":10}"));
    append(StateLine(++sequence, 48, "pop.lifecycle.observed_disappeared",
        "{\"country_tag_candidate\":\"BEL\",\"province_id_candidate\":2,"
        "\"pop_type_id_candidate\":3,\"pop_id\":9}", "{\"size_candidate\":5}"));
    append(StateLine(++sequence, 48, "pop.lifecycle.scope_changed", "{\"pop_id\":7}",
        "{\"previous_country_tag_candidate\":\"BEL\",\"previous_province_id_candidate\":2,"
        "\"previous_pop_type_id_candidate\":3,\"current_country_tag_candidate\":\"FRA\","
        "\"current_province_id_candidate\":1,\"current_pop_type_id_candidate\":2}"));
    append(HealthLine(++sequence, "telemetry.family.summary", "{\"family\":\"pop.aggregate\"}",
        "{\"polls_due\":2,\"dropped\":0,\"invalid\":0}"));
    append(HealthLine(++sequence, "telemetry.family.summary", "{\"family\":\"pop.lifecycle\"}",
        "{\"polls_due\":2,\"dropped\":0,\"invalid\":0}"));
    append(HealthLine(++sequence, "telemetry.summary", "{}", "{\"dropped\":0,\"write_failed\":false}"));
    Write(input, trace_text);

    std::string error;
    ASSERT_TRUE(trace::ExportPopStockLifecycleCsv(input, output, "FRA", false, &error)) << error;
    std::ifstream csv(output, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(csv)), {});
    EXPECT_NE(contents.find("\"stock\",\"FRA\",1,2,,2,100,80"), std::string::npos);
    EXPECT_EQ(contents.find("\"stock\",\"BEL\""), std::string::npos);
    EXPECT_NE(contents.find("\"lifecycle.summary\""), std::string::npos);
    EXPECT_NE(contents.find("\"lifecycle.appeared\",\"FRA\",1,2,11"), std::string::npos);
    EXPECT_NE(contents.find("\"lifecycle.scope_changed\""), std::string::npos);
    EXPECT_NE(contents.find("\"BEL\",2,3,\"FRA\",1,2"), std::string::npos);

    auto invalid = trace_text;
    const auto complete = invalid.rfind("\"complete\":true");
    ASSERT_NE(complete, std::string::npos);
    invalid.replace(complete, std::string("\"complete\":true").size(), "\"complete\":false");
    const auto invalid_trace = Path(L"pop-stock-lifecycle-invalid");
    const auto invalid_output = Path(L"pop-stock-lifecycle-invalid", L".csv");
    Write(invalid_trace, invalid);
    EXPECT_FALSE(trace::ExportPopStockLifecycleCsv(invalid_trace, invalid_output, {}, false, &error));
    EXPECT_NE(error.find("does not reconcile"), std::string::npos);
    EXPECT_FALSE(fs::exists(invalid_output));

    auto invalid_partition = trace_text;
    const auto unchanged = invalid_partition.find("\"unchanged_count\":1");
    ASSERT_NE(unchanged, std::string::npos);
    invalid_partition.replace(unchanged, std::string("\"unchanged_count\":1").size(),
        "\"unchanged_count\":0");
    const auto invalid_partition_trace = Path(L"pop-stock-lifecycle-invalid-partition");
    Write(invalid_partition_trace, invalid_partition);
    EXPECT_FALSE(trace::ExportPopStockLifecycleCsv(invalid_partition_trace,
        Path(L"pop-stock-lifecycle-invalid-partition", L".csv"), {}, false, &error));
    EXPECT_NE(error.find("does not reconcile"), std::string::npos);

    auto skipped_date = trace_text;
    size_t date_position = 0;
    while ((date_position = skipped_date.find("\"game_date_raw\":48", date_position)) != std::string::npos) {
        skipped_date.replace(date_position, std::string("\"game_date_raw\":48").size(), "\"game_date_raw\":72");
    }
    const auto skipped_trace = Path(L"pop-stock-lifecycle-skipped");
    Write(skipped_trace, skipped_date);
    EXPECT_FALSE(trace::ExportPopStockLifecycleCsv(skipped_trace,
        Path(L"pop-stock-lifecycle-skipped", L".csv"), {}, false, &error));
    EXPECT_NE(error.find("not consecutive"), std::string::npos);

    auto unhealthy = trace_text;
    const auto dropped = unhealthy.rfind("\"dropped\":0");
    ASSERT_NE(dropped, std::string::npos);
    unhealthy.replace(dropped, std::string("\"dropped\":0").size(), "\"dropped\":1");
    const auto unhealthy_trace = Path(L"pop-stock-lifecycle-unhealthy");
    Write(unhealthy_trace, unhealthy);
    EXPECT_FALSE(trace::ExportPopStockLifecycleCsv(unhealthy_trace,
        Path(L"pop-stock-lifecycle-unhealthy", L".csv"), {}, false, &error));
    EXPECT_NE(error.find("unhealthy"), std::string::npos);
}

TEST_F(TraceTest, ExportsStrictNominalRealAndPerCapitaCountryGdp)
{
    const auto input = Path(L"country-gdp");
    const auto output = Path(L"country-gdp", L".csv");
    const std::string factory = "{\"country_tag\":\"FRA\",\"state_id\":1,\"factory_type\":\"test_factory\"}";
    const std::string factory_input =
        "{\"country_tag\":\"FRA\",\"state_id\":1,\"factory_type\":\"test_factory\",\"good_ordinal\":0}";
    const std::string artisan = "{\"country_tag\":\"FRA\",\"province_id\":1,\"pop_id\":10}";
    const std::string artisan_input =
        "{\"country_tag\":\"FRA\",\"province_id\":1,\"pop_id\":10,\"good_ordinal\":0}";
    const std::string ordinary_rgo = "{\"country_tag\":\"FRA\",\"province_id\":1}";
    const std::string gold_rgo = "{\"country_tag\":\"FRA\",\"province_id\":2}";
    const std::string population =
        "{\"country_tag\":\"FRA\",\"province_id_candidate\":1,\"pop_type_id_candidate\":2}";
    uint64_t sequence = 0;
    std::string trace_text;
    auto append = [&](const std::string &line) { trace_text += line + '\n'; };
    const std::vector<std::pair<std::string, std::vector<std::string>>> captures = {
        {"world.market", {"price"}}, {"state.factory", {"production", "flows"}},
        {"province.rgo", {"identity", "production"}},
        {"pop.artisan", {"identity", "production", "inputs"}},
        {"pop.aggregate", {"size_candidate"}},
    };
    for (const auto &[family, fields] : captures) {
        const bool world = family == "world.market";
        append(HealthLine(++sequence, "telemetry.capture.rule",
            "{\"family\":\"" + family + "\"}",
            "{\"cadence\":\"daily\",\"all_fields\":false,\"country_filter_count\":"
                + std::to_string(world ? 0 : 1)
                + ",\"province_filter_count\":0,\"bounded_dates\":false}"));
        for (const auto &field : fields) {
            append(HealthLine(++sequence, "telemetry.capture.field",
                "{\"family\":\"" + family + "\",\"field\":\"" + field + "\"}", "{}"));
        }
        if (!world) {
            append(HealthLine(++sequence, "telemetry.capture.country",
                "{\"family\":\"" + family + "\",\"country_tag\":\"FRA\"}", "{}"));
        }
    }
    for (const int date : {24, 48, 72}) {
        append(StateLine(++sequence, date, "world.market.price", "{\"good_ordinal\":0}",
            "{\"price_raw\":32768}"));
        append(StateLine(++sequence, date, "world.market.price", "{\"good_ordinal\":1}",
            "{\"price_raw\":65536}"));
        append(StateLine(++sequence, date, "state.factory.production", factory,
            "{\"output_raw\":32768,\"output_good_ordinal\":1}"));
        append(StateLine(++sequence, date, "state.factory.input.flow.summary", factory,
            "{\"post_consumption_seen\":true,\"pre_purchase_seen\":true,"
            "\"primary_delivery_seen\":true,\"secondary_delivery_seen\":true,\"settlement_count\":1}"));
        append(StateLine(++sequence, date, "state.factory.input.flow", factory_input,
            "{\"post_consumption_raw\":0,\"pre_purchase_raw\":0,"
            "\"delivered_primary_raw\":32768,\"delivered_secondary_raw\":0}"));
        append(StateLine(++sequence, date, "pop.artisan.identity", artisan,
            "{\"production_type\":\"artisan_test\",\"output_good_ordinal\":1,\"output_good\":\"test_good\"}"));
        append(StateLine(++sequence, date, "pop.artisan.production", artisan,
            "{\"base_output_raw\":32768,\"current_producing_raw\":16384,\"gross_output_raw\":16384}"));
        append(StateLine(++sequence, date, "pop.artisan.input", artisan_input,
            "{\"stockpile_raw\":0,\"need_raw\":32768}"));
        append(StateLine(++sequence, date, "province.rgo.identity", ordinary_rgo,
            "{\"production_type\":\"ordinary_rgo\",\"output_good_ordinal\":1,\"output_good\":\"test_good\"}"));
        append(StateLine(++sequence, date, "province.rgo.production", ordinary_rgo,
            "{\"gross_output_raw\":32768}"));
        append(StateLine(++sequence, date, "province.rgo.identity", gold_rgo,
            "{\"production_type\":\"precious_metal_mine\",\"output_good_ordinal\":17,"
            "\"output_good\":\"precious_metal\"}"));
        append(StateLine(++sequence, date, "province.rgo.production", gold_rgo,
            "{\"gross_output_raw\":65536}"));
        append(StateLine(++sequence, date, "pop.aggregate", population, "{\"size_candidate\":100}"));
    }
    for (const char *family : {"world.market", "state.factory", "province.rgo", "pop.artisan", "pop.aggregate"}) {
        append(HealthLine(++sequence, "telemetry.family.summary",
            std::string("{\"family\":\"") + family + "\"}",
            "{\"dropped\":0,\"invalid\":0,\"polls_due\":3}"));
    }
    append(HealthLine(++sequence, "telemetry.summary", "{}", "{\"dropped\":0,\"write_failed\":false}"));
    Write(input, trace_text);

    std::string error;
    EXPECT_FALSE(trace::ExportCountryGdpCsv(input, Path(L"country-gdp-no-rate", L".csv"),
        "FRA", std::nullopt, std::nullopt, false, &error));
    EXPECT_EQ(error, "precious-metal GDP requires --gold-to-cash-rate for the active mod");

    error.clear();
    ASSERT_TRUE(trace::ExportCountryGdpCsv(input, output, "FRA", 24, 0.5, false, &error)) << error;
    std::ifstream csv(output, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(csv)), {});
    EXPECT_NE(contents.find(",24,0.500000000,\"FRA\",100,2.000000000,1.000000000,1.000000000,"
        "1.000000000,3.000000000,3.000000000,1.000000000,1.000000000,0.500000000,"
        "0.500000000,0.500000000,4.500000000,4.500000000,0.045000000,0.045000000,"
        "\"verified-runtime\""), std::string::npos);

    auto missing_inputs = trace_text;
    const auto input_field = missing_inputs.find("\"field\":\"inputs\"");
    ASSERT_NE(input_field, std::string::npos);
    missing_inputs.replace(input_field, std::string("\"field\":\"inputs\"").size(), "\"field\":\"finance\"");
    const auto missing_input_trace = Path(L"country-gdp-missing-inputs");
    Write(missing_input_trace, missing_inputs);
    EXPECT_FALSE(trace::ExportCountryGdpCsv(missing_input_trace, Path(L"country-gdp-missing-inputs", L".csv"),
        "FRA", 24, 0.5, false, &error));
    EXPECT_EQ(error, "country GDP capture is missing required fields for pop.artisan");

    auto partial_rgo = trace_text;
    const auto rgo_rule = partial_rgo.find("\"family\":\"province.rgo\"");
    ASSERT_NE(rgo_rule, std::string::npos);
    const auto province_count = partial_rgo.find("\"province_filter_count\":0", rgo_rule);
    ASSERT_NE(province_count, std::string::npos);
    partial_rgo.replace(province_count, std::string("\"province_filter_count\":0").size(),
        "\"province_filter_count\":1");
    const auto partial_rgo_trace = Path(L"country-gdp-partial-rgo");
    Write(partial_rgo_trace, partial_rgo);
    EXPECT_FALSE(trace::ExportCountryGdpCsv(partial_rgo_trace, Path(L"country-gdp-partial-rgo", L".csv"),
        "FRA", 24, 0.5, false, &error));
    EXPECT_EQ(error, "country GDP requires complete daily capture scope for province.rgo");
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

TEST_F(TraceTest, SummarizesAndComparesUniqueLifecyclePhaseTimings)
{
    const auto left = Path(L"lifecycle left");
    const auto right = Path(L"lifecycle right");
    Write(left, LifecycleLine(1, 100, "campaign.save_selection_requested", "left")
        + "\n" + LifecycleLine(2, 250, "campaign.save_load_completed", "left")
        + "\n" + LifecycleLine(3, 400, "campaign.entered", "left")
        + "\n" + LifecycleLine(4, 700, "observer.configured", "left"));
    Write(right, LifecycleLine(1, 100, "campaign.save_selection_requested", "right")
        + "\n" + LifecycleLine(2, 300, "campaign.save_load_completed", "right")
        + "\n" + LifecycleLine(3, 550, "campaign.entered", "right")
        + "\n" + LifecycleLine(4, 1050, "observer.configured", "right"));
    trace::Summary left_summary, right_summary;
    std::string error;
    ASSERT_TRUE(trace::Read(left, {}, &left_summary, nullptr, 0, &error)) << error;
    ASSERT_TRUE(trace::Read(right, {}, &right_summary, nullptr, 0, &error)) << error;
    EXPECT_EQ(left_summary.lifecycle.save_load_us, 150u);
    EXPECT_EQ(left_summary.lifecycle.campaign_enter_us, 150u);
    EXPECT_EQ(left_summary.lifecycle.observer_configure_us, 300u);
    EXPECT_NE(trace::FormatSummary(left_summary).find("lifecycle_save_load_us=150"), std::string::npos);
    EXPECT_NE(trace::FormatCompare(left_summary, right_summary).find("lifecycle_observer_configure_us 300 | 500 | 200"), std::string::npos);
}

TEST_F(TraceTest, LeavesAmbiguousLifecyclePhaseTimingsUnavailable)
{
    const auto path = Path(L"ambiguous lifecycle");
    Write(path, LifecycleLine(1, 10, "campaign.save_selection_requested")
        + "\n" + LifecycleLine(2, 20, "campaign.save_selection_requested")
        + "\n" + LifecycleLine(3, 30, "campaign.save_load_completed")
        + "\n" + LifecycleLine(4, 40, "campaign.entered"));
    trace::Summary summary;
    std::string error;
    ASSERT_TRUE(trace::Read(path, {}, &summary, nullptr, 0, &error)) << error;
    EXPECT_FALSE(summary.lifecycle.save_load_us);
    EXPECT_EQ(summary.lifecycle.campaign_enter_us, 10u);
    EXPECT_FALSE(summary.lifecycle.observer_configure_us);
    EXPECT_NE(trace::FormatSummary(summary).find("lifecycle_save_load_us=unavailable"), std::string::npos);
}

TEST_F(TraceTest, IgnoresMalformedLifecycleEndpointsAndPreservesExactLargeDeltas)
{
    const auto malformed = Path(L"malformed lifecycle");
    auto entered = LifecycleLine(3, 30, "campaign.entered");
    entered.replace(entered.find("\"requested_speed\":5"), std::string("\"requested_speed\":5").size(), "\"requested_speed\":9");
    Write(malformed, LifecycleLine(1, 10, "campaign.save_selection_requested")
        + "\n" + LifecycleLine(2, 20, "campaign.save_load_completed") + "\n" + entered);
    trace::Summary summary;
    std::string error;
    ASSERT_TRUE(trace::Read(malformed, {}, &summary, nullptr, 0, &error)) << error;
    EXPECT_EQ(summary.lifecycle.save_load_us, 10u);
    EXPECT_FALSE(summary.lifecycle.campaign_enter_us);

    trace::Summary left, right;
    left.run_id = "left";
    right.run_id = "right";
    left.lifecycle.save_load_us = UINT64_C(9007199254740992);
    right.lifecycle.save_load_us = UINT64_C(9007199254740993);
    EXPECT_NE(trace::FormatCompare(left, right).find("lifecycle_save_load_us 9007199254740992 | 9007199254740993 | 1"), std::string::npos);
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
