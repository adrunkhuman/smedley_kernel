#include "probe_core.hpp"
#include "pair_queue.hpp"

#include <smedley/events/dailyinterest.hpp>
#include <smedley/plugin.hpp>
#include <smedley/v2/gamestate.hpp>

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
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
            output_ << "date_raw,phase,country,state_count_reported,states_walked,province_element_candidates,states_with_savings,"
                       "states_with_interest,creditor_count,creditor_destinations,creditors_was_paid,treasury_raw,"
                       "state_savings_candidate_raw,state_interest_candidate_raw,bank_interest_raw,"
                       "creditor_interest_candidate_raw,creditor_debt_candidate_raw,destination_bank_interest_raw,"
                       "destination_state_savings_candidate_raw,destination_state_interest_candidate_raw,"
                       "flags,collection_us,dropped_pairs\n";
            output_.flush();
            if (!output_) throw std::runtime_error("cannot initialize interest_probe.csv in the game directory");
            QueryPerformanceFrequency(&performance_frequency_);
            worker_ = std::thread([this] { WriteSamples(); });
            try {
                AddEventHandler<smedley::events::DailyInterestEvent>(
                    "interest_probe.boundary", [this](smedley::events::DailyInterestEvent &event) { OnDailyInterest(event); });
            } catch (...) {
                stop_.store(true, std::memory_order_release);
                worker_.join();
                throw;
            }
            logger().Info("writing bounded provisional interest observations to interest_probe.csv");
        }

        void OnUnload() override
        {
            RemoveEventHandler<smedley::events::DailyInterestEvent>("interest_probe.boundary");
            stop_.store(true, std::memory_order_release);
            if (worker_.joinable()) worker_.join();
            output_.flush();
            if (!output_) ReportWriteFailure("interest probe final output flush failed");
        }

    private:
        void OnDailyInterest(smedley::events::DailyInterestEvent &event)
        {
            LARGE_INTEGER started{};
            LARGE_INTEGER finished{};
            QueryPerformanceCounter(&started);
            const auto *game_state = smedley::v2::CCurrentGameState::instance();
            Sample sample = CollectSample(event.GetCountry(), game_state == nullptr ? 0 : game_state->current_date_raw(),
                game_state == nullptr ? nullptr : ResolveCountry, game_state);
            if (game_state == nullptr) sample.flags |= SAMPLE_DATE_UNAVAILABLE;
            if (smedley::events::DailyInterestEvent::CallbackFailures() != 0) sample.flags |= SAMPLE_EVENT_CALLBACK_FAILURE;
            QueryPerformanceCounter(&finished);
            if (performance_frequency_.QuadPart > 0) {
                const uint64_t elapsed = static_cast<uint64_t>(finished.QuadPart - started.QuadPart);
                sample.collection_us = static_cast<uint32_t>((std::min)(
                    elapsed * 1000000u / static_cast<uint64_t>(performance_frequency_.QuadPart),
                    static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())));
            }
            if (event.GetPhase() == smedley::events::DailyInterestPhase::BEFORE) {
                if (has_pending_before_) dropped_.fetch_add(1, std::memory_order_relaxed);
                pending_before_ = sample;
                has_pending_before_ = true;
                return;
            }
            if (!has_pending_before_) {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (pending_before_.date_raw != sample.date_raw
                || std::memcmp(pending_before_.country_tag, sample.country_tag, sizeof(sample.country_tag)) != 0) {
                has_pending_before_ = false;
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            TryPush({pending_before_, sample});
            has_pending_before_ = false;
        }

        void TryPush(const SamplePair &sample)
        {
            if (write_failed_.load(std::memory_order_acquire)) {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (!queue_.TryPush(sample)) {
                dropped_.fetch_add(1, std::memory_order_relaxed);
            }
        }

        void WriteSamples()
        {
            while (!stop_.load(std::memory_order_acquire) || !queue_.Empty()) {
                if (write_failed_.load(std::memory_order_acquire)) {
                    SamplePair discarded{};
                    while (queue_.TryPop(&discarded)) {
                        dropped_.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (!stop_.load(std::memory_order_acquire)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                bool wrote = false;
                SamplePair pair{};
                while (queue_.TryPop(&pair)) {
                    WriteSample(pair.before, "before");
                    WriteSample(pair.after, "after");
                    if (!output_) {
                        ReportWriteFailure("interest probe output failed; subsequent samples are being dropped");
                        break;
                    }
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

        void WriteSample(const Sample &sample, const char *phase)
        {
            if ((sample.flags & SAMPLE_DATE_UNAVAILABLE) == 0) output_ << sample.date_raw;
            output_ << ',' << phase << ',' << sample.country_tag << ',' << sample.state_count_reported << ','
                    << sample.states_walked << ',' << sample.province_element_candidates << ',' << sample.states_with_savings << ','
                    << sample.states_with_interest << ',' << sample.creditor_count << ',' << sample.creditor_destinations << ','
                    << sample.creditors_was_paid << ',' << sample.treasury_raw << ',' << sample.state_savings_raw << ','
                    << sample.state_interest_raw << ',' << sample.bank_interest_raw << ',' << sample.creditor_interest_raw << ','
                    << sample.creditor_debt_raw << ',' << sample.destination_bank_interest_raw << ','
                    << sample.destination_state_savings_raw << ',' << sample.destination_state_interest_raw << ",0x"
                    << std::hex << sample.flags << std::dec << ',' << sample.collection_us << ','
                    << dropped_.load(std::memory_order_relaxed) << '\n';
        }

        static const void *ResolveCountry(const void *context, int32_t ordinal)
        {
            const auto *game_state = static_cast<const smedley::v2::CCurrentGameState *>(context);
            return game_state == nullptr ? nullptr : game_state->country(ordinal);
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
        PairQueue<queue_capacity> queue_{};
        Sample pending_before_{};
        bool has_pending_before_ = false;
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
