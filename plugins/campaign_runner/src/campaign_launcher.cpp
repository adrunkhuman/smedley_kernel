#include "campaign_launcher.hpp"
#include "campaign_save_selection.hpp"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace campaign_runner
{
    namespace
    {
        namespace fs = std::filesystem;

        std::atomic<CampaignLauncher *> launcher_instance = nullptr;
        std::recursive_mutex launcher_callback_mutex;
    }

    CampaignLauncher::CampaignLauncher(smedley::Logger &logger) noexcept
        : logger_(logger)
    {
    }

    namespace
    {
        uint64_t MonotonicMicroseconds()
        {
            LARGE_INTEGER frequency{}, counter{};
            if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&counter) || frequency.QuadPart <= 0) return 0;
            const uint64_t ticks = static_cast<uint64_t>(counter.QuadPart);
            const uint64_t rate = static_cast<uint64_t>(frequency.QuadPart);
            return ticks / rate * 1000000ull + ticks % rate * 1000000ull / rate;
        }

        struct PopupSuppression
        {
            PopupSuppression &operator=(bool enabled) noexcept
            {
                smedley::game_state::SetCampaignMessagePopupSuppression(enabled);
                return *this;
            }
        } suppress_message_popups;

        struct SuppressedMessageCount
        {
            operator long() const noexcept { return smedley::game_state::CampaignSuppressedMessageCount(); }
            SuppressedMessageCount &operator=(long) noexcept
            {
                smedley::game_state::SetCampaignObserverMode(false);
                return *this;
            }
        } suppressed_message_count;
    }

    void __stdcall CampaignLauncher::NotifyFrontendControllerCaptured(smedley::game_state::FrontendControllerKind kind) noexcept
    {
        try {
            const std::lock_guard<std::recursive_mutex> lock(launcher_callback_mutex);
            auto *launcher = launcher_instance.load(std::memory_order_acquire);
            if (launcher != nullptr) launcher->OnFrontendControllerCaptured(kind);
        } catch (...) {
        }
    }

    void __stdcall CampaignLauncher::NotifyObserverAnnexation(int annexed_ordinal) noexcept
    {
        (void)annexed_ordinal;
    }

    void __stdcall CampaignLauncher::NotifyConsoleCommandManagerCaptured(
        smedley::game_state::CampaignConsoleCaptureStatus status) noexcept
    {
        auto *launcher = launcher_instance.load(std::memory_order_acquire);
        if (launcher != nullptr) launcher->console_capture_status_.store(static_cast<uint32_t>(status), std::memory_order_release);
    }

    bool CampaignLauncher::QueueConsoleCommand(uint32_t command, uint32_t argument_count, uint32_t arguments_valid,
                                               const char *first_argument) noexcept
    {
        uint32_t expected = 0;
        if (!console_request_state_.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) return false;
        console_request_argument_count_.store(argument_count, std::memory_order_relaxed);
        console_request_arguments_valid_.store(arguments_valid, std::memory_order_relaxed);
        for (uint32_t index = 0; index < SMEDLEY_CAMPAIGN_CONSOLE_MAX_ARGUMENT_BYTES; ++index)
            console_request_argument_[index].store(first_argument[index], std::memory_order_relaxed);
        console_request_state_.store(command + 2, std::memory_order_release);
        return true;
    }

    bool CampaignLauncher::Start(
        std::wstring save_path,
        bool observe,
        std::wstring observer_view_tag,
        int speed,
        bool start_paused,
        bool quit_after_run,
        CampaignRunCondition condition)
    {
        const std::lock_guard<std::recursive_mutex> lock(launcher_callback_mutex);
        save_path_ = std::move(save_path);
        observe_ = observe;
        target_speed_ = speed;
        start_paused_ = start_paused;
        quit_after_run_ = quit_after_run;
        run_condition_ = condition;
        observer_enabled_ = false;
        speed_ready_ = false;
        smedley::game_state::SetCampaignObserverMode(false);
        launcher_instance.store(this, std::memory_order_release);
        if (target_speed_ < 1 || target_speed_ > 5) {
            logger_.Failure("campaign speed must be from 1 through 5");
            launcher_instance.store(nullptr, std::memory_order_release);
            return false;
        }
        if (observe_ && start_paused_) {
            logger_.Failure("observer mode cannot start paused because its watchdog requires advancement");
            launcher_instance.store(nullptr, std::memory_order_release);
            return false;
        }
        if (run_condition_.requested() && (start_paused_ || !observer_view_tag.empty())) {
            logger_.Failure("benchmark target runs require unpaused start and do not support an initial view switch");
            launcher_instance.store(nullptr, std::memory_order_release);
            return false;
        }
        if (quit_after_run_ && !run_condition_.requested()) {
            logger_.Failure("quit-after-run requires a benchmark run target");
            launcher_instance.store(nullptr, std::memory_order_release);
            return false;
        }
        if (!observer_view_tag.empty()) {
            if (!observe || observer_view_tag.size() != 3) {
                logger_.Failure("observer view tag requires --observe and three ASCII alphanumeric characters");
                launcher_instance.store(nullptr, std::memory_order_release);
                return false;
            }
            for (const auto character : observer_view_tag) {
                if (!((character >= L'A' && character <= L'Z') || (character >= L'a' && character <= L'z')
                      || (character >= L'0' && character <= L'9'))) {
                    logger_.Failure("observer view tag requires --observe and three ASCII alphanumeric characters");
                    launcher_instance.store(nullptr, std::memory_order_release);
                    return false;
                }
                initial_observer_view_tag_.push_back(static_cast<char>(
                    character >= L'a' && character <= L'z'
                        ? character - L'a' + L'A'
                        : character));
            }
        }
        if (save_path_.empty()) {
            logger_.Warn("no unattended save argument found");
            return false;
        }
        if (smedley::game_state::InstallFrontendAutomationHooks()
            != smedley::game_state::FrontendOperationStatus::completed) {
            observe_ = false;
            launcher_instance.store(nullptr, std::memory_order_release);
            return false;
        }
        if (smedley::game_state::InstallCampaignAutomationHooks({
                &CampaignLauncher::NotifyObserverAnnexation,
                &CampaignLauncher::NotifyConsoleCommandManagerCaptured})
            != smedley::game_state::CampaignOperationStatus::completed) {
            logger_.Failure("observer/message hook signature mismatch; campaign automation disabled");
            smedley::game_state::RollbackFrontendAutomationHooks();
            observe_ = false;
            launcher_instance.store(nullptr, std::memory_order_release);
            return false;
        }

        observer_enabled_ = observe;
        smedley::game_state::SetCampaignObserverMode(observe);
        if (smedley::game_state::SetFrontendControllerCaptureCallback(&CampaignLauncher::NotifyFrontendControllerCaptured)
            != smedley::game_state::FrontendOperationStatus::completed) {
            logger_.Failure("frontend controller capture callback is unavailable");
            smedley::game_state::RollbackFrontendAutomationHooks();
            launcher_instance.store(nullptr, std::memory_order_release);
            return false;
        }
        logger_.Info("waiting for the frontend before unattended save loading");
        return true;
    }

    void CampaignLauncher::Stop()
    {
        {
            const std::lock_guard<std::recursive_mutex> lock(launcher_callback_mutex);
            if (observer_view_switch_pending_) {
                smedley::game_state::ObserverCountrySnapshot target{};
                if (smedley::game_state::ReadObserverCountry(observer_target_ordinal_, &target)
                        == smedley::game_state::ObserverObservationStatus::completed
                    && target.exists && target.human_controlled && !target.has_ai) {
                    smedley::game_state::ReturnObserverCountryToAI(target);
                }
            }
            if (save_timer_ != 0) {
                KillTimer(nullptr, save_timer_);
                save_timer_ = 0;
            }
            frontend_controller_ = {};
            main_menu_controller_ = {};
            observer_console_ready_ = false;
            launcher_instance.store(nullptr, std::memory_order_release);
        }
        smedley::game_state::DeactivateFrontendAutomation();
        smedley::game_state::DeactivateCampaignAutomation();
        // Plugin modules remain loaded. Leave callbacks inert rather than rewriting
        // executable memory without a process-wide quiescence protocol.
    }

    void CampaignLauncher::OnConsoleCommandManagerCaptured(
        smedley::game_state::CampaignConsoleCaptureStatus status)
    {
        logger_.Info("captured native console command manager");
        if (status == smedley::game_state::CampaignConsoleCaptureStatus::completed) {
            observer_console_ready_ = true;
            logger_.Info("registered observer-safe switch command and disabled native tag");
        } else if (status == smedley::game_state::CampaignConsoleCaptureStatus::command_conflict) {
            logger_.Failure("console command switch already exists; safe observer switching disabled");
        } else if (status == smedley::game_state::CampaignConsoleCaptureStatus::native_tag_unavailable) {
            logger_.Failure("native tag command is unavailable; safe observer switching disabled");
        }
    }

    void CampaignLauncher::PrepareObserverForAnnexation(int annexed_ordinal)
    {
        smedley::game_state::ObserverStateSnapshot before{};
        if (!observer_enabled_ || !observer_monitoring_ || observer_view_switch_pending_
            || smedley::game_state::ReadObserverState(&before)
                != smedley::game_state::ObserverObservationStatus::completed
            || annexed_ordinal != before.view_country.tag.ordinal) return;
        smedley::game_state::ObserverCountrySnapshot target{};
        if (smedley::game_state::FindHealthyObserverCountry(annexed_ordinal, &target)
            != smedley::game_state::ObserverObservationStatus::completed) {
            logger_.Failure("observer could not select a safe view before country annexation");
            return;
        }
        smedley::game_state::ObserverStateSnapshot after{};
        if (smedley::game_state::SetObserverViewCountry(target, &after)
                != smedley::game_state::ObserverOperationStatus::completed
            || after.human_control_present || after.country_ai_count != before.country_ai_count) {
            logger_.Failure("observer pre-annexation view handoff violated AI ownership state");
            return;
        }
        logger_.Info(std::string("observer view moved to ") + target.tag.str() + " before annexation");
    }

    bool CampaignLauncher::RequestObserverSwitch(std::string requested_tag, std::string *message)
    {
        const auto fail = [&](const char *text) { if (message != nullptr) *message = text; return false; };
        if (!observer_enabled_ || !observer_monitoring_) {
            return fail("observer not ready");
        }
        if (observer_view_switch_pending_) {
            return fail("switch already pending");
        }
        if (requested_tag.size() != 3) {
            return fail("TAG must be 3 ASCII alphanumeric characters");
        }
        for (auto &character : requested_tag) {
            if (!((character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
                  || (character >= '0' && character <= '9'))) {
                return fail("TAG must be 3 ASCII alphanumeric characters");
            }
            if (character >= 'a' && character <= 'z') {
                character = character - 'a' + 'A';
            }
        }

        smedley::game_state::ObserverStateSnapshot observer{};
        if (smedley::game_state::ReadObserverState(&observer)
            != smedley::game_state::ObserverObservationStatus::completed) {
            return fail("campaign unavailable");
        }
        smedley::game_state::ObserverCountrySnapshot target{};
        if (smedley::game_state::ResolveObserverCountry(requested_tag.c_str(), &target)
                != smedley::game_state::ObserverObservationStatus::completed
            || !target.exists) {
            return fail("country does not exist");
        }
        if (target.tag.ordinal == observer.view_country.tag.ordinal) {
            return fail("already viewing country");
        }
        if (!target.healthy_ai()) {
            return fail("target AI is not healthy");
        }

        smedley::game_state::CampaignRuntimeSnapshot runtime{};
        if (smedley::game_state::ReadCampaignRuntime(&runtime)
            != smedley::game_state::CampaignRuntimeObservationStatus::completed) {
            return fail("campaign unavailable");
        }
        const bool paused_by_command = !runtime.paused;
        if (smedley::game_state::SetCampaignPaused(true) != smedley::game_state::CampaignOperationStatus::completed) {
            return fail("failed to pause");
        }

        observer_target_ordinal_ = target.tag.ordinal;
        observer_target_tag_ = requested_tag;
        observer_ai_count_before_switch_ = observer.country_ai_count;
        observer_view_switch_pending_ = true;
        observer_attempts_ = 0;
        smedley::game_state::CampaignConsoleCommandResult result{};
        if (smedley::game_state::StartNativeObserverTagSwitch(target.tag, &result)
                != smedley::game_state::ObserverOperationStatus::completed
            || !result.success) {
            observer_view_switch_pending_ = false;
            observer_target_ordinal_ = 0;
            observer_target_tag_.clear();
            if (paused_by_command) {
                smedley::game_state::SetCampaignPaused(false);
            }
            return fail(result.message_available ? result.message : "native observer view switch failed");
        }
        logger_.Info(std::string("requested observer-safe switch to ") + requested_tag);
        if (message != nullptr) *message = "observer switch queued";
        return true;
    }

    bool CampaignLauncher::ScheduleTimer(UINT delay, const char *failure_message)
    {
        if (save_timer_ != 0) {
            KillTimer(nullptr, save_timer_);
            save_timer_ = 0;
        }
        save_timer_ = SetTimer(nullptr, 0, delay, SaveTimerCallback);
        if (save_timer_ != 0) {
            return true;
        }
        logger_.Failure(failure_message);
        return false;
    }

    bool CampaignLauncher::SelectSpeed()
    {
        smedley::game_state::CampaignRuntimeSnapshot runtime{};
        if (smedley::game_state::ReadCampaignRuntime(&runtime)
            != smedley::game_state::CampaignRuntimeObservationStatus::completed) {
            logger_.Failure("native campaign runtime observation is unavailable");
            return false;
        }
        const int speed = runtime.speed_index;
        const int previous_speed = speed + 1;
        const int target_index = target_speed_ - 1;
        if (smedley::game_state::SetCampaignSpeedIndex(target_index)
            != smedley::game_state::CampaignOperationStatus::completed) {
            logger_.Failure("native speed handler did not produce the requested speed index");
            return false;
        }
        ReportTelemetryResult(telemetry_.SpeedConfigured(previous_speed, target_speed_, target_speed_));
        logger_.Info(std::string("selected native speed ") + std::to_string(target_speed_));
        return true;
    }

    void CampaignLauncher::ReportTelemetryResult(SmedleyTelemetryResult result)
    {
        if (result == SMEDLEY_TELEMETRY_INVALID && !telemetry_invalid_logged_) {
            telemetry_invalid_logged_ = true;
            logger_.Warn("campaign lifecycle telemetry rejected an invalid ABI record");
        } else if (result == SMEDLEY_TELEMETRY_DROPPED && !telemetry_dropped_logged_) {
            telemetry_dropped_logged_ = true;
            logger_.Warn("campaign lifecycle telemetry queue dropped a record");
        }
    }

    bool CampaignLauncher::ObserverInvariantsValid() const
    {
        smedley::game_state::ObserverStateSnapshot observer{};
        return observer_ai_ready_ && !observer_view_switch_pending_
            && smedley::game_state::ReadObserverState(&observer)
                == smedley::game_state::ObserverObservationStatus::completed
            && !observer.human_control_present && observer.full_map_visibility_enabled
            && observer.view_country.healthy_ai() && observer.view_country.tag.normalized_candidate();
    }

    void CampaignLauncher::OnFrontendControllerCaptured(smedley::game_state::FrontendControllerKind kind)
    {
        if (kind != smedley::game_state::FrontendControllerKind::frontend || save_timer_ != 0 || save_attempts_ != 0) return;
        if (ScheduleTimer(10'000, "failed to schedule save loading on the frontend thread")) {
            logger_.Info("scheduled save loading on the frontend thread");
        }
    }

    void CampaignLauncher::DrainConsoleRequest()
    {
        uint32_t state = console_request_state_.load(std::memory_order_acquire);
        if (state < 2) return;
        if (state == SMEDLEY_CAMPAIGN_CONSOLE_NATIVE_TAG + 2) {
            logger_.Warn("blocked native tag command in observer mode");
            console_request_state_.store(0, std::memory_order_release);
            return;
        }
        std::string tag;
        for (uint32_t index = 0; index < SMEDLEY_CAMPAIGN_CONSOLE_MAX_ARGUMENT_BYTES; ++index) {
            const char character = console_request_argument_[index].load(std::memory_order_relaxed);
            if (character == '\0') break;
            tag.push_back(character);
        }
        if (console_request_arguments_valid_.load(std::memory_order_relaxed) == 0
            || console_request_argument_count_.load(std::memory_order_relaxed) != 1) {
            logger_.Warn("observer switch request rejected: usage: switch TAG");
            console_request_state_.store(0, std::memory_order_release);
            return;
        }
        console_request_state_.store(0, std::memory_order_release);
        std::string message;
        if (RequestObserverSwitch(std::move(tag), &message)) logger_.Info(message);
        else logger_.Warn("observer switch request rejected: " + message);
    }

    void CampaignLauncher::DrainHookWork()
    {
        const uint32_t status = console_capture_status_.exchange(UINT32_MAX, std::memory_order_acq_rel);
        if (status != UINT32_MAX) OnConsoleCommandManagerCaptured(
            static_cast<smedley::game_state::CampaignConsoleCaptureStatus>(status));
        DrainConsoleRequest();
    }

    bool CampaignLauncher::EmitObserverConfiguredIfReady()
    {
        smedley::game_state::ObserverStateSnapshot observer{};
        if (!ObserverInvariantsValid()
            || smedley::game_state::ReadObserverState(&observer)
                != smedley::game_state::ObserverObservationStatus::completed) return false;
        ReportTelemetryResult(telemetry_.ObserverConfigured(observer.view_country.tag.str()));
        return true;
    }

    void CampaignLauncher::StartBenchmark(const smedley::game_state::CampaignRuntimeSnapshot &runtime)
    {
        if (!run_condition_.requested() || benchmark_started_) return;
        const char *error = nullptr;
        const auto process_metrics = smedley::game_state::SampleProcessMetrics();
        if (!benchmark_.Begin(runtime.date_raw, run_condition_.days, run_condition_.target_date_raw,
                               run_condition_.timeout_seconds, MonotonicMicroseconds(), &error)) {
            logger_.Failure(std::string("benchmark did not start: ") + (error == nullptr ? "invalid target" : error));
            FinishInvalidBenchmark(runtime);
            return;
        }
        benchmark_started_ = true;
        benchmark_process_cpu_start_us_ = process_metrics.process_cpu_us;
        benchmark_working_set_start_bytes_ = process_metrics.working_set_bytes;
        benchmark_private_bytes_start_ = process_metrics.private_bytes;
        ReportTelemetryResult(telemetry_.BenchmarkStarted(benchmark_.start_date_raw(), benchmark_.target_date_raw(),
                                                          benchmark_.requested_days(), run_condition_.timeout_seconds));
        logger_.Info("benchmark started at raw date " + std::to_string(benchmark_.start_date_raw())
                     + " target=" + std::to_string(benchmark_.target_date_raw()));
        if (save_timer_ != 0) {
            KillTimer(nullptr, save_timer_);
            save_timer_ = 0;
        }
        if (!ScheduleTimer(USER_TIMER_MINIMUM, "failed to schedule benchmark timer")) {
            const auto pause_status = smedley::game_state::SetCampaignPaused(true);
            smedley::game_state::CampaignRuntimeSnapshot final_runtime{};
            const auto observation = smedley::game_state::ReadCampaignRuntime(&final_runtime);
            FinishBenchmark("timer_unavailable", observation == smedley::game_state::CampaignRuntimeObservationStatus::completed
                ? std::optional<int>(final_runtime.date_raw) : std::nullopt,
                pause_status == smedley::game_state::CampaignOperationStatus::completed
                    && observation == smedley::game_state::CampaignRuntimeObservationStatus::completed
                ? std::optional<bool>(final_runtime.paused) : std::nullopt);
        }
    }

    void CampaignLauncher::FinishInvalidBenchmark(const smedley::game_state::CampaignRuntimeSnapshot &runtime)
    {
        if (benchmark_terminal_) return;
        benchmark_terminal_ = true;
        if (save_timer_ != 0) {
            KillTimer(nullptr, save_timer_);
            save_timer_ = 0;
        }
        observer_monitoring_ = false;
        suppress_message_popups = false;
        const auto pause_status = smedley::game_state::SetCampaignPaused(true);
        smedley::game_state::CampaignRuntimeSnapshot final_runtime{};
        const auto observation = smedley::game_state::ReadCampaignRuntime(&final_runtime);
        const int actual = observation == smedley::game_state::CampaignRuntimeObservationStatus::completed
            ? final_runtime.date_raw : runtime.date_raw;
        const std::optional<bool> paused = pause_status == smedley::game_state::CampaignOperationStatus::completed
            && observation == smedley::game_state::CampaignRuntimeObservationStatus::completed
            ? std::optional<bool>(final_runtime.paused) : std::nullopt;
        const int target = run_condition_.target_date_raw.value_or(actual);
        ReportTelemetryResult(telemetry_.BenchmarkFailed(actual, target, actual, 1, "invalid_target", paused));
        logger_.Failure(std::string("benchmark failed: invalid_target; campaign remains ")
            + (paused && *paused ? "paused and open" : "open; pause state is unverified"));
    }

    void CampaignLauncher::FinishBenchmark(const char *reason, std::optional<int> actual_date_raw, std::optional<bool> paused)
    {
        if (benchmark_terminal_) return;
        benchmark_terminal_ = true;
        if (save_timer_ != 0) {
            KillTimer(nullptr, save_timer_);
            save_timer_ = 0;
        }
        observer_monitoring_ = false;
        suppress_message_popups = false;
        const auto process_metrics = smedley::game_state::SampleProcessMetrics();
        const uint64_t now = MonotonicMicroseconds();
        const int64_t elapsed = (std::max)(int64_t{1}, now >= benchmark_.start_monotonic_us()
            ? static_cast<int64_t>(now - benchmark_.start_monotonic_us()) : int64_t{0});
        std::optional<int64_t> process_cpu_us;
        if (benchmark_process_cpu_start_us_ && process_metrics.process_cpu_us
            && *process_metrics.process_cpu_us >= *benchmark_process_cpu_start_us_) {
            process_cpu_us = *process_metrics.process_cpu_us - *benchmark_process_cpu_start_us_;
        }
        ReportTelemetryResult(telemetry_.BenchmarkResources(actual_date_raw, process_cpu_us,
            benchmark_working_set_start_bytes_, process_metrics.working_set_bytes,
            benchmark_private_bytes_start_, process_metrics.private_bytes,
            process_metrics.process_peak_working_set_bytes));
        if (reason == nullptr) {
            ReportTelemetryResult(telemetry_.BenchmarkCompleted(benchmark_.start_date_raw(), benchmark_.target_date_raw(),
                actual_date_raw.value_or(benchmark_.target_date_raw()), benchmark_.requested_days(), elapsed));
            if (quit_after_run_ && actual_date_raw && *actual_date_raw == benchmark_.target_date_raw()) {
                QuitAfterRun();
            } else {
                logger_.Info("benchmark completed; campaign remains paused and open");
            }
        } else {
            ReportTelemetryResult(telemetry_.BenchmarkFailed(benchmark_.start_date_raw(), benchmark_.target_date_raw(),
                actual_date_raw, elapsed, reason, paused));
            logger_.Failure(std::string("benchmark failed: ") + reason + "; campaign remains open");
        }
    }

    bool CampaignLauncher::DrainTelemetryBeforeQuit()
    {
        constexpr uint32_t drain_timeout_ms = 5000;
        const auto result = telemetry_.Drain(drain_timeout_ms);
        if (TelemetryDrainAllowsQuit(result)) {
            logger_.Info(result == SMEDLEY_TELEMETRY_DRAIN_COMPLETED
                ? "telemetry drained before native game exit"
                : "telemetry drain is unavailable; continuing native game exit");
            return true;
        }
        const char *reason = result == SMEDLEY_TELEMETRY_DRAIN_BUSY ? "busy"
            : result == SMEDLEY_TELEMETRY_DRAIN_TIMEOUT ? "timeout" : "failure";
        logger_.Failure(std::string("telemetry pre-exit drain ended with ") + reason + "; campaign remains paused and open");
        return false;
    }

    void CampaignLauncher::QuitAfterRun()
    {
        smedley::game_state::CampaignRuntimeSnapshot runtime{};
        if (smedley::game_state::ReadCampaignRuntime(&runtime)
            != smedley::game_state::CampaignRuntimeObservationStatus::completed) {
            logger_.Failure("native quit request failed because CInGameIdler is unavailable; campaign remains paused and open");
            return;
        }
        // The paused UI-thread call cannot transition away from this validated idler while the synchronous drain waits.
        if (!DrainTelemetryBeforeQuit()) return;
        if (smedley::game_state::RequestCampaignQuit() != smedley::game_state::CampaignOperationStatus::completed) {
            logger_.Failure("native quit request did not set the expected state; campaign remains paused and open");
            return;
        }
        logger_.Info("requested native game exit after successful bounded run");
    }

    bool CampaignLauncher::TickBenchmark(
        const smedley::game_state::CampaignRuntimeSnapshot &runtime, bool observer_valid)
    {
        if (!benchmark_started_ || benchmark_terminal_) return benchmark_terminal_;
        const BenchmarkDecision decision = benchmark_.Observe({true, runtime.date_raw, runtime.paused ? 1 : 0,
                                                                 observer_valid, MonotonicMicroseconds()});
        if (decision.action == BenchmarkAction::Continue) return false;
        const auto pause_status = smedley::game_state::SetCampaignPaused(true);
        smedley::game_state::CampaignRuntimeSnapshot final_runtime{};
        const auto observation = smedley::game_state::ReadCampaignRuntime(&final_runtime);
        const int actual = observation == smedley::game_state::CampaignRuntimeObservationStatus::completed
            ? final_runtime.date_raw : runtime.date_raw;
        const std::optional<bool> paused = pause_status == smedley::game_state::CampaignOperationStatus::completed
            && observation == smedley::game_state::CampaignRuntimeObservationStatus::completed
            ? std::optional<bool>(final_runtime.paused) : std::nullopt;
        if (!paused || !*paused) {
            FinishBenchmark("pause_failed", actual, paused);
        } else if (decision.action == BenchmarkAction::Complete && actual == benchmark_.target_date_raw()) {
            FinishBenchmark(nullptr, actual, true);
        } else if (decision.action == BenchmarkAction::Complete) {
            FinishBenchmark(actual < benchmark_.target_date_raw() ? "date_regressed" : "date_overshoot", actual, true);
        } else {
            FinishBenchmark(decision.reason, actual, true);
        }
        return true;
    }

    void CALLBACK CampaignLauncher::SaveTimerCallback(HWND, UINT, UINT_PTR timer, DWORD) noexcept
    {
        try {
        const std::lock_guard<std::recursive_mutex> lock(launcher_callback_mutex);
        auto *launcher = launcher_instance.load(std::memory_order_acquire);
        if (launcher == nullptr || timer != launcher->save_timer_) {
            return;
        }
        launcher->DrainHookWork();
        if (launcher->play_requested_ && !launcher->observer_monitoring_ && !launcher->benchmark_.active()) {
            KillTimer(nullptr, timer);
            launcher->save_timer_ = 0;
        }
        if (launcher->play_requested_) {
            smedley::game_state::CampaignRuntimeSnapshot runtime{};
            if (smedley::game_state::ReadCampaignRuntime(&runtime)
                != smedley::game_state::CampaignRuntimeObservationStatus::completed) {
                if (launcher->benchmark_.active()) {
                    launcher->benchmark_.Observe({false, std::nullopt, -1, false, MonotonicMicroseconds()});
                    launcher->FinishBenchmark("idler_unavailable", std::nullopt, std::nullopt);
                    return;
                }
                if (launcher->observer_monitoring_) {
                    KillTimer(nullptr, timer);
                    launcher->save_timer_ = 0;
                    suppress_message_popups = false;
                    launcher->observer_monitoring_ = false;
                    launcher->logger_.Failure("observer campaign left CInGameIdler");
                    return;
                }
                ++launcher->campaign_attempts_;
                if (launcher->campaign_attempts_ < 30) {
                    launcher->ScheduleTimer(1'000, "failed to schedule campaign-entry check");
                } else {
                    launcher->logger_.Failure("campaign did not enter CInGameIdler within 30 seconds");
                }
                return;
            }
            auto pause_state = runtime.paused ? 1 : 0;
            if (!launcher->pause_before_configuration_) launcher->pause_before_configuration_ = runtime.paused;
            launcher->ReportTelemetryResult(launcher->telemetry_.Entered(
                launcher->observer_enabled_, launcher->target_speed_, launcher->start_paused_));
            const bool observer_recovery_pending = launcher->observer_monitoring_ && pause_state == 1;
            if (launcher->benchmark_.active() && !observer_recovery_pending
                && launcher->TickBenchmark(runtime, !launcher->observer_enabled_ || launcher->ObserverInvariantsValid())) return;
            if (launcher->observer_monitoring_) {
                if (launcher->benchmark_.active()) {
                    const uint64_t now = MonotonicMicroseconds();
                    if (now < launcher->next_observer_watchdog_us_) return;
                    launcher->next_observer_watchdog_us_ = now + 1000000ull;
                }
                const auto suppressed = suppressed_message_count;
                if (suppressed != launcher->observed_suppressed_messages_) {
                    std::ostringstream message;
                    message << "observer mode suppressed "
                            << suppressed - launcher->observed_suppressed_messages_
                            << " generic message popup(s), total=" << suppressed;
                    launcher->logger_.Info(message.str());
                    launcher->observed_suppressed_messages_ = suppressed;
                }
                const auto stop_monitoring = [&](const char *reason, bool restore_target) {
                    smedley::game_state::ObserverCountrySnapshot target{};
                    const auto target_status = smedley::game_state::ReadObserverCountry(
                        launcher->observer_target_ordinal_, &target);
                    if (restore_target
                        && target_status == smedley::game_state::ObserverObservationStatus::completed
                        && target.exists && target.human_controlled && !target.has_ai) {
                        smedley::game_state::ReturnObserverCountryToAI(target);
                    }
                    smedley::game_state::ObserverStateSnapshot observer{};
                    const bool ownership_clean = smedley::game_state::ReadObserverState(&observer)
                            == smedley::game_state::ObserverObservationStatus::completed
                        && !observer.human_control_present
                        && (!restore_target || target_status != smedley::game_state::ObserverObservationStatus::completed
                            || target.healthy_ai());
                    KillTimer(nullptr, timer);
                    launcher->save_timer_ = 0;
                    suppress_message_popups = false;
                    launcher->observer_monitoring_ = false;
                    launcher->observer_view_switch_pending_ = false;
                    launcher->observer_target_ordinal_ = 0;
                    launcher->observer_target_tag_.clear();
                    launcher->logger_.Failure(
                        std::string(reason)
                        + (ownership_clean ? "" : "; observer ownership cleanup failed"));
                };
                if (!launcher->initial_observer_view_tag_.empty()
                    && !launcher->observer_view_switch_pending_) {
                    std::string result;
                    const bool switched = launcher->RequestObserverSwitch(launcher->initial_observer_view_tag_, &result);
                    launcher->initial_observer_view_tag_.clear();
                    if (!switched) {
                        stop_monitoring(result.c_str(), false);
                    }
                    return;
                }
                smedley::game_state::ObserverStateSnapshot observer{};
                const auto observer_status = smedley::game_state::ReadObserverState(&observer);
                if (launcher->observer_view_switch_pending_
                    || observer_status != smedley::game_state::ObserverObservationStatus::completed
                    || !observer.view_country.exists) {
                    if (pause_state == 0) {
                        if (smedley::game_state::SetCampaignPaused(true)
                            == smedley::game_state::CampaignOperationStatus::completed) pause_state = 1;
                    }
                    if (pause_state != 1) {
                        stop_monitoring("failed to pause for observer view failover", false);
                        return;
                    }

                    if (!smedley::game_state::IsCampaignObserverConsoleReady()) {
                        stop_monitoring("native console command manager is unavailable", false);
                        return;
                    }
                    if (!launcher->observer_view_switch_pending_) {
                        smedley::game_state::ObserverCountrySnapshot target{};
                        if (smedley::game_state::FindHealthyObserverCountry(-1, &target)
                            != smedley::game_state::ObserverObservationStatus::completed) {
                            stop_monitoring("no living AI country is available for observer view failover", false);
                            return;
                        }
                        launcher->observer_target_ordinal_ = target.tag.ordinal;
                        launcher->observer_target_tag_ = target.tag.str();
                        launcher->observer_ai_count_before_switch_ = observer.country_ai_count;
                        launcher->observer_view_switch_pending_ = true;
                        launcher->observer_attempts_ = 0;
                        smedley::game_state::CampaignConsoleCommandResult result{};
                        if (smedley::game_state::StartNativeObserverTagSwitch(target.tag, &result)
                                != smedley::game_state::ObserverOperationStatus::completed
                            || !result.success) {
                            launcher->observer_view_switch_pending_ = false;
                            stop_monitoring("native observer view failover command failed", false);
                            return;
                        }
                        launcher->logger_.Info(
                            std::string("requested observer view failover to ")
                            + launcher->observer_target_tag_);
                        return;
                    }

                    ++launcher->observer_attempts_;
                    if (observer_status != smedley::game_state::ObserverObservationStatus::completed
                        || observer.view_country.tag.ordinal != launcher->observer_target_ordinal_) {
                        if (launcher->observer_attempts_ == 30) {
                            launcher->logger_.Warn(
                                "observer view failover is still pending; simulation remains paused");
                        }
                        return;
                    }
                    smedley::game_state::ObserverCountrySnapshot target{};
                    if (smedley::game_state::ReadObserverCountry(launcher->observer_target_ordinal_, &target)
                            != smedley::game_state::ObserverObservationStatus::completed
                        || !target.exists || !target.human_controlled || target.has_ai
                        || observer.country_ai_count + 1 != launcher->observer_ai_count_before_switch_) {
                        stop_monitoring("native tag switch left unexpected observer ownership state", true);
                        return;
                    }
                    smedley::game_state::ObserverStateSnapshot restored{};
                    if (smedley::game_state::ReturnObserverCountryToAI(target, &restored)
                            != smedley::game_state::ObserverOperationStatus::completed
                        || restored.human_control_present
                        || restored.country_ai_count != launcher->observer_ai_count_before_switch_) {
                        stop_monitoring("observer view failover did not restore target AI", true);
                        return;
                    }
                    launcher->logger_.Info(
                        std::string("observer view failed over to ")
                        + launcher->observer_target_tag_ + " and restored its AI");
                    launcher->observer_view_switch_pending_ = false;
                    launcher->observer_target_ordinal_ = 0;
                    launcher->observer_target_tag_.clear();
                    launcher->observer_attempts_ = 0;
                    if (smedley::game_state::SetCampaignPaused(false)
                        != smedley::game_state::CampaignOperationStatus::completed) {
                        stop_monitoring("observer view failover could not resume simulation", false);
                        return;
                    }
                    return;
                }
                if (pause_state == 1) {
                    if (smedley::game_state::SetCampaignPaused(false)
                        != smedley::game_state::CampaignOperationStatus::completed) {
                        stop_monitoring("observer simulation could not recover from an unexpected pause", false);
                        return;
                    }
                    launcher->logger_.Warn("observer simulation recovered from an unexpected pause");
                } else if (pause_state != 0) {
                    stop_monitoring("observer simulation has an invalid pause state", false);
                    return;
                }
                if (launcher->EmitObserverConfiguredIfReady() && !launcher->benchmark_started_) {
                    launcher->StartBenchmark(runtime);
                }
                return;
            }
            if (launcher->observe_) {
                if (!launcher->observer_console_ready_) {
                    ++launcher->observer_attempts_;
                    if (launcher->observer_attempts_ < 30) {
                        launcher->ScheduleTimer(1'000, "failed to schedule observer console setup");
                    } else {
                        launcher->logger_.Failure(
                            "safe observer console commands were not installed within 30 seconds");
                    }
                    return;
                }
                launcher->observer_attempts_ = 0;
                if (pause_state == 0) {
                    if (smedley::game_state::SetCampaignPaused(true)
                        != smedley::game_state::CampaignOperationStatus::completed) {
                        launcher->logger_.Failure("failed to pause campaign before observer switch");
                        return;
                    }
                    launcher->logger_.Info("paused campaign before observer switch");
                } else if (pause_state != 1) {
                    launcher->logger_.Failure("CInGameIdler pause state is neither paused nor unpaused");
                    return;
                }
                if (!launcher->observer_ai_ready_) {
                    smedley::game_state::ObserverStateSnapshot before{};
                    if (smedley::game_state::ReadObserverState(&before)
                            != smedley::game_state::ObserverObservationStatus::completed
                        || before.view_country.tag.ordinal <= 0) {
                        launcher->logger_.Failure("current player country is invalid for observer mode");
                        return;
                    }
                    if (!before.view_country.human_controlled || before.view_country.has_ai) {
                        launcher->logger_.Failure("player country was not in the expected human-controlled state");
                        return;
                    }
                    smedley::game_state::ObserverStateSnapshot after{};
                    if (smedley::game_state::ReturnObserverCountryToAI(before.view_country, &after)
                            != smedley::game_state::ObserverOperationStatus::completed
                        || after.human_control_present || !after.view_country.healthy_ai()
                        || after.country_ai_count != before.country_ai_count + 1) {
                        launcher->logger_.Failure("native observer transition did not restore full AI control");
                        return;
                    }
                    std::ostringstream message;
                    message << "observer mode restored AI control for " << before.view_country.tag.str()
                            << " scheduler_count=" << before.country_ai_count
                            << "->" << after.country_ai_count;
                    launcher->logger_.Info(message.str());
                    launcher->observer_ai_ready_ = true;
                }

                smedley::game_state::ObserverStateSnapshot observer{};
                if (smedley::game_state::ReadObserverState(&observer)
                    != smedley::game_state::ObserverObservationStatus::completed) {
                    launcher->logger_.Failure("observer state is unavailable before FOW setup");
                    return;
                }
                if (!observer.full_map_visibility_enabled) {
                    if (!smedley::game_state::IsCampaignObserverConsoleReady()) {
                        ++launcher->observer_attempts_;
                        if (launcher->observer_attempts_ >= 30) {
                            launcher->logger_.Failure("native console command manager is unavailable");
                            return;
                        }
                        launcher->ScheduleTimer(1'000, "failed to schedule observer FOW setup");
                        return;
                    }
                    if (smedley::game_state::EnableObserverFullMapVisibility()
                        != smedley::game_state::ObserverOperationStatus::completed) {
                        launcher->logger_.Failure("native FOW command did not enable full map visibility");
                        return;
                    }
                }
                launcher->logger_.Info("observer mode enabled full map visibility");
                suppress_message_popups = true;
                launcher->observe_ = false;
            }
            if (!launcher->speed_ready_) {
                if (!launcher->SelectSpeed()) {
                    suppress_message_popups = false;
                    return;
                }
                launcher->speed_ready_ = true;
            }
            if (launcher->start_paused_) {
                if (pause_state == 0) {
                    if (smedley::game_state::SetCampaignPaused(true)
                        == smedley::game_state::CampaignOperationStatus::completed) pause_state = 1;
                }
                if (pause_state != 1) {
                    suppress_message_popups = false;
                    launcher->logger_.Failure("could not leave campaign paused at requested start state");
                } else {
                    launcher->logger_.Info("left campaign paused at requested start state");
                    if (!launcher->final_pause_recorded_) {
                        launcher->final_pause_recorded_ = true;
                        launcher->ReportTelemetryResult(launcher->telemetry_.PauseConfigured(
                            launcher->pause_before_configuration_.value_or(true), true, true));
                    }
                }
                return;
            }
            if (pause_state == 0) {
                launcher->logger_.Info("campaign is already unpaused");
                if (!launcher->final_pause_recorded_) {
                    launcher->final_pause_recorded_ = true;
                    launcher->ReportTelemetryResult(launcher->telemetry_.PauseConfigured(
                        launcher->pause_before_configuration_.value_or(false), false, false));
                }
                if (launcher->observer_ai_ready_) {
                    launcher->observer_monitoring_ = true;
                    if (!launcher->ScheduleTimer(1'000, "failed to schedule observer pause watchdog")) {
                        suppress_message_popups = false;
                        launcher->observer_monitoring_ = false;
                    }
                }
                launcher->EmitObserverConfiguredIfReady();
                launcher->StartBenchmark(runtime);
                return;
            }
            if (pause_state != 1) {
                suppress_message_popups = false;
                launcher->logger_.Failure("CInGameIdler pause state is neither paused nor unpaused");
                return;
            }
            if (smedley::game_state::SetCampaignPaused(false)
                == smedley::game_state::CampaignOperationStatus::completed) {
                launcher->logger_.Info("unpaused campaign through CInGameIdler");
                if (!launcher->final_pause_recorded_) {
                    launcher->final_pause_recorded_ = true;
                    launcher->ReportTelemetryResult(launcher->telemetry_.PauseConfigured(
                        launcher->pause_before_configuration_.value_or(true), false, false));
                }
                if (launcher->observer_ai_ready_) {
                    launcher->observer_monitoring_ = true;
                    if (!launcher->ScheduleTimer(1'000, "failed to schedule observer pause watchdog")) {
                        suppress_message_popups = false;
                        launcher->observer_monitoring_ = false;
                    }
                }
                launcher->EmitObserverConfiguredIfReady();
                launcher->StartBenchmark(runtime);
            } else {
                suppress_message_popups = false;
                launcher->logger_.Failure("CInGameIdler remained paused after toggle");
            }
            return;
        }
        if (launcher->frontend_controller_.value == 0) {
            const auto acquire_frontend = smedley::game_state::AcquireFrontendController(
                smedley::game_state::FrontendControllerKind::frontend, &launcher->frontend_controller_);
            if (acquire_frontend != smedley::game_state::FrontendOperationStatus::completed) {
                launcher->ScheduleTimer(1'000, "failed to schedule frontend controller check");
                return;
            }
        }
        if (!launcher->lobby_requested_) {
            if (smedley::game_state::AcquireFrontendController(
                    smedley::game_state::FrontendControllerKind::main_menu, &launcher->main_menu_controller_)
                != smedley::game_state::FrontendOperationStatus::completed) {
                launcher->ScheduleTimer(1'000, "failed to schedule main-menu controller check");
                return;
            }
            if (smedley::game_state::DispatchMainMenuSinglePlayer(launcher->main_menu_controller_)
                != smedley::game_state::FrontendOperationStatus::completed) {
                launcher->logger_.Failure("main-menu Single Player signal failed runtime validation");
                return;
            }
            launcher->lobby_requested_ = true;
            launcher->ScheduleTimer(3'000, "failed to schedule lobby save selection");
            return;
        }
        if (!launcher->save_selection_requested_) {
            const auto filename = fs::path(launcher->save_path_).filename().string();
            smedley::game_state::FrontendSaveSnapshot save{};
            if (filename.empty() || filename.size() > 259
                || smedley::game_state::ObserveFrontendSave(launcher->frontend_controller_, &save)
                    != smedley::game_state::FrontendOperationStatus::completed) {
                launcher->logger_.Failure("frontend save-selection fields failed runtime validation");
                return;
            }
            if (save.request_pending || save.completed) {
                launcher->logger_.Failure("frontend save-selection flags were not idle");
                return;
            }
            if (!CanSelectRequestedSave(filename, save.selected_basename)) {
                launcher->logger_.Failure("frontend save selection already names a different save; automation stopped");
                return;
            }
            if (smedley::game_state::RequestFrontendSave(launcher->frontend_controller_, filename.c_str())
                != smedley::game_state::FrontendOperationStatus::completed) {
                launcher->logger_.Failure("frontend save-selection request failed postcondition validation");
                return;
            }
            launcher->save_selection_requested_ = true;
            launcher->ReportTelemetryResult(launcher->telemetry_.SaveSelectionRequested());
            launcher->ScheduleTimer(5'000, "failed to schedule save-selection check");
            return;
        }
        smedley::game_state::FrontendSaveSnapshot save{};
        if (smedley::game_state::ObserveFrontendSave(launcher->frontend_controller_, &save)
            != smedley::game_state::FrontendOperationStatus::completed) {
            launcher->logger_.Failure("frontend save-selection status failed runtime validation");
            return;
        }
        if (save.completed) {
            if (save.request_pending) {
                launcher->logger_.Failure("frontend reported save completion while the request remained active");
                return;
            }
            launcher->ReportTelemetryResult(launcher->telemetry_.SaveLoadCompleted());
            if (smedley::game_state::DispatchFrontendControl(launcher->frontend_controller_, "play_button")
                != smedley::game_state::FrontendOperationStatus::completed) {
                launcher->logger_.Failure("play_button native signal dispatch failed runtime validation");
                return;
            }
            launcher->play_requested_ = true;
            smedley::game_state::ReleaseFrontendController(launcher->frontend_controller_);
            launcher->frontend_controller_ = {};
            launcher->ScheduleTimer(1'000, "failed to schedule campaign unpause");
            return;
        }
        ++launcher->save_attempts_;
        if (launcher->save_attempts_ < 24) {
            launcher->ScheduleTimer(5'000, "failed to schedule save-selection check");
        } else {
            launcher->logger_.Failure("save selection did not finish within 120 seconds");
        }
        } catch (...) {}
    }

}
