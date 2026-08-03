#include "scripting_runtime.hpp"

#include <smedley/events/dailyupdate.hpp>
#include <smedley/memory.hpp>
#include <smedley/plugin.hpp>
#include <smedley/v2/country.hpp>
#include <smedley/v2/gamestate.hpp>

#include <shellapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace scripting_plugin
{
    namespace
    {
        bool IsReadable(const void *pointer, size_t size)
        {
            if (pointer == nullptr || size == 0) return false;
            const uintptr_t begin = reinterpret_cast<uintptr_t>(pointer);
            if (begin > (std::numeric_limits<uintptr_t>::max)() - size) return false;
            const uintptr_t end = begin + size;
            for (uintptr_t cursor = begin; cursor < end;) {
                MEMORY_BASIC_INFORMATION region{};
                if (VirtualQuery(reinterpret_cast<const void *>(cursor), &region, sizeof(region)) != sizeof(region)
                    || region.State != MEM_COMMIT
                    || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
                    || (region.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
                        | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) return false;
                const uintptr_t region_begin = reinterpret_cast<uintptr_t>(region.BaseAddress);
                if (region_begin > (std::numeric_limits<uintptr_t>::max)() - region.RegionSize) return false;
                const uintptr_t region_end = region_begin + region.RegionSize;
                if (region_end <= cursor) return false;
                cursor = (std::min)(end, region_end);
            }
            return true;
        }

        bool CopyReadable(void *destination, const void *source, size_t size)
        {
            if (!IsReadable(source, size)) return false;
            __try {
                std::memcpy(destination, source, size);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        template <typename T>
        bool ReadValue(uintptr_t address, T *value)
        {
            return value != nullptr && CopyReadable(value, reinterpret_cast<const void *>(address), sizeof(T));
        }

        std::vector<std::wstring> CommandLineArguments()
        {
            int count = 0;
            wchar_t **values = CommandLineToArgvW(GetCommandLineW(), &count);
            if (values == nullptr) return {};
            std::vector<std::wstring> arguments(values + 1, values + count);
            LocalFree(values);
            return arguments;
        }

        bool IsInGameIdler(const void *object)
        {
            constexpr char expected[] = ".?AVCInGameIdler@@";
            uintptr_t vtable = 0;
            uintptr_t locator = 0;
            uintptr_t type_descriptor = 0;
            std::array<char, sizeof(expected)> type_name{};
            const uintptr_t object_address = reinterpret_cast<uintptr_t>(object);
            if (!ReadValue(object_address, &vtable) || vtable < sizeof(uintptr_t)
                || !ReadValue(vtable - sizeof(uintptr_t), &locator)
                || locator > (std::numeric_limits<uintptr_t>::max)() - 0x0c
                || !ReadValue(locator + 0x0c, &type_descriptor)
                || type_descriptor > (std::numeric_limits<uintptr_t>::max)() - 0x08
                || !CopyReadable(type_name.data(), reinterpret_cast<const void *>(type_descriptor + 0x08),
                                 type_name.size())) return false;
            return std::memcmp(type_name.data(), expected, sizeof(expected)) == 0;
        }

        bool PauseSignatureMatches()
        {
            constexpr unsigned char expected[] = {0x55, 0x8b, 0xec, 0x64, 0xa1, 0x00, 0x00, 0x00, 0x00};
            const auto address = smedley::memory::Map::base_addr + 0x26a2c0;
            return std::memcmp(reinterpret_cast<const void *>(address), expected, sizeof(expected)) == 0;
        }
    }

    class Plugin final : public smedley::Plugin
    {
    public:
        void OnLoad() override
        {
            const auto arguments = CommandLineArguments();
            const bool configured = std::any_of(arguments.begin(), arguments.end(), [](const std::wstring &argument) {
                return argument.rfind(L"-smedley-script", 0) == 0;
            });
            if (!configured) return;
            smedley::scripting::Config config;
            std::string error;
            if (!smedley::scripting::ParseLaunchArguments(arguments, &config, &error)) {
                throw std::runtime_error("scripting arguments are invalid: " + error);
            }
            if (!PauseSignatureMatches()) throw std::runtime_error("scripting pause operation signature does not match");
            pause_request_state_.store(0, std::memory_order_release);
            runtime_ = std::make_unique<smedley::scripting::Runtime>(std::move(config),
                [this](bool failure, const std::string &message) {
                    Log(failure, message);
                },
                [this] {
                    int expected = 0;
                    return pause_request_state_.compare_exchange_strong(expected, 1, std::memory_order_acq_rel);
                },
                [this] {
                    int expected = 3;
                    pause_request_state_.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
                });
            if (!runtime_->Start(&error)) {
                pause_request_state_.store(2, std::memory_order_release);
                runtime_.reset();
                throw std::runtime_error("scripting runtime did not start: " + error);
            }
            try {
                AddEventHandler<smedley::events::DailyUpdateEvent>(
                    "scripting.daily", [this](smedley::events::DailyUpdateEvent &event) { OnDailyUpdate(event); });
            } catch (...) {
                pause_request_state_.store(2, std::memory_order_release);
                runtime_->Stop();
                runtime_.reset();
                throw;
            }
            logger().Info("started constrained Lua 5.1 scripting worker");
        }

        void OnUnload() override
        {
            const int previous_pause_state = pause_request_state_.exchange(2, std::memory_order_acq_rel);
            RemoveEventHandler<smedley::events::DailyUpdateEvent>("scripting.daily");
            if (!runtime_) return;
            if (previous_pause_state == 1) runtime_->ReportPauseResult(smedley::scripting::PauseResult::Shutdown);
            runtime_->Stop();
            const auto stats = runtime_->stats();
            logger().Info("scripting stopped: accepted=" + std::to_string(stats.accepted)
                + " processed=" + std::to_string(stats.processed) + " dropped=" + std::to_string(stats.dropped)
                + " script_errors=" + std::to_string(stats.script_errors)
                + " disabled_scripts=" + std::to_string(stats.disabled_scripts)
                + " high_water=" + std::to_string(stats.high_water)
                + " worker_failed=" + (stats.worker_failed ? "true" : "false"));
            runtime_.reset();
        }

    private:
        void Log(bool failure, const std::string &message)
        {
            std::lock_guard<std::mutex> lock(log_mutex_);
            if (failure) logger().Failure(message);
            else logger().Info(message);
        }

        void ApplyPauseRequest()
        {
            int expected = 1;
            if (!pause_request_state_.compare_exchange_strong(expected, 3, std::memory_order_acq_rel)) return;
            auto *game_state = smedley::v2::CCurrentGameState::instance();
            auto *idler = game_state == nullptr ? nullptr : game_state->idler();
            if (!IsInGameIdler(idler)) {
                runtime_->ReportPauseResult(smedley::scripting::PauseResult::OutsideCampaign);
                return;
            }
            const auto previous = idler->pause_state();
            if (previous != 0 && previous != 1) {
                runtime_->ReportPauseResult(smedley::scripting::PauseResult::InvalidState);
                return;
            }
            if (!PauseSignatureMatches()) {
                runtime_->ReportPauseResult(smedley::scripting::PauseResult::SignatureMismatch);
                return;
            }
            if (previous == 0) idler->TogglePause();
            if (idler->pause_state() != 1) {
                runtime_->ReportPauseResult(smedley::scripting::PauseResult::ReadbackFailed);
                return;
            }
            runtime_->ReportPauseResult(smedley::scripting::PauseResult::Completed);
        }

        void OnDailyUpdate(smedley::events::DailyUpdateEvent &event)
        {
            if (!runtime_) return;
            ApplyPauseRequest();
            auto *game_state = smedley::v2::CCurrentGameState::instance();
            auto *country = event.GetCountry();
            if (game_state == nullptr || country == nullptr) return;
            smedley::scripting::EventSnapshot snapshot;
            snapshot.date_raw = game_state->current_date_raw();
            snapshot.treasury_raw = country->treasury_raw();
            snapshot.country_slot_count = static_cast<uint32_t>(game_state->country_count());
            snapshot.ai_scheduler_entry_count = static_cast<uint32_t>(game_state->country_ai_count());
            snapshot.country_tag = {country->tag().str()[0], country->tag().str()[1], country->tag().str()[2], '\0'};
            snapshot.country_exists = country->exists();
            snapshot.human_control_present = game_state->has_human_controlled_country();
            runtime_->TryPush(snapshot);
        }

        std::unique_ptr<smedley::scripting::Runtime> runtime_;
        // Pause-request states: 0 = accepted, 1 = pending, 2 = closed, and
        // 3 = awaiting worker-side result consumption.
        std::atomic<int> pause_request_state_{2};
        std::mutex log_mutex_;
    };
}

PLUGIN_API smedley::Plugin *CreatePlugin()
{
    return new scripting_plugin::Plugin{};
}
