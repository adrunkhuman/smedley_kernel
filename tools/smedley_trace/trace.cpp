#include "trace.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>

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

        bool CsvCountry(const Record &record, std::string *line, std::string *error)
        {
            const auto country = record.entities.find("country_tag"), raw = record.payload.find("treasury_raw"), treasury = record.payload.find("treasury");
            int64_t raw_value = 0;
            char *end = nullptr;
            if (record.event != "country.daily" || record.category != "state" || !record.game_date_raw || country == record.entities.end() || country->second.kind != JsonKind::String || !IsTag(country->second.text) || raw == record.payload.end() || raw->second.kind != JsonKind::Number || !ParseIntegerText(raw->second.text, &raw_value) || treasury == record.payload.end() || treasury->second.kind != JsonKind::Number) { *error = "country.daily has an unsupported schema"; return false; }
            const double actual = std::strtod(treasury->second.text.c_str(), &end);
            const double expected = static_cast<double>(raw_value) / 32768.0;
            if (!end || *end || !std::isfinite(actual) || std::fabs(actual - expected) > 0.000001 + std::fabs(expected) * 1e-9) { *error = "country.daily treasury values disagree"; return false; }
            auto text = [](std::string value) { if (!value.empty() && std::strchr("=+-@", value.front())) value.insert(value.begin(), '\''); std::string quoted = "\""; for (char character : value) quoted += character == '\"' ? "\"\"" : std::string(1, character); return quoted + "\""; };
            *line = text(record.run_id) + ',' + std::to_string(record.sequence) + ',' + text(record.wall_time_utc) + ',' + std::to_string(record.monotonic_us) + ',' + std::to_string(*record.game_date_raw) + ',' + text(record.quality) + ',' + text(country->second.text) + ',' + raw->second.text + ',' + treasury->second.text + "\r\n";
            return true;
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
