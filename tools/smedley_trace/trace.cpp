#include "trace.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>

namespace smedley::trace
{
    namespace
    {
        struct Json {
            JsonKind kind = JsonKind::Null;
            std::string text;
            std::map<std::string, Json> object;
            std::vector<Json> array;
        };

        constexpr size_t max_line_bytes = 1024 * 1024;
        constexpr int max_json_nesting = 64;
        constexpr double raw_date_units_per_day = 24.0;

        class Parser
        {
        public:
            explicit Parser(const std::string &input) : input_(input) {}

            bool Parse(Json *value, std::string *error)
            {
                Skip();
                if (!Value(value) || (Skip(), position_ != input_.size())) {
                    *error = unexpected_eof_ ? "unexpected EOF" : error_.empty() ? "invalid JSON" : error_;
                    return false;
                }
                return true;
            }

        private:
            void Skip()
            {
                while (position_ < input_.size() && std::strchr(" \t\r\n", input_[position_])) ++position_;
            }

            bool Take(char expected)
            {
                Skip();
                if (position_ == input_.size()) { unexpected_eof_ = true; return false; }
                if (input_[position_] != expected) return false;
                ++position_;
                return true;
            }

            bool AppendCodepoint(uint32_t codepoint, std::string *output)
            {
                if (codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
                if (codepoint < 0x80) output->push_back(static_cast<char>(codepoint));
                else if (codepoint < 0x800) {
                    output->push_back(static_cast<char>(0xc0 | codepoint >> 6));
                    output->push_back(static_cast<char>(0x80 | codepoint & 0x3f));
                } else if (codepoint < 0x10000) {
                    output->push_back(static_cast<char>(0xe0 | codepoint >> 12));
                    output->push_back(static_cast<char>(0x80 | codepoint >> 6 & 0x3f));
                    output->push_back(static_cast<char>(0x80 | codepoint & 0x3f));
                } else {
                    output->push_back(static_cast<char>(0xf0 | codepoint >> 18));
                    output->push_back(static_cast<char>(0x80 | codepoint >> 12 & 0x3f));
                    output->push_back(static_cast<char>(0x80 | codepoint >> 6 & 0x3f));
                    output->push_back(static_cast<char>(0x80 | codepoint & 0x3f));
                }
                return true;
            }

            bool Hex(char character, uint32_t *value)
            {
                *value <<= 4;
                if (character >= '0' && character <= '9') *value += character - '0';
                else if (character >= 'a' && character <= 'f') *value += character - 'a' + 10;
                else if (character >= 'A' && character <= 'F') *value += character - 'A' + 10;
                else return false;
                return true;
            }

            bool String(std::string *output)
            {
                if (!Take('"')) return false;
                while (position_ < input_.size()) {
                    const unsigned char character = input_[position_++];
                    if (character == '"') return true;
                    if (character < 0x20) { error_ = "unescaped control in JSON string"; return false; }
                    if (character != '\\') {
                        if (character < 0x80) output->push_back(static_cast<char>(character));
                        else if (!Utf8(character, output)) return false;
                        continue;
                    }
                    if (position_ == input_.size()) break;
                    const char escaped = input_[position_++];
                    if (escaped == '"' || escaped == '\\' || escaped == '/') output->push_back(escaped);
                    else if (escaped == 'b') output->push_back('\b');
                    else if (escaped == 'f') output->push_back('\f');
                    else if (escaped == 'n') output->push_back('\n');
                    else if (escaped == 'r') output->push_back('\r');
                    else if (escaped == 't') output->push_back('\t');
                    else if (escaped == 'u' && UnicodeEscape(output)) {}
                    else { error_ = "invalid JSON escape"; return false; }
                }
                unexpected_eof_ = true;
                return false;
            }

            bool Utf8(unsigned char first, std::string *output)
            {
                const size_t bytes = first >= 0xc2 && first <= 0xdf ? 2 : first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
                if (!bytes) { error_ = "invalid UTF-8"; return false; }
                if (position_ + bytes - 1 > input_.size()) { unexpected_eof_ = true; return false; }
                const unsigned char second = static_cast<unsigned char>(input_[position_]);
                if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0)
                    || (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90)) { error_ = "invalid UTF-8"; return false; }
                output->push_back(static_cast<char>(first));
                for (size_t index = 1; index < bytes; ++index) {
                    const unsigned char next = input_[position_++];
                    if ((next & 0xc0) != 0x80) { error_ = "invalid UTF-8"; return false; }
                    output->push_back(static_cast<char>(next));
                }
                return true;
            }

            bool UnicodeEscape(std::string *output)
            {
                if (position_ + 4 > input_.size()) { unexpected_eof_ = true; return false; }
                uint32_t codepoint = 0;
                for (int index = 0; index < 4; ++index) if (!Hex(input_[position_++], &codepoint)) return false;
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (position_ + 6 > input_.size()) { unexpected_eof_ = true; return false; }
                    if (input_[position_++] != '\\' || input_[position_++] != 'u') return false;
                    uint32_t low = 0;
                    for (int index = 0; index < 4; ++index) if (!Hex(input_[position_++], &low)) return false;
                    if (low < 0xdc00 || low > 0xdfff) return false;
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + low - 0xdc00;
                }
                return AppendCodepoint(codepoint, output);
            }

            bool Value(Json *value)
            {
                Skip();
                if (position_ == input_.size()) { unexpected_eof_ = true; return false; }
                if (input_[position_] == '"') { value->kind = JsonKind::String; return String(&value->text); }
                if (input_[position_] == '{') return Object(value, depth_ + 1);
                if (input_[position_] == '[') return Array(value, depth_ + 1);
                if (Literal("true", JsonKind::Boolean, value)) return true;
                if (Literal("false", JsonKind::Boolean, value)) return true;
                if (Literal("null", JsonKind::Null, value)) return true;
                return Number(value);
            }

            bool Literal(std::string_view literal, JsonKind kind, Json *value)
            {
                const size_t remaining = input_.size() - position_;
                if (remaining >= literal.size()) {
                    if (input_.compare(position_, literal.size(), literal) != 0) return false;
                    value->kind = kind;
                    if (kind != JsonKind::Null) value->text.assign(literal);
                    position_ += literal.size();
                    return true;
                }
                if (literal.compare(0, remaining, input_, position_, remaining) == 0) unexpected_eof_ = true;
                return false;
            }

            bool Object(Json *value, int depth)
            {
                if (depth > max_json_nesting) { error_ = "JSON nesting exceeds 64"; return false; }
                const int previous_depth = depth_;
                depth_ = depth;
                value->kind = JsonKind::Object;
                ++position_;
                if (Take('}')) { depth_ = previous_depth; return true; }
                for (;;) {
                    std::string key;
                    Json child;
                    if (!String(&key) || !Take(':') || !Value(&child)) { depth_ = previous_depth; return false; }
                    if (!value->object.emplace(std::move(key), std::move(child)).second) { depth_ = previous_depth; error_ = "duplicate object key"; return false; }
                    if (Take('}')) { depth_ = previous_depth; return true; }
                    if (!Take(',')) { depth_ = previous_depth; return false; }
                }
            }

            bool Array(Json *value, int depth)
            {
                if (depth > max_json_nesting) { error_ = "JSON nesting exceeds 64"; return false; }
                const int previous_depth = depth_;
                depth_ = depth;
                value->kind = JsonKind::Array;
                ++position_;
                if (Take(']')) { depth_ = previous_depth; return true; }
                for (;;) {
                    Json child;
                    if (!Value(&child)) { depth_ = previous_depth; return false; }
                    value->array.push_back(std::move(child));
                    if (Take(']')) { depth_ = previous_depth; return true; }
                    if (!Take(',')) { depth_ = previous_depth; return false; }
                }
            }

            bool Number(Json *value)
            {
                const size_t begin = position_;
                if (input_[position_] == '-') ++position_;
                if (position_ == input_.size()) { unexpected_eof_ = true; return false; }
                if (input_[position_] == '0') ++position_;
                else {
                    if (input_[position_] < '1' || input_[position_] > '9') return false;
                    while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
                }
                if (position_ < input_.size() && input_[position_] == '.') {
                    const size_t fraction = ++position_;
                    while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
                    if (fraction == position_) { unexpected_eof_ = position_ == input_.size(); return false; }
                }
                if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
                    ++position_;
                    if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
                    const size_t exponent = position_;
                    while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
                    if (exponent == position_) { unexpected_eof_ = position_ == input_.size(); return false; }
                }
                value->kind = JsonKind::Number;
                value->text = input_.substr(begin, position_ - begin);
                return true;
            }

            const std::string &input_;
            size_t position_ = 0;
            int depth_ = 0;
            bool unexpected_eof_ = false;
            std::string error_;
        };

        const Json *Member(const Json &object, const char *name)
        {
            const auto found = object.object.find(name);
            return found == object.object.end() ? nullptr : &found->second;
        }

        bool ParseInteger(const Json *value, int64_t *result)
        {
            return value && value->kind == JsonKind::Number && value->text.find_first_of(".eE") == std::string::npos
                && std::from_chars(value->text.data(), value->text.data() + value->text.size(), *result).ec == std::errc{};
        }

        bool ParseUnsigned(const Json *value, uint64_t *result)
        {
            return value && value->kind == JsonKind::Number && value->text.find_first_of("-.eE") == std::string::npos
                && std::from_chars(value->text.data(), value->text.data() + value->text.size(), *result).ec == std::errc{};
        }

        bool ParseIntegerText(const std::string &value, int64_t *result)
        {
            return value.find_first_of(".eE") == std::string::npos
                && std::from_chars(value.data(), value.data() + value.size(), *result).ec == std::errc{};
        }

        bool IsSafeRunId(const std::string &value)
        {
            return !value.empty() && value.size() <= 64 && std::all_of(value.begin(), value.end(), [](unsigned char character) {
                return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
                    || (character >= '0' && character <= '9') || character == '-';
            });
        }

        bool IsIdentifier(const std::string &value)
        {
            return !value.empty() && value.size() <= 128 && std::all_of(value.begin(), value.end(), [](unsigned char character) {
                return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
                    || (character >= '0' && character <= '9') || character == '_' || character == '-' || character == '.';
            });
        }

        bool IsTimestamp(const std::string &value)
        {
            if (value.size() != 24 || value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' || value[16] != ':' || value[19] != '.' || value[23] != 'Z') return false;
            for (size_t index = 0; index < value.size(); ++index) if (index != 4 && index != 7 && index != 10 && index != 13 && index != 16 && index != 19 && index != 23 && (value[index] < '0' || value[index] > '9')) return false;
            const auto number = [&](size_t offset, size_t length) { return std::stoi(value.substr(offset, length)); };
            const int year = number(0, 4), month = number(5, 2), day = number(8, 2), hour = number(11, 2), minute = number(14, 2), second = number(17, 2);
            const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
            const int month_days[] = {31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            return year >= 1 && month >= 1 && month <= 12 && day >= 1 && day <= month_days[month - 1] && hour <= 23 && minute <= 59 && second <= 59;
        }

        bool IsTag(const std::string &value)
        {
            return value.size() == 3 && std::all_of(value.begin(), value.end(), [](unsigned char character) {
                return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
            });
        }

        bool IsQuality(const Json *value)
        {
            return value && value->kind == JsonKind::String && (value->text == "verified-runtime" || value->text == "verified-current"
                || value->text == "verified-static-callsites" || value->text == "provisional" || value->text == "historical-unverified" || value->text == "historical-skeleton");
        }

        bool Scalars(const Json &object, std::map<std::string, Scalar> *destination)
        {
            for (const auto &[key, value] : object.object) {
                if (!IsIdentifier(key)) return false;
                (*destination)[key] = {value.kind, value.text};
            }
            return true;
        }

        bool Decode(const std::string &line, Record *record, std::string *error)
        {
            Json root;
            if (!Parser(line).Parse(&root, error) || root.kind != JsonKind::Object) { if (error->empty()) *error = "record must be an object"; return false; }
            const Json *schema = Member(root, "schema"), *version = Member(root, "schema_version"), *run = Member(root, "run_id"), *sequence = Member(root, "sequence"), *wall = Member(root, "wall_time_utc"), *monotonic = Member(root, "monotonic_us"), *date = Member(root, "game_date_raw"), *event = Member(root, "event_type"), *category = Member(root, "category"), *mapping = Member(root, "mapping_id"), *quality = Member(root, "quality"), *entities = Member(root, "entities"), *payload = Member(root, "payload");
            int64_t schema_version = 0, raw_date = 0;
            uint64_t parsed_sequence = 0, parsed_monotonic = 0;
            if (!schema || schema->kind != JsonKind::String || schema->text != "smedley.telemetry" || !ParseInteger(version, &schema_version) || schema_version != 1
                || !run || run->kind != JsonKind::String || !IsSafeRunId(run->text) || !ParseUnsigned(sequence, &parsed_sequence) || parsed_sequence == 0
                || !wall || wall->kind != JsonKind::String || !IsTimestamp(wall->text) || !ParseUnsigned(monotonic, &parsed_monotonic)
                || !date || (date->kind != JsonKind::Null && (!ParseInteger(date, &raw_date) || raw_date < (std::numeric_limits<int>::min)() || raw_date > (std::numeric_limits<int>::max)()))
                || !event || event->kind != JsonKind::String || !IsIdentifier(event->text) || !category || category->kind != JsonKind::String || (category->text != "lifecycle" && category->text != "state")
                || !mapping || mapping->kind != JsonKind::String || !IsIdentifier(mapping->text) || !IsQuality(quality) || !entities || entities->kind != JsonKind::Object || !payload || payload->kind != JsonKind::Object) { *error = "invalid telemetry envelope v1"; return false; }
            record->raw = line;
            record->run_id = run->text;
            record->sequence = parsed_sequence;
            record->wall_time_utc = wall->text;
            record->monotonic_us = parsed_monotonic;
            if (date->kind != JsonKind::Null) record->game_date_raw = static_cast<int>(raw_date);
            record->event = event->text;
            record->category = category->text;
            record->mapping_id = mapping->text;
            record->quality = quality->text;
            if (!Scalars(*entities, &record->entities) || !Scalars(*payload, &record->payload)) {
                *error = "entity and payload keys must be stable identifiers";
                return false;
            }
            return true;
        }

        const Scalar *Field(const Record &record, const char *key)
        {
            const auto found = record.payload.find(key);
            return found == record.payload.end() ? nullptr : &found->second;
        }

        bool IntegerField(const Record &record, const char *key, int64_t *value)
        {
            const auto *field = Field(record, key);
            return field && field->kind == JsonKind::Number && ParseIntegerText(field->text, value);
        }

        bool BooleanField(const Record &record, const char *key, bool *value)
        {
            const auto *field = Field(record, key);
            if (field == nullptr || field->kind != JsonKind::Boolean) return false;
            *value = field->text == "true";
            return field->text == "true" || field->text == "false";
        }

        const Scalar *Entity(const Record &record, const char *key)
        {
            const auto found = record.entities.find(key);
            return found == record.entities.end() ? nullptr : &found->second;
        }

        bool IntegerEntity(const Record &record, const char *key, int64_t *value)
        {
            const auto *field = Entity(record, key);
            return field && field->kind == JsonKind::Number && ParseIntegerText(field->text, value);
        }

        bool HasOnlyFields(const Record &record, std::initializer_list<const char *> allowed)
        {
            return std::all_of(record.payload.begin(), record.payload.end(), [&](const auto &entry) {
                return std::find(allowed.begin(), allowed.end(), entry.first) != allowed.end();
            });
        }

        bool BenchmarkSchema(const Record &record, std::string *error)
        {
            const bool resources = record.event == "benchmark.resources";
            if (record.event != "benchmark.started" && record.event != "benchmark.completed"
                && record.event != "benchmark.failed" && !resources) return true;
            if (record.category != "lifecycle" || record.quality != (resources ? "verified-current" : "provisional")) {
                *error = "benchmark record has an unsupported envelope";
                return false;
            }
            if (resources) {
                if (!record.entities.empty() || record.payload.empty() || record.payload.size() > 6
                    || !HasOnlyFields(record, {"process_cpu_us", "working_set_start_bytes", "working_set_end_bytes",
                                              "private_bytes_start", "private_bytes_end", "process_peak_working_set_bytes"})) {
                    *error = "benchmark.resources has an unsupported schema";
                    return false;
                }
                for (const auto &[key, field] : record.payload) {
                    int64_t value = 0;
                    if (field.kind != JsonKind::Number || !ParseIntegerText(field.text, &value)
                        || value < 0) {
                        *error = "benchmark.resources contains an invalid metric";
                        return false;
                    }
                }
                int64_t start = 0, end = 0, peak = 0;
                const bool has_start = IntegerField(record, "working_set_start_bytes", &start);
                const bool has_end = IntegerField(record, "working_set_end_bytes", &end);
                const bool has_peak = IntegerField(record, "process_peak_working_set_bytes", &peak);
                if (has_peak && ((has_start && peak < start) || (has_end && peak < end))) {
                    *error = "benchmark.resources peak working set is inconsistent";
                    return false;
                }
                return true;
            }
            int64_t start = 0, target = 0, elapsed = 0;
            if (!IntegerField(record, "start_date_raw", &start) || !IntegerField(record, "target_date_raw", &target)
                || start < (std::numeric_limits<int>::min)() || start > (std::numeric_limits<int>::max)()
                || target < (std::numeric_limits<int>::min)() || target > (std::numeric_limits<int>::max)()) { *error = "benchmark record has invalid date fields"; return false; }
            if (record.event == "benchmark.started") {
                int64_t days = 0, timeout = 0;
                if (record.payload.size() != 4 || !record.game_date_raw || *record.game_date_raw != start
                    || !IntegerField(record, "requested_days", &days) || !IntegerField(record, "timeout_seconds", &timeout)
                    || target <= start || (target - start) % 24 != 0 || days != (target - start) / 24
                    || days < 1 || timeout < 1 || timeout > 86400) { *error = "benchmark.started has an unsupported schema"; return false; }
                return true;
            }
            const auto *reason = Field(record, "reason");
            const bool invalid_target = record.event == "benchmark.failed" && reason != nullptr
                && reason->kind == JsonKind::String && reason->text == "invalid_target";
            if (!invalid_target && (target <= start || (target - start) % 24 != 0)) { *error = "benchmark record has invalid date fields"; return false; }
            if (!IntegerField(record, "elapsed_us", &elapsed) || elapsed <= 0) { *error = "benchmark terminal elapsed_us is invalid"; return false; }
            if (record.event == "benchmark.completed") {
                int64_t actual = 0, days = 0, overshoot = 0;
                const auto *paused = Field(record, "paused");
                if (record.payload.size() != 7 || !record.game_date_raw || !IntegerField(record, "actual_date_raw", &actual)
                    || !IntegerField(record, "game_days", &days) || !IntegerField(record, "overshoot_raw", &overshoot)
                    || !paused || paused->kind != JsonKind::Boolean || paused->text != "true" || actual != target
                    || *record.game_date_raw != actual || days != (target - start) / 24 || overshoot != 0) { *error = "benchmark.completed has an unsupported schema"; return false; }
                return true;
            }
            if (record.payload.size() < 4 || record.payload.size() > 6 || !reason || reason->kind != JsonKind::String
                || !IsBenchmarkFailureReason(reason->text)
                || !HasOnlyFields(record, {"start_date_raw", "target_date_raw", "actual_date_raw", "elapsed_us", "reason", "paused"})) { *error = "benchmark.failed has an unsupported schema"; return false; }
            const auto *actual = Field(record, "actual_date_raw");
            const auto *paused = Field(record, "paused");
            int64_t ignored = 0;
            if ((actual != nullptr) != record.game_date_raw.has_value()
                || (actual && (actual->kind != JsonKind::Number || !ParseIntegerText(actual->text, &ignored)
                            || ignored < (std::numeric_limits<int>::min)() || ignored > (std::numeric_limits<int>::max)()
                            || !record.game_date_raw || *record.game_date_raw != ignored))
                || (paused && paused->kind != JsonKind::Boolean)) { *error = "benchmark.failed fields are inconsistent"; return false; }
            const bool paused_true = paused != nullptr && paused->text == "true";
            if ((invalid_target && target > start && (target - start) % 24 == 0)
                || (reason->text == "date_overshoot" && (!actual || ignored <= target || !paused_true))
                || (reason->text == "date_regressed" && (!actual || !paused_true))
                || (reason->text == "timeout" && (!actual || !paused_true))
                || (reason->text == "idler_unavailable" && (actual || paused))
                || (reason->text == "invalid_pause_state" && (!actual || !paused_true))
                || (reason->text == "pause_failed" && (!actual || paused_true))
                || (reason->text == "observer_invariant_failed" && (!actual || !paused_true))
                || (reason->text == "unexpected_pause" && (!actual || !paused_true))
                || (reason->text == "timer_unavailable" && !actual)
                || (reason->text == "invalid_target" && !actual)) {
                *error = "benchmark.failed reason is inconsistent with its fields";
                return false;
            }
            return true;
        }

        struct BenchmarkTraceState
        {
            bool started = false;
            bool resources = false;
            bool terminal = false;
            int start_date_raw = 0;
            int target_date_raw = 0;
            int timeout_seconds = 0;
            std::optional<int> resources_date_raw;
        };

        struct LifecycleMilestone
        {
            uint64_t sequence = 0;
            uint64_t monotonic_us = 0;
            bool duplicate = false;
        };

        struct LifecycleTraceState
        {
            LifecycleMilestone save_selection, save_load, campaign_enter, observer_configure;
        };

        bool IsLifecycleEndpoint(const Record &record)
        {
            if (record.category != "lifecycle" || record.mapping_id != "v2game-3.04"
                || record.quality != "verified-runtime" || record.game_date_raw) return false;
            if (record.event == "campaign.save_selection_requested") {
                const auto *source = Field(record, "source");
                return record.entities.empty() && record.payload.size() == 1 && source
                    && source->kind == JsonKind::String && source->text == "campaign_runner";
            }
            if (record.event == "campaign.save_load_completed") {
                return record.entities.empty() && record.payload.empty();
            }
            if (record.event == "campaign.entered") {
                const auto *observer = Field(record, "observer_requested");
                const auto *paused = Field(record, "requested_paused");
                int64_t speed = 0;
                return record.entities.empty() && record.payload.size() == 3
                    && HasOnlyFields(record, {"observer_requested", "requested_speed", "requested_paused"})
                    && observer && observer->kind == JsonKind::Boolean
                    && IntegerField(record, "requested_speed", &speed) && speed >= 1 && speed <= 5
                    && paused && paused->kind == JsonKind::Boolean;
            }
            if (record.event == "observer.configured") {
                const auto country = record.entities.find("viewing_country");
                const auto *ai = Field(record, "full_ai_control");
                const auto *visibility = Field(record, "full_map_visibility");
                return record.entities.size() == 1 && country != record.entities.end()
                    && country->second.kind == JsonKind::String && IsTag(country->second.text)
                    && record.payload.size() == 2 && HasOnlyFields(record, {"full_ai_control", "full_map_visibility"})
                    && ai && ai->kind == JsonKind::Boolean && ai->text == "true"
                    && visibility && visibility->kind == JsonKind::Boolean && visibility->text == "true";
            }
            return false;
        }

        void CaptureLifecycle(const Record &record, LifecycleTraceState *state)
        {
            LifecycleMilestone *milestone = nullptr;
            if (record.event == "campaign.save_selection_requested") milestone = &state->save_selection;
            else if (record.event == "campaign.save_load_completed") milestone = &state->save_load;
            else if (record.event == "campaign.entered") milestone = &state->campaign_enter;
            else if (record.event == "observer.configured") milestone = &state->observer_configure;
            if (milestone == nullptr) return;
            if (!IsLifecycleEndpoint(record) || milestone->sequence != 0) {
                milestone->duplicate = true;
                return;
            }
            milestone->sequence = record.sequence;
            milestone->monotonic_us = record.monotonic_us;
        }

        std::optional<uint64_t> LifecycleDuration(const LifecycleMilestone &start, const LifecycleMilestone &end)
        {
            if (start.sequence == 0 || end.sequence == 0 || start.duplicate || end.duplicate
                || start.sequence >= end.sequence || start.monotonic_us > end.monotonic_us) return std::nullopt;
            return end.monotonic_us - start.monotonic_us;
        }

        bool BenchmarkTransition(const Record &record, BenchmarkTraceState *state, std::string *error)
        {
            if (record.event != "benchmark.started" && record.event != "benchmark.completed"
                && record.event != "benchmark.failed" && record.event != "benchmark.resources") return true;
            if (record.event == "benchmark.resources") {
                if (!state->started || state->resources || state->terminal) {
                    *error = "benchmark.resources has no active benchmark or is duplicated";
                    return false;
                }
                state->resources = true;
                state->resources_date_raw = record.game_date_raw;
                return true;
            }
            int64_t start = 0, target = 0;
            IntegerField(record, "start_date_raw", &start);
            IntegerField(record, "target_date_raw", &target);
            if (record.event == "benchmark.started") {
                if (state->started || state->terminal) { *error = "benchmark.started is duplicated or follows a terminal record"; return false; }
                state->started = true;
                state->start_date_raw = static_cast<int>(start);
                state->target_date_raw = static_cast<int>(target);
                int64_t timeout = 0;
                IntegerField(record, "timeout_seconds", &timeout);
                state->timeout_seconds = static_cast<int>(timeout);
                return true;
            }
            const auto *reason = Field(record, "reason");
            const bool invalid_target = reason != nullptr && reason->kind == JsonKind::String && reason->text == "invalid_target";
            if (state->terminal || (invalid_target ? state->started : !state->started)
                || (!invalid_target && (state->start_date_raw != start || state->target_date_raw != target))) {
                *error = "benchmark terminal has no matching start or is duplicated";
                return false;
            }
            if (state->resources && state->resources_date_raw != record.game_date_raw) {
                *error = "benchmark.resources game date does not match its terminal record";
                return false;
            }
            int64_t elapsed = 0;
            if (reason != nullptr && reason->text == "timeout"
                && (!IntegerField(record, "elapsed_us", &elapsed)
                    || elapsed < static_cast<int64_t>(state->timeout_seconds) * 1000000)) {
                *error = "benchmark timeout elapsed before its configured deadline";
                return false;
            }
            state->terminal = true;
            return true;
        }

        void CaptureBenchmark(const Record &record, BenchmarkSummary *benchmark)
        {
            auto integer = [&](const char *key) -> std::optional<int64_t> { int64_t value = 0; return IntegerField(record, key, &value) ? std::optional<int64_t>(value) : std::nullopt; };
            if (record.event == "benchmark.resources") {
                const auto capture = [&](const char *key, std::optional<uint64_t> *destination) {
                    if (const auto value = integer(key)) *destination = static_cast<uint64_t>(*value);
                };
                capture("process_cpu_us", &benchmark->process_cpu_us);
                capture("working_set_start_bytes", &benchmark->working_set_start_bytes);
                capture("working_set_end_bytes", &benchmark->working_set_end_bytes);
                capture("private_bytes_start", &benchmark->private_bytes_start);
                capture("private_bytes_end", &benchmark->private_bytes_end);
                capture("process_peak_working_set_bytes", &benchmark->process_peak_working_set_bytes);
                return;
            }
            if (record.event != "benchmark.started" && record.event != "benchmark.completed" && record.event != "benchmark.failed") return;
            benchmark->start_date_raw = static_cast<int>(*integer("start_date_raw"));
            benchmark->target_date_raw = static_cast<int>(*integer("target_date_raw"));
            if (record.event == "benchmark.started") { benchmark->status = "started"; return; }
            benchmark->status = record.event == "benchmark.completed" ? "completed" : "failed";
            if (const auto value = integer("actual_date_raw")) benchmark->actual_date_raw = static_cast<int>(*value);
            if (const auto value = integer("game_days")) benchmark->game_days = static_cast<int>(*value);
            if (const auto value = integer("elapsed_us")) benchmark->elapsed_us = static_cast<uint64_t>(*value);
            if (const auto *reason = Field(record, "reason")) benchmark->reason = reason->text;
            if (const auto *paused = Field(record, "paused")) benchmark->paused = paused->text == "true";
        }

        bool Match(const Record &record, const Filter &filter)
        {
            const auto country = record.entities.find("country_tag");
            return (filter.event.empty() || record.event == filter.event) && (filter.category.empty() || record.category == filter.category)
                && (filter.country.empty() || (country != record.entities.end() && country->second.kind == JsonKind::String && country->second.text == filter.country));
        }

        bool FileInfo(const fs::path &path, BY_HANDLE_FILE_INFORMATION *info, LARGE_INTEGER *size, std::string *error)
        {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) { *error = "source must be a normal file"; return false; }
            HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle, info) || !GetFileSizeEx(handle, size)) { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); *error = "could not inspect source"; return false; }
            CloseHandle(handle);
            return true;
        }

        bool HasReparseParent(const fs::path &path)
        {
            fs::path current = path.parent_path();
            while (!current.empty()) {
                const DWORD attributes = GetFileAttributesW(current.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return true;
                const fs::path parent = current.parent_path();
                if (parent == current) break;
                current = parent;
            }
            return false;
        }

        bool SameFile(const BY_HANDLE_FILE_INFORMATION &left, const BY_HANDLE_FILE_INFORMATION &right)
        {
            return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber && left.nFileIndexHigh == right.nFileIndexHigh && left.nFileIndexLow == right.nFileIndexLow;
        }

        bool AbsolutePath(const fs::path &path, fs::path *absolute, std::string *error)
        {
            std::error_code path_error;
            *absolute = fs::absolute(path, path_error).lexically_normal();
            if (path_error) {
                *error = "could not resolve path";
                return false;
            }
            return true;
        }

        struct Output {
            fs::path destination, temporary;
            HANDLE handle = INVALID_HANDLE_VALUE;
            bool overwrite = false;
            ~Output() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); if (!temporary.empty()) { std::error_code ignored; fs::remove(temporary, ignored); } }
        };

        bool StartOutput(const fs::path &input, const fs::path &output, const wchar_t *extension, bool overwrite, Output *result, std::string *error)
        {
            if (_wcsicmp(output.extension().c_str(), extension) != 0 || HasReparseParent(output)) { *error = "output has an invalid extension or reparse parent"; return false; }
            std::error_code path_error;
            const fs::path parent = output.has_parent_path() ? output.parent_path() : fs::current_path(path_error);
            if (path_error) { *error = "could not resolve output parent"; return false; }
            const DWORD parent_attributes = GetFileAttributesW(parent.c_str());
            if (parent_attributes == INVALID_FILE_ATTRIBUTES || (parent_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                *error = "output parent must be an existing directory";
                return false;
            }
            BY_HANDLE_FILE_INFORMATION input_info{}, output_info{};
            LARGE_INTEGER input_size{};
            if (HasReparseParent(input) || !FileInfo(input, &input_info, &input_size, error)) { if (error->empty()) *error = "input has a reparse parent"; return false; }
            const DWORD attributes = GetFileAttributesW(output.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES) {
                LARGE_INTEGER unused{};
                if (!FileInfo(output, &output_info, &unused, error) || output_info.nNumberOfLinks > 1 || SameFile(input_info, output_info)) { *error = "output is unsafe or aliases input"; return false; }
                if (!overwrite) { *error = "output exists (use --overwrite)"; return false; }
            }
            result->destination = output;
            result->overwrite = overwrite;
            for (unsigned attempt = 0; attempt != 100; ++attempt) {
                result->temporary = output.parent_path() / (output.filename().wstring() + L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(attempt) + L".tmp");
                result->handle = CreateFileW(result->temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (result->handle != INVALID_HANDLE_VALUE) return true;
            }
            *error = "could not create temporary output";
            return false;
        }

        bool Write(Output *output, const std::string &text, std::string *error)
        {
            size_t offset = 0;
            while (offset < text.size()) {
                const DWORD requested = static_cast<DWORD>(std::min<size_t>(text.size() - offset, (std::numeric_limits<DWORD>::max)()));
                DWORD written = 0;
                if (!WriteFile(output->handle, text.data() + offset, requested, &written, nullptr) || written == 0) { *error = "could not write output"; return false; }
                offset += written;
            }
            return true;
        }

        bool Publish(Output *output, std::string *error)
        {
            if (!FlushFileBuffers(output->handle)) { *error = "could not flush output"; return false; }
            CloseHandle(output->handle);
            output->handle = INVALID_HANDLE_VALUE;
            const DWORD flags = output->overwrite ? MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH : MOVEFILE_WRITE_THROUGH;
            if (!MoveFileExW(output->temporary.c_str(), output->destination.c_str(), flags)) { *error = "could not publish output"; return false; }
            output->temporary.clear();
            return true;
        }

        std::string CsvText(std::string value);

        bool CsvCountry(const Record &record, std::string *line, std::string *error)
        {
            const auto country = record.entities.find("country_tag"), raw = record.payload.find("treasury_raw"), treasury = record.payload.find("treasury");
            int64_t raw_value = 0;
            char *end = nullptr;
            if (record.event != "country.daily" || record.category != "state" || !record.game_date_raw || country == record.entities.end() || country->second.kind != JsonKind::String || !IsTag(country->second.text) || raw == record.payload.end() || raw->second.kind != JsonKind::Number || !ParseIntegerText(raw->second.text, &raw_value) || treasury == record.payload.end() || treasury->second.kind != JsonKind::Number) { *error = "country.daily has an unsupported schema"; return false; }
            const double actual = std::strtod(treasury->second.text.c_str(), &end);
            const double expected = static_cast<double>(raw_value) / 32768.0;
            if (!end || *end || !std::isfinite(actual) || std::fabs(actual - expected) > 0.000001 + std::fabs(expected) * 1e-9) { *error = "country.daily treasury values disagree"; return false; }
            *line = CsvText(record.run_id) + ',' + std::to_string(record.sequence) + ',' + CsvText(record.wall_time_utc) + ',' + std::to_string(record.monotonic_us) + ',' + std::to_string(*record.game_date_raw) + ',' + CsvText(record.quality) + ',' + CsvText(country->second.text) + ',' + raw->second.text + ',' + treasury->second.text + "\r\n";
            return true;
        }

        std::string CsvText(std::string value)
        {
            if (!value.empty() && std::strchr("=+-@", value.front())) value.insert(value.begin(), '\'');
            std::string quoted = "\"";
            for (const char character : value) quoted += character == '\"' ? "\"\"" : std::string(1, character);
            return quoted + "\"";
        }

        using FactoryKey = std::tuple<std::string, int64_t, std::string>;

        struct FactoryDay
        {
            bool production_seen = false;
            int64_t output_raw = 0;
            int64_t output_good_ordinal = -1;
            bool input_flow_summary_seen = false;
            bool input_flow_complete = false;
            bool input_no_purchase = false;
            std::map<int64_t, std::array<int64_t, 4>> input_flow_raw;
        };

        struct ProductionDay
        {
            std::map<int64_t, int64_t> price_raw;
            std::map<FactoryKey, FactoryDay> factories;
        };

        using ArtisanKey = std::pair<std::string, int64_t>;
        using RgoKey = std::pair<std::string, int64_t>;

        struct ArtisanDay
        {
            bool inactive = false;
            bool identity_seen = false;
            bool production_seen = false;
            int64_t output_raw = 0;
            int64_t current_producing_raw = 0;
            int64_t output_good_ordinal = -1;
            bool input_flow_summary_seen = false;
            bool input_flow_complete = false;
            bool input_no_purchase = false;
            std::map<int64_t, std::array<int64_t, 4>> input_flow_raw;
            std::map<int64_t, int64_t> input_need_raw;
        };

        struct RgoDay
        {
            bool identity_seen = false;
            bool production_seen = false;
            bool finance_seen = false;
            std::string output_good;
            int64_t output_good_ordinal = -1;
            int64_t gross_output_raw = 0;
            int64_t income_raw = 0;
        };

        struct GdpDay : ProductionDay
        {
            std::map<ArtisanKey, ArtisanDay> artisans;
            std::map<RgoKey, RgoDay> rgos;
            std::map<std::string, int64_t> population;
            std::set<std::tuple<std::string, int64_t, int64_t>> population_groups;
        };

        bool CountryEntity(const Record &record, std::string *country, std::string *error)
        {
            const auto *field = Entity(record, "country_tag");
            if (record.category != "state" || record.mapping_id != "v2game-3.04"
                || record.quality != "provisional" || !record.game_date_raw
                || field == nullptr || field->kind != JsonKind::String || !IsTag(field->text)) {
                *error = record.event + " has an unsupported schema";
                return false;
            }
            *country = field->text;
            return true;
        }

        bool ArtisanRecord(const Record &record, ArtisanKey *key, std::string *error)
        {
            std::string country;
            int64_t pop_id = -1, province_id = -1;
            if (!CountryEntity(record, &country, error)
                || !IntegerEntity(record, "pop_id", &pop_id) || pop_id < 0
                || !IntegerEntity(record, "province_id", &province_id) || province_id < 0) {
                if (error->empty()) *error = record.event + " has invalid artisan identity";
                return false;
            }
            *key = {country, pop_id};
            return true;
        }

        bool CaptureArtisanIdentity(const Record &record, GdpDay *day, std::string *error)
        {
            ArtisanKey key;
            int64_t output_good = -1;
            const auto *name = Field(record, "output_good");
            if (!ArtisanRecord(record, &key, error)
                || !IntegerField(record, "output_good_ordinal", &output_good) || output_good < 0 || output_good >= 64
                || name == nullptr || name->kind != JsonKind::String || !IsIdentifier(name->text)) {
                if (error->empty()) *error = "pop.artisan.identity has invalid values";
                return false;
            }
            auto &artisan = day->artisans[key];
            if (artisan.identity_seen) { *error = "duplicate pop.artisan.identity record"; return false; }
            artisan.identity_seen = true;
            artisan.output_good_ordinal = output_good;
            return true;
        }

        bool CaptureInactiveArtisan(const Record &record, GdpDay *day, std::string *error)
        {
            ArtisanKey key;
            if (!ArtisanRecord(record, &key, error)) return false;
            auto &artisan = day->artisans[key];
            if (artisan.inactive || artisan.identity_seen || artisan.production_seen) {
                *error = "duplicate or conflicting pop.artisan.inactive record";
                return false;
            }
            artisan.inactive = true;
            return true;
        }

        bool CaptureArtisanProduction(const Record &record, GdpDay *day, std::string *error)
        {
            ArtisanKey key;
            int64_t output = 0, current_producing = 0;
            if (!ArtisanRecord(record, &key, error)
                || !IntegerField(record, "gross_output_raw", &output) || output < 0
                || !IntegerField(record, "current_producing_raw", &current_producing)
                || current_producing < 0) {
                if (error->empty()) *error = "pop.artisan.production has invalid values";
                return false;
            }
            auto &artisan = day->artisans[key];
            if (artisan.production_seen) { *error = "duplicate pop.artisan.production record"; return false; }
            artisan.production_seen = true;
            artisan.output_raw = output;
            artisan.current_producing_raw = current_producing;
            return true;
        }

        bool CaptureArtisanFlowSummary(const Record &record, GdpDay *day, std::string *error)
        {
            ArtisanKey key;
            bool post = false, pre = false, primary = false, secondary = false;
            int64_t count = 0;
            if (!ArtisanRecord(record, &key, error)
                || !BooleanField(record, "post_consumption_seen", &post)
                || !BooleanField(record, "pre_purchase_seen", &pre)
                || !BooleanField(record, "primary_delivery_seen", &primary)
                || !BooleanField(record, "secondary_delivery_seen", &secondary)
                || !IntegerField(record, "settlement_count", &count) || count < 0) {
                if (error->empty()) *error = "pop.artisan.input.flow.summary has invalid values";
                return false;
            }
            auto &artisan = day->artisans[key];
            if (artisan.input_flow_summary_seen) {
                *error = "duplicate pop.artisan.input.flow.summary record";
                return false;
            }
            artisan.input_flow_summary_seen = true;
            artisan.input_no_purchase = post && !pre && !primary && !secondary && count == 0;
            artisan.input_flow_complete = artisan.input_no_purchase
                || (post && pre && primary && secondary && count == 1);
            return true;
        }

        bool CaptureArtisanFlow(const Record &record, GdpDay *day, std::string *error)
        {
            ArtisanKey key;
            int64_t good = -1;
            std::array<int64_t, 4> values{};
            if (!ArtisanRecord(record, &key, error)
                || !IntegerEntity(record, "good_ordinal", &good) || good < 0 || good >= 64
                || !IntegerField(record, "post_consumption_raw", &values[0]) || values[0] < 0
                || !IntegerField(record, "pre_purchase_raw", &values[1]) || values[1] < 0
                || !IntegerField(record, "delivered_primary_raw", &values[2]) || values[2] < 0
                || !IntegerField(record, "delivered_secondary_raw", &values[3]) || values[3] < 0) {
                if (error->empty()) *error = "pop.artisan.input.flow has invalid values";
                return false;
            }
            if (!day->artisans[key].input_flow_raw.emplace(good, values).second) {
                *error = "duplicate pop.artisan.input.flow record";
                return false;
            }
            return true;
        }

        bool CaptureArtisanInput(const Record &record, GdpDay *day, std::string *error)
        {
            ArtisanKey key;
            int64_t good = -1, need = 0;
            if (!ArtisanRecord(record, &key, error)
                || !IntegerEntity(record, "good_ordinal", &good) || good < 0 || good >= 64
                || !IntegerField(record, "need_raw", &need) || need < 0) {
                if (error->empty()) *error = "pop.artisan.input has invalid values";
                return false;
            }
            if (!day->artisans[key].input_need_raw.emplace(good, need).second) {
                *error = "duplicate pop.artisan.input record";
                return false;
            }
            return true;
        }

        bool RgoRecord(const Record &record, RgoKey *key, std::string *error)
        {
            std::string country;
            int64_t province = -1;
            if (!CountryEntity(record, &country, error)
                || !IntegerEntity(record, "province_id", &province) || province < 0) {
                if (error->empty()) *error = record.event + " has invalid RGO identity";
                return false;
            }
            *key = {country, province};
            return true;
        }

        bool CaptureRgoIdentity(const Record &record, GdpDay *day, std::string *error)
        {
            RgoKey key;
            int64_t output_good = -1;
            const auto *name = Field(record, "output_good");
            if (!RgoRecord(record, &key, error)
                || !IntegerField(record, "output_good_ordinal", &output_good) || output_good < 0 || output_good >= 64
                || name == nullptr || name->kind != JsonKind::String || !IsIdentifier(name->text)) {
                if (error->empty()) *error = "province.rgo.identity has invalid values";
                return false;
            }
            auto &rgo = day->rgos[key];
            if (rgo.identity_seen) { *error = "duplicate province.rgo.identity record"; return false; }
            rgo.identity_seen = true;
            rgo.output_good = name->text;
            rgo.output_good_ordinal = output_good;
            return true;
        }

        bool CaptureRgoProduction(const Record &record, GdpDay *day, std::string *error)
        {
            RgoKey key;
            int64_t output = 0;
            if (!RgoRecord(record, &key, error)
                || !IntegerField(record, "gross_output_raw", &output) || output < 0) {
                if (error->empty()) *error = "province.rgo.production has invalid values";
                return false;
            }
            auto &rgo = day->rgos[key];
            if (rgo.production_seen) { *error = "duplicate province.rgo.production record"; return false; }
            rgo.production_seen = true;
            rgo.gross_output_raw = output;
            return true;
        }

        bool CaptureRgoFinance(const Record &record, GdpDay *day, std::string *error)
        {
            RgoKey key;
            int64_t income = 0;
            if (!RgoRecord(record, &key, error)
                || !IntegerField(record, "income_raw", &income) || income < 0) {
                if (error->empty()) *error = "province.rgo.finance has invalid values";
                return false;
            }
            auto &rgo = day->rgos[key];
            if (rgo.finance_seen) { *error = "duplicate province.rgo.finance record"; return false; }
            rgo.finance_seen = true;
            rgo.income_raw = income;
            return true;
        }

        bool CapturePopulation(const Record &record, GdpDay *day, std::string *error)
        {
            std::string country;
            int64_t province = -1, type = -1, size = -1;
            if (!CountryEntity(record, &country, error)
                || !IntegerEntity(record, "province_id_candidate", &province) || province < 0
                || !IntegerEntity(record, "pop_type_id_candidate", &type) || type < 0
                || !IntegerField(record, "size_candidate", &size) || size < 0) {
                if (error->empty()) *error = "pop.aggregate has invalid values";
                return false;
            }
            if (!day->population_groups.emplace(country, province, type).second) {
                *error = "duplicate pop.aggregate record";
                return false;
            }
            auto &total = day->population[country];
            if (total > (std::numeric_limits<int64_t>::max)() - size) {
                *error = "pop.aggregate population overflows";
                return false;
            }
            total += size;
            return true;
        }

        bool FactoryRecord(const Record &record, FactoryKey *key, std::string *error)
        {
            const auto *country = Entity(record, "country_tag");
            const auto *type = Entity(record, "factory_type");
            int64_t state_id = 0;
            if (record.category != "state" || record.mapping_id != "v2game-3.04" || record.quality != "provisional"
                || !record.game_date_raw || country == nullptr || country->kind != JsonKind::String
                || !IsTag(country->text) || type == nullptr || type->kind != JsonKind::String
                || !IsIdentifier(type->text) || !IntegerEntity(record, "state_id", &state_id) || state_id < 0) {
                *error = record.event + " has an unsupported schema";
                return false;
            }
            *key = {country->text, state_id, type->text};
            return true;
        }

        bool CaptureFactoryProduction(const Record &record, ProductionDay *day, std::string *error)
        {
            FactoryKey key;
            int64_t output_raw = 0, output_good = -1;
            if (!FactoryRecord(record, &key, error)
                || !IntegerField(record, "output_raw", &output_raw) || output_raw < 0
                || !IntegerField(record, "output_good_ordinal", &output_good) || output_good < 0 || output_good >= 64) {
                if (error->empty()) *error = "state.factory.production has invalid values";
                return false;
            }
            auto &factory = day->factories[key];
            if (factory.production_seen) { *error = "duplicate state.factory.production record"; return false; }
            factory.production_seen = true;
            factory.output_raw = output_raw;
            factory.output_good_ordinal = output_good;
            return true;
        }

        bool CaptureFactoryInputFlowSummary(const Record &record, ProductionDay *day, std::string *error)
        {
            FactoryKey key;
            bool post_consumption = false, pre_purchase = false, primary = false, secondary = false;
            int64_t count = 0;
            if (!FactoryRecord(record, &key, error)
                || !BooleanField(record, "post_consumption_seen", &post_consumption)
                || !BooleanField(record, "pre_purchase_seen", &pre_purchase)
                || !BooleanField(record, "primary_delivery_seen", &primary)
                || !BooleanField(record, "secondary_delivery_seen", &secondary)
                || !IntegerField(record, "settlement_count", &count) || count < 0) {
                if (error->empty()) *error = "state.factory.input.flow.summary has invalid values";
                return false;
            }
            auto &factory = day->factories[key];
            if (factory.input_flow_summary_seen) {
                *error = "duplicate state.factory.input.flow.summary record";
                return false;
            }
            factory.input_flow_summary_seen = true;
            factory.input_no_purchase = post_consumption && !pre_purchase && !primary && !secondary && count == 0;
            factory.input_flow_complete = factory.input_no_purchase
                || (post_consumption && pre_purchase && primary && secondary && count == 1);
            return true;
        }

        bool CaptureFactoryInputFlow(const Record &record, ProductionDay *day, std::string *error)
        {
            FactoryKey key;
            int64_t good = -1;
            std::array<int64_t, 4> values{};
            if (!FactoryRecord(record, &key, error)
                || !IntegerEntity(record, "good_ordinal", &good) || good < 0 || good >= 64
                || !IntegerField(record, "post_consumption_raw", &values[0]) || values[0] < 0
                || !IntegerField(record, "pre_purchase_raw", &values[1]) || values[1] < 0
                || !IntegerField(record, "delivered_primary_raw", &values[2]) || values[2] < 0
                || !IntegerField(record, "delivered_secondary_raw", &values[3]) || values[3] < 0) {
                if (error->empty()) *error = "state.factory.input.flow has invalid values";
                return false;
            }
            if (!day->factories[key].input_flow_raw.emplace(good, values).second) {
                *error = "duplicate state.factory.input.flow record";
                return false;
            }
            return true;
        }

        bool CaptureMarketPrice(const Record &record, ProductionDay *day, std::string *error)
        {
            int64_t good = -1, price = 0;
            if (record.category != "state" || record.mapping_id != "v2game-3.04" || record.quality != "provisional"
                || !record.game_date_raw || !IntegerEntity(record, "good_ordinal", &good)
                || good < 0 || good >= 64 || !IntegerField(record, "price_raw", &price) || price < 0) {
                *error = "world.market.price has an unsupported schema";
                return false;
            }
            if (!day->price_raw.emplace(good, price).second) { *error = "duplicate world.market.price record"; return false; }
            return true;
        }

        bool HealthyFactoryTrace(const Summary &summary, std::string *error)
        {
            if (!summary.warning.empty()) { *error = "factory value added requires a complete final record"; return false; }
            if (summary.gaps != 0) { *error = "factory value added requires a trace without sequence gaps"; return false; }
            if (summary.date_regressed) { *error = "factory value added requires monotonic game dates"; return false; }
            const auto dropped = summary.progress.find("dropped");
            int64_t dropped_count = 0;
            if (dropped != summary.progress.end()
                && (dropped->second.kind != JsonKind::Number
                    || !ParseIntegerText(dropped->second.text, &dropped_count) || dropped_count != 0)) {
                *error = "factory value added requires healthy telemetry output";
                return false;
            }
            const auto write_failed = summary.progress.find("write_failed");
            if (write_failed != summary.progress.end()
                && (write_failed->second.kind != JsonKind::Boolean || write_failed->second.text != "false")) {
                *error = "factory value added requires healthy telemetry output";
                return false;
            }
            return true;
        }

        bool CaptureFamilyHealth(const Record &record, bool *factory_seen, bool *market_seen, std::string *error)
        {
            const auto *family = Entity(record, "family");
            if (record.category != "lifecycle" || record.mapping_id != "v2game-3.04"
                || record.quality != "verified-current" || record.game_date_raw || family == nullptr
                || family->kind != JsonKind::String) {
                *error = "telemetry.family.summary is unhealthy or malformed";
                return false;
            }
            bool *seen = nullptr;
            if (family->text == "state.factory") seen = factory_seen;
            else if (family->text == "world.market") seen = market_seen;
            else return true;
            int64_t dropped = -1, invalid = -1;
            if (!IntegerField(record, "dropped", &dropped) || !IntegerField(record, "invalid", &invalid)
                || dropped != 0 || invalid != 0) {
                *error = "telemetry.family.summary is unhealthy or malformed";
                return false;
            }
            if (*seen) { *error = "duplicate telemetry.family.summary record"; return false; }
            *seen = true;
            return true;
        }

        bool CaptureTerminalHealth(const Record &record, uint64_t *sequence, std::string *error)
        {
            int64_t dropped = -1;
            const auto *write_failed = Field(record, "write_failed");
            if (record.category != "lifecycle" || record.mapping_id != "v2game-3.04"
                || record.quality != "verified-current" || record.game_date_raw || !record.entities.empty()
                || !IntegerField(record, "dropped", &dropped) || dropped != 0 || write_failed == nullptr
                || write_failed->kind != JsonKind::Boolean || write_failed->text != "false") {
                *error = "telemetry.summary is unhealthy or malformed";
                return false;
            }
            if (*sequence != 0) { *error = "duplicate telemetry.summary record"; return false; }
            *sequence = record.sequence;
            return true;
        }

        struct CaptureRuleMetadata
        {
            bool seen = false;
            bool all_fields = false;
            bool bounded_dates = false;
            std::string cadence;
            int64_t country_filter_count = -1;
            int64_t province_filter_count = -1;
            std::set<std::string> fields;
            std::set<std::string> countries;
        };

        bool CaptureGdpMetadata(const Record &record, std::map<std::string, CaptureRuleMetadata> *metadata,
                                std::string *error)
        {
            const auto *family = Entity(record, "family");
            if (record.category != "lifecycle" || record.mapping_id != "v2game-3.04"
                || record.quality != "verified-current" || record.game_date_raw
                || family == nullptr || family->kind != JsonKind::String || !IsIdentifier(family->text)) {
                *error = record.event + " is malformed";
                return false;
            }
            auto &rule = (*metadata)[family->text];
            if (record.event == "telemetry.capture.rule") {
                const auto *cadence = Field(record, "cadence");
                bool all_fields = false, bounded_dates = false;
                int64_t country_count = -1, province_count = -1;
                if (rule.seen || cadence == nullptr || cadence->kind != JsonKind::String
                    || !BooleanField(record, "all_fields", &all_fields)
                    || !BooleanField(record, "bounded_dates", &bounded_dates)
                    || !IntegerField(record, "country_filter_count", &country_count) || country_count < 0
                    || !IntegerField(record, "province_filter_count", &province_count) || province_count < 0) {
                    *error = "telemetry.capture.rule is malformed or duplicated";
                    return false;
                }
                rule.seen = true;
                rule.cadence = cadence->text;
                rule.all_fields = all_fields;
                rule.bounded_dates = bounded_dates;
                rule.country_filter_count = country_count;
                rule.province_filter_count = province_count;
                return true;
            }
            if (record.event == "telemetry.capture.field") {
                const auto *field = Entity(record, "field");
                if (field == nullptr || field->kind != JsonKind::String || !IsIdentifier(field->text)
                    || !rule.fields.insert(field->text).second) {
                    *error = "telemetry.capture.field is malformed or duplicated";
                    return false;
                }
                return true;
            }
            const auto *country = Entity(record, "country_tag");
            if (country == nullptr || country->kind != JsonKind::String || !IsTag(country->text)
                || !rule.countries.insert(country->text).second) {
                *error = "telemetry.capture.country is malformed or duplicated";
                return false;
            }
            return true;
        }

        bool CaptureGdpFamilyHealth(const Record &record, std::set<std::string> *seen,
                                    std::map<std::string, int64_t> *polls, std::string *error)
        {
            const auto *family = Entity(record, "family");
            if (record.category != "lifecycle" || record.mapping_id != "v2game-3.04"
                || record.quality != "verified-current" || record.game_date_raw || family == nullptr
                || family->kind != JsonKind::String) {
                *error = "telemetry.family.summary is unhealthy or malformed";
                return false;
            }
            static const std::set<std::string> required = {
                "world.market", "state.factory", "province.rgo", "pop.artisan", "pop.aggregate"};
            if (required.find(family->text) == required.end()) return true;
            int64_t dropped = -1, invalid = -1, polls_due = -1;
            if (!IntegerField(record, "dropped", &dropped) || !IntegerField(record, "invalid", &invalid)
                || !IntegerField(record, "polls_due", &polls_due) || polls_due < 0
                || dropped != 0 || invalid != 0) {
                *error = "telemetry.family.summary is unhealthy or malformed";
                return false;
            }
            if (!seen->insert(family->text).second) {
                *error = "duplicate telemetry.family.summary record";
                return false;
            }
            (*polls)[family->text] = polls_due;
            return true;
        }

        bool ValidateGdpMetadata(const std::map<std::string, CaptureRuleMetadata> &metadata,
                                 const std::map<std::string, int64_t> &polls, size_t day_count,
                                 std::string *error)
        {
            static const std::map<std::string, std::set<std::string>> required = {
                {"world.market", {"price"}},
                {"state.factory", {"production", "flows"}},
                {"province.rgo", {"identity", "production"}},
                {"pop.artisan", {"identity", "production", "inputs"}},
                {"pop.aggregate", {"size_candidate"}},
            };
            for (const auto &[family, fields] : required) {
                const auto candidate = metadata.find(family);
                const auto family_polls = polls.find(family);
                if (candidate == metadata.end() || !candidate->second.seen
                    || candidate->second.cadence != "daily" || candidate->second.bounded_dates
                    || candidate->second.province_filter_count != 0
                    || candidate->second.country_filter_count
                        != static_cast<int64_t>(candidate->second.countries.size())
                    || family_polls == polls.end() || family_polls->second != static_cast<int64_t>(day_count)) {
                    *error = "country GDP requires complete daily capture scope for " + family;
                    return false;
                }
                if (!candidate->second.all_fields
                    && !std::includes(candidate->second.fields.begin(), candidate->second.fields.end(),
                        fields.begin(), fields.end())) {
                    *error = "country GDP capture is missing required fields for " + family;
                    return false;
                }
            }
            return true;
        }

        struct ProducerValues
        {
            long double nominal_gross = 0.0L;
            long double nominal_inputs = 0.0L;
            long double real_gross = 0.0L;
            long double real_inputs = 0.0L;
        };

        template<typename Map>
        bool ValueProducerSet(const Map &opening_map, const Map &current_map,
                              const std::map<int64_t, int64_t> &current_prices,
                              const std::map<int64_t, int64_t> &base_prices,
                              const std::string &country, int current_date,
                              std::string_view sector, ProducerValues *values, std::string *error)
        {
            using Key = typename Map::key_type;
            std::set<Key> opening_keys, current_keys;
            for (const auto &[key, unused] : opening_map) {
                if (std::get<0>(key) == country) opening_keys.insert(key);
            }
            for (const auto &[key, unused] : current_map) {
                if (std::get<0>(key) == country) current_keys.insert(key);
            }
            for (const auto &key : current_keys) {
                if (opening_keys.find(key) == opening_keys.end()) {
                    *error = std::string(sector) + " entered without opening flow evidence before date "
                        + std::to_string(current_date) + " for " + country;
                    return false;
                }
            }
            constexpr long double scale = 32768.0L * 32768.0L;
            const std::array<int64_t, 4> empty{};
            for (const auto &key : current_keys) {
                const auto &opening = opening_map.at(key);
                const auto &current = current_map.at(key);
                if (!opening.input_flow_complete || !current.input_flow_complete) {
                    *error = std::string(sector) + " input flow is incomplete before date "
                        + std::to_string(current_date);
                    return false;
                }
                const auto nominal_output_price = current_prices.find(current.output_good_ordinal);
                const auto real_output_price = base_prices.find(current.output_good_ordinal);
                if (nominal_output_price == current_prices.end() || real_output_price == base_prices.end()) {
                    *error = std::string(sector) + " output price is missing at date " + std::to_string(current_date);
                    return false;
                }
                values->nominal_gross += static_cast<long double>(current.output_raw)
                    * nominal_output_price->second / scale;
                values->real_gross += static_cast<long double>(current.output_raw)
                    * real_output_price->second / scale;
                std::set<int64_t> goods;
                for (const auto &[good, unused] : opening.input_flow_raw) goods.insert(good);
                for (const auto &[good, unused] : current.input_flow_raw) goods.insert(good);
                for (const int64_t good : goods) {
                    const auto nominal_price = current_prices.find(good);
                    const auto real_price = base_prices.find(good);
                    if (nominal_price == current_prices.end() || real_price == base_prices.end()) {
                        *error = std::string(sector) + " input price is missing at date " + std::to_string(current_date);
                        return false;
                    }
                    const auto opening_entry = opening.input_flow_raw.find(good);
                    const auto current_entry = current.input_flow_raw.find(good);
                    const auto &opening_flow = opening_entry == opening.input_flow_raw.end() ? empty : opening_entry->second;
                    const auto &current_flow = current_entry == current.input_flow_raw.end() ? empty : current_entry->second;
                    const bool opening_invalid = !opening.input_no_purchase
                        && (opening_flow[2] > (std::numeric_limits<int64_t>::max)() - opening_flow[3]
                            || opening_flow[1] > (std::numeric_limits<int64_t>::max)()
                                - opening_flow[2] - opening_flow[3]);
                    const bool current_invalid = current.input_no_purchase
                        ? current_flow[1] != 0 || current_flow[2] != 0 || current_flow[3] != 0
                        : current_flow[0] != current_flow[1]
                            || current_flow[2] > (std::numeric_limits<int64_t>::max)() - current_flow[3]
                            || current_flow[1] > (std::numeric_limits<int64_t>::max)()
                                - current_flow[2] - current_flow[3];
                    if (opening_invalid || current_invalid) {
                        *error = std::string(sector) + " input flow does not reconcile at date "
                            + std::to_string(current_date);
                        return false;
                    }
                    const int64_t opening_stock = opening.input_no_purchase ? opening_flow[0]
                        : opening_flow[1] + opening_flow[2] + opening_flow[3];
                    if (current_flow[0] > opening_stock) {
                        *error = std::string(sector) + " input stock increased before settlement at date "
                            + std::to_string(current_date) + " for producer "
                            + std::to_string(std::get<1>(key)) + " good " + std::to_string(good);
                        return false;
                    }
                    const int64_t consumed = opening_stock - current_flow[0];
                    values->nominal_inputs += static_cast<long double>(consumed) * nominal_price->second / scale;
                    values->real_inputs += static_cast<long double>(consumed) * real_price->second / scale;
                }
            }
            return true;
        }

        bool ValueArtisans(const GdpDay &day, const std::map<int64_t, int64_t> &base_prices,
                           const std::string &country, int date, ProducerValues *values, std::string *error)
        {
            constexpr long double scale = 32768.0L * 32768.0L;
            for (const auto &[key, artisan] : day.artisans) {
                if (key.first != country) continue;
                if (artisan.inactive) continue;
                if (!artisan.identity_seen || !artisan.production_seen) {
                    *error = "artisan production is incomplete at date " + std::to_string(date)
                        + " for POP " + std::to_string(key.second);
                    return false;
                }
                const auto nominal_output_price = day.price_raw.find(artisan.output_good_ordinal);
                const auto real_output_price = base_prices.find(artisan.output_good_ordinal);
                if (nominal_output_price == day.price_raw.end() || real_output_price == base_prices.end()) {
                    *error = "artisan output price is missing at date " + std::to_string(date);
                    return false;
                }
                values->nominal_gross += static_cast<long double>(artisan.output_raw)
                    * nominal_output_price->second / scale;
                values->real_gross += static_cast<long double>(artisan.output_raw)
                    * real_output_price->second / scale;
                for (const auto &[good, need] : artisan.input_need_raw) {
                    if (need != 0 && artisan.current_producing_raw
                        > (std::numeric_limits<int64_t>::max)() / need) {
                        *error = "artisan input consumption overflows at date " + std::to_string(date)
                            + " for POP " + std::to_string(key.second);
                        return false;
                    }
                    const auto nominal_price = day.price_raw.find(good);
                    const auto real_price = base_prices.find(good);
                    if (nominal_price == day.price_raw.end() || real_price == base_prices.end()) {
                        *error = "artisan input price is missing at date " + std::to_string(date);
                        return false;
                    }
                    const int64_t consumed = need * artisan.current_producing_raw / 32768;
                    values->nominal_inputs += static_cast<long double>(consumed) * nominal_price->second / scale;
                    values->real_inputs += static_cast<long double>(consumed) * real_price->second / scale;
                }
            }
            return true;
        }

        bool ValueRgos(const GdpDay &day, const std::map<int64_t, int64_t> &base_prices,
                       std::optional<double> gold_to_cash_rate,
                       const std::string &country, int date, long double *nominal,
                       long double *real, long double *precious, std::string *error)
        {
            constexpr long double quantity_price_scale = 32768.0L * 32768.0L;
            for (const auto &[key, rgo] : day.rgos) {
                if (key.first != country) continue;
                if (!rgo.identity_seen || !rgo.production_seen) {
                    *error = "RGO snapshot is incomplete at date " + std::to_string(date);
                    return false;
                }
                if (rgo.output_good == "precious_metal") {
                    if (!gold_to_cash_rate) {
                        *error = "precious-metal GDP requires --gold-to-cash-rate for the active mod";
                        return false;
                    }
                    const long double value = static_cast<long double>(rgo.gross_output_raw)
                        * static_cast<long double>(*gold_to_cash_rate) / 32768.0L;
                    *nominal += value;
                    *real += value;
                    *precious += value;
                    continue;
                }
                const auto current_price = day.price_raw.find(rgo.output_good_ordinal);
                const auto base_price = base_prices.find(rgo.output_good_ordinal);
                if (current_price == day.price_raw.end() || base_price == base_prices.end()) {
                    *error = "RGO output price is missing at date " + std::to_string(date);
                    return false;
                }
                *nominal += static_cast<long double>(rgo.gross_output_raw) * current_price->second
                    / quantity_price_scale;
                *real += static_cast<long double>(rgo.gross_output_raw) * base_price->second
                    / quantity_price_scale;
            }
            return true;
        }

        std::string Decimal(long double value)
        {
            std::ostringstream output;
            output << std::fixed << std::setprecision(9) << value;
            return output.str();
        }
    }

    bool Stream(const fs::path &path, Summary *summary, const Visitor &visitor, std::string *error)
    {
        fs::path source_path;
        if (!AbsolutePath(path, &source_path, error)) return false;
        if (HasReparseParent(source_path)) { *error = "source has a reparse parent"; return false; }
        *summary = {};
        HANDLE source = CreateFileW(source_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (source == INVALID_HANDLE_VALUE) { *error = "could not open source"; return false; }
        auto close_source = [&] { CloseHandle(source); };
        BY_HANDLE_FILE_INFORMATION source_info{};
        LARGE_INTEGER snapshot{};
        if (!GetFileInformationByHandle(source, &source_info) || !GetFileSizeEx(source, &snapshot)
            || (source_info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
            close_source();
            *error = "source must be a normal file";
            return false;
        }
        std::string line;
        uint64_t consumed = 0, previous_sequence = 0, previous_monotonic_us = 0;
        std::optional<int> previous_date;
        BenchmarkTraceState benchmark_state;
        LifecycleTraceState lifecycle_state;
        bool have_sequence = false;
        const uint64_t snapshot_bytes = static_cast<uint64_t>(snapshot.QuadPart);
        auto process = [&](bool complete_line) {
            Record record;
            std::string detail;
            if (!Decode(line, &record, &detail)) {
                if (!complete_line && consumed == snapshot_bytes && detail == "unexpected EOF") {
                    summary->warning = "incomplete final line ignored";
                    return true;
                }
                *error = "line " + std::to_string(summary->records + 1) + ": " + detail;
                return false;
            }
            if (!BenchmarkSchema(record, &detail)) { *error = "line " + std::to_string(summary->records + 1) + ": " + detail; return false; }
            if (!BenchmarkTransition(record, &benchmark_state, &detail)) { *error = "line " + std::to_string(summary->records + 1) + ": " + detail; return false; }
            if (!summary->run_id.empty() && summary->run_id != record.run_id) { *error = "inconsistent run_id"; return false; }
            summary->run_id = record.run_id;
            if (have_sequence && record.sequence <= previous_sequence) { *error = "sequence is not strictly increasing"; return false; }
            if (have_sequence && record.monotonic_us < previous_monotonic_us) { *error = "monotonic_us decreased"; return false; }
            if (!have_sequence) summary->gaps = record.sequence - 1;
            else summary->gaps += record.sequence - previous_sequence - 1;
            previous_sequence = record.sequence;
            previous_monotonic_us = record.monotonic_us;
            have_sequence = true;
            ++summary->records;
            ++summary->events[record.event];
            ++summary->categories[record.category];
            ++summary->qualities[record.quality];
            if (!summary->first_sequence) { summary->first_sequence = record.sequence; summary->first_monotonic_us = record.monotonic_us; }
            summary->last_sequence = record.sequence;
            summary->last_monotonic_us = record.monotonic_us;
            if (record.game_date_raw) {
                if (!summary->first_date) {
                    summary->first_date = *record.game_date_raw;
                    summary->first_date_monotonic_us = record.monotonic_us;
                }
                summary->last_date = *record.game_date_raw;
                summary->last_date_monotonic_us = record.monotonic_us;
                if (previous_date && *record.game_date_raw < *previous_date) summary->date_regressed = true;
                previous_date = record.game_date_raw;
            }
            CaptureLifecycle(record, &lifecycle_state);
            if (record.event == "telemetry.progress" || record.event == "telemetry.summary") {
                summary->progress_seen = true;
                summary->progress = record.payload;
            }
            CaptureBenchmark(record, &summary->benchmark);
            if (visitor && !visitor(record, error)) return false;
            return true;
        };
        std::array<char, 64 * 1024> buffer{};
        while (consumed < snapshot_bytes) {
            const uint64_t remaining = snapshot_bytes - consumed;
            const DWORD requested = static_cast<DWORD>(std::min<uint64_t>(remaining, buffer.size()));
            DWORD read = 0;
            if (!ReadFile(source, buffer.data(), requested, &read, nullptr) || read == 0) {
                close_source();
                *error = "could not read source";
                return false;
            }
            for (DWORD index = 0; index < read; ++index) {
                const char character = buffer[index];
                if (character == '\n') {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (!process(true)) { close_source(); return false; }
                    line.clear();
                } else {
                    if (!line.empty() && line.back() == '\r') { close_source(); *error = "embedded carriage return"; return false; }
                    if (line.size() == max_line_bytes) { close_source(); *error = "trace line exceeds 1 MiB"; return false; }
                    line += character;
                }
            }
            consumed += read;
        }
        if (!line.empty() && line.back() == '\r') {
            close_source();
            *error = "embedded carriage return";
            return false;
        }
        if (!line.empty() && !process(false)) { close_source(); return false; }
        close_source();
        if (summary->records == 0) { *error = "trace contains no complete telemetry records"; return false; }
        summary->lifecycle.save_load_us = LifecycleDuration(lifecycle_state.save_selection, lifecycle_state.save_load);
        summary->lifecycle.campaign_enter_us = LifecycleDuration(lifecycle_state.save_load, lifecycle_state.campaign_enter);
        summary->lifecycle.observer_configure_us = LifecycleDuration(lifecycle_state.campaign_enter, lifecycle_state.observer_configure);
        return true;
    }

    bool Read(const fs::path &path, const Filter &filter, Summary *summary, std::vector<Record> *selected, size_t limit, std::string *error)
    {
        return Stream(path, summary, [&](const Record &record, std::string *) {
            if (selected && Match(record, filter) && selected->size() < limit) selected->push_back(record);
            return true;
        }, error);
    }

    bool ExportTrace(const fs::path &input, const fs::path &output, const Filter &filter, bool overwrite,
                     std::string *error, std::string *warning)
    {
        fs::path input_path, output_path;
        if (!AbsolutePath(input, &input_path, error) || !AbsolutePath(output, &output_path, error)) return false;
        if (filter.event == "benchmark.started" || filter.event == "benchmark.resources"
            || filter.event == "benchmark.completed" || filter.event == "benchmark.failed") {
            *error = "benchmark lifecycle events cannot be split by a trace export filter";
            return false;
        }
        Output destination;
        if (!StartOutput(input_path, output_path, L".jsonl", overwrite, &destination, error)) return false;
        Summary summary;
        uint64_t matched = 0;
        if (!Stream(input_path, &summary, [&](const Record &record, std::string *visitor_error) {
            if (!Match(record, filter)) return true;
            ++matched;
            return Write(&destination, record.raw + '\n', visitor_error);
        }, error)) return false;
        if (matched == 0) { *error = "no records matched the export filter"; return false; }
        if (warning) *warning = summary.warning;
        return Publish(&destination, error);
    }

    bool ExportCountryCsv(const fs::path &input, const fs::path &output, bool overwrite,
                           std::string *error, std::string *warning)
    {
        fs::path input_path, output_path;
        if (!AbsolutePath(input, &input_path, error) || !AbsolutePath(output, &output_path, error)) return false;
        Output destination;
        if (!StartOutput(input_path, output_path, L".csv", overwrite, &destination, error)) return false;
        Summary summary;
        if (!Write(&destination, "run_id,sequence,wall_time_utc,monotonic_us,game_date_raw,quality,country_tag,treasury_raw,treasury\r\n", error)) return false;
        if (!Stream(input_path, &summary, [&](const Record &record, std::string *visitor_error) {
            if (record.event != "country.daily") return true;
            std::string line;
            return CsvCountry(record, &line, visitor_error) && Write(&destination, line, visitor_error);
        }, error)) return false;
        if (warning) *warning = summary.warning;
        return Publish(&destination, error);
    }

    bool ExportFactoryValueAddedCsv(const fs::path &input, const fs::path &output,
                                    const std::string &country, bool overwrite,
                                    std::string *error, std::string *warning)
    {
        fs::path input_path, output_path;
        if (!AbsolutePath(input, &input_path, error) || !AbsolutePath(output, &output_path, error)) return false;
        std::map<int, ProductionDay> days;
        bool factory_health_seen = false, market_health_seen = false;
        uint64_t terminal_health_sequence = 0;
        Summary summary;
        if (!Stream(input_path, &summary, [&](const Record &record, std::string *visitor_error) {
            if (record.event == "telemetry.family.summary") {
                return CaptureFamilyHealth(record, &factory_health_seen, &market_health_seen, visitor_error);
            }
            if (record.event == "telemetry.summary") {
                return CaptureTerminalHealth(record, &terminal_health_sequence, visitor_error);
            }
            if (record.event != "world.market.price" && record.event != "state.factory.production"
                && record.event != "state.factory.input.flow.summary"
                && record.event != "state.factory.input.flow") return true;
            if (!record.game_date_raw) { *visitor_error = record.event + " is missing game_date_raw"; return false; }
            auto &day = days[*record.game_date_raw];
            if (record.event == "world.market.price") return CaptureMarketPrice(record, &day, visitor_error);
            if (record.event == "state.factory.production") return CaptureFactoryProduction(record, &day, visitor_error);
            if (record.event == "state.factory.input.flow.summary") {
                return CaptureFactoryInputFlowSummary(record, &day, visitor_error);
            }
            return CaptureFactoryInputFlow(record, &day, visitor_error);
        }, error) || !HealthyFactoryTrace(summary, error)) return false;
        if (!factory_health_seen || !market_health_seen || terminal_health_sequence == 0
            || !summary.last_sequence || terminal_health_sequence != *summary.last_sequence) {
            *error = "factory value added requires terminal factory, market, and writer health summaries";
            return false;
        }
        if (days.size() < 2) { *error = "factory value added requires at least two daily snapshots"; return false; }

        for (auto day_iterator = days.begin(); day_iterator != days.end(); ++day_iterator) {
            const auto &[date, day] = *day_iterator;
            for (const auto &[key, factory] : day.factories) {
                if (!factory.production_seen || !factory.input_flow_summary_seen) {
                    *error = "factory snapshot is missing production or flow-summary evidence at date " + std::to_string(date);
                    return false;
                }
                if (day_iterator != days.begin() && !factory.input_flow_complete) {
                    *error = "factory input flow is incomplete at date " + std::to_string(date);
                    return false;
                }
            }
        }

        Output destination;
        if (!StartOutput(input_path, output_path, L".csv", overwrite, &destination, error)) return false;
        if (!Write(&destination,
                "run_id,opening_date_raw,game_date_raw,country_tag,factory_count,gross_output_value,intermediate_consumption,value_added,quality\r\n",
                error)) return false;

        constexpr long double quantity_price_scale = 32768.0L * 32768.0L;
        size_t rows = 0;
        for (auto current = std::next(days.begin()); current != days.end(); ++current) {
            const auto previous = std::prev(current);
            if (current->first - previous->first != 24) {
                *error = "factory value added requires consecutive daily snapshots";
                return false;
            }
            std::set<std::string> countries;
            for (const auto &[key, unused] : previous->second.factories) countries.insert(std::get<0>(key));
            for (const auto &[key, unused] : current->second.factories) countries.insert(std::get<0>(key));
            for (const auto &tag : countries) {
                if (!country.empty() && tag != country) continue;
                std::set<FactoryKey> previous_factories, current_factories;
                for (const auto &[key, unused] : previous->second.factories) {
                    if (std::get<0>(key) == tag) previous_factories.insert(key);
                }
                for (const auto &[key, unused] : current->second.factories) {
                    if (std::get<0>(key) == tag) current_factories.insert(key);
                }
                if (previous_factories != current_factories) {
                    *error = "factory set changed between dates " + std::to_string(previous->first)
                        + " and " + std::to_string(current->first) + " for " + tag;
                    return false;
                }
                if (current_factories.empty()) continue;

                const bool opening_complete = std::all_of(previous_factories.begin(), previous_factories.end(),
                    [&](const FactoryKey &key) { return previous->second.factories.at(key).input_flow_complete; });
                if (!opening_complete) {
                    if (previous != days.begin()) {
                        *error = "factory input flow is incomplete at date " + std::to_string(previous->first);
                        return false;
                    }
                    continue;
                }

                long double gross_output = 0.0L, intermediate_consumption = 0.0L;
                for (const auto &key : current_factories) {
                    const auto &opening = previous->second.factories.at(key);
                    const auto &closing = current->second.factories.at(key);
                    const auto output_price = current->second.price_raw.find(closing.output_good_ordinal);
                    if (output_price == current->second.price_raw.end()) {
                        *error = "missing output price at date " + std::to_string(current->first);
                        return false;
                    }
                    gross_output += static_cast<long double>(closing.output_raw)
                        * static_cast<long double>(output_price->second) / quantity_price_scale;
                    std::set<int64_t> input_goods;
                    for (const auto &[good, unused] : opening.input_flow_raw) input_goods.insert(good);
                    for (const auto &[good, unused] : closing.input_flow_raw) input_goods.insert(good);
                    for (const int64_t good : input_goods) {
                        const auto price = current->second.price_raw.find(good);
                        if (price == current->second.price_raw.end()) {
                            *error = "missing input price at date " + std::to_string(current->first);
                            return false;
                        }
                        const std::array<int64_t, 4> empty_flow{};
                        const auto opening_flow = opening.input_flow_raw.find(good);
                        const auto closing_flow = closing.input_flow_raw.find(good);
                        const auto &opening_values = opening_flow == opening.input_flow_raw.end() ? empty_flow : opening_flow->second;
                        const auto &values = closing_flow == closing.input_flow_raw.end() ? empty_flow : closing_flow->second;
                        if (!opening.input_no_purchase
                            && (opening_values[2] > (std::numeric_limits<int64_t>::max)() - opening_values[3]
                                || opening_values[1] > (std::numeric_limits<int64_t>::max)()
                                    - opening_values[2] - opening_values[3])) {
                            *error = "factory opening input flow overflows at date " + std::to_string(previous->first);
                            return false;
                        }
                        const int64_t opening_raw = opening.input_no_purchase ? opening_values[0]
                            : opening_values[1] + opening_values[2] + opening_values[3];
                        const bool closing_invalid = closing.input_no_purchase
                            ? values[1] != 0 || values[2] != 0 || values[3] != 0
                            : values[0] != values[1]
                                || values[2] > (std::numeric_limits<int64_t>::max)() - values[3]
                                || values[1] > (std::numeric_limits<int64_t>::max)() - values[2] - values[3];
                        if (closing_invalid) {
                            *error = "factory input flow does not reconcile at date " + std::to_string(current->first);
                            return false;
                        }
                        if (values[0] > opening_raw) {
                            *error = "factory input stock increased before settlement at date " + std::to_string(current->first);
                            return false;
                        }
                        intermediate_consumption += static_cast<long double>(opening_raw - values[0])
                            * static_cast<long double>(price->second) / quantity_price_scale;
                    }
                }
                const long double value_added = gross_output - intermediate_consumption;
                const std::string line = CsvText(summary.run_id) + ',' + std::to_string(previous->first) + ','
                    + std::to_string(current->first) + ',' + CsvText(tag) + ',' + std::to_string(current_factories.size()) + ','
                    + Decimal(gross_output) + ',' + Decimal(intermediate_consumption) + ',' + Decimal(value_added)
                    + ',' + CsvText("verified-runtime") + "\r\n";
                if (!Write(&destination, line, error)) return false;
                ++rows;
            }
        }
        if (rows == 0) { *error = country.empty() ? "trace contains no complete factory intervals" : "country has no complete factory intervals"; return false; }
        if (warning) *warning = summary.warning;
        return Publish(&destination, error);
    }

    bool ExportProducerSalesCsv(const fs::path &input, const fs::path &output,
                                const std::string &country, bool overwrite,
                                std::string *error, std::string *warning)
    {
        fs::path input_path, output_path;
        if (!AbsolutePath(input, &input_path, error) || !AbsolutePath(output, &output_path, error)) return false;
        if (!country.empty() && !IsTag(country)) { *error = "producer-sales country filter must be a normalized tag"; return false; }
        using SalesKey = std::tuple<int, std::string, std::string, int64_t, int64_t, int64_t, std::string>;
        struct SalesRow {
            bool summary_seen = false, complete = false, quantity_seen = false, revenue_seen = false;
            uint64_t sequence = 0;
            std::string quality;
            int64_t output_good = -1, opening = 0, produced = 0, sold = 0, closing = 0, proceeds = 0;
            int64_t domestic_fraction = -1, export_fraction = -1;
        };
        std::map<SalesKey, SalesRow> rows;
        std::set<std::string> event_families, healthy_families;
        uint64_t terminal_health_sequence = 0;
        size_t complete_rows = 0;
        std::optional<int> active_date;
        Summary summary;
        Output destination;
        if (!StartOutput(input_path, output_path, L".csv", overwrite, &destination, error)) return false;
        if (!Write(&destination, "run_id,sequence,game_date_raw,producer_family,country_tag,state_id,province_id,pop_id,producer_key,output_good_ordinal,opening_inventory_raw,produced_raw,sold_raw,closing_inventory_raw,proceeds_raw,percent_sold_domestic_raw,percent_sold_export_raw,quality\r\n", error)) return false;
        const auto flush_rows = [&](std::string *flush_error) {
            for (const auto &[key, row] : rows) {
                if (!row.summary_seen
                    || (row.complete && (!row.quantity_seen || !row.revenue_seen))
                    || (!row.complete && (row.quantity_seen || row.revenue_seen))) {
                    *flush_error = "producer sales summary and detail records are inconsistent";
                    return false;
                }
                if (!row.complete) continue;
                const auto &[date, family, tag, state_id, province_id, pop_id, producer_key] = key;
                const auto optional_integer = [](int64_t value) {
                    return value < 0 ? std::string{} : std::to_string(value);
                };
                const std::string line = CsvText(summary.run_id) + ',' + std::to_string(row.sequence) + ','
                    + std::to_string(date) + ',' + CsvText(family) + ',' + CsvText(tag) + ','
                    + optional_integer(state_id) + ',' + optional_integer(province_id) + ',' + optional_integer(pop_id)
                    + ',' + CsvText(producer_key) + ',' + std::to_string(row.output_good) + ','
                    + std::to_string(row.opening) + ',' + std::to_string(row.produced) + ',' + std::to_string(row.sold)
                    + ',' + std::to_string(row.closing) + ',' + std::to_string(row.proceeds) + ','
                    + optional_integer(row.domestic_fraction) + ',' + optional_integer(row.export_fraction)
                    + ',' + CsvText(row.quality) + "\r\n";
                if (!Write(&destination, line, flush_error)) return false;
                ++complete_rows;
            }
            rows.clear();
            return true;
        };
        if (!Stream(input_path, &summary, [&](const Record &record, std::string *visitor_error) {
            if (record.event == "telemetry.family.summary") {
                const auto *family = Entity(record, "family");
                if (family == nullptr || family->kind != JsonKind::String) return true;
                if (family->text != "state.factory" && family->text != "province.rgo"
                    && family->text != "pop.artisan") return true;
                int64_t dropped = -1, invalid = -1;
                if (record.category != "lifecycle" || record.mapping_id != "v2game-3.04"
                    || record.quality != "verified-current" || record.game_date_raw
                    || !IntegerField(record, "dropped", &dropped) || dropped != 0
                    || !IntegerField(record, "invalid", &invalid) || invalid != 0
                    || !healthy_families.insert(family->text).second) {
                    *visitor_error = "producer sales family health is missing, duplicated, or unhealthy";
                    return false;
                }
                return true;
            }
            if (record.event == "telemetry.summary") {
                return CaptureTerminalHealth(record, &terminal_health_sequence, visitor_error);
            }
            std::string family;
            if (record.event.rfind("state.factory.sales.", 0) == 0) family = "state.factory";
            else if (record.event.rfind("province.rgo.sales.", 0) == 0) family = "province.rgo";
            else if (record.event.rfind("pop.artisan.sales.", 0) == 0) family = "pop.artisan";
            else return true;
            event_families.insert(family);
            if (record.category != "state" || record.mapping_id != "v2game-3.04"
                || record.quality != "provisional" || !record.game_date_raw) {
                *visitor_error = record.event + " has an unsupported envelope";
                return false;
            }
            std::string tag;
            if (!CountryEntity(record, &tag, visitor_error)) return false;
            if (!country.empty() && tag != country) return true;
            if (active_date && *active_date != *record.game_date_raw) {
                if (!flush_rows(visitor_error)) return false;
            }
            active_date = *record.game_date_raw;
            int64_t state_id = -1, province_id = -1, pop_id = -1;
            std::string producer_key;
            if (family == "state.factory") {
                const auto *type = Entity(record, "factory_type");
                if (!IntegerEntity(record, "state_id", &state_id) || state_id < 0
                    || type == nullptr || type->kind != JsonKind::String || !IsIdentifier(type->text)) {
                    *visitor_error = record.event + " has invalid factory identity";
                    return false;
                }
                producer_key = type->text;
            } else if (family == "province.rgo") {
                if (!IntegerEntity(record, "province_id", &province_id) || province_id < 0) {
                    *visitor_error = record.event + " has invalid RGO identity";
                    return false;
                }
            } else if (!IntegerEntity(record, "province_id", &province_id) || province_id < 0
                || !IntegerEntity(record, "pop_id", &pop_id) || pop_id < 0) {
                *visitor_error = record.event + " has invalid artisan identity";
                return false;
            }
            auto &row = rows[{*record.game_date_raw, family, tag, state_id, province_id, pop_id, producer_key}];
            row.quality = record.quality;
            const auto has_suffix = [&](std::string_view suffix) {
                return record.event.size() >= suffix.size()
                    && record.event.compare(record.event.size() - suffix.size(), suffix.size(), suffix) == 0;
            };
            if (has_suffix(".summary")) {
                bool settlement_seen = false, complete = false;
                if (row.summary_seen || !BooleanField(record, "settlement_seen", &settlement_seen)
                    || !BooleanField(record, "complete", &complete)) {
                    *visitor_error = record.event + " is malformed or duplicated";
                    return false;
                }
                if (family == "state.factory") {
                    int64_t settlement_count = -1;
                    if (!IntegerField(record, "settlement_count", &settlement_count) || settlement_count < 0
                        || settlement_seen != (settlement_count != 0) || (complete && settlement_count != 1)) {
                        *visitor_error = "state.factory.sales.summary does not reconcile";
                        return false;
                    }
                } else {
                    bool opening_inventory_seen = false;
                    if (!settlement_seen
                        || !BooleanField(record, "opening_inventory_seen", &opening_inventory_seen)
                        || (complete && !opening_inventory_seen)) {
                        *visitor_error = record.event + " has an invalid inventory-chain summary";
                        return false;
                    }
                }
                row.summary_seen = true;
                row.complete = complete;
                row.sequence = record.sequence;
                return true;
            }
            if (has_suffix(".quantity")) {
                if (row.quantity_seen || !IntegerField(record, "output_good_ordinal", &row.output_good)
                    || row.output_good < 0 || row.output_good >= 64
                    || !IntegerField(record, "opening_inventory_raw", &row.opening) || row.opening < 0
                    || !IntegerField(record, "produced_raw", &row.produced) || row.produced < 0
                    || !IntegerField(record, "sold_raw", &row.sold) || row.sold < 0
                    || !IntegerField(record, "closing_inventory_raw", &row.closing) || row.closing < 0
                    || row.opening > (std::numeric_limits<int64_t>::max)() - row.produced
                    || row.closing > row.opening + row.produced
                    || row.sold != row.opening + row.produced - row.closing) {
                    *visitor_error = record.event + " is malformed, duplicated, or unreconciled";
                    return false;
                }
                row.quantity_seen = true;
                return true;
            }
            if (!has_suffix(".revenue")) {
                *visitor_error = "unsupported producer sales event";
                return false;
            }
            if (row.revenue_seen || !IntegerField(record, "proceeds_raw", &row.proceeds) || row.proceeds < 0) {
                *visitor_error = record.event + " is malformed or duplicated";
                return false;
            }
            if (family != "state.factory"
                && (!IntegerField(record, "percent_sold_domestic_raw", &row.domestic_fraction)
                    || row.domestic_fraction < 0 || row.domestic_fraction > 32768
                    || !IntegerField(record, "percent_sold_export_raw", &row.export_fraction)
                    || row.export_fraction < 0 || row.export_fraction > 32768)) {
                *visitor_error = record.event + " has invalid market fractions";
                return false;
            }
            row.revenue_seen = true;
            return true;
        }, error) || !flush_rows(error)) return false;
        if (!summary.warning.empty() || summary.gaps != 0 || summary.date_regressed) {
            *error = "producer sales requires a complete trace without sequence or date gaps";
            return false;
        }
        if (event_families.empty()) { *error = "trace contains no producer sales records"; return false; }
        if (!std::includes(healthy_families.begin(), healthy_families.end(),
                event_families.begin(), event_families.end()) || terminal_health_sequence == 0
            || !summary.last_sequence || terminal_health_sequence != *summary.last_sequence) {
            *error = "producer sales requires terminal health for every captured producer family and the writer";
            return false;
        }
        if (complete_rows == 0) { *error = "trace contains no complete producer sales intervals"; return false; }
        if (warning) *warning = summary.warning;
        return Publish(&destination, error);
    }

    bool ExportCountryGdpCsv(const fs::path &input, const fs::path &output,
                             const std::string &country, std::optional<int> base_date,
                             std::optional<double> gold_to_cash_rate, bool overwrite,
                             std::string *error, std::string *warning)
    {
        fs::path input_path, output_path;
        if (!AbsolutePath(input, &input_path, error) || !AbsolutePath(output, &output_path, error)) return false;
        if (!country.empty() && !IsTag(country)) { *error = "GDP country filter must be a normalized tag"; return false; }
        std::map<int, GdpDay> days;
        std::set<std::string> family_health;
        std::map<std::string, int64_t> family_polls;
        std::map<std::string, CaptureRuleMetadata> capture_metadata;
        uint64_t terminal_health_sequence = 0;
        Summary summary;
        if (!Stream(input_path, &summary, [&](const Record &record, std::string *visitor_error) {
            if (record.event == "telemetry.capture.rule" || record.event == "telemetry.capture.field"
                || record.event == "telemetry.capture.country") {
                return CaptureGdpMetadata(record, &capture_metadata, visitor_error);
            }
            if (record.event == "telemetry.family.summary") {
                return CaptureGdpFamilyHealth(record, &family_health, &family_polls, visitor_error);
            }
            if (record.event == "telemetry.summary") {
                return CaptureTerminalHealth(record, &terminal_health_sequence, visitor_error);
            }
            static const std::set<std::string> events = {
                "world.market.price", "state.factory.production", "state.factory.input.flow.summary",
                "state.factory.input.flow", "province.rgo.identity", "province.rgo.production",
                "province.rgo.finance", "pop.artisan.identity", "pop.artisan.production",
                "pop.artisan.input", "pop.artisan.inactive", "pop.aggregate"};
            if (events.find(record.event) == events.end()) return true;
            if (!record.game_date_raw) { *visitor_error = record.event + " is missing game_date_raw"; return false; }
            auto &day = days[*record.game_date_raw];
            if (record.event == "world.market.price") return CaptureMarketPrice(record, &day, visitor_error);
            if (record.event == "state.factory.production") return CaptureFactoryProduction(record, &day, visitor_error);
            if (record.event == "state.factory.input.flow.summary") {
                return CaptureFactoryInputFlowSummary(record, &day, visitor_error);
            }
            if (record.event == "state.factory.input.flow") return CaptureFactoryInputFlow(record, &day, visitor_error);
            if (record.event == "province.rgo.identity") return CaptureRgoIdentity(record, &day, visitor_error);
            if (record.event == "province.rgo.production") return CaptureRgoProduction(record, &day, visitor_error);
            if (record.event == "province.rgo.finance") return CaptureRgoFinance(record, &day, visitor_error);
            if (record.event == "pop.artisan.identity") return CaptureArtisanIdentity(record, &day, visitor_error);
            if (record.event == "pop.artisan.production") return CaptureArtisanProduction(record, &day, visitor_error);
            if (record.event == "pop.artisan.input") return CaptureArtisanInput(record, &day, visitor_error);
            if (record.event == "pop.artisan.inactive") return CaptureInactiveArtisan(record, &day, visitor_error);
            return CapturePopulation(record, &day, visitor_error);
        }, error) || !HealthyFactoryTrace(summary, error)) return false;
        static const std::set<std::string> expected_health = {
            "world.market", "state.factory", "province.rgo", "pop.artisan", "pop.aggregate"};
        if (family_health != expected_health || terminal_health_sequence == 0
            || !summary.last_sequence || terminal_health_sequence != *summary.last_sequence) {
            *error = "country GDP requires terminal market, producer, population, and writer health summaries";
            return false;
        }
        if (days.size() < 3) { *error = "country GDP requires at least three daily snapshots"; return false; }
        if (!ValidateGdpMetadata(capture_metadata, family_polls, days.size(), error)) return false;
        for (auto current = std::next(days.begin()); current != days.end(); ++current) {
            if (current->first - std::prev(current)->first != 24) {
                *error = "country GDP requires consecutive daily snapshots";
                return false;
            }
        }
        const int base = base_date.value_or(days.begin()->first);
        const auto base_day = days.find(base);
        if (base_day == days.end()) { *error = "GDP base date has no daily snapshot"; return false; }
        if (base_day->second.price_raw.empty()) { *error = "GDP base date has no market prices"; return false; }
        for (const auto &[date, day] : days) {
            for (const auto &[key, factory] : day.factories) {
                if (!factory.production_seen || !factory.input_flow_summary_seen) {
                    *error = "factory snapshot is incomplete at date " + std::to_string(date);
                    return false;
                }
            }
            for (const auto &[key, artisan] : day.artisans) {
                if (!artisan.inactive && (!artisan.identity_seen || !artisan.production_seen)) {
                    *error = "artisan snapshot is incomplete at date " + std::to_string(date);
                    return false;
                }
            }
            for (const auto &[key, rgo] : day.rgos) {
                if (!rgo.identity_seen || !rgo.production_seen) {
                    *error = "RGO snapshot is incomplete at date " + std::to_string(date);
                    return false;
                }
            }
        }

        Output destination;
        if (!StartOutput(input_path, output_path, L".csv", overwrite, &destination, error)) return false;
        if (!Write(&destination,
                "run_id,opening_date_raw,game_date_raw,base_date_raw,gold_to_cash_rate,country_tag,population,"
                "factory_gross_output,factory_intermediate_consumption,factory_value_added,factory_real_value_added,"
                "rgo_value_added,rgo_real_value_added,precious_metal_value_added,"
                "artisan_gross_output,artisan_intermediate_consumption,artisan_value_added,artisan_real_value_added,"
                "nominal_gdp,real_gdp,nominal_gdp_per_capita,real_gdp_per_capita,quality\r\n", error)) return false;

        size_t rows = 0;
        for (auto current = std::next(days.begin(), 2); current != days.end(); ++current) {
            const auto opening = std::prev(current);
            std::set<std::string> countries;
            for (const auto &[tag, unused] : current->second.population) countries.insert(tag);
            if (!country.empty()) countries = {country};
            for (const auto &tag : countries) {
                for (const char *family : {"state.factory", "province.rgo", "pop.artisan", "pop.aggregate"}) {
                    const auto &rule = capture_metadata.at(family);
                    if (rule.country_filter_count != 0 && rule.countries.find(tag) == rule.countries.end()) {
                        *error = "country GDP capture scope does not include " + tag + " for " + family;
                        return false;
                    }
                }
                const auto population = current->second.population.find(tag);
                if (population == current->second.population.end() || population->second <= 0) {
                    *error = "country GDP requires a positive population at date " + std::to_string(current->first)
                        + " for " + tag;
                    return false;
                }
                ProducerValues factories, artisans;
                if (!ValueProducerSet(opening->second.factories, current->second.factories,
                        current->second.price_raw, base_day->second.price_raw, tag, current->first,
                        "factory", &factories, error)
                    || !ValueArtisans(current->second, base_day->second.price_raw, tag,
                        current->first, &artisans, error)) return false;
                long double rgo_nominal = 0.0L, rgo_real = 0.0L, precious = 0.0L;
                if (!ValueRgos(current->second, base_day->second.price_raw, gold_to_cash_rate,
                        tag, current->first,
                        &rgo_nominal, &rgo_real, &precious, error)) return false;
                const long double factory_va = factories.nominal_gross - factories.nominal_inputs;
                const long double factory_real_va = factories.real_gross - factories.real_inputs;
                const long double artisan_va = artisans.nominal_gross - artisans.nominal_inputs;
                const long double artisan_real_va = artisans.real_gross - artisans.real_inputs;
                const long double nominal = factory_va + rgo_nominal + artisan_va;
                const long double real = factory_real_va + rgo_real + artisan_real_va;
                const long double denominator = static_cast<long double>(population->second);
                const std::string line = CsvText(summary.run_id) + ',' + std::to_string(opening->first) + ','
                    + std::to_string(current->first) + ',' + std::to_string(base) + ','
                    + (gold_to_cash_rate ? Decimal(*gold_to_cash_rate) : std::string{}) + ',' + CsvText(tag) + ','
                    + std::to_string(population->second) + ',' + Decimal(factories.nominal_gross) + ','
                    + Decimal(factories.nominal_inputs) + ',' + Decimal(factory_va) + ','
                    + Decimal(factory_real_va) + ',' + Decimal(rgo_nominal) + ',' + Decimal(rgo_real) + ','
                    + Decimal(precious) + ',' + Decimal(artisans.nominal_gross) + ','
                    + Decimal(artisans.nominal_inputs) + ',' + Decimal(artisan_va) + ','
                    + Decimal(artisan_real_va) + ',' + Decimal(nominal) + ',' + Decimal(real) + ','
                    + Decimal(nominal / denominator) + ',' + Decimal(real / denominator) + ','
                    + CsvText("verified-runtime") + "\r\n";
                if (!Write(&destination, line, error)) return false;
                ++rows;
            }
        }
        if (rows == 0) { *error = "trace contains no complete country GDP intervals"; return false; }
        if (warning) *warning = summary.warning;
        return Publish(&destination, error);
    }

    bool IsBenchmarkFailureReason(const std::string &reason)
    {
        constexpr std::array<std::string_view, 10> reasons = {"timeout", "date_overshoot", "idler_unavailable",
            "invalid_pause_state", "pause_failed", "observer_invariant_failed", "date_regressed", "unexpected_pause",
            "timer_unavailable", "invalid_target"};
        return std::find(reasons.begin(), reasons.end(), reason) != reasons.end();
    }

    bool VerifyBenchmark(const Summary &summary, const BenchmarkExpectation &expectation, std::string *error)
    {
        if (error == nullptr) return false;
        auto fail = [&](const char *message) { *error = message; return false; };
        if (!summary.warning.empty()) return fail("benchmark trace has an incomplete final record");
        if (summary.gaps != 0) return fail("benchmark trace has sequence gaps");
        if (summary.progress_seen) {
            const auto dropped = summary.progress.find("dropped");
            const auto write_failed = summary.progress.find("write_failed");
            int64_t dropped_count = -1;
            if (dropped == summary.progress.end() || dropped->second.kind != JsonKind::Number
                || !ParseIntegerText(dropped->second.text, &dropped_count) || dropped_count != 0) {
                return fail("benchmark trace reports dropped records or invalid drop accounting");
            }
            if (write_failed == summary.progress.end() || write_failed->second.kind != JsonKind::Boolean
                || write_failed->second.text != "false") {
                return fail("benchmark trace reports a writer failure or invalid writer status");
            }
        }
        const auto &benchmark = summary.benchmark;
        if (expectation.status == BenchmarkStatus::Completed) {
            if (!expectation.reason.empty()) return fail("completed benchmark expectation cannot include a failure reason");
            if (benchmark.status != "completed" || !benchmark.start_date_raw || !benchmark.target_date_raw
                || !benchmark.actual_date_raw || !benchmark.game_days || !benchmark.elapsed_us || !benchmark.paused
                || !*benchmark.paused || *benchmark.actual_date_raw != *benchmark.target_date_raw
                || *benchmark.target_date_raw <= *benchmark.start_date_raw || *benchmark.game_days < 1
                || *benchmark.elapsed_us == 0 || !benchmark.reason.empty()) {
                return fail("trace does not contain a valid completed benchmark");
            }
            if (!summary.progress_seen) return fail("completed benchmark has no telemetry progress accounting");
            if (expectation.game_days && *benchmark.game_days != *expectation.game_days) {
                return fail("completed benchmark game-day count does not match the expectation");
            }
            return true;
        }
        if (expectation.game_days) return fail("failed benchmark expectation cannot include a game-day count");
        if (expectation.reason.empty()) return fail("failed benchmark expectation requires a reason");
        if (benchmark.status != "failed" || benchmark.reason != expectation.reason || !benchmark.start_date_raw
            || !benchmark.target_date_raw || !benchmark.elapsed_us || *benchmark.elapsed_us == 0) {
            return fail("trace does not contain the expected failed benchmark");
        }
        if (benchmark.reason == "date_overshoot" && !summary.progress_seen) {
            return fail("overshot benchmark has no telemetry progress accounting");
        }
        return true;
    }

    std::string FormatBenchmarkVerification(const Summary &summary)
    {
        const auto value = [](const auto &field) { return field ? std::to_string(*field) : "unavailable"; };
        const auto metric = [&](const char *key) {
            const auto found = summary.progress.find(key);
            return found == summary.progress.end() ? std::string("unavailable") : found->second.text;
        };
        const auto &benchmark = summary.benchmark;
        std::ostringstream output;
        output << "benchmark_verified status=" << benchmark.status
               << " start=" << value(benchmark.start_date_raw)
               << " target=" << value(benchmark.target_date_raw)
               << " actual=" << value(benchmark.actual_date_raw)
               << " game_days=" << value(benchmark.game_days)
               << " elapsed_us=" << value(benchmark.elapsed_us)
               << " process_cpu_us=" << value(benchmark.process_cpu_us)
               << " working_set_end_bytes=" << value(benchmark.working_set_end_bytes)
               << " private_bytes_end=" << value(benchmark.private_bytes_end)
               << " process_peak_working_set_bytes=" << value(benchmark.process_peak_working_set_bytes)
               << " reason=" << (benchmark.reason.empty() ? "unavailable" : benchmark.reason)
               << " paused=" << (benchmark.paused ? (*benchmark.paused ? "true" : "false") : "unavailable")
               << " gaps=" << summary.gaps
               << " dropped=" << metric("dropped")
               << " write_failed=" << metric("write_failed");
        return output.str();
    }

    std::string FormatSummary(const Summary &summary)
    {
        auto metric = [&](const char *key) { const auto found = summary.progress.find(key); return found == summary.progress.end() ? "unavailable" : found->second.text; };
        std::ostringstream output;
        output << "run_id=" << (summary.run_id.empty() ? "unavailable" : summary.run_id)
               << " first_sequence=" << (summary.first_sequence ? std::to_string(*summary.first_sequence) : "unavailable")
               << " last_sequence=" << (summary.last_sequence ? std::to_string(*summary.last_sequence) : "unavailable")
               << " records=" << summary.records << " gaps=" << summary.gaps
               << " first_date=" << (summary.first_date ? std::to_string(*summary.first_date) : "unavailable")
               << " last_date=" << (summary.last_date ? std::to_string(*summary.last_date) : "unavailable")
               << " game_date_span_days=" << (!summary.date_regressed && summary.first_date
                    ? std::to_string((static_cast<int64_t>(*summary.last_date) - *summary.first_date) / raw_date_units_per_day)
                    : "unavailable")
               << " elapsed_us=";
        if (summary.first_monotonic_us && summary.last_monotonic_us && *summary.last_monotonic_us >= *summary.first_monotonic_us) output << *summary.last_monotonic_us - *summary.first_monotonic_us; else output << "unavailable";
        output << " game_days_per_sec=";
        if (!summary.date_regressed && summary.first_date && summary.last_date && summary.first_date_monotonic_us
            && summary.last_date_monotonic_us && *summary.last_date_monotonic_us > *summary.first_date_monotonic_us) {
            output << (static_cast<int64_t>(*summary.last_date) - *summary.first_date) / raw_date_units_per_day
                * 1000000.0 / (*summary.last_date_monotonic_us - *summary.first_date_monotonic_us);
        } else output << "unavailable";
        const auto benchmark_value = [](const auto &value) { return value ? std::to_string(*value) : "unavailable"; };
        const auto benchmark_delta = [](const std::optional<uint64_t> &start, const std::optional<uint64_t> &end) {
            if (!start || !end) return std::string("unavailable");
            return *end >= *start ? std::to_string(*end - *start) : "-" + std::to_string(*start - *end);
        };
        output << " lifecycle_save_load_us=" << benchmark_value(summary.lifecycle.save_load_us)
               << " lifecycle_campaign_enter_us=" << benchmark_value(summary.lifecycle.campaign_enter_us)
               << " lifecycle_observer_configure_us=" << benchmark_value(summary.lifecycle.observer_configure_us)
               << " benchmark_status=" << summary.benchmark.status
               << " benchmark_start=" << benchmark_value(summary.benchmark.start_date_raw)
               << " benchmark_target=" << benchmark_value(summary.benchmark.target_date_raw)
               << " benchmark_actual=" << benchmark_value(summary.benchmark.actual_date_raw)
               << " benchmark_game_days=" << benchmark_value(summary.benchmark.game_days)
               << " benchmark_elapsed_us=" << benchmark_value(summary.benchmark.elapsed_us)
               << " benchmark_game_days_per_sec=";
        if (summary.benchmark.status == "completed" && summary.benchmark.game_days && summary.benchmark.elapsed_us && *summary.benchmark.elapsed_us > 0) output << *summary.benchmark.game_days * 1000000.0 / *summary.benchmark.elapsed_us; else output << "unavailable";
        output << " benchmark_process_cpu_us=" << benchmark_value(summary.benchmark.process_cpu_us)
               << " benchmark_process_cpu_percent=";
        if (summary.benchmark.process_cpu_us && summary.benchmark.elapsed_us && *summary.benchmark.elapsed_us > 0) output << *summary.benchmark.process_cpu_us * 100.0 / *summary.benchmark.elapsed_us; else output << "unavailable";
        output << " benchmark_working_set_start_bytes=" << benchmark_value(summary.benchmark.working_set_start_bytes)
               << " benchmark_working_set_end_bytes=" << benchmark_value(summary.benchmark.working_set_end_bytes)
               << " benchmark_working_set_delta_bytes=" << benchmark_delta(summary.benchmark.working_set_start_bytes, summary.benchmark.working_set_end_bytes)
               << " benchmark_private_start_bytes=" << benchmark_value(summary.benchmark.private_bytes_start)
               << " benchmark_private_end_bytes=" << benchmark_value(summary.benchmark.private_bytes_end)
               << " benchmark_private_delta_bytes=" << benchmark_delta(summary.benchmark.private_bytes_start, summary.benchmark.private_bytes_end)
               << " benchmark_process_peak_working_set_bytes=" << benchmark_value(summary.benchmark.process_peak_working_set_bytes)
               << " benchmark_reason=" << (summary.benchmark.reason.empty() ? "unavailable" : summary.benchmark.reason)
               << " benchmark_paused=" << (summary.benchmark.paused ? (*summary.benchmark.paused ? "true" : "false") : "unavailable");
        for (const char *key : {"accepted", "written", "dropped", "high_water", "write_failed", "callback_enqueue_format_us_total", "callback_enqueue_format_us_mean", "callback_count"}) output << ' ' << key << '=' << metric(key);
        for (const auto &[key, value] : summary.events) output << " event." << key << '=' << value;
        for (const auto &[key, value] : summary.categories) output << " category." << key << '=' << value;
        for (const auto &[key, value] : summary.qualities) output << " quality." << key << '=' << value;
        if (!summary.warning.empty()) output << " warning=" << summary.warning;
        return output.str();
    }

    std::string FormatCompare(const Summary &left, const Summary &right)
    {
        auto elapsed = [](const Summary &summary) -> std::string {
            if (!summary.first_monotonic_us || !summary.last_monotonic_us || *summary.last_monotonic_us < *summary.first_monotonic_us) return "unavailable";
            return std::to_string(*summary.last_monotonic_us - *summary.first_monotonic_us);
        };
        auto date_span = [](const Summary &summary) -> std::string {
            if (summary.date_regressed || !summary.first_date || !summary.last_date) return "unavailable";
            return std::to_string((static_cast<int64_t>(*summary.last_date) - *summary.first_date) / raw_date_units_per_day);
        };
        auto rate = [](const Summary &summary) -> std::string {
            if (summary.date_regressed || !summary.first_date || !summary.last_date || !summary.first_date_monotonic_us
                || !summary.last_date_monotonic_us || *summary.last_date_monotonic_us <= *summary.first_date_monotonic_us) return "unavailable";
            return std::to_string((static_cast<int64_t>(*summary.last_date) - *summary.first_date) / raw_date_units_per_day
                * 1000000.0 / (*summary.last_date_monotonic_us - *summary.first_date_monotonic_us));
        };
        auto number = [](const std::string &value, double *result) { char *end = nullptr; *result = std::strtod(value.c_str(), &end); return end && !*end && std::isfinite(*result); };
        auto metric = [&](const Summary &summary, const char *key) { const auto found = summary.progress.find(key); return found == summary.progress.end() ? std::string("unavailable") : found->second.text; };
        auto benchmark_metric = [](const Summary &summary, const char *key) {
            if (summary.benchmark.status != "completed") return std::string("unavailable");
            const std::string name(key);
            if (name == "benchmark_game_days") return summary.benchmark.game_days ? std::to_string(*summary.benchmark.game_days) : "unavailable";
            if (name == "benchmark_elapsed_us") return summary.benchmark.elapsed_us ? std::to_string(*summary.benchmark.elapsed_us) : "unavailable";
            if (name == "benchmark_process_cpu_us") return summary.benchmark.process_cpu_us ? std::to_string(*summary.benchmark.process_cpu_us) : "unavailable";
            if (name == "benchmark_working_set_end_bytes") return summary.benchmark.working_set_end_bytes ? std::to_string(*summary.benchmark.working_set_end_bytes) : "unavailable";
            if (name == "benchmark_private_end_bytes") return summary.benchmark.private_bytes_end ? std::to_string(*summary.benchmark.private_bytes_end) : "unavailable";
            if (name == "benchmark_process_peak_working_set_bytes") return summary.benchmark.process_peak_working_set_bytes ? std::to_string(*summary.benchmark.process_peak_working_set_bytes) : "unavailable";
            if (name == "benchmark_process_cpu_percent") {
                if (!summary.benchmark.process_cpu_us || !summary.benchmark.elapsed_us || *summary.benchmark.elapsed_us == 0) return std::string("unavailable");
                return std::to_string(*summary.benchmark.process_cpu_us * 100.0 / *summary.benchmark.elapsed_us);
            }
            if (!summary.benchmark.game_days || !summary.benchmark.elapsed_us || *summary.benchmark.elapsed_us == 0) return std::string("unavailable");
            return std::to_string(*summary.benchmark.game_days * 1000000.0 / *summary.benchmark.elapsed_us);
        };
        auto lifecycle_metric = [](const Summary &summary, const char *key) {
            const std::string name(key);
            const std::optional<uint64_t> *value = nullptr;
            if (name == "lifecycle_save_load_us") value = &summary.lifecycle.save_load_us;
            else if (name == "lifecycle_campaign_enter_us") value = &summary.lifecycle.campaign_enter_us;
            else if (name == "lifecycle_observer_configure_us") value = &summary.lifecycle.observer_configure_us;
            if (value == nullptr) return std::string("unavailable");
            return *value ? std::to_string(**value) : std::string("unavailable");
        };
        auto lifecycle_delta = [](const Summary &left_summary, const Summary &right_summary, const char *key) {
            const std::string name(key);
            const std::optional<uint64_t> *left_value = nullptr, *right_value = nullptr;
            if (name == "lifecycle_save_load_us") {
                left_value = &left_summary.lifecycle.save_load_us;
                right_value = &right_summary.lifecycle.save_load_us;
            } else if (name == "lifecycle_campaign_enter_us") {
                left_value = &left_summary.lifecycle.campaign_enter_us;
                right_value = &right_summary.lifecycle.campaign_enter_us;
            } else if (name == "lifecycle_observer_configure_us") {
                left_value = &left_summary.lifecycle.observer_configure_us;
                right_value = &right_summary.lifecycle.observer_configure_us;
            }
            if (left_value == nullptr || !*left_value || !*right_value) return std::string("unavailable");
            return **right_value >= **left_value ? std::to_string(**right_value - **left_value)
                : "-" + std::to_string(**left_value - **right_value);
        };
        auto comparable_metric = [&](const Summary &summary, const char *key) {
            const std::string name(key);
            if (name.rfind("benchmark_", 0) == 0) return benchmark_metric(summary, key);
            if (name.rfind("lifecycle_", 0) == 0) return lifecycle_metric(summary, key);
            if (name == "records") return std::to_string(summary.records);
            if (name == "gaps") return std::to_string(summary.gaps);
            if (name == "game_date_span_days") return date_span(summary);
            if (name == "elapsed_us") return elapsed(summary);
            if (name == "game_days_per_sec") return rate(summary);
            return metric(summary, key);
        };
        std::ostringstream output;
        output << "left_run_id=" << left.run_id << " right_run_id=" << right.run_id << '\n';
        for (const char *key : {"records", "gaps", "game_date_span_days", "elapsed_us", "game_days_per_sec",
             "lifecycle_save_load_us", "lifecycle_campaign_enter_us", "lifecycle_observer_configure_us",
             "benchmark_game_days", "benchmark_elapsed_us", "benchmark_game_days_per_sec", "benchmark_process_cpu_us",
             "benchmark_process_cpu_percent", "benchmark_working_set_end_bytes", "benchmark_private_end_bytes",
             "benchmark_process_peak_working_set_bytes", "accepted", "written", "dropped", "high_water", "write_failed",
             "callback_enqueue_format_us_total", "callback_enqueue_format_us_mean", "callback_count"}) {
            const std::string left_value = comparable_metric(left, key);
            const std::string right_value = comparable_metric(right, key);
            double a = 0, b = 0;
            const bool boolean = std::string(key) == "write_failed";
            const bool lifecycle = std::string(key).rfind("lifecycle_", 0) == 0;
            output << key << " " << left_value << " | " << right_value << " | "
                   << (lifecycle ? lifecycle_delta(left, right, key)
                       : !boolean && number(left_value, &a) && number(right_value, &b)
                           ? std::to_string(b - a) : "unavailable") << '\n';
        }
        return output.str();
    }
}
