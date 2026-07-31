#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace smedley::scripting
{
    constexpr int kMinInstructionBudget = 1'000;
    constexpr int kMaxInstructionBudget = 10'000'000;
    constexpr size_t kMinMemoryBytes = 256 * 1024;
    constexpr size_t kMaxMemoryBytes = 64 * 1024 * 1024;
    constexpr size_t kMinQueueCapacity = 16;
    constexpr size_t kMaxQueueCapacity = 4096;
    constexpr size_t kMaxScripts = 16;
    constexpr size_t kMaxScriptBytes = 1024 * 1024;

    struct Config
    {
        std::vector<std::filesystem::path> scripts;
        int instruction_budget = 100'000;
        size_t memory_limit_bytes = 8 * 1024 * 1024;
        size_t queue_capacity = 256;
    };

    struct EventSnapshot
    {
        int date_raw = 0;
        int64_t treasury_raw = 0;
        uint32_t country_slot_count = 0;
        uint32_t ai_scheduler_entry_count = 0;
        std::array<char, 4> country_tag{};
        bool country_exists = false;
        bool human_control_present = false;
    };

    struct Stats
    {
        uint64_t accepted = 0;
        uint64_t processed = 0;
        uint64_t dropped = 0;
        uint64_t script_errors = 0;
        uint64_t disabled_scripts = 0;
        uint64_t high_water = 0;
        bool worker_failed = false;
    };

    enum class PauseResult
    {
        Completed = 1,
        OutsideCampaign,
        InvalidState,
        SignatureMismatch,
        ReadbackFailed,
        Shutdown,
    };

    class Runtime
    {
    public:
        using Log = std::function<void(bool failure, const std::string &message)>;
        using RequestPause = std::function<bool()>;
        using PauseResultConsumed = std::function<void()>;

        Runtime(Config config, Log log, RequestPause request_pause, PauseResultConsumed pause_result_consumed = {});
        ~Runtime();
        Runtime(const Runtime &) = delete;
        Runtime &operator=(const Runtime &) = delete;

        bool Start(std::string *error);
        bool TryPush(const EventSnapshot &event);
        void ReportPauseResult(PauseResult result);
        void Stop();
        Stats stats() const;

    private:
        struct Script;

        void Run();
        bool Initialize(std::string *error);
        void Process(const EventSnapshot &event);
        bool ProcessPauseResult();
        bool Pop(EventSnapshot *event);

        Config config_;
        Log log_;
        RequestPause request_pause_;
        PauseResultConsumed pause_result_consumed_;
        std::vector<EventSnapshot> queue_;
        std::vector<Script *> scripts_;
        size_t head_ = 0;
        size_t tail_ = 0;
        size_t size_ = 0;
        std::atomic<bool> started_{false};
        bool stopping_ = false;
        bool initialized_ = false;
        std::string initialization_error_;
        mutable std::mutex mutex_;
        std::condition_variable wake_;
        std::condition_variable initialized_wake_;
        std::thread thread_;
        std::atomic<uint64_t> accepted_{0};
        std::atomic<uint64_t> processed_{0};
        std::atomic<uint64_t> dropped_{0};
        std::atomic<uint64_t> script_errors_{0};
        std::atomic<uint64_t> disabled_scripts_{0};
        std::atomic<uint64_t> high_water_{0};
        std::atomic<int> pause_result_{0};
        std::atomic<bool> worker_failed_{false};
    };

    bool ValidateConfig(const Config &config, std::string *error);
    bool ParseLaunchArguments(const std::vector<std::wstring> &arguments, Config *config, std::string *error);
}
