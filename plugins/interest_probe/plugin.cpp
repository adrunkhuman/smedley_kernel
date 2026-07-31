#include "probe_core.hpp"
#include "pair_queue.hpp"

#include <smedley/events/dailyinterest.hpp>
#include <smedley/events/dailyupdate.hpp>
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
        constexpr size_t max_global_countries = 512;

    }

    class Plugin final : public smedley::Plugin
    {
    public:
        void OnLoad() override
        {
            output_.open("interest_probe.csv", std::ios::trunc);
            if (!output_) throw std::runtime_error("cannot open interest_probe.csv in the game directory");
            output_ << "date_raw,phase,country,country_ordinal,state_count_reported,states_walked,province_element_candidates,states_with_savings,"
                       "states_with_interest,creditor_count,creditor_destinations,creditors_was_paid,"
                       "destination_provinces_resolved,destination_province_attempts,destination_pop_lists,"
                       "destination_pops,destination_pop_attempts,treasury_raw,"
                       "state_savings_candidate_raw,state_interest_candidate_raw,bank_interest_raw,"
                       "creditor_interest_candidate_raw,creditor_debt_candidate_raw,destination_bank_interest_raw,"
                       "destination_transfer_count,destination_transfer_raw,"
                       "destination_state_savings_candidate_raw,destination_state_interest_candidate_raw,"
                       "destination_pop_savings_candidate_raw,destination_pop_savings_state_scale_candidate_raw,"
                       "daily_start_bank_interest_raw,daily_start_state_interest_candidate_raw,daily_start_available,"
                       "global_bank_interest_raw,global_state_interest_candidate_raw,global_snapshot_available,"
                       "flags,collection_us,dropped_pairs\n";
            output_.flush();
            if (!output_) throw std::runtime_error("cannot initialize interest_probe.csv in the game directory");
            transfer_output_.open("interest_probe_transfers.csv", std::ios::trunc);
            if (!transfer_output_) throw std::runtime_error("cannot open interest_probe_transfers.csv in the game directory");
            transfer_output_ << "date_raw,country,destination_ordinal,transfer_raw\n";
            transfer_output_.flush();
            if (!transfer_output_) throw std::runtime_error("cannot initialize interest_probe_transfers.csv in the game directory");
            QueryPerformanceFrequency(&performance_frequency_);
            worker_ = std::thread([this] { WriteSamples(); });
            bool daily_registered = false;
            try {
                AddEventHandler<smedley::events::DailyUpdateEvent>(
                    "interest_probe.daily_start",
                    [this](smedley::events::DailyUpdateEvent &event) { OnDailyUpdate(event); });
                daily_registered = true;
                AddEventHandler<smedley::events::DailyInterestEvent>(
                    "interest_probe.boundary", [this](smedley::events::DailyInterestEvent &event) { OnDailyInterest(event); });
            } catch (...) {
                if (daily_registered) RemoveEventHandler<smedley::events::DailyUpdateEvent>("interest_probe.daily_start");
                stop_.store(true, std::memory_order_release);
                worker_.join();
                throw;
            }
            logger().Info("writing bounded provisional interest observations to interest_probe.csv");
        }

        void OnUnload() override
        {
            RemoveEventHandler<smedley::events::DailyInterestEvent>("interest_probe.boundary");
            RemoveEventHandler<smedley::events::DailyUpdateEvent>("interest_probe.daily_start");
            stop_.store(true, std::memory_order_release);
            if (worker_.joinable()) worker_.join();
            output_.flush();
            if (!output_) ReportWriteFailure("interest probe final output flush failed");
            transfer_output_.flush();
            if (!transfer_output_) ReportWriteFailure("interest probe final transfer output flush failed");
        }

    private:
        void OnDailyUpdate(smedley::events::DailyUpdateEvent &event)
        {
            LARGE_INTEGER started{};
            QueryPerformanceCounter(&started);
            const auto *game_state = smedley::v2::CCurrentGameState::instance();
            pending_daily_start_ = CollectSample(
                event.GetCountry(), game_state == nullptr ? 0 : game_state->current_date_raw());
            if (game_state == nullptr) pending_daily_start_.flags |= SAMPLE_DATE_UNAVAILABLE;
            if (game_state != nullptr && pending_daily_start_.country_ordinal == 1) {
                pending_daily_start_.global_snapshot_available = CollectGlobalSnapshot(game_state,
                    &pending_daily_start_.global_bank_interest_raw,
                    &pending_daily_start_.global_state_interest_raw);
            }
            FinishCollectionTiming(started, &pending_daily_start_);
            has_pending_daily_start_ = true;
        }

        void OnDailyInterest(smedley::events::DailyInterestEvent &event)
        {
            LARGE_INTEGER started{};
            QueryPerformanceCounter(&started);
            const auto *game_state = smedley::v2::CCurrentGameState::instance();
            Sample sample = CollectSample(event.GetCountry(), game_state == nullptr ? 0 : game_state->current_date_raw(),
                game_state == nullptr ? nullptr : ResolveCountry,
                game_state == nullptr ? nullptr : ResolveProvince, game_state);
            if (game_state == nullptr) sample.flags |= SAMPLE_DATE_UNAVAILABLE;
            if (smedley::events::DailyInterestEvent::CallbackFailures() != 0) sample.flags |= SAMPLE_EVENT_CALLBACK_FAILURE;
            if (event.GetPhase() == smedley::events::DailyInterestPhase::BEFORE) {
                uint32_t daily_start_collection_us = 0;
                if (has_pending_daily_start_
                    && pending_daily_start_.date_raw == sample.date_raw
                    && std::memcmp(pending_daily_start_.country_tag,
                        sample.country_tag, sizeof(sample.country_tag)) == 0
                    && pending_daily_start_.flags == 0) {
                    sample.daily_start_bank_interest_raw = pending_daily_start_.bank_interest_raw;
                    sample.daily_start_state_interest_raw = pending_daily_start_.state_interest_raw;
                    sample.daily_start_available = 1;
                    sample.global_bank_interest_raw = pending_daily_start_.global_bank_interest_raw;
                    sample.global_state_interest_raw = pending_daily_start_.global_state_interest_raw;
                    sample.global_snapshot_available = pending_daily_start_.global_snapshot_available;
                    daily_start_collection_us = pending_daily_start_.collection_us;
                } else {
                    sample.flags |= SAMPLE_DAILY_START_UNAVAILABLE;
                }
                has_pending_daily_start_ = false;
                FinishCollectionTiming(started, &sample, daily_start_collection_us);
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
            sample.daily_start_bank_interest_raw = pending_before_.daily_start_bank_interest_raw;
            sample.daily_start_state_interest_raw = pending_before_.daily_start_state_interest_raw;
            sample.daily_start_available = pending_before_.daily_start_available;
            if (sample.daily_start_available == 0) sample.flags |= SAMPLE_DAILY_START_UNAVAILABLE;
            if (game_state != nullptr
                && sample.country_ordinal > 0
                && static_cast<size_t>(sample.country_ordinal) + 1 == game_state->country_count()) {
                sample.global_snapshot_available = CollectGlobalSnapshot(game_state,
                    &sample.global_bank_interest_raw, &sample.global_state_interest_raw);
            }
            ComputeDestinationTransfers(pending_before_, &sample);
            FinishCollectionTiming(started, &sample);
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
                    WriteTransfers(pair.after);
                    if (!output_ || !transfer_output_) {
                        ReportWriteFailure("interest probe output failed; subsequent samples are being dropped");
                        break;
                    }
                    wrote = true;
                }
                if (wrote && !write_failed_.load(std::memory_order_acquire)) {
                    output_.flush();
                    transfer_output_.flush();
                    if (!output_ || !transfer_output_) {
                        ReportWriteFailure("interest probe output flush failed; subsequent samples are being dropped");
                    }
                }
                else std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        void WriteSample(const Sample &sample, const char *phase)
        {
            if ((sample.flags & SAMPLE_DATE_UNAVAILABLE) == 0) output_ << sample.date_raw;
            output_ << ',' << phase << ',' << sample.country_tag << ',' << sample.country_ordinal << ','
                    << sample.state_count_reported << ','
                    << sample.states_walked << ',' << sample.province_element_candidates << ',' << sample.states_with_savings << ','
                    << sample.states_with_interest << ',' << sample.creditor_count << ',' << sample.creditor_destinations << ','
                    << sample.creditors_was_paid << ',' << sample.destination_provinces_resolved << ','
                    << sample.destination_province_attempts << ',' << sample.destination_pop_lists << ','
                    << sample.destination_pops << ',' << sample.destination_pop_attempts << ',' << sample.treasury_raw << ','
                    << sample.state_savings_raw << ','
                    << sample.state_interest_raw << ',' << sample.bank_interest_raw << ',' << sample.creditor_interest_raw << ','
                    << sample.creditor_debt_raw << ',' << sample.destination_bank_interest_raw << ','
                    << sample.destination_transfer_count << ',' << sample.destination_transfer_raw << ','
                    << sample.destination_state_savings_raw << ',' << sample.destination_state_interest_raw << ','
                    << sample.destination_pop_savings_raw << ',' << sample.destination_pop_savings_state_scale_raw << ','
                    << sample.daily_start_bank_interest_raw << ',' << sample.daily_start_state_interest_raw << ','
                    << static_cast<uint32_t>(sample.daily_start_available) << ','
                    << sample.global_bank_interest_raw << ',' << sample.global_state_interest_raw << ','
                    << static_cast<uint32_t>(sample.global_snapshot_available) << ",0x"
                    << std::hex << sample.flags << std::dec << ',' << sample.collection_us << ','
                    << dropped_.load(std::memory_order_relaxed) << '\n';
        }

        void WriteTransfers(const Sample &sample)
        {
            if (sample.flags != 0) return;
            for (uint32_t index = 0; index < sample.creditor_destinations; ++index) {
                const int64_t transfer = sample.destination_transfers_raw[index];
                if (transfer == 0) continue;
                transfer_output_ << sample.date_raw << ',' << sample.country_tag << ','
                                 << sample.destination_ordinals[index] << ',' << transfer << '\n';
            }
        }

        static const void *ResolveCountry(const void *context, int32_t ordinal)
        {
            const auto *game_state = static_cast<const smedley::v2::CCurrentGameState *>(context);
            return game_state == nullptr ? nullptr : game_state->country(ordinal);
        }

        static const void *ResolveProvince(const void *context, int32_t id)
        {
            const auto *game_state = static_cast<const smedley::v2::CCurrentGameState *>(context);
            return game_state == nullptr ? nullptr : game_state->province(id);
        }

        static uint8_t CollectGlobalSnapshot(const smedley::v2::CCurrentGameState *game_state,
                                             int64_t *bank_interest, int64_t *state_interest)
        {
            *bank_interest = 0;
            *state_interest = 0;
            const size_t country_count = game_state->country_count();
            if (country_count == 0 || country_count > max_global_countries) return 0;
            for (size_t ordinal = 1; ordinal < country_count; ++ordinal) {
                const Sample country = CollectSample(game_state->country(static_cast<int32_t>(ordinal)), 0);
                if (country.flags != 0 || country.country_ordinal != static_cast<int32_t>(ordinal)) return 0;
                if ((country.bank_interest_raw > 0
                        && *bank_interest > (std::numeric_limits<int64_t>::max)() - country.bank_interest_raw)
                    || (country.bank_interest_raw < 0
                        && *bank_interest < (std::numeric_limits<int64_t>::min)() - country.bank_interest_raw)
                    || (country.state_interest_raw > 0
                        && *state_interest > (std::numeric_limits<int64_t>::max)() - country.state_interest_raw)
                    || (country.state_interest_raw < 0
                        && *state_interest < (std::numeric_limits<int64_t>::min)() - country.state_interest_raw)) {
                    return 0;
                }
                *bank_interest += country.bank_interest_raw;
                *state_interest += country.state_interest_raw;
            }
            return 1;
        }

        void FinishCollectionTiming(LARGE_INTEGER started, Sample *sample, uint32_t prior_us = 0) const
        {
            LARGE_INTEGER finished{};
            QueryPerformanceCounter(&finished);
            uint64_t elapsed_us = 0;
            if (performance_frequency_.QuadPart > 0 && finished.QuadPart >= started.QuadPart) {
                const uint64_t elapsed = static_cast<uint64_t>(finished.QuadPart - started.QuadPart);
                elapsed_us = (std::min)(
                    elapsed * 1000000u / static_cast<uint64_t>(performance_frequency_.QuadPart),
                    static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)()));
            }
            const uint64_t combined = elapsed_us + prior_us;
            sample->collection_us = static_cast<uint32_t>((std::min)(combined,
                static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())));
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
        std::ofstream transfer_output_;
        PairQueue<queue_capacity> queue_{};
        Sample pending_daily_start_{};
        Sample pending_before_{};
        bool has_pending_daily_start_ = false;
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
