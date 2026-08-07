// Temporary read-only probe for issue #29: observes the fiscal (treasury) and
// credit (destination-bank) boundaries around CCountry::PayDailyInterest via
// the verified DailyInterestEvent BEFORE/AFTER phases. It mutates nothing.
// This is investigation-only and is not merged into the product.

#include <smedley/events/dailyinterest.hpp>
#include <smedley/game_state/readers.hpp>
#include <smedley/game_state/runtime.hpp>
#include <smedley/plugin.hpp>

#include <shellapi.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <thread>

namespace fiscal_credit_probe
{
    using namespace smedley::game_state;

    namespace
    {
        constexpr size_t queue_capacity = 1024;

        bool DebugEnabled()
        {
            int count = 0;
            wchar_t **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
            if (arguments == nullptr) return false;
            bool enabled = false;
            for (int index = 1; index < count; ++index) {
                if (std::wstring_view(arguments[index]) == L"-smedley-probe-debug=1") enabled = true;
            }
            LocalFree(arguments);
            return enabled;
        }

        CountryRef ResolveCountryFromContext(const void *context, int32_t ordinal)
        {
            return ResolveCountry(GameStateRef{context}, ordinal);
        }

        struct BoundaryRow
        {
            int32_t date_raw = 0;
            char country_tag[4]{'-', '-', '-', '\0'};
            int64_t treasury_before_raw = 0;
            int64_t treasury_after_raw = 0;
            int64_t treasury_delta_raw = 0;
            int32_t creditor_count = 0;
            int32_t creditor_destinations = 0;
            int64_t bank_before_raw = 0;
            int64_t bank_after_raw = 0;
            int64_t bank_delta_raw = 0;
            uint32_t flags = 0;
        };

        template <size_t Capacity>
        class ResultQueue
        {
        public:
            bool TryPush(const BoundaryRow &result) noexcept
            {
                const uint32_t write = write_.load(std::memory_order_relaxed);
                const uint32_t next = (write + 1) % Capacity;
                if (next == read_.load(std::memory_order_acquire)) return false;
                rows_[write] = result;
                write_.store(next, std::memory_order_release);
                return true;
            }

            bool TryPop(BoundaryRow *result) noexcept
            {
                const uint32_t read = read_.load(std::memory_order_relaxed);
                if (read == write_.load(std::memory_order_acquire)) return false;
                *result = rows_[read];
                read_.store((read + 1) % Capacity, std::memory_order_release);
                return true;
            }

            bool Empty() const noexcept
            {
                return read_.load(std::memory_order_relaxed) == write_.load(std::memory_order_acquire);
            }

        private:
            std::array<BoundaryRow, Capacity> rows_{};
            std::atomic<uint32_t> write_{0};
            std::atomic<uint32_t> read_{0};
        };
    }

    class Probe final : public smedley::Plugin
    {
    public:
        void OnLoad() override
        {
            debug_ = DebugEnabled();
            debug_ = true;              // throwaway probe: always write the boundary CSV
            if (debug_) StartDiagnostics();
            try {
                AddEventHandler<smedley::events::DailyInterestEvent>(
                    "fiscal_credit_probe.boundary", [this](smedley::events::DailyInterestEvent &event) {
                        OnBoundary(event);
                    });
            } catch (...) {
                RemoveEventHandler<smedley::events::DailyInterestEvent>("fiscal_credit_probe.boundary");
                StopDiagnostics();
                throw;
            }
            if (debug_) logger().Info("enabled fiscal/credit boundary probe diagnostics");
        }

        void OnUnload() override
        {
            RemoveEventHandler<smedley::events::DailyInterestEvent>("fiscal_credit_probe.boundary");
            StopDiagnostics();
        }

    private:
        void OnBoundary(smedley::events::DailyInterestEvent &event)
        {
            try {
                DailyInterestAccess access = DailyInterestAccess::FromEvent(event);
                int32_t date_raw = 0;
                if (!access.game_state() || !ReadCurrentDate(access.game_state(), &date_raw)) return;
                const void *context = detail::RawPointer(access.game_state());

                if (event.GetPhase() == smedley::events::DailyInterestPhase::BEFORE) {
                    before_ = ReadCountryCreditors(access.country(), date_raw, &ResolveCountryFromContext, context);
                    TelemetryCountrySnapshot tc{};
                    before_treasury_available_ = ReadTelemetryCountry(access.country(), &tc) && tc.daily_available();
                    before_treasury_raw_ = before_treasury_available_ ? tc.treasury_raw() : 0;
                    return;
                }

                const CountryEconomySnapshot after = ReadCountryCreditorBalances(
                    before_, access.country(), date_raw, &ResolveCountryFromContext, context);
                TelemetryCountrySnapshot tc{};
                const bool after_available = ReadTelemetryCountry(access.country(), &tc) && tc.daily_available();

                BoundaryRow row{};
                row.date_raw = date_raw;
                std::memcpy(row.country_tag, before_.country_tag, sizeof(row.country_tag));
                row.treasury_before_raw = before_treasury_raw_;
                row.treasury_after_raw = after_available ? tc.treasury_raw() : 0;
                row.treasury_delta_raw = row.treasury_after_raw - row.treasury_before_raw;
                row.creditor_count = after.creditor_count;
                row.creditor_destinations = after.creditor_destinations;
                row.bank_before_raw = before_.destination_bank_interest_raw;
                row.bank_after_raw = after.destination_bank_interest_raw;
                row.bank_delta_raw = after.destination_bank_interest_raw - before_.destination_bank_interest_raw;
                row.flags = after.flags | (before_treasury_available_ && after_available ? 0u : 1u << 31);
                Publish(row);
            } catch (...) {
                logger().Failure("fiscal/credit probe disabled after a boundary exception");
            }
        }

        void Publish(const BoundaryRow &row)
        {
            if (!debug_) return;
            if (writer_failed_.load(std::memory_order_acquire) || !queue_.TryPush(row)) {
                dropped_.fetch_add(1, std::memory_order_relaxed);
            }
        }

        void StartDiagnostics()
        {
            output_.open("fiscal_credit_probe.csv", std::ios::trunc);
            if (!output_) throw std::runtime_error("cannot open fiscal_credit_probe.csv in the game directory");
            output_ << "date_raw,country,treasury_before_raw,treasury_after_raw,treasury_delta_raw,"
                       "creditor_count,creditor_destinations,bank_before_raw,bank_after_raw,bank_delta_raw,flags,dropped\n";
            output_.flush();
            if (!output_) throw std::runtime_error("cannot initialize fiscal_credit_probe.csv");
            worker_ = std::thread([this] { WriteRows(); });
        }

        void StopDiagnostics() noexcept
        {
            if (!debug_) return;
            stop_.store(true, std::memory_order_release);
            if (worker_.joinable()) worker_.join();
            output_.flush();
        }

        void WriteRows()
        {
            while (!stop_.load(std::memory_order_acquire) || !queue_.Empty()) {
                bool wrote = false;
                BoundaryRow row{};
                while (queue_.TryPop(&row)) {
                    output_ << row.date_raw << ',' << row.country_tag << ',' << row.treasury_before_raw << ','
                            << row.treasury_after_raw << ',' << row.treasury_delta_raw << ','
                            << row.creditor_count << ',' << row.creditor_destinations << ','
                            << row.bank_before_raw << ',' << row.bank_after_raw << ',' << row.bank_delta_raw << ','
                            << "0x" << std::hex << row.flags << std::dec << ','
                            << dropped_.load(std::memory_order_relaxed) << '\n';
                    if (!output_) {
                        writer_failed_.store(true, std::memory_order_release);
                        return;
                    }
                    wrote = true;
                }
                if (wrote) {
                    output_.flush();
                    if (!output_) {
                        writer_failed_.store(true, std::memory_order_release);
                        return;
                    }
                }
                else std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        ResultQueue<queue_capacity> queue_{};
        std::ofstream output_;
        CountryEconomySnapshot before_{};
        int64_t before_treasury_raw_ = 0;
        bool before_treasury_available_ = false;
        bool debug_ = false;
        std::atomic<uint64_t> dropped_{0};
        std::atomic<bool> stop_{false};
        std::atomic<bool> writer_failed_{false};
        std::thread worker_;
    };
}

PLUGIN_API smedley::Plugin *CreatePlugin()
{
    return new fiscal_credit_probe::Probe();
}
