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
#include <vector>

#include <windows.h>

namespace smedley::telemetry
{
    constexpr size_t kMaxRecordBytes = 1024;
    constexpr int kMinQueueCapacity = 64;
    constexpr int kMaxQueueCapacity = 8192;
    constexpr int kMaxSampleDays = 365;

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

    std::string EscapeJson(std::string_view value);
    std::string FormatEnvelope(const Envelope &envelope);
    bool ValidateConfig(const Config &config, std::string *error);
    bool ParseLaunchArguments(const std::vector<std::wstring> &arguments, Config *config, std::string *error);
    bool HasCategory(const Config &config, std::string_view category);
    bool HasCountryTag(const Config &config, std::string_view tag);
    bool IsDateInRange(const Config &config, std::optional<int> date);
    std::string UtcNow();
    uint64_t MonotonicMicroseconds();
    uint64_t QpcToMicroseconds(uint64_t counter, uint64_t frequency);
    bool ShouldSampleDate(std::optional<int> date, int sample_days, std::optional<int> *last_sampled_date);

    class BoundedQueue
    {
    public:
        explicit BoundedQueue(size_t capacity);
        bool Push(std::string_view line);
        bool TryPush(std::string_view line);
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
        bool TryWrite(std::string_view line);
        void Stop(const std::function<std::string(const QueueStats &)> &summary_builder = {});
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
