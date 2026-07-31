#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace smedley::trace
{
    namespace fs = std::filesystem;

    enum class JsonKind { Null, Boolean, Number, String, Array, Object };
    struct Scalar { JsonKind kind = JsonKind::Null; std::string text; };
    struct Filter { std::string event; std::string category; std::string country; };
    struct Record {
        std::string raw, run_id, wall_time_utc, event, category, quality;
        uint64_t sequence = 0, monotonic_us = 0;
        std::optional<int> game_date_raw;
        std::map<std::string, Scalar> entities, payload;
    };
    struct Summary {
        uint64_t records = 0, gaps = 0;
        bool date_regressed = false;
        std::map<std::string, uint64_t> events, categories, qualities;
        std::optional<uint64_t> first_sequence, last_sequence, first_monotonic_us, last_monotonic_us;
        std::optional<uint64_t> first_date_monotonic_us, last_date_monotonic_us;
        std::optional<int> first_date, last_date;
        std::map<std::string, Scalar> progress;
        std::string run_id, warning;
    };

    using Visitor = std::function<bool(const Record &, std::string *)>;
    bool Stream(const fs::path &path, Summary *summary, const Visitor &visitor, std::string *error);
    bool Read(const fs::path &path, const Filter &filter, Summary *summary, std::vector<Record> *selected,
              size_t limit, std::string *error);
    bool ExportTrace(const fs::path &input, const fs::path &output, const Filter &filter, bool overwrite,
                     std::string *error, std::string *warning = nullptr);
    bool ExportCountryCsv(const fs::path &input, const fs::path &output, bool overwrite,
                          std::string *error, std::string *warning = nullptr);
    std::string FormatSummary(const Summary &summary);
    std::string FormatCompare(const Summary &left, const Summary &right);
}
