#include "telemetry_core.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
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
                return std::isalnum(character) || character == '-';
            });
        }

        bool IsKnownCategory(std::string_view category)
        {
            return category == "lifecycle" || category == "state";
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
        if (config.sample_days < 1 || config.sample_days > kMaxSampleDays) {
            *error = "-smedley-telemetry-sample-days must be from 1 through 365";
            return false;
        }
        if (config.queue_capacity < kMinQueueCapacity || config.queue_capacity > kMaxQueueCapacity) {
            *error = "-smedley-telemetry-queue-capacity must be from 64 through 8192";
            return false;
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
            } else if (argument.rfind(L"-smedley-telemetry-overwrite", 0) == 0) {
                *error = "malformed -smedley-telemetry-overwrite argument"; return false;
            }
        }
        if (!have_run_id || !have_output || !have_categories || !have_sample_days || !have_queue_capacity || !have_overwrite) {
            *error = "telemetry requires run ID, output, categories, sample days, queue capacity, and overwrite arguments";
            return false;
        }
        return ValidateConfig(*config, error);
    }

    bool HasCategory(const Config &config, std::string_view category)
    {
        return std::find(config.categories.begin(), config.categories.end(), category) != config.categories.end();
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
        if (!*last_sampled_date || *date < **last_sampled_date || *date - **last_sampled_date >= sample_days) {
            *last_sampled_date = *date;
            return true;
        }
        return *date == **last_sampled_date;
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
        if (line.empty() || line.size() > kMaxRecordBytes) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock() || stopped_ || size_ == slots_.size()) {
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
        if (!started_ || !queue_.TryPush(line)) return false;
        wake_.notify_one();
        return true;
    }

    bool Writer::WriteInitial(std::string_view line)
    {
        if (!started_ || !queue_.Push(line)) return false;
        wake_.notify_one();
        return true;
    }

    void Writer::Stop(const std::function<std::string(const QueueStats &)> &summary_builder)
    {
        if (!started_) return;
        queue_.Stop();
        wake_.notify_one();
        if (thread_.joinable()) thread_.join();
        if (!write_failed_.load(std::memory_order_relaxed) && summary_builder) {
            const auto summary = summary_builder(stats());
            if (!summary.empty()) WriteLine(summary);
        }
        if (!write_failed_.load(std::memory_order_relaxed)) Flush();
        CloseHandle(output_);
        output_ = INVALID_HANDLE_VALUE;
        started_ = false;
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
