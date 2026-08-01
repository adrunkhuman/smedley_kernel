#include "telemetry_core.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include <windows.h>

namespace smedley::telemetry
{
    namespace
    {
        bool IsValidUtf8(std::string_view value, size_t index, size_t *length)
        {
            const unsigned char first = static_cast<unsigned char>(value[index]);
            if (first < 0x80) {
                *length = 1;
                return true;
            }
            const size_t bytes = first >= 0xc2 && first <= 0xdf ? 2 : first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
            if (bytes == 0 || index + bytes > value.size()) return false;
            for (size_t offset = 1; offset < bytes; ++offset) {
                if ((static_cast<unsigned char>(value[index + offset]) & 0xc0) != 0x80) return false;
            }
            const unsigned char second = static_cast<unsigned char>(value[index + 1]);
            if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0)
                || (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90)) return false;
            *length = bytes;
            return true;
        }

        bool IsSafeRunId(std::string_view value)
        {
            return !value.empty() && value.size() <= 64 && std::all_of(value.begin(), value.end(), [](unsigned char character) {
                return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
                    || (character >= '0' && character <= '9') || character == '-';
            });
        }

        bool IsKnownCategory(std::string_view category)
        {
            return category == "lifecycle" || category == "state";
        }

        bool IsCanonicalQuality(std::string_view quality)
        {
            return quality == "verified-runtime" || quality == "verified-current"
                || quality == "verified-static-callsites" || quality == "provisional"
                || quality == "historical-unverified" || quality == "historical-skeleton";
        }

        bool IsIdentifier(std::string_view value)
        {
            return !value.empty() && value.size() <= SMEDLEY_TELEMETRY_MAX_IDENTIFIER_BYTES
                && std::all_of(value.begin(), value.end(), [](unsigned char character) {
                    return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
                        || (character >= '0' && character <= '9') || character == '.' || character == '_'
                        || character == '-';
                });
        }

        bool IsValidText(const char *data, uint32_t length, size_t maximum, bool identifier)
        {
            if (length > maximum || (length != 0 && data == nullptr)) return false;
            const std::string_view value(data == nullptr ? "" : data, length);
            if (!identifier && length == 0) return true;
            if (identifier && !IsIdentifier(value)) return false;
            for (size_t index = 0; index < value.size();) {
                size_t character_length = 0;
                if (!IsValidUtf8(value, index, &character_length)) return false;
                index += character_length;
            }
            return true;
        }

        bool ValidateFields(const SmedleyTelemetryFieldV1 *fields, uint32_t count, std::string *error)
        {
            if (count > SMEDLEY_TELEMETRY_MAX_FIELDS || (count != 0 && fields == nullptr)) {
                *error = "invalid field array";
                return false;
            }
            for (uint32_t index = 0; index < count; ++index) {
                const auto &field = fields[index];
                if (field.struct_size != sizeof(field) || field.version != SMEDLEY_TELEMETRY_ABI_VERSION_V1
                    || field.reserved != 0 || !IsValidText(field.key, field.key_length,
                                                           SMEDLEY_TELEMETRY_MAX_IDENTIFIER_BYTES, true)) {
                    *error = "invalid field header or key";
                    return false;
                }
                if (field.type > SMEDLEY_TELEMETRY_UTF8_STRING
                    || (field.type == SMEDLEY_TELEMETRY_BOOL && field.value.bool_value > 1)
                    || (field.type == SMEDLEY_TELEMETRY_DOUBLE && !std::isfinite(field.value.double_value))
                    || (field.type == SMEDLEY_TELEMETRY_UTF8_STRING
                        && (field.value.string_value.reserved != 0
                            || !IsValidText(field.value.string_value.data, field.value.string_value.length,
                                            SMEDLEY_TELEMETRY_MAX_STRING_BYTES, false)))) {
                    *error = "invalid field value";
                    return false;
                }
                for (uint32_t previous = 0; previous < index; ++previous) {
                    if (field.key_length == fields[previous].key_length
                        && std::memcmp(field.key, fields[previous].key, field.key_length) == 0) {
                        *error = "duplicate field key";
                        return false;
                    }
                }
            }
            return true;
        }

        void AppendFields(std::string *json, const SmedleyTelemetryFieldV1 *fields, uint32_t count)
        {
            json->push_back('{');
            for (uint32_t index = 0; index < count; ++index) {
                if (index != 0) json->push_back(',');
                const auto &field = fields[index];
                json->append("\"");
                json->append(EscapeJson(std::string_view(field.key, field.key_length)));
                json->append("\":");
                switch (field.type) {
                case SMEDLEY_TELEMETRY_NULL: json->append("null"); break;
                case SMEDLEY_TELEMETRY_BOOL: json->append(field.value.bool_value ? "true" : "false"); break;
                case SMEDLEY_TELEMETRY_INT64: json->append(std::to_string(field.value.int64_value)); break;
                case SMEDLEY_TELEMETRY_DOUBLE: json->append(std::to_string(field.value.double_value)); break;
                case SMEDLEY_TELEMETRY_UTF8_STRING:
                    json->append("\"");
                    json->append(EscapeJson(std::string_view(field.value.string_value.data == nullptr ? "" : field.value.string_value.data,
                                                              field.value.string_value.length)));
                    json->append("\"");
                    break;
                }
            }
            json->push_back('}');
        }

        bool IsCountryTag(std::string_view tag)
        {
            return tag.size() == 3 && std::all_of(tag.begin(), tag.end(), [](unsigned char character) {
                return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
            });
        }

        bool IsJsonLinesPath(const std::filesystem::path &path)
        {
            auto extension = path.extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
            return extension == L".jsonl";
        }

        bool HasReparsePointParent(const std::filesystem::path &path)
        {
            std::filesystem::path current = path.root_path();
            for (const auto &part : path.relative_path()) {
                current /= part;
                const DWORD attributes = GetFileAttributesW(current.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES) break;
                if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return true;
            }
            return false;
        }

        bool ValidateOutputPath(const Config &config, std::string *error)
        {
            if (!IsJsonLinesPath(config.output_path)) {
                *error = "telemetry output must end in .jsonl";
                return false;
            }
            if (HasReparsePointParent(config.output_path)) {
                *error = "telemetry output must not use a reparse point";
                return false;
            }
            const DWORD attributes = GetFileAttributesW(config.output_path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES) {
                if ((attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
                    *error = "telemetry output must be a normal file";
                    return false;
                }
                if (!config.overwrite) {
                    *error = "telemetry output already exists; enable overwrite to replace it";
                    return false;
                }
            }
            return true;
        }

        bool ParsePositive(const std::wstring &value, int minimum, int maximum, int *result)
        {
            if (value.empty()) return false;
            long long parsed = 0;
            for (const wchar_t character : value) {
                if (character < L'0' || character > L'9') return false;
                parsed = parsed * 10 + (character - L'0');
                if (parsed > maximum) return false;
            }
            if (parsed < minimum) return false;
            *result = static_cast<int>(parsed);
            return true;
        }

        bool ParseInteger(const std::wstring &value, int *result)
        {
            if (value.empty()) return false;
            size_t index = 0;
            bool negative = false;
            if (value.front() == L'-') {
                negative = true;
                index = 1;
            }
            if (index == value.size()) return false;
            int64_t parsed = 0;
            const int64_t limit = negative ? -(static_cast<int64_t>((std::numeric_limits<int>::min)())) : (std::numeric_limits<int>::max)();
            for (; index < value.size(); ++index) {
                if (value[index] < L'0' || value[index] > L'9') return false;
                parsed = parsed * 10 + (value[index] - L'0');
                if (parsed > limit) return false;
            }
            *result = static_cast<int>(negative ? -parsed : parsed);
            return true;
        }

        std::string WideToUtf8(const std::wstring &value)
        {
            if (value.empty()) return {};
            const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            if (length <= 0) return {};
            std::string result(static_cast<size_t>(length), '\0');
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
            return result;
        }
    }

    std::string EscapeJson(std::string_view value)
    {
        std::string escaped;
        escaped.reserve(value.size() + 8);
        for (size_t index = 0; index < value.size();) {
            const unsigned char character = static_cast<unsigned char>(value[index]);
            switch (character) {
            case '"': escaped += "\\\""; ++index; break;
            case '\\': escaped += "\\\\"; ++index; break;
            case '\b': escaped += "\\b"; ++index; break;
            case '\f': escaped += "\\f"; ++index; break;
            case '\n': escaped += "\\n"; ++index; break;
            case '\r': escaped += "\\r"; ++index; break;
            case '\t': escaped += "\\t"; ++index; break;
            default:
                if (character < 0x20 || character == 0x7f) {
                    char encoded[7];
                    std::snprintf(encoded, sizeof(encoded), "\\u%04x", character);
                    escaped += encoded;
                    ++index;
                } else if (character < 0x80) {
                    escaped += static_cast<char>(character);
                    ++index;
                } else {
                    size_t length = 0;
                    if (IsValidUtf8(value, index, &length)) {
                        escaped.append(value.substr(index, length));
                        index += length;
                    } else {
                        char encoded[7];
                        std::snprintf(encoded, sizeof(encoded), "\\u%04x", character);
                        escaped += encoded;
                        ++index;
                    }
                }
            }
        }
        return escaped;
    }

    std::string FormatEnvelope(const Envelope &envelope)
    {
        std::string record = "{\"schema\":\"smedley.telemetry\",\"schema_version\":1";
        record += ",\"run_id\":\"" + EscapeJson(envelope.run_id) + "\"";
        record += ",\"sequence\":" + std::to_string(envelope.sequence);
        record += ",\"wall_time_utc\":\"" + EscapeJson(envelope.wall_time_utc) + "\"";
        record += ",\"monotonic_us\":" + std::to_string(envelope.monotonic_us);
        record += ",\"game_date_raw\":";
        record += envelope.game_date_raw ? std::to_string(*envelope.game_date_raw) : "null";
        record += ",\"event_type\":\"" + EscapeJson(envelope.event_type) + "\"";
        record += ",\"category\":\"" + EscapeJson(envelope.category) + "\"";
        record += ",\"mapping_id\":\"" + EscapeJson(envelope.mapping_id) + "\"";
        record += ",\"quality\":\"" + EscapeJson(envelope.quality) + "\"";
        record += ",\"entities\":" + envelope.entities_json;
        record += ",\"payload\":" + envelope.payload_json + "}";
        return record;
    }

    bool ValidateRecordV1(const SmedleyTelemetryRecordV1 *record, std::string *error)
    {
        if (record == nullptr || error == nullptr) return false;
        if (record->struct_size != sizeof(*record) || record->version != SMEDLEY_TELEMETRY_ABI_VERSION_V1
            || (record->flags & ~SMEDLEY_TELEMETRY_RECORD_HAS_GAME_DATE) != 0 || record->reserved != 0
            || record->reserved_date != 0 || std::any_of(std::begin(record->reserved_tail), std::end(record->reserved_tail), [](uint32_t value) { return value != 0; })) {
            *error = "invalid record header";
            return false;
        }
        if (!IsValidText(record->event_type, record->event_type_length, SMEDLEY_TELEMETRY_MAX_IDENTIFIER_BYTES, true)
            || !IsValidText(record->category, record->category_length, SMEDLEY_TELEMETRY_MAX_IDENTIFIER_BYTES, true)
            || !IsValidText(record->mapping_id, record->mapping_id_length, SMEDLEY_TELEMETRY_MAX_IDENTIFIER_BYTES, true)
            || !IsValidText(record->quality, record->quality_length, SMEDLEY_TELEMETRY_MAX_IDENTIFIER_BYTES, true)) {
            *error = "invalid record identifier";
            return false;
        }
        if (!IsKnownCategory(std::string_view(record->category, record->category_length))
            || !IsCanonicalQuality(std::string_view(record->quality, record->quality_length))) {
            *error = "unknown record category or quality";
            return false;
        }
        if (record->entity_field_count > SMEDLEY_TELEMETRY_MAX_FIELDS
            || record->payload_field_count > SMEDLEY_TELEMETRY_MAX_FIELDS
            || record->entity_field_count > SMEDLEY_TELEMETRY_MAX_FIELDS - record->payload_field_count) {
            *error = "too many total fields";
            return false;
        }
        return ValidateFields(record->entity_fields, record->entity_field_count, error)
            && ValidateFields(record->payload_fields, record->payload_field_count, error);
    }

    bool FormatRecordV1(const SmedleyTelemetryRecordV1 *record, std::string_view run_id, uint64_t sequence,
                        std::string_view wall_time_utc, uint64_t monotonic_us, std::string *line, std::string *error)
    {
        if (line == nullptr || sequence == 0 || !IsSafeRunId(run_id)
            || !IsValidText(wall_time_utc.data(), static_cast<uint32_t>(wall_time_utc.size()), 64, false)
            || !ValidateRecordV1(record, error)) {
            if (error != nullptr && error->empty()) *error = "invalid telemetry envelope metadata";
            return false;
        }
        Envelope envelope;
        envelope.run_id = std::string(run_id);
        envelope.sequence = sequence;
        envelope.wall_time_utc = std::string(wall_time_utc);
        envelope.monotonic_us = monotonic_us;
        if ((record->flags & SMEDLEY_TELEMETRY_RECORD_HAS_GAME_DATE) != 0) envelope.game_date_raw = record->game_date_raw;
        envelope.event_type.assign(record->event_type, record->event_type_length);
        envelope.category.assign(record->category, record->category_length);
        envelope.mapping_id.assign(record->mapping_id, record->mapping_id_length);
        envelope.quality.assign(record->quality, record->quality_length);
        envelope.entities_json.clear();
        envelope.payload_json.clear();
        AppendFields(&envelope.entities_json, record->entity_fields, record->entity_field_count);
        AppendFields(&envelope.payload_json, record->payload_fields, record->payload_field_count);
        *line = FormatEnvelope(envelope);
        if (line->size() > kMaxRecordBytes) {
            *error = "formatted record exceeds bounded record size";
            return false;
        }
        return true;
    }

    bool PrepareRecordV1(const SmedleyTelemetryRecordV1 *record, std::string_view run_id,
                         std::string_view wall_time_utc, uint64_t monotonic_us, PreparedRecordV1 *prepared, std::string *error)
    {
        if (prepared == nullptr || !FormatRecordV1(record, run_id, (std::numeric_limits<uint64_t>::max)(),
                                                   wall_time_utc, monotonic_us, &prepared->line, error)) return false;
        const std::string marker = "\"sequence\":" + std::to_string((std::numeric_limits<uint64_t>::max)());
        const size_t marker_offset = prepared->line.find(marker);
        if (marker_offset == std::string::npos) {
            if (error != nullptr) *error = "could not locate prepared sequence";
            return false;
        }
        prepared->sequence_offset = marker_offset + std::strlen("\"sequence\":");
        return true;
    }

    bool FinalizeRecordV1(const PreparedRecordV1 &prepared, uint64_t sequence, std::string *line)
    {
        constexpr size_t max_sequence_digits = 20;
        if (line == nullptr || sequence == 0
            || prepared.sequence_offset + max_sequence_digits > prepared.line.size()) return false;
        *line = prepared.line;
        line->replace(prepared.sequence_offset, max_sequence_digits, std::to_string(sequence));
        return true;
    }

    bool PrepareEnvelope(const Envelope &envelope, PreparedRecordV1 *prepared)
    {
        if (prepared == nullptr) return false;
        Envelope maximum_sequence = envelope;
        maximum_sequence.sequence = (std::numeric_limits<uint64_t>::max)();
        prepared->line = FormatEnvelope(maximum_sequence);
        if (prepared->line.size() > kMaxRecordBytes) return false;
        const std::string marker = "\"sequence\":" + std::to_string((std::numeric_limits<uint64_t>::max)());
        const size_t marker_offset = prepared->line.find(marker);
        if (marker_offset == std::string::npos) return false;
        prepared->sequence_offset = marker_offset + std::strlen("\"sequence\":");
        return true;
    }

    SmedleyTelemetryResult PublishPreparedRecord(const PreparedRecordV1 &prepared, std::atomic<uint64_t> *sequence,
                                                 std::mutex *emission_mutex, bool blocking,
                                                 const std::function<bool(std::string_view)> &enqueue,
                                                 const std::function<void()> &mark_dropped)
    {
        if (sequence == nullptr || emission_mutex == nullptr || !enqueue || !mark_dropped) return SMEDLEY_TELEMETRY_UNAVAILABLE;
        std::unique_lock<std::mutex> lock(*emission_mutex, std::defer_lock);
        if (blocking) lock.lock();
        else if (!lock.try_lock()) {
            uint64_t current = sequence->load(std::memory_order_relaxed);
            while (current != (std::numeric_limits<uint64_t>::max)()
                   && !sequence->compare_exchange_weak(current, current + 1, std::memory_order_relaxed)) {}
            mark_dropped();
            return SMEDLEY_TELEMETRY_DROPPED;
        }
        uint64_t current = sequence->load(std::memory_order_relaxed);
        while (current != (std::numeric_limits<uint64_t>::max)()
               && !sequence->compare_exchange_weak(current, current + 1, std::memory_order_relaxed)) {}
        if (current == (std::numeric_limits<uint64_t>::max)()) {
            mark_dropped();
            return SMEDLEY_TELEMETRY_DROPPED;
        }
        std::string line;
        if (!FinalizeRecordV1(prepared, current + 1, &line)) {
            mark_dropped();
            return SMEDLEY_TELEMETRY_DROPPED;
        }
        try {
            if (enqueue(line)) return SMEDLEY_TELEMETRY_ACCEPTED;
        } catch (...) {
            mark_dropped();
            return SMEDLEY_TELEMETRY_DROPPED;
        }
        return SMEDLEY_TELEMETRY_DROPPED;
    }

    SmedleyTelemetryResult DispatchRecordV1(const Config *config, const SmedleyTelemetryRecordV1 *record,
                                            uint64_t *sequence, const std::function<bool(std::string_view)> &enqueue)
    {
        if (config == nullptr || sequence == nullptr || !enqueue) return SMEDLEY_TELEMETRY_UNAVAILABLE;
        std::string error;
        if (!ValidateRecordV1(record, &error)) return SMEDLEY_TELEMETRY_INVALID;
        if (!HasCategory(*config, std::string_view(record->category, record->category_length))) return SMEDLEY_TELEMETRY_FILTERED;
        if (*sequence == (std::numeric_limits<uint64_t>::max)()) return SMEDLEY_TELEMETRY_DROPPED;
        PreparedRecordV1 prepared;
        if (!PrepareRecordV1(record, config->run_id, UtcNow(), MonotonicMicroseconds(), &prepared, &error)) {
            return SMEDLEY_TELEMETRY_INVALID;
        }
        const uint64_t next_sequence = *sequence + 1;
        std::string line;
        if (!FinalizeRecordV1(prepared, next_sequence, &line)) return SMEDLEY_TELEMETRY_DROPPED;
        *sequence = next_sequence;
        try {
            return enqueue(line) ? SMEDLEY_TELEMETRY_ACCEPTED : SMEDLEY_TELEMETRY_DROPPED;
        } catch (...) {
            return SMEDLEY_TELEMETRY_DROPPED;
        }
    }

    bool ValidateConfig(const Config &config, std::string *error)
    {
        if (!IsSafeRunId(config.run_id)) {
            *error = "-smedley-run-id must be a non-empty ASCII letter, digit, or hyphen identifier";
            return false;
        }
        if (config.output_path.empty()) {
            *error = "-smedley-telemetry-output requires a non-empty path";
            return false;
        }
        if (config.categories.empty()) {
            *error = "-smedley-telemetry-categories requires one or more categories";
            return false;
        }
        for (const auto &category : config.categories) {
            if (!IsKnownCategory(category)) {
                *error = "-smedley-telemetry-categories contains an unknown category";
                return false;
            }
        }
        for (size_t index = 0; index < config.country_tags.size(); ++index) {
            if (!IsCountryTag(config.country_tags[index])) {
                *error = "-smedley-telemetry-country-tags must contain normalized three-character ASCII tags";
                return false;
            }
            if (std::find(config.country_tags.begin(), config.country_tags.begin() + index, config.country_tags[index])
                != config.country_tags.begin() + index) {
                *error = "-smedley-telemetry-country-tags must not contain duplicates";
                return false;
            }
        }
        if (config.start_date_raw && config.end_date_raw && *config.start_date_raw > *config.end_date_raw) {
            *error = "-smedley-telemetry-start-date-raw must not exceed -smedley-telemetry-end-date-raw";
            return false;
        }
        if (config.sample_days < 1 || config.sample_days > kMaxSampleDays) {
            *error = "-smedley-telemetry-sample-days must be from 1 through 365";
            return false;
        }
        if (config.queue_capacity < kMinQueueCapacity || config.queue_capacity > kMaxQueueCapacity) {
            *error = "-smedley-telemetry-queue-capacity must be from 64 through 8192";
            return false;
        }
        if (config.capture_rules.size() > kMaxCaptureRules) {
            *error = "telemetry supports at most 32 capture rules";
            return false;
        }
        if (!config.capture_rules.empty() && !HasCategory(config, "state")) {
            *error = "telemetry capture rules require the state category";
            return false;
        }
        for (const auto &rule : config.capture_rules) {
            if (rule.family != "world.daily" && rule.family != "world.economy" && rule.family != "country.daily"
                && rule.family != "province.daily" && rule.family != "pop.economy"
                && rule.family != "pop.demographics" && rule.family != "pop.aggregate") {
                *error = "telemetry capture rule contains an unknown family";
                return false;
            }
            if (rule.start_date_raw && rule.end_date_raw && *rule.start_date_raw > *rule.end_date_raw) {
                *error = "telemetry capture rule start date must not exceed its end date";
                return false;
            }
            if (rule.cadence == CaptureCadence::FixedDays && (rule.fixed_days < 1 || rule.fixed_days > kMaxSampleDays)) {
                *error = "telemetry fixed-day cadence must be from 1 through 365";
                return false;
            }
            for (size_t index = 0; index < rule.country_tags.size(); ++index) {
                if (!IsCountryTag(rule.country_tags[index])
                    || std::find(rule.country_tags.begin(), rule.country_tags.begin() + index, rule.country_tags[index])
                        != rule.country_tags.begin() + index) {
                    *error = "telemetry capture country tags must be unique normalized three-character ASCII tags";
                    return false;
                }
            }
            const auto known_field = [&](const std::string &field) {
                if (rule.family == "world.daily") {
                    return field == "country_slot_count" || field == "ai_scheduler_entry_count"
                        || field == "human_control_present";
                }
                if (rule.family == "country.daily") return field == "treasury_raw" || field == "treasury";
                if (rule.family == "province.daily") {
                    return field == "owner_tag_candidate" || field == "controller_tag_candidate"
                        || field == "colonial_level_candidate" || field == "life_rating_candidate"
                        || field == "infrastructure_candidate_raw";
                }
                if (rule.family == "pop.economy") {
                    return field == "money_raw" || field == "savings_raw"
                        || field == "interest_cash_flow_raw" || field == "total_cash_flow_raw";
                }
                if (rule.family == "pop.demographics") {
                    return field == "size_candidate" || field == "employed_candidate"
                        || field == "consciousness_candidate_raw" || field == "militancy_candidate_raw"
                        || field == "literacy_candidate_raw";
                }
                if (rule.family == "pop.aggregate") {
                    return field == "pop_count" || field == "size_candidate"
                        || field == "employed_candidate" || field == "money_raw" || field == "savings_raw";
                }
                return field == "health" || field == "capacity" || field == "holdings" || field == "credit";
            };
            for (size_t index = 0; index < rule.fields.size(); ++index) {
                if (!known_field(rule.fields[index])
                    || std::find(rule.fields.begin(), rule.fields.begin() + index, rule.fields[index])
                        != rule.fields.begin() + index) {
                    *error = "telemetry capture fields must be known and unique for their family";
                    return false;
                }
            }
            if (rule.family != "country.daily" && !rule.country_tags.empty()) {
                *error = "telemetry capture country filters are only supported by country.daily";
                return false;
            }
            for (size_t index = 0; index < rule.province_ids.size(); ++index) {
                if (rule.province_ids[index] < 0
                    || std::find(rule.province_ids.begin(), rule.province_ids.begin() + index, rule.province_ids[index])
                        != rule.province_ids.begin() + index) {
                    *error = "telemetry capture province IDs must be unique nonnegative integers";
                    return false;
                }
            }
            if (rule.family != "province.daily" && rule.family != "pop.economy"
                && rule.family != "pop.demographics" && rule.family != "pop.aggregate"
                && !rule.province_ids.empty()) {
                *error = "telemetry capture province filters are only supported by province and POP families";
                return false;
            }
        }
        return IsJsonLinesPath(config.output_path) ? true : (*error = "-smedley-telemetry-output must end in .jsonl", false);
    }

    bool ParseLaunchArguments(const std::vector<std::wstring> &arguments, Config *config, std::string *error)
    {
        bool have_run_id = false;
        bool have_output = false;
        bool have_categories = false;
        bool have_sample_days = false;
        bool have_queue_capacity = false;
        bool have_overwrite = false;
        bool have_country_tags = false;
        bool have_start_date = false;
        bool have_end_date = false;
        bool have_capture_rules = false;
        for (const auto &argument : arguments) {
            const auto parse_value = [&](const wchar_t *prefix, std::wstring *value) {
                const std::wstring_view view(argument);
                const std::wstring_view prefix_view(prefix);
                if (view.rfind(prefix_view, 0) != 0) return false;
                *value = argument.substr(prefix_view.size());
                return true;
            };
            std::wstring value;
            if (parse_value(L"-smedley-run-id=", &value)) {
                if (have_run_id || value.empty()) { *error = "-smedley-run-id must appear once with a value"; return false; }
                config->run_id = WideToUtf8(value);
                have_run_id = true;
            } else if (argument.rfind(L"-smedley-run-id", 0) == 0) {
                *error = "malformed -smedley-run-id argument"; return false;
            } else if (parse_value(L"-smedley-telemetry-output=", &value)) {
                if (have_output || value.empty()) { *error = "-smedley-telemetry-output must appear once with a value"; return false; }
                config->output_path = value;
                have_output = true;
            } else if (argument.rfind(L"-smedley-telemetry-output", 0) == 0) {
                *error = "malformed -smedley-telemetry-output argument"; return false;
            } else if (parse_value(L"-smedley-telemetry-categories=", &value)) {
                if (have_categories || value.empty()) { *error = "-smedley-telemetry-categories must appear once with a value"; return false; }
                size_t begin = 0;
                while (begin <= value.size()) {
                    const size_t end = value.find(L',', begin);
                    const std::wstring item = value.substr(begin, end == std::wstring::npos ? end : end - begin);
                    const std::string category = WideToUtf8(item);
                    if (category.empty() || std::find(config->categories.begin(), config->categories.end(), category) != config->categories.end()) {
                        *error = "-smedley-telemetry-categories must contain unique non-empty values"; return false;
                    }
                    config->categories.push_back(category);
                    if (end == std::wstring::npos) break;
                    begin = end + 1;
                }
                have_categories = true;
            } else if (argument.rfind(L"-smedley-telemetry-categories", 0) == 0) {
                *error = "malformed -smedley-telemetry-categories argument"; return false;
            } else if (parse_value(L"-smedley-telemetry-country-tags=", &value)) {
                if (have_country_tags || value.empty()) { *error = "-smedley-telemetry-country-tags must appear once with a value"; return false; }
                size_t begin = 0;
                while (begin <= value.size()) {
                    const size_t end = value.find(L',', begin);
                    const std::wstring item = value.substr(begin, end == std::wstring::npos ? end : end - begin);
                    const std::string tag = WideToUtf8(item);
                    if (!IsCountryTag(tag) || std::find(config->country_tags.begin(), config->country_tags.end(), tag) != config->country_tags.end()) {
                        *error = "-smedley-telemetry-country-tags must contain unique normalized three-character ASCII tags"; return false;
                    }
                    config->country_tags.push_back(tag);
                    if (end == std::wstring::npos) break;
                    begin = end + 1;
                }
                have_country_tags = true;
            } else if (argument.rfind(L"-smedley-telemetry-country-tags", 0) == 0) {
                *error = "malformed -smedley-telemetry-country-tags argument"; return false;
            } else if (parse_value(L"-smedley-telemetry-start-date-raw=", &value)) {
                int date = 0;
                if (have_start_date || !ParseInteger(value, &date)) {
                    *error = "-smedley-telemetry-start-date-raw must appear once with an integer"; return false;
                }
                config->start_date_raw = date;
                have_start_date = true;
            } else if (argument.rfind(L"-smedley-telemetry-start-date-raw", 0) == 0) {
                *error = "malformed -smedley-telemetry-start-date-raw argument"; return false;
            } else if (parse_value(L"-smedley-telemetry-end-date-raw=", &value)) {
                int date = 0;
                if (have_end_date || !ParseInteger(value, &date)) {
                    *error = "-smedley-telemetry-end-date-raw must appear once with an integer"; return false;
                }
                config->end_date_raw = date;
                have_end_date = true;
            } else if (argument.rfind(L"-smedley-telemetry-end-date-raw", 0) == 0) {
                *error = "malformed -smedley-telemetry-end-date-raw argument"; return false;
            } else if (parse_value(L"-smedley-telemetry-sample-days=", &value)) {
                if (have_sample_days || !ParsePositive(value, 1, kMaxSampleDays, &config->sample_days)) {
                    *error = "-smedley-telemetry-sample-days must appear once with a value from 1 through 365"; return false;
                }
                have_sample_days = true;
            } else if (argument.rfind(L"-smedley-telemetry-sample-days", 0) == 0) {
                *error = "malformed -smedley-telemetry-sample-days argument"; return false;
            } else if (parse_value(L"-smedley-telemetry-queue-capacity=", &value)) {
                if (have_queue_capacity || !ParsePositive(value, kMinQueueCapacity, kMaxQueueCapacity, &config->queue_capacity)) {
                    *error = "-smedley-telemetry-queue-capacity must appear once with a value from 64 through 8192"; return false;
                }
                have_queue_capacity = true;
            } else if (argument.rfind(L"-smedley-telemetry-queue-capacity", 0) == 0) {
                *error = "malformed -smedley-telemetry-queue-capacity argument"; return false;
            } else if (parse_value(L"-smedley-telemetry-overwrite=", &value)) {
                if (have_overwrite || (value != L"0" && value != L"1")) {
                    *error = "-smedley-telemetry-overwrite must appear once with 0 or 1"; return false;
                }
                config->overwrite = value == L"1";
                have_overwrite = true;
            } else if (parse_value(L"-smedley-telemetry-capture=", &value)) {
                if (value.empty() || config->capture_rules.size() >= kMaxCaptureRules) {
                    *error = "-smedley-telemetry-capture requires a value and may appear at most 32 times"; return false;
                }
                CaptureRule rule;
                if (!ParseCaptureRule(value, &rule, error)) return false;
                if (std::any_of(config->capture_rules.begin(), config->capture_rules.end(), [&](const CaptureRule &existing) {
                    return existing.family == rule.family;
                })) {
                    *error = "-smedley-telemetry-capture families must be unique"; return false;
                }
                config->capture_rules.push_back(std::move(rule));
                have_capture_rules = true;
            } else if (argument.rfind(L"-smedley-telemetry-overwrite", 0) == 0) {
                *error = "malformed -smedley-telemetry-overwrite argument"; return false;
            } else if (argument.rfind(L"-smedley-telemetry-capture", 0) == 0) {
                *error = "malformed -smedley-telemetry-capture argument"; return false;
            }
        }
        if (!have_run_id || !have_output || !have_categories || !have_sample_days || !have_queue_capacity || !have_overwrite) {
            *error = "telemetry requires run ID, output, categories, sample days, queue capacity, and overwrite arguments";
            return false;
        }
        if (!have_capture_rules && HasCategory(*config, "state")) {
            const auto legacy = [&](const char *family) {
                CaptureRule rule;
                rule.family = family;
                rule.cadence = CaptureCadence::FixedDays;
                rule.fixed_days = config->sample_days;
                rule.country_tags = family == std::string_view("country.daily") ? config->country_tags : std::vector<std::string>{};
                rule.start_date_raw = config->start_date_raw;
                rule.end_date_raw = config->end_date_raw;
                config->capture_rules.push_back(std::move(rule));
            };
            legacy("world.daily");
            legacy("world.economy");
            legacy("country.daily");
        }
        return ValidateConfig(*config, error);
    }

    bool HasCategory(const Config &config, std::string_view category)
    {
        return std::find(config.categories.begin(), config.categories.end(), category) != config.categories.end();
    }

    bool HasCountryTag(const Config &config, std::string_view tag)
    {
        return config.country_tags.empty()
            || std::find(config.country_tags.begin(), config.country_tags.end(), tag) != config.country_tags.end();
    }

    bool IsDateInRange(const Config &config, std::optional<int> date)
    {
        return date && (!config.start_date_raw || *date >= *config.start_date_raw)
            && (!config.end_date_raw || *date <= *config.end_date_raw);
    }

    bool IsDateInRange(const CaptureRule &rule, int date)
    {
        return (!rule.start_date_raw || date >= *rule.start_date_raw)
            && (!rule.end_date_raw || date <= *rule.end_date_raw);
    }

    std::string CaptureCadenceName(CaptureCadence cadence)
    {
        switch (cadence) {
        case CaptureCadence::FixedDays: return "fixed_days";
        case CaptureCadence::Daily: return "daily";
        case CaptureCadence::Weekly: return "weekly";
        case CaptureCadence::Monthly: return "monthly";
        case CaptureCadence::Yearly: return "yearly";
        }
        return {};
    }

    bool ParseCaptureRule(std::wstring_view value, CaptureRule *rule, std::string *error)
    {
        if (rule == nullptr) return false;
        std::array<std::wstring, 7> parts;
        size_t begin = 0;
        for (size_t index = 0; index < parts.size(); ++index) {
            const size_t end = value.find(L'|', begin);
            if (index + 1 < parts.size() && end == std::wstring_view::npos) {
                *error = "telemetry capture must contain family|cadence|fields|countries|provinces|start|end";
                return false;
            }
            parts[index] = std::wstring(value.substr(begin, end == std::wstring_view::npos ? value.size() - begin : end - begin));
            if (end == std::wstring_view::npos) {
                if (index + 1 != parts.size()) return false;
                begin = value.size();
            } else {
                begin = end + 1;
            }
        }
        if (begin < value.size()) {
            *error = "telemetry capture contains too many components";
            return false;
        }
        rule->family = WideToUtf8(parts[0]);
        const std::string cadence = WideToUtf8(parts[1]);
        if (cadence == "daily") rule->cadence = CaptureCadence::Daily;
        else if (cadence == "weekly") rule->cadence = CaptureCadence::Weekly;
        else if (cadence == "monthly") rule->cadence = CaptureCadence::Monthly;
        else if (cadence == "yearly") rule->cadence = CaptureCadence::Yearly;
        else {
            *error = "telemetry capture cadence must be daily, weekly, monthly, or yearly";
            return false;
        }
        const auto parse_list = [&](const std::wstring &text, std::vector<std::string> *destination) {
            if (text.empty()) return true;
            size_t item_begin = 0;
            while (item_begin <= text.size()) {
                const size_t item_end = text.find(L',', item_begin);
                const auto item = WideToUtf8(text.substr(item_begin, item_end == std::wstring::npos ? item_end : item_end - item_begin));
                if (item.empty()) return false;
                destination->push_back(item);
                if (item_end == std::wstring::npos) break;
                item_begin = item_end + 1;
            }
            return true;
        };
        if (rule->family.empty() || !parse_list(parts[2], &rule->fields) || !parse_list(parts[3], &rule->country_tags)) {
            *error = "telemetry capture family and list values must not contain empty items";
            return false;
        }
        if (!parts[4].empty()) {
            size_t item_begin = 0;
            while (item_begin <= parts[4].size()) {
                const size_t item_end = parts[4].find(L',', item_begin);
                int parsed = 0;
                if (!ParseInteger(parts[4].substr(item_begin, item_end == std::wstring::npos ? item_end : item_end - item_begin), &parsed)) {
                    *error = "telemetry capture province IDs must be integers";
                    return false;
                }
                rule->province_ids.push_back(parsed);
                if (item_end == std::wstring::npos) break;
                item_begin = item_end + 1;
            }
        }
        if (!parts[5].empty()) {
            int parsed = 0;
            if (!ParseInteger(parts[5], &parsed)) { *error = "telemetry capture start must be an integer"; return false; }
            rule->start_date_raw = parsed;
        }
        if (!parts[6].empty()) {
            int parsed = 0;
            if (!ParseInteger(parts[6], &parsed)) { *error = "telemetry capture end must be an integer"; return false; }
            rule->end_date_raw = parsed;
        }
        return true;
    }

    std::optional<CalendarDate> DecodeClausewitzDate(int raw_date)
    {
        constexpr std::array<int, 12> month_lengths = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        auto floor_divide = [](int64_t value, int64_t divisor) {
            int64_t quotient = value / divisor;
            if (value % divisor < 0) --quotient;
            return quotient;
        };
        const int64_t day_index = floor_divide(raw_date, 24);
        const int64_t hour = static_cast<int64_t>(raw_date) - day_index * 24;
        const int64_t year_index = floor_divide(day_index, 365);
        int day_of_year = static_cast<int>(day_index - year_index * 365);
        CalendarDate result;
        result.year = static_cast<int>(year_index - 5000);
        result.hour = static_cast<int>(hour);
        result.month = 1;
        for (const int length : month_lengths) {
            if (day_of_year < length) {
                result.day = day_of_year + 1;
                return result;
            }
            day_of_year -= length;
            ++result.month;
        }
        return std::nullopt;
    }

    bool ShouldCaptureDate(int raw_date, const CaptureRule &rule, ScheduleState *state)
    {
        if (state == nullptr || !IsDateInRange(rule, raw_date)) return false;
        if (state->last_observed_date && raw_date < *state->last_observed_date) {
            state->last_capture_date.reset();
            state->last_period.reset();
        }
        state->last_observed_date = raw_date;
        if (state->last_capture_date == raw_date) return true;
        std::optional<int64_t> current_period;
        if (rule.cadence == CaptureCadence::Monthly || rule.cadence == CaptureCadence::Yearly) {
            const auto date = DecodeClausewitzDate(raw_date);
            if (!date) return false;
            current_period = rule.cadence == CaptureCadence::Monthly
                ? static_cast<int64_t>(date->year) * 12 + date->month - 1 : date->year;
        }
        bool due = false;
        if (!state->last_capture_date) {
            due = true;
        } else if (rule.cadence == CaptureCadence::Daily) {
            due = true;
        } else if (rule.cadence == CaptureCadence::FixedDays || rule.cadence == CaptureCadence::Weekly) {
            const int days = rule.cadence == CaptureCadence::Weekly ? 7 : rule.fixed_days;
            due = static_cast<int64_t>(raw_date) - *state->last_capture_date >= static_cast<int64_t>(days) * 24;
        } else {
            due = !state->last_period || *current_period != *state->last_period;
        }
        if (due) {
            state->last_capture_date = raw_date;
            if (current_period) state->last_period = current_period;
        }
        return due;
    }

    std::string UtcNow()
    {
        SYSTEMTIME time{};
        GetSystemTime(&time);
        char value[32];
        std::snprintf(value, sizeof(value), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                      time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
        return value;
    }

    uint64_t MonotonicMicroseconds()
    {
        static const uint64_t frequency = [] {
            LARGE_INTEGER result{};
            QueryPerformanceFrequency(&result);
            return static_cast<uint64_t>(result.QuadPart);
        }();
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        return QpcToMicroseconds(static_cast<uint64_t>(counter.QuadPart), frequency);
    }

    uint64_t QpcToMicroseconds(uint64_t counter, uint64_t frequency)
    {
        if (frequency == 0) return 0;
        const uint64_t seconds = counter / frequency;
        if (seconds > (std::numeric_limits<uint64_t>::max)() / 1000000ULL) return (std::numeric_limits<uint64_t>::max)();
        const uint64_t base = seconds * 1000000ULL;
        const uint64_t fraction = (counter % frequency) * 1000000ULL / frequency;
        return base > (std::numeric_limits<uint64_t>::max)() - fraction ? (std::numeric_limits<uint64_t>::max)() : base + fraction;
    }

    bool ShouldSampleDate(std::optional<int> date, int sample_days, std::optional<int> *last_sampled_date)
    {
        if (!date) return false;
        constexpr int raw_date_units_per_day = 24;
        if (!*last_sampled_date || *date < **last_sampled_date
            || static_cast<int64_t>(*date) - **last_sampled_date >= static_cast<int64_t>(sample_days) * raw_date_units_per_day) {
            *last_sampled_date = *date;
            return true;
        }
        return *date == **last_sampled_date;
    }

    bool ObserveDateRegression(int current_date, std::optional<int> *last_observed_date, int64_t *delta)
    {
        const bool regressed = last_observed_date && *last_observed_date && current_date < **last_observed_date;
        if (delta != nullptr) *delta = regressed ? static_cast<int64_t>(current_date) - static_cast<int64_t>(**last_observed_date) : 0;
        if (last_observed_date != nullptr) *last_observed_date = current_date;
        return regressed;
    }

    BoundedQueue::BoundedQueue(size_t capacity) : slots_(capacity), lengths_(capacity) {}

    bool BoundedQueue::Push(std::string_view line)
    {
        if (line.empty() || line.size() > kMaxRecordBytes) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_ || size_ == slots_.size()) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        std::memcpy(slots_[tail_].data(), line.data(), line.size());
        lengths_[tail_] = line.size();
        tail_ = (tail_ + 1) % slots_.size();
        ++size_;
        accepted_.fetch_add(1, std::memory_order_relaxed);
        uint64_t previous = high_water_.load(std::memory_order_relaxed);
        while (previous < size_ && !high_water_.compare_exchange_weak(previous, size_, std::memory_order_relaxed)) {}
        return true;
    }

    bool BoundedQueue::TryPush(std::string_view line)
    {
        return TryPush(line, 0);
    }

    bool BoundedQueue::TryPush(std::string_view line, size_t reserved_slots)
    {
        if (line.empty() || line.size() > kMaxRecordBytes) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        const size_t usable_capacity = reserved_slots >= slots_.size() ? 0 : slots_.size() - reserved_slots;
        if (!lock.owns_lock() || stopped_ || size_ >= usable_capacity) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        std::memcpy(slots_[tail_].data(), line.data(), line.size());
        lengths_[tail_] = line.size();
        tail_ = (tail_ + 1) % slots_.size();
        ++size_;
        accepted_.fetch_add(1, std::memory_order_relaxed);
        uint64_t previous = high_water_.load(std::memory_order_relaxed);
        while (previous < size_ && !high_water_.compare_exchange_weak(previous, size_, std::memory_order_relaxed)) {}
        return true;
    }

    bool BoundedQueue::Pop(std::string *line)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (size_ == 0) return false;
        line->assign(slots_[head_].data(), lengths_[head_]);
        head_ = (head_ + 1) % slots_.size();
        --size_;
        return true;
    }

    void BoundedQueue::MarkWritten()
    {
        written_.fetch_add(1, std::memory_order_relaxed);
    }

    void BoundedQueue::Stop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }

    bool BoundedQueue::stopped() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return stopped_ && size_ == 0;
    }

    QueueStats BoundedQueue::stats() const
    {
        return {accepted_.load(std::memory_order_relaxed), written_.load(std::memory_order_relaxed),
                dropped_.load(std::memory_order_relaxed), high_water_.load(std::memory_order_relaxed)};
    }

    Writer::Writer(Config config) : config_(std::move(config)), queue_(static_cast<size_t>(config_.queue_capacity)) {}

    Writer::~Writer()
    {
        Stop();
    }

    bool Writer::Start(std::string *error)
    {
        if (started_) return true;
        std::error_code filesystem_error;
        std::filesystem::create_directories(config_.output_path.parent_path(), filesystem_error);
        if (filesystem_error || !ValidateOutputPath(config_, error)) {
            if (filesystem_error) *error = "could not create telemetry output directory: " + filesystem_error.message();
            return false;
        }
        output_ = CreateFileW(config_.output_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              config_.overwrite ? CREATE_ALWAYS : CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output_ == INVALID_HANDLE_VALUE) {
            *error = "could not create telemetry output (Windows error " + std::to_string(GetLastError()) + ")";
            return false;
        }
        try {
            started_ = true;
            thread_ = std::thread(&Writer::Run, this);
            return true;
        } catch (const std::exception &exception) {
            *error = "could not start telemetry writer: " + std::string(exception.what());
        } catch (...) {
            *error = "could not start telemetry writer";
        }
        started_ = false;
        CloseHandle(output_);
        output_ = INVALID_HANDLE_VALUE;
        return false;
    }

    bool Writer::TryWrite(std::string_view line)
    {
        const size_t reserve = (std::min)(size_t{16}, static_cast<size_t>(config_.queue_capacity / 8));
        if (!started_ || !queue_.TryPush(line, reserve)) return false;
        wake_.notify_one();
        return true;
    }

    bool Writer::WriteReliable(std::string_view line)
    {
        if (!started_ || !queue_.Push(line)) return false;
        wake_.notify_one();
        return true;
    }

    void Writer::MarkDropped()
    {
        queue_.dropped_.fetch_add(1, std::memory_order_relaxed);
    }

    bool Writer::WriteInitial(std::string_view line)
    {
        if (!started_ || !queue_.Push(line)) return false;
        wake_.notify_one();
        return true;
    }

    bool Writer::Stop(const std::function<std::string(const QueueStats &)> &summary_builder)
    {
        if (!started_) return !write_failed_.load(std::memory_order_relaxed);
        queue_.Stop();
        wake_.notify_one();
        if (thread_.joinable()) thread_.join();
        if (!write_failed_.load(std::memory_order_relaxed) && summary_builder) {
            const auto summary = summary_builder(stats());
            if (!summary.empty()) WriteLine(summary);
        }
        if (!write_failed_.load(std::memory_order_relaxed)) Flush();
        if (!CloseHandle(output_)) FailWrite();
        output_ = INVALID_HANDLE_VALUE;
        started_ = false;
        return !write_failed_.load(std::memory_order_relaxed);
    }

    QueueStats Writer::stats() const
    {
        auto stats = queue_.stats();
        stats.write_failed = write_failed_.load(std::memory_order_relaxed);
        return stats;
    }

    void Writer::Run()
    {
        const auto flush_interval = std::chrono::seconds(1);
        auto next_flush = std::chrono::steady_clock::now() + flush_interval;
        for (;;) {
            std::string line;
            while (queue_.Pop(&line)) {
                if (!WriteLine(line)) break;
                queue_.MarkWritten();
                const auto now = std::chrono::steady_clock::now();
                if (now >= next_flush) {
                    if (!Flush()) break;
                    next_flush = now + flush_interval;
                }
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_flush) {
                if (!Flush()) break;
                next_flush = now + flush_interval;
            }
            if (queue_.stopped()) break;
            std::unique_lock<std::mutex> lock(wake_mutex_);
            wake_.wait_until(lock, next_flush);
        }
    }

    bool Writer::WriteLine(const std::string &line)
    {
        std::string record = line + '\n';
        DWORD written = 0;
        if (!WriteFile(output_, record.data(), static_cast<DWORD>(record.size()), &written, nullptr) || written != record.size()) {
            FailWrite();
            return false;
        }
        return true;
    }

    bool Writer::Flush()
    {
        if (!FlushFileBuffers(output_)) {
            FailWrite();
            return false;
        }
        return true;
    }

    void Writer::FailWrite()
    {
        write_failed_.store(true, std::memory_order_relaxed);
        queue_.Stop();
    }
}
