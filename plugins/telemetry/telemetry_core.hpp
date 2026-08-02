#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include <windows.h>

#include <smedley/telemetry.h>

namespace smedley::telemetry
{
    constexpr size_t kMaxRecordBytes = 1024;
    constexpr int kMinQueueCapacity = 64;
    constexpr int kMaxQueueCapacity = 32768;
    constexpr int kMaxSampleDays = 365;
    constexpr size_t kMaxCaptureRules = 32;

    enum class CaptureCadence : uint8_t
    {
        FixedDays,
        Daily,
        Weekly,
        Monthly,
        Yearly,
    };

    struct CaptureRule
    {
        std::string family;
        CaptureCadence cadence = CaptureCadence::Daily;
        std::vector<std::string> fields;
        std::vector<std::string> country_tags;
        std::vector<int> province_ids;
        std::optional<int> start_date_raw;
        std::optional<int> end_date_raw;
        int fixed_days = 1;
    };

    struct CalendarDate
    {
        int year = 0;
        int month = 0;
        int day = 0;
        int hour = 0;
    };

    struct ScheduleState
    {
        std::optional<int> last_observed_date;
        std::optional<int> last_capture_date;
        std::optional<int64_t> last_period;
    };

    static_assert(std::is_standard_layout_v<SmedleyTelemetryFieldV1>);
    static_assert(std::is_standard_layout_v<SmedleyTelemetryRecordV1>);
#if INTPTR_MAX == INT32_MAX
    static_assert(sizeof(SmedleyTelemetryUtf8V1) == 12 && offsetof(SmedleyTelemetryUtf8V1, reserved) == 8);
    static_assert(sizeof(SmedleyTelemetryFieldV1) == 40 && offsetof(SmedleyTelemetryFieldV1, value) == 24);
    static_assert(sizeof(SmedleyTelemetryRecordV1) == 88 && offsetof(SmedleyTelemetryRecordV1, reserved_tail) == 72);
#endif

    struct Config
    {
        std::string run_id;
        std::filesystem::path output_path;
        std::vector<std::string> categories;
        std::vector<std::string> country_tags;
        std::optional<int> start_date_raw;
        std::optional<int> end_date_raw;
        int sample_days = 1;
        int queue_capacity = 1024;
        bool overwrite = false;
        std::vector<CaptureRule> capture_rules;
        std::optional<double> gold_to_cash_rate;
    };

    struct Envelope
    {
        std::string run_id;
        uint64_t sequence = 0;
        std::string wall_time_utc;
        uint64_t monotonic_us = 0;
        std::optional<int> game_date_raw;
        std::string event_type;
        std::string category;
        std::string mapping_id;
        std::string quality;
        std::string entities_json = "{}";
        std::string payload_json = "{}";
    };

    struct QueueStats
    {
        uint64_t accepted = 0;
        uint64_t written = 0;
        uint64_t dropped = 0;
        uint64_t high_water = 0;
        bool write_failed = false;
    };

    struct PreparedRecordV1
    {
        std::string line;
        size_t sequence_offset = 0;
    };

    std::string EscapeJson(std::string_view value);
    std::string FormatEnvelope(const Envelope &envelope);
    bool ValidateRecordV1(const SmedleyTelemetryRecordV1 *record, std::string *error);
    bool FormatRecordV1(const SmedleyTelemetryRecordV1 *record, std::string_view run_id, uint64_t sequence,
                        std::string_view wall_time_utc, uint64_t monotonic_us, std::string *line, std::string *error);
    bool PrepareRecordV1(const SmedleyTelemetryRecordV1 *record, std::string_view run_id,
                         std::string_view wall_time_utc, uint64_t monotonic_us, PreparedRecordV1 *prepared, std::string *error);
    bool FinalizeRecordV1(const PreparedRecordV1 &prepared, uint64_t sequence, std::string *line);
    bool PrepareEnvelope(const Envelope &envelope, PreparedRecordV1 *prepared);
    SmedleyTelemetryResult PublishPreparedRecord(const PreparedRecordV1 &prepared, std::atomic<uint64_t> *sequence,
                                                 std::mutex *emission_mutex, bool blocking,
                                                 const std::function<bool(std::string_view)> &enqueue,
                                                 const std::function<void()> &mark_dropped);
    SmedleyTelemetryResult DispatchRecordV1(const Config *config, const SmedleyTelemetryRecordV1 *record,
                                            uint64_t *sequence, const std::function<bool(std::string_view)> &enqueue);
    bool ValidateConfig(const Config &config, std::string *error);
    bool ParseLaunchArguments(const std::vector<std::wstring> &arguments, Config *config, std::string *error);
    bool HasCategory(const Config &config, std::string_view category);
    bool HasCountryTag(const Config &config, std::string_view tag);
    bool IsDateInRange(const Config &config, std::optional<int> date);
    bool IsDateInRange(const CaptureRule &rule, int date);
    bool ParseCaptureRule(std::wstring_view value, CaptureRule *rule, std::string *error);
    std::string CaptureCadenceName(CaptureCadence cadence);
    std::optional<CalendarDate> DecodeClausewitzDate(int raw_date);
    bool ShouldCaptureDate(int raw_date, const CaptureRule &rule, ScheduleState *state);
    std::string UtcNow();
    uint64_t MonotonicMicroseconds();
    uint64_t QpcToMicroseconds(uint64_t counter, uint64_t frequency);
    bool ShouldSampleDate(std::optional<int> date, int sample_days, std::optional<int> *last_sampled_date);
    bool ObserveDateRegression(int current_date, std::optional<int> *last_observed_date, int64_t *delta);

    class BoundedQueue
    {
    public:
        explicit BoundedQueue(size_t capacity);
        bool Push(std::string_view line);
        bool TryPush(std::string_view line);
        bool TryPush(std::string_view line, size_t reserved_slots);
        bool Pop(std::string *line);
        void MarkWritten();
        void Stop();
        bool stopped() const;
        QueueStats stats() const;

    private:
        std::vector<std::array<char, kMaxRecordBytes>> slots_;
        std::vector<size_t> lengths_;
        size_t head_ = 0;
        size_t tail_ = 0;
        size_t size_ = 0;
        bool stopped_ = false;
        mutable std::mutex mutex_;
        std::atomic<uint64_t> accepted_{0};
        std::atomic<uint64_t> written_{0};
        std::atomic<uint64_t> dropped_{0};
        std::atomic<uint64_t> high_water_{0};

        friend class Writer;
    };

    class Writer
    {
    public:
        explicit Writer(Config config);
        ~Writer();

        bool Start(std::string *error);
        bool WriteInitial(std::string_view line);
        bool WriteReliable(std::string_view line);
        bool TryWrite(std::string_view line);
        void MarkDropped();
        bool Stop(const std::function<std::string(const QueueStats &)> &summary_builder = {});
        QueueStats stats() const;

    private:
        void Run();
        bool WriteLine(const std::string &line);
        bool Flush();
        void FailWrite();

        Config config_;
        BoundedQueue queue_;
        HANDLE output_ = INVALID_HANDLE_VALUE;
        std::thread thread_;
        std::condition_variable wake_;
        std::mutex wake_mutex_;
        bool started_ = false;
        std::atomic<bool> write_failed_{false};
    };
}
