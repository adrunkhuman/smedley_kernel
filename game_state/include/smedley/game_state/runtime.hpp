#pragma once

#include <thread>

#include <smedley/game_state/readers.hpp>

namespace smedley::events
{
    class DailyInterestEvent;
}

namespace smedley::game_state
{
    enum class PopInterestMutationStatus
    {
        // Among failures, only postcondition_failed can follow a native write.
        success,
        invalid_context,
        invalid_phase,
        invalid_thread,
        invalid_amount,
        balance_unreadable,
        balance_overflow,
        not_writable,
        signature_mismatch,
        unavailable,
        state_changed,
        postcondition_failed,
    };

    struct PopInterestPreflight
    {
        PopRef pop{};
        int64_t amount = 0;
        PopMoneySnapshot before{};
        PopInterestMutationStatus status = PopInterestMutationStatus::invalid_context;
    };

    struct GameSession
    {
        GameStateRef game_state{};
        uint64_t epoch = 0;
    };

    GameSession CurrentGameSession();

    class DailyInterestAccess
    {
    public:
        DailyInterestAccess(const DailyInterestAccess &) = delete;
        DailyInterestAccess &operator=(const DailyInterestAccess &) = delete;
        DailyInterestAccess(DailyInterestAccess &&) = default;
        DailyInterestAccess &operator=(DailyInterestAccess &&) = default;

        static DailyInterestAccess FromEvent(events::DailyInterestEvent &event);

        GameStateRef game_state() const noexcept { return game_state_; }
        CountryRef country() const noexcept { return country_; }
        uint64_t session_epoch() const noexcept { return session_epoch_; }

    private:
        DailyInterestAccess(GameSession session, CountryRef country, bool after, uint64_t generation) noexcept;
        PopInterestMutationStatus CheckMutationAccess() const;
        PopInterestMutationStatus CheckSignature(bool recheck = false);

        GameStateRef game_state_{};
        CountryRef country_{};
        std::thread::id thread_{};
        uint64_t generation_ = 0;
        uint64_t session_epoch_ = 0;
        bool after_ = false;
        bool signature_checked_ = false;
        PopInterestMutationStatus signature_status_ = PopInterestMutationStatus::unavailable;

        friend PopInterestMutationStatus PreparePopInterest(
            DailyInterestAccess &access, PopRef pop, int64_t amount, PopInterestPreflight *preflight);
        friend PopInterestMutationStatus ApplyPopInterest(
            DailyInterestAccess &access, PopRef pop, int64_t amount, const PopInterestPreflight &preflight,
            PopMoneySnapshot *after);
    };

    /** Checks the complete verified POP money write span without writing it. */
    bool IsPopInterestWritable(PopRef pop);
    /**
     * Captures and validates one positive payout during the active AFTER callback.
     * The result is bound to the supplied POP and amount and performs no write.
     */
    PopInterestMutationStatus PreparePopInterest(
        DailyInterestAccess &access, PopRef pop, int64_t amount, PopInterestPreflight *preflight);
    /**
     * Revalidates a preflight and invokes the native operation synchronously.
     * postcondition_failed means the native call occurred and may have written;
     * every other failure is detected before this invocation writes.
     */
    PopInterestMutationStatus ApplyPopInterest(
        DailyInterestAccess &access, PopRef pop, int64_t amount, const PopInterestPreflight &preflight,
        PopMoneySnapshot *after = nullptr);
}
