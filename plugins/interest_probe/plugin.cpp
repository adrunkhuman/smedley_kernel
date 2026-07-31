#include "probe_core.hpp"

#include <smedley/events/dailyupdate.hpp>
#include <smedley/plugin.hpp>
#include <smedley/v2/gamestate.hpp>

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <thread>

namespace interest_probe
{
    namespace
    {
        constexpr uint32_t queue_capacity = 1024;
    }

    class Plugin final : public smedley::Plugin
    {
    public:
        void OnLoad() override
        {
            output_.open("interest_probe.csv", std::ios::trunc);
            if (!output_) throw std::runtime_error("cannot open interest_probe.csv in the game directory");
            output_ << "date_raw,country,state_count_reported,states_walked,province_element_candidates,states_with_savings,"
                       "states_with_interest,creditor_count,treasury_raw,state_savings_candidate_raw,"
                       "state_interest_candidate_raw,bank_interest_raw,flags,collection_us,dropped_samples\n";
            output_.flush();
            if (!output_) throw std::runtime_error("cannot initialize interest_probe.csv in the game directory");
            QueryPerformanceFrequency(&performance_frequency_);
            worker_ = std::thread([this] { WriteSamples(); });
            try {
                AddEventHandler<smedley::events::DailyUpdateEvent>(
                    "interest_probe.daily", [this](smedley::events::DailyUpdateEvent &event) { OnDailyUpdate(event); });
            } catch (...) {
                stop_.store(true, std::memory_order_release);
                worker_.join();
                throw;
            }
            logger().Info("writing bounded provisional interest observations to interest_probe.csv");
        }

        void OnUnload() override
        {
            RemoveEventHandler<smedley::events::DailyUpdateEvent>("interest_probe.daily");
            stop_.store(true, std::memory_order_release);
            if (worker_.joinable()) worker_.join();
            output_.flush();
            if (!output_) ReportWriteFailure("interest probe final output flush failed");
        }

    private:
        void OnDailyUpdate(smedley::events::DailyUpdateEvent &event)
        {
            LARGE_INTEGER started{};
            LARGE_INTEGER finished{};
            QueryPerformanceCounter(&started);
            const auto *game_state = smedley::v2::CCurrentGameState::instance();
            Sample sample = CollectSample(event.GetCountry(), game_state == nullptr ? 0 : game_state->current_date_raw());
            if (game_state == nullptr) sample.flags |= SAMPLE_DATE_UNAVAILABLE;
            QueryPerformanceCounter(&finished);
            if (performance_frequency_.QuadPart > 0) {
                const uint64_t elapsed = static_cast<uint64_t>(finished.QuadPart - started.QuadPart);
                sample.collection_us = static_cast<uint32_t>((std::min)(
                    elapsed * 1000000u / static_cast<uint64_t>(performance_frequency_.QuadPart),
                    static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())));
            }
            TryPush(sample);
        }

        void TryPush(const Sample &sample)
        {
            if (write_failed_.load(std::memory_order_acquire)) {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            const uint32_t write = write_.load(std::memory_order_relaxed);
            const uint32_t next = (write + 1) % queue_capacity;
            if (next == read_.load(std::memory_order_acquire)) {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            queue_[write] = sample;
            write_.store(next, std::memory_order_release);
        }

        void WriteSamples()
        {
            while (!stop_.load(std::memory_order_acquire) || read_.load(std::memory_order_relaxed) != write_.load(std::memory_order_acquire)) {
                if (write_failed_.load(std::memory_order_acquire)) {
                    uint32_t read = read_.load(std::memory_order_relaxed);
                    const uint32_t write = write_.load(std::memory_order_acquire);
                    while (read != write) {
                        read = (read + 1) % queue_capacity;
                        dropped_.fetch_add(1, std::memory_order_relaxed);
                    }
                    read_.store(read, std::memory_order_release);
                    if (!stop_.load(std::memory_order_acquire)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                bool wrote = false;
                uint32_t read = read_.load(std::memory_order_relaxed);
                const uint32_t write = write_.load(std::memory_order_acquire);
                while (read != write) {
                    const Sample &sample = queue_[read];
                    if ((sample.flags & SAMPLE_DATE_UNAVAILABLE) == 0) output_ << sample.date_raw;
                    output_ << ',' << sample.country_tag << ',' << sample.state_count_reported << ','
                            << sample.states_walked << ',' << sample.province_element_candidates << ',' << sample.states_with_savings << ','
                            << sample.states_with_interest << ',' << sample.creditor_count << ',' << sample.treasury_raw << ','
                            << sample.state_savings_raw << ',' << sample.state_interest_raw << ',' << sample.bank_interest_raw << ",0x"
                            << std::hex << sample.flags << std::dec << ',' << sample.collection_us << ','
                            << dropped_.load(std::memory_order_relaxed) << '\n';
                    if (!output_) {
                        ReportWriteFailure("interest probe output failed; subsequent samples are being dropped");
                        break;
                    }
                    read = (read + 1) % queue_capacity;
                    read_.store(read, std::memory_order_release);
                    wrote = true;
                }
                if (wrote && !write_failed_.load(std::memory_order_acquire)) {
                    output_.flush();
                    if (!output_) {
                        ReportWriteFailure("interest probe output flush failed; subsequent samples are being dropped");
                    }
                }
                else std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        void ReportWriteFailure(const char *message) noexcept
        {
            if (write_failed_.exchange(true, std::memory_order_acq_rel)) return;
            try {
                logger().Failure(message);
            } catch (...) {
                // A secondary logging failure must not terminate the game process.
            }
        }

        std::ofstream output_;
        std::array<Sample, queue_capacity> queue_{};
        std::atomic<uint32_t> write_{0};
        std::atomic<uint32_t> read_{0};
        std::atomic<uint64_t> dropped_{0};
        std::atomic<bool> stop_{false};
        std::atomic<bool> write_failed_{false};
        LARGE_INTEGER performance_frequency_{};
        std::thread worker_;
    };
}

PLUGIN_API smedley::Plugin *CreatePlugin()
{
    return new interest_probe::Plugin();
}
