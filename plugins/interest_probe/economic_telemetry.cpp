#include "economic_telemetry_core.hpp"
#include "probe_core.hpp"
#include "telemetry_bridge.hpp"

#include <smedley/events/dailyupdate.hpp>
#include <smedley/plugin.hpp>
#include <smedley/v2/gamestate.hpp>

#include <shellapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace interest_probe
{
    namespace
    {
        std::vector<std::wstring> CommandLineArguments()
        {
            int argc = 0;
            wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            if (argv == nullptr) return {};
            std::vector<std::wstring> arguments(argv + 1, argv + argc);
            LocalFree(argv);
            return arguments;
        }

    }

    class EconomicTelemetry final : public smedley::Plugin
    {
    public:
        void OnLoad() override
        {
            config_ = ParseEconomicTelemetryArguments(CommandLineArguments());
            if (!config_.enabled) {
                logger().Info("economic telemetry inactive because state telemetry is not selected");
                return;
            }
            AddEventHandler<smedley::events::DailyUpdateEvent>(
                "economic_telemetry.world", [this](smedley::events::DailyUpdateEvent &) { OnDailyUpdate(); });
            registered_ = true;
            logger().Info("enabled bounded sampled world economic telemetry");
        }

        void OnUnload() override
        {
            if (registered_) RemoveEventHandler<smedley::events::DailyUpdateEvent>("economic_telemetry.world");
        }

    private:
        void OnDailyUpdate()
        {
            const auto *game_state = smedley::v2::CCurrentGameState::instance();
            if (game_state == nullptr) return;
            const int32_t date = game_state->current_date_raw();
            if (!ShouldCaptureEconomicDate(date, config_, &last_observed_date_, &last_sampled_date_)) return;
            const EconomicSnapshot snapshot = Collect(game_state, date);
            Emit(snapshot);
        }

        EconomicSnapshot Collect(const smedley::v2::CCurrentGameState *game_state, int32_t date)
        {
            const auto started = std::chrono::steady_clock::now();
            EconomicSnapshot snapshot{};
            snapshot.date_raw = date;
            const size_t slots = game_state->country_count();
            if (slots == 0 || slots > static_cast<size_t>(max_world_countries) + 1) {
                snapshot.snapshot_flags |= SNAPSHOT_COUNTRY_LIMIT;
                FinishTiming(started, &snapshot);
                return snapshot;
            }

            uint32_t candidate_count = 0;
            for (size_t ordinal = 1; ordinal < slots; ++ordinal) {
                const auto *country = game_state->country(static_cast<int32_t>(ordinal));
                const Sample credit_quality = CollectSample(country, date);
                constexpr uint32_t credit_flag_mask = SAMPLE_SUM_OVERFLOW | SAMPLE_CREDITOR_VECTOR_INVALID
                    | SAMPLE_CREDITOR_UNREADABLE | SAMPLE_CREDITOR_TAG_INVALID;
                snapshot.credit_flags |= credit_quality.flags & credit_flag_mask;
                Sample quality{};
                uint32_t collected = 0;
                const uint32_t province_remaining = snapshot.province_count >= max_sample_destination_provinces
                    ? 0 : max_sample_destination_provinces - snapshot.province_count;
                if (!CollectCountryPops(country, date,
                        ResolveProvince, game_state, candidates_.data() + candidate_count,
                        candidates_.size() - candidate_count, province_remaining, &collected, &quality)) {
                    snapshot.snapshot_flags |= SNAPSHOT_COLLECTION_FAILED;
                    snapshot.probe_flags |= quality.flags;
                    snapshot.state_count += quality.states_walked;
                    snapshot.province_count += quality.destination_province_attempts;
                    snapshot.pop_count += quality.destination_pop_attempts;
                    break;
                }
                ++snapshot.country_count;
                snapshot.state_count += quality.states_walked;
                snapshot.province_count += quality.destination_province_attempts;
                snapshot.pop_count += collected;
                snapshot.creditor_count += credit_quality.creditor_count;
                snapshot.creditors_was_paid += credit_quality.creditors_was_paid;
                if (credit_quality.creditor_count != 0) ++snapshot.countries_with_creditors;
                if (quality.treasury_raw < 0) ++snapshot.countries_with_negative_treasury;
                AddEconomicValue(quality.treasury_raw, &snapshot.treasury_observed_raw, &snapshot.snapshot_flags);
                AddEconomicValue(quality.bank_interest_raw, &snapshot.bank_interest_accumulator_raw, &snapshot.snapshot_flags);
                AddEconomicValue(quality.state_savings_raw, &snapshot.state_savings_candidate_raw, &snapshot.snapshot_flags);
                AddEconomicValue(quality.state_interest_raw, &snapshot.state_interest_candidate_raw, &snapshot.snapshot_flags);
                AddEconomicValue(credit_quality.creditor_interest_raw,
                    &snapshot.creditor_interest_candidate_raw, &snapshot.credit_flags);
                AddEconomicValue(credit_quality.creditor_debt_raw,
                    &snapshot.creditor_debt_candidate_raw, &snapshot.credit_flags);
                candidate_count += collected;
            }

            if (snapshot.snapshot_flags == 0 && snapshot.probe_flags == 0) {
                std::sort(candidates_.begin(), candidates_.begin() + candidate_count,
                    [](const PopCandidate &left, const PopCandidate &right) {
                        return reinterpret_cast<uintptr_t>(left.address) < reinterpret_cast<uintptr_t>(right.address);
                    });
                for (uint32_t index = 1; index < candidate_count; ++index) {
                    if (candidates_[index - 1].address == candidates_[index].address) {
                        snapshot.snapshot_flags |= SNAPSHOT_DUPLICATE_POP;
                        break;
                    }
                }
            }
            if (snapshot.snapshot_flags == 0 && snapshot.probe_flags == 0) {
                for (uint32_t index = 0; index < candidate_count; ++index) {
                    PopMoneySnapshot pop{};
                    if (!ReadPopMoneySnapshot(candidates_[index].address, &pop)
                        || pop.savings_raw != candidates_[index].savings_raw) {
                        snapshot.snapshot_flags |= SNAPSHOT_POP_UNREADABLE;
                        break;
                    }
                    if (pop.money_raw > 0) ++snapshot.positive_money_pops;
                    if (pop.savings_raw > 0) ++snapshot.positive_savings_pops;
                    AddEconomicValue(pop.money_raw, &snapshot.pop_money_observed_raw, &snapshot.snapshot_flags);
                    AddEconomicValue(pop.savings_raw, &snapshot.pop_savings_observed_raw, &snapshot.snapshot_flags);
                }
            }
            FinishTiming(started, &snapshot);
            return snapshot;
        }

        static const void *ResolveProvince(const void *context, int32_t id)
        {
            return static_cast<const smedley::v2::CCurrentGameState *>(context)->province(id);
        }

        static void FinishTiming(std::chrono::steady_clock::time_point started, EconomicSnapshot *snapshot)
        {
            snapshot->collection_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started).count());
        }

        void Emit(const EconomicSnapshot &snapshot)
        {
            const SmedleyTelemetryFieldV1 health[] = {
                TelemetryBoolField("complete", snapshot.complete()),
                TelemetryIntField("snapshot_flags", snapshot.snapshot_flags),
                TelemetryIntField("probe_flags", snapshot.probe_flags),
                TelemetryIntField("credit_flags", snapshot.credit_flags),
                TelemetryIntField("country_count", snapshot.country_count),
                TelemetryIntField("state_count", snapshot.state_count),
                TelemetryIntField("province_count", snapshot.province_count),
                TelemetryIntField("pop_count", snapshot.pop_count),
            };
            static_assert(std::size(health) <= SMEDLEY_TELEMETRY_MAX_FIELDS);
            bridge_.Emit("world.economy.health", "provisional", snapshot.date_raw, nullptr, 0, health, 8, true);

            if (!snapshot.complete()) return;
            const SmedleyTelemetryFieldV1 capacity[] = {
                TelemetryIntField("country_limit", max_world_countries),
                TelemetryIntField("province_limit", max_sample_destination_provinces),
                TelemetryIntField("pop_limit", max_sample_pops),
                TelemetryIntField("country_utilization_bp", UtilizationBasisPoints(snapshot.country_count, max_world_countries)),
                TelemetryIntField("province_utilization_bp", UtilizationBasisPoints(snapshot.province_count, max_sample_destination_provinces)),
                TelemetryIntField("pop_utilization_bp", UtilizationBasisPoints(snapshot.pop_count, max_sample_pops)),
                TelemetryIntField("collection_us", static_cast<int64_t>(snapshot.collection_us)),
            };
            static_assert(std::size(capacity) <= SMEDLEY_TELEMETRY_MAX_FIELDS);
            bridge_.Emit("world.economy.capacity", "provisional", snapshot.date_raw, nullptr, 0, capacity, 7, true);

            const SmedleyTelemetryFieldV1 holdings[] = {
                TelemetryIntField("treasury_observed_raw", snapshot.treasury_observed_raw),
                TelemetryIntField("pop_money_observed_raw", snapshot.pop_money_observed_raw),
                TelemetryIntField("pop_savings_observed_raw", snapshot.pop_savings_observed_raw),
                TelemetryIntField("bank_interest_accumulator_raw", snapshot.bank_interest_accumulator_raw),
                TelemetryIntField("positive_money_pops", snapshot.positive_money_pops),
                TelemetryIntField("positive_savings_pops", snapshot.positive_savings_pops),
                TelemetryIntField("negative_treasury_countries", snapshot.countries_with_negative_treasury),
            };
            static_assert(std::size(holdings) <= SMEDLEY_TELEMETRY_MAX_FIELDS);
            bridge_.Emit("world.economy.holdings", "provisional", snapshot.date_raw, nullptr, 0, holdings, 7, true);

            if (snapshot.credit_flags != 0) return;
            const SmedleyTelemetryFieldV1 credit[] = {
                TelemetryIntField("creditor_count", snapshot.creditor_count),
                TelemetryIntField("creditors_was_paid", snapshot.creditors_was_paid),
                TelemetryIntField("countries_with_creditors", snapshot.countries_with_creditors),
                TelemetryIntField("creditor_interest_candidate_raw", snapshot.creditor_interest_candidate_raw),
                TelemetryIntField("creditor_debt_candidate_raw", snapshot.creditor_debt_candidate_raw),
                TelemetryIntField("state_savings_candidate_raw", snapshot.state_savings_candidate_raw),
                TelemetryIntField("state_interest_candidate_raw", snapshot.state_interest_candidate_raw),
            };
            static_assert(std::size(credit) <= SMEDLEY_TELEMETRY_MAX_FIELDS);
            bridge_.Emit("world.economy.credit", "provisional", snapshot.date_raw, nullptr, 0, credit, 7, true);
        }

        CaptureConfig config_{};
        TelemetryBridge bridge_{};
        std::optional<int32_t> last_observed_date_;
        std::optional<int32_t> last_sampled_date_;
        std::array<PopCandidate, max_sample_pops> candidates_{};
        bool registered_ = false;
    };
}

PLUGIN_API smedley::Plugin *CreatePlugin()
{
    return new interest_probe::EconomicTelemetry();
}
