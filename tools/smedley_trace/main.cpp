#include "trace.hpp"

#include <windows.h>

#include <algorithm>
#include <iostream>
#include <limits>

namespace
{
    using smedley::trace::Filter;

    void Usage()
    {
        std::cout << "Usage:\n"
                  << "  smedley_trace validate TRACE\n"
                  << "  smedley_trace summary TRACE [--wait]\n"
                  << "  smedley_trace inspect TRACE [--event TYPE] [--category lifecycle|state] [--country TAG] [--limit N]\n"
                  << "  smedley_trace compare LEFT RIGHT\n"
                  << "  smedley_trace assert-benchmark TRACE (--completed [--days N] | --failed REASON)\n"
                  << "  smedley_trace export-csv TRACE OUTPUT [--overwrite] --event country.daily\n"
                  << "  smedley_trace export-trace TRACE OUTPUT [--event TYPE] [--category lifecycle|state] [--country TAG] [--overwrite]\n";
    }

    bool Utf8(const wchar_t *value, std::string *result)
    {
        const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
        if (length <= 1) return false;
        result->assign(static_cast<size_t>(length), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result->data(), length, nullptr, nullptr) != length) return false;
        result->pop_back();
        return true;
    }

    bool IsTag(const std::string &value)
    {
        return value.size() == 3 && std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
        });
    }

    bool IsIdentifier(const std::string &value)
    {
        return !value.empty() && value.size() <= 128 && std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
                || (character >= '0' && character <= '9') || character == '_' || character == '-' || character == '.';
        });
    }

    bool ParseFilters(int argc, wchar_t **argv, int *index, Filter *filter, size_t *limit, bool *overwrite, bool allow_limit)
    {
        bool event = false, category = false, country = false, limit_seen = false, overwrite_seen = false;
        while (*index < argc) {
            const std::wstring option = argv[(*index)++];
            if (option == L"--overwrite") {
                if (overwrite_seen) return false;
                overwrite_seen = true;
                *overwrite = true;
                continue;
            }
            if (*index == argc) return false;
            std::string value;
            if (!Utf8(argv[(*index)++], &value)) return false;
            if (option == L"--event" && !event && IsIdentifier(value)) { filter->event = value; event = true; }
            else if (option == L"--category" && !category && (value == "lifecycle" || value == "state")) { filter->category = value; category = true; }
            else if (option == L"--country" && !country && IsTag(value)) { filter->country = value; country = true; }
            else if (option == L"--limit") {
                if (!allow_limit || limit_seen) return false;
                size_t used = 0;
                try {
                    const auto parsed = std::stoull(value, &used);
                    if (used != value.size() || parsed == 0 || parsed > (std::numeric_limits<size_t>::max)()) return false;
                    *limit = static_cast<size_t>(parsed);
                    limit_seen = true;
                } catch (...) { return false; }
            } else return false;
        }
        return true;
    }

    void WaitForKey()
    {
        std::cout << "Press Enter to close." << std::endl;
        std::cin.get();
    }

    std::string Display(const smedley::trace::Scalar &value)
    {
        if (value.kind == smedley::trace::JsonKind::Null) return "null";
        if (value.kind == smedley::trace::JsonKind::Array) return "<array>";
        if (value.kind == smedley::trace::JsonKind::Object) return "<object>";
        if (value.kind != smedley::trace::JsonKind::String) return value.text;
        std::string escaped = "\"";
        constexpr char hex[] = "0123456789abcdef";
        for (const unsigned char character : value.text) {
            if (character == '\\' || character == '\"') {
                escaped += '\\';
                escaped += static_cast<char>(character);
            } else if (character == '\b') escaped += "\\b";
            else if (character == '\f') escaped += "\\f";
            else if (character == '\n') escaped += "\\n";
            else if (character == '\r') escaped += "\\r";
            else if (character == '\t') escaped += "\\t";
            else if (character < 0x20 || character == 0x7f) {
                escaped += "\\u00";
                escaped += hex[character >> 4];
                escaped += hex[character & 0x0f];
            } else escaped += static_cast<char>(character);
        }
        return escaped + "\"";
    }
}

int wmain(int argc, wchar_t **argv)
{
    using namespace smedley::trace;
    if (argc == 1 || (argc == 2 && (std::wstring(argv[1]) == L"help" || std::wstring(argv[1]) == L"--help"))) { Usage(); return 0; }
    if (argc < 3) { Usage(); return 2; }
    const std::wstring command = argv[1];
    std::string error;

    if (command == L"validate" || command == L"summary") {
        bool wait = false;
        if (command == L"summary" && argc == 4 && std::wstring(argv[3]) == L"--wait") wait = true;
        else if (argc != 3) { std::cerr << "smedley_trace: invalid arguments\n"; return 2; }
        Summary summary;
        if (!Read(argv[2], {}, &summary, nullptr, 0, &error)) { std::cerr << "smedley_trace: " << error << '\n'; return 1; }
        std::cout << (command == L"validate" ? "valid" : FormatSummary(summary)) << '\n';
        if (!summary.warning.empty()) std::cerr << "warning: " << summary.warning << '\n';
        if (wait) WaitForKey();
        return 0;
    }

    if (command == L"compare" && argc == 4) {
        Summary left, right;
        if (!Read(argv[2], {}, &left, nullptr, 0, &error) || !Read(argv[3], {}, &right, nullptr, 0, &error)) { std::cerr << "smedley_trace: " << error << '\n'; return 1; }
        std::cout << FormatCompare(left, right);
        return 0;
    }

    if (command == L"assert-benchmark" && argc >= 4) {
        BenchmarkExpectation expectation;
        bool status = false, days = false;
        for (int index = 3; index < argc; ++index) {
            const std::wstring option = argv[index];
            if (option == L"--completed" && !status) {
                expectation.status = BenchmarkStatus::Completed;
                status = true;
            } else if (option == L"--failed" && !status && ++index < argc) {
                expectation.status = BenchmarkStatus::Failed;
                if (!Utf8(argv[index], &expectation.reason) || !IsBenchmarkFailureReason(expectation.reason)) {
                    std::cerr << "smedley_trace: invalid benchmark failure reason\n";
                    return 2;
                }
                status = true;
            } else if (option == L"--days" && !days && ++index < argc) {
                const std::wstring text = argv[index];
                size_t used = 0;
                try {
                    const auto parsed = std::stoll(text, &used);
                    if (used != text.size() || parsed < 1 || parsed > 1000000) throw std::out_of_range("days");
                    expectation.game_days = static_cast<int>(parsed);
                } catch (...) {
                    std::cerr << "smedley_trace: benchmark days must be from 1 through 1000000\n";
                    return 2;
                }
                days = true;
            } else {
                std::cerr << "smedley_trace: invalid benchmark assertion arguments\n";
                return 2;
            }
        }
        if (!status || (expectation.status == BenchmarkStatus::Failed && days)) {
            std::cerr << "smedley_trace: invalid benchmark assertion arguments\n";
            return 2;
        }
        Summary summary;
        if (!Read(argv[2], {}, &summary, nullptr, 0, &error) || !VerifyBenchmark(summary, expectation, &error)) {
            std::cerr << "smedley_trace: " << error << '\n';
            return 1;
        }
        std::cout << FormatBenchmarkVerification(summary) << '\n';
        return 0;
    }

    if (command == L"inspect") {
        Filter filter;
        size_t limit = 20;
        bool overwrite = false;
        int index = 3;
        if (!ParseFilters(argc, argv, &index, &filter, &limit, &overwrite, true) || overwrite) { std::cerr << "smedley_trace: invalid arguments\n"; return 2; }
        Summary summary;
        std::vector<Record> records;
        if (!Read(argv[2], filter, &summary, &records, limit, &error)) { std::cerr << "smedley_trace: " << error << '\n'; return 1; }
        for (const Record &record : records) {
            std::cout << "run_id=" << record.run_id << " sequence=" << record.sequence << " wall_time_utc=" << record.wall_time_utc
                      << " monotonic_us=" << record.monotonic_us << " game_date_raw=" << (record.game_date_raw ? std::to_string(*record.game_date_raw) : "null")
                      << " event=" << record.event << " category=" << record.category << " quality=" << record.quality;
            for (const auto &[key, value] : record.entities) std::cout << " entity." << key << '=' << Display(value);
            for (const auto &[key, value] : record.payload) std::cout << " payload." << key << '=' << Display(value);
            std::cout << '\n';
        }
        return 0;
    }

    if (command == L"export-trace" && argc >= 4) {
        Filter filter;
        size_t ignored_limit = 0;
        bool overwrite = false;
        int index = 4;
        if (!ParseFilters(argc, argv, &index, &filter, &ignored_limit, &overwrite, false) || ignored_limit != 0) { std::cerr << "smedley_trace: invalid arguments\n"; return 2; }
        std::string warning;
        if (!ExportTrace(argv[2], argv[3], filter, overwrite, &error, &warning)) { std::cerr << "smedley_trace: " << error << '\n'; return 1; }
        if (!warning.empty()) std::cerr << "warning: " << warning << '\n';
        return 0;
    }

    if (command == L"export-csv" && argc >= 5) {
        bool overwrite = false;
        bool event = false;
        for (int index = 4; index < argc; ++index) {
            const std::wstring option = argv[index];
            if (option == L"--overwrite" && !overwrite) overwrite = true;
            else if (option == L"--event" && !event && ++index < argc && std::wstring(argv[index]) == L"country.daily") event = true;
            else { std::cerr << "smedley_trace: export-csv supports only --event country.daily [--overwrite]\n"; return 2; }
        }
        if (!event) { std::cerr << "smedley_trace: export-csv requires --event country.daily\n"; return 2; }
        std::string warning;
        if (!ExportCountryCsv(argv[2], argv[3], overwrite, &error, &warning)) { std::cerr << "smedley_trace: " << error << '\n'; return 1; }
        if (!warning.empty()) std::cerr << "warning: " << warning << '\n';
        return 0;
    }

    std::cerr << "smedley_trace: invalid arguments\n";
    return 2;
}
