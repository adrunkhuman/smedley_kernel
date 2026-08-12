#pragma once

#include <cstdint>
#include <optional>
#include <thread>

#include <smedley/game_state/readers.hpp>

namespace smedley::events
{
    class BankInterestEvent;
    class DailyInterestEvent;
    class DailyUpdateEvent;
}

namespace smedley
{
    class Plugin;
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

    struct PopInterestBatchEntry
    {
        PopRef pop{};
        int64_t amount = 0;
        PopMoneySnapshot before{};
        PopInterestMutationStatus status = PopInterestMutationStatus::invalid_context;
    };

    struct PopInterestBatchResult
    {
        PopInterestMutationStatus status = PopInterestMutationStatus::invalid_context;
        uint32_t failed_index = 0;
        uint32_t write_count = 0;
        uint32_t verified_count = 0;
    };

    struct StateInterestInitializationResult
    {
        PopInterestMutationStatus status = PopInterestMutationStatus::invalid_context;
        uint32_t country_count = 0;
        uint32_t state_count = 0;
        uint32_t cleared_state_count = 0;
        int64_t discarded_raw = 0;
        uint32_t flags = 0;
    };

    struct GameSession
    {
        GameStateRef game_state{};
        uint64_t epoch = 0;
    };

    GameSession CurrentGameSession();

    struct TelemetryTag
    {
        char value[4]{};

        bool normalized_candidate() const noexcept { return value[0] != '\0'; }
        const char *str() const noexcept { return value; }
    };

    struct TelemetryCurrentState
    {
        GameStateRef game_state{};
        int32_t date_raw = 0;
        uint32_t country_count_value = 0;
        uint32_t country_ai_count_value = 0;
        bool human_control_present_value = false;
        uint32_t ongoing_war_count_value = 0;
        uint32_t province_count_value = 0;
        bool world_daily_available_value = false;
        bool military_available_value = false;
        bool province_count_available_value = false;

        uint32_t country_count() const noexcept { return country_count_value; }
        uint32_t country_ai_count() const noexcept { return country_ai_count_value; }
        bool has_human_controlled_country() const noexcept { return human_control_present_value; }
        size_t province_count() const noexcept { return province_count_value; }
        bool world_daily_available() const noexcept { return world_daily_available_value; }
        bool military_available() const noexcept { return military_available_value; }
        bool ongoing_war_count_candidate(int *count) const noexcept;
        bool province_count_candidate(size_t *count) const noexcept;
    };

    struct DailyUpdateSnapshot
    {
        int32_t date_raw = 0;
        int64_t treasury_raw = 0;
        uint32_t country_slot_count = 0;
        uint32_t ai_scheduler_entry_count = 0;
        TelemetryTag country_tag{};
        bool country_exists = false;
        bool human_control_present = false;
    };

    struct TelemetryCountrySnapshot
    {
        CountryRef country{};
        TelemetryTag tag_value{};
        int64_t treasury_raw_value = 0;
        int64_t prestige_candidate_raw_value = 0;
        int64_t infamy_candidate_raw_value = 0;
        int64_t plurality_candidate_raw_value = 0;
        int64_t war_exhaustion_candidate_raw_value = 0;
        int64_t diplomatic_points_candidate_raw_value = 0;
        int64_t research_points_candidate_raw_value = 0;
        int64_t leadership_candidate_raw_value = 0;
        int32_t ranking_candidate_value = 0;
        int32_t military_ranking_candidate_value = 0;
        int32_t industrial_ranking_candidate_value = 0;
        int32_t prestige_ranking_candidate_value = 0;
        bool mobilized_candidate_value = false;
        bool substate_candidate_value = false;
        bool vassal_candidate_value = false;
        TelemetryTag overlord_tag_value{};
        TelemetryTag sphere_leader_tag_value{};
        uint32_t unit_count_candidate_value = 0;
        uint32_t scheduled_mobilization_count_candidate_value = 0;
        uint32_t sphereling_count_candidate_value = 0;
        uint32_t vassal_count_candidate_value = 0;
        uint32_t ally_count_candidate_value = 0;
        uint32_t guaranteed_count_candidate_value = 0;
        uint32_t neighbor_count_candidate_value = 0;
        bool daily_available_value = false;
        bool power_available_value = false;
        bool politics_available_value = false;
        bool military_available_value = false;
        bool diplomacy_status_available_value = false;
        bool diplomacy_relations_available_value = false;

        const TelemetryTag &tag() const noexcept { return tag_value; }
        int64_t treasury_raw() const noexcept { return treasury_raw_value; }
        int64_t prestige_candidate_raw() const noexcept { return prestige_candidate_raw_value; }
        int64_t infamy_candidate_raw() const noexcept { return infamy_candidate_raw_value; }
        int64_t plurality_candidate_raw() const noexcept { return plurality_candidate_raw_value; }
        int64_t war_exhaustion_candidate_raw() const noexcept { return war_exhaustion_candidate_raw_value; }
        int64_t diplomatic_points_candidate_raw() const noexcept { return diplomatic_points_candidate_raw_value; }
        int64_t research_points_candidate_raw() const noexcept { return research_points_candidate_raw_value; }
        int64_t leadership_candidate_raw() const noexcept { return leadership_candidate_raw_value; }
        int32_t ranking_candidate() const noexcept { return ranking_candidate_value; }
        int32_t military_ranking_candidate() const noexcept { return military_ranking_candidate_value; }
        int32_t industrial_ranking_candidate() const noexcept { return industrial_ranking_candidate_value; }
        int32_t prestige_ranking_candidate() const noexcept { return prestige_ranking_candidate_value; }
        bool mobilized_candidate() const noexcept { return mobilized_candidate_value; }
        bool substate_candidate() const noexcept { return substate_candidate_value; }
        bool vassal_candidate() const noexcept { return vassal_candidate_value; }
        const TelemetryTag &overlord_candidate() const noexcept { return overlord_tag_value; }
        const TelemetryTag &sphere_leader_candidate() const noexcept { return sphere_leader_tag_value; }
        bool daily_available() const noexcept { return daily_available_value; }
        bool power_available() const noexcept { return power_available_value; }
        bool politics_available() const noexcept { return politics_available_value; }
        bool military_available() const noexcept { return military_available_value; }
        bool diplomacy_status_available() const noexcept { return diplomacy_status_available_value; }
        bool diplomacy_relations_available() const noexcept { return diplomacy_relations_available_value; }
        bool unit_count_candidate(int *count) const noexcept;
        bool scheduled_mobilization_count_candidate(size_t *count) const noexcept;
        bool sphereling_count_candidate(size_t *count) const noexcept;
        bool vassal_count_candidate(size_t *count) const noexcept;
        bool ally_count_candidate(size_t *count) const noexcept;
        bool guaranteed_count_candidate(size_t *count) const noexcept;
        bool neighbor_count_candidate(size_t *count) const noexcept;
    };

    struct TelemetryProvinceSnapshot
    {
        ProvinceRef province{};
        int32_t id_value = -1;
        TelemetryTag owner_tag_value{};
        TelemetryTag controller_tag_value{};
        int32_t colonial_level_value = 0;
        int32_t life_rating_value = 0;
        int64_t infrastructure_value = 0;
        size_t building_slot_count_value = 0;
        int32_t construction_count_value = 0;
        bool owner_available_value = false;
        bool daily_available_value = false;
        bool production_available_value = false;

        int32_t id_candidate() const noexcept { return id_value; }
        const TelemetryTag &owner_candidate() const noexcept { return owner_tag_value; }
        const TelemetryTag &controller_candidate() const noexcept { return controller_tag_value; }
        int32_t colonial_level_candidate() const noexcept { return colonial_level_value; }
        int32_t life_rating_candidate() const noexcept { return life_rating_value; }
        int64_t infrastructure_candidate() const noexcept { return infrastructure_value; }
        bool owner_available() const noexcept { return owner_available_value; }
        bool daily_available() const noexcept { return daily_available_value; }
        bool production_available() const noexcept { return production_available_value; }
        bool building_slot_count_candidate(size_t *count) const noexcept;
        bool construction_count_candidate(int *count) const noexcept;
    };

    bool ReadTelemetryCurrentState(TelemetryCurrentState *state);
    CountryRef DailyUpdateCountry(events::DailyUpdateEvent &event);
    bool ReadDailyUpdateSnapshot(CountryRef country, DailyUpdateSnapshot *snapshot);
    bool ReadDailyUpdateSnapshot(events::DailyUpdateEvent &event, DailyUpdateSnapshot *snapshot);
    bool ReadTelemetryCountry(CountryRef country, TelemetryCountrySnapshot *snapshot);
    bool ReadTelemetryProvince(ProvinceRef province, TelemetryProvinceSnapshot *snapshot);

    enum class CampaignOperationStatus
    {
        completed,
        outside_campaign,
        invalid_state,
        signature_mismatch,
        readback_failed,
    };

    using PauseOperationStatus = CampaignOperationStatus;

    enum class CampaignRuntimeObservationStatus
    {
        completed,
        outside_campaign,
        invalid_state,
        signature_mismatch,
    };

    struct CampaignRuntimeSnapshot
    {
        int32_t date_raw = 0;
        int32_t speed_index = 0;
        bool paused = false;
    };

    struct ProcessMetricsSnapshot
    {
        std::optional<int64_t> process_cpu_us;
        std::optional<int64_t> working_set_bytes;
        std::optional<int64_t> private_bytes;
        std::optional<int64_t> process_peak_working_set_bytes;
    };

    /**
     * Copies the current in-campaign date, speed, and pause state after
     * validating the current CInGameIdler. The snapshot retains no game pointer.
     */
    CampaignRuntimeObservationStatus ReadCampaignRuntime(CampaignRuntimeSnapshot *snapshot);
    /** Sets the verified native pause state and requires immediate readback. */
    CampaignOperationStatus SetCampaignPaused(bool paused);
    /** Sets the zero-based native speed index through verified native handlers. */
    CampaignOperationStatus SetCampaignSpeedIndex(int32_t speed_index);
    /** Requests native game exit through the verified idler virtual operation. */
    CampaignOperationStatus RequestCampaignQuit();
    ProcessMetricsSnapshot SampleProcessMetrics();

    enum class FrontendOperationStatus
    {
        completed,
        unavailable,
        invalid_token,
        invalid_thread,
        invalid_controller,
        signature_mismatch,
        precondition_failed,
        readback_failed,
    };

    enum class FrontendControllerKind : uint8_t
    {
        frontend,
        main_menu,
    };

    using FrontendControllerCaptureCallback = void (__stdcall *)(FrontendControllerKind kind);

    /** An opaque, generation-bound capability for one currently captured controller. */
    class FrontendControllerToken
    {
    public:
        FrontendControllerToken() = default;
        explicit operator bool() const noexcept { return generation_ != 0; }

    private:
        uint64_t generation_ = 0;
        FrontendControllerKind kind_ = FrontendControllerKind::frontend;

        FrontendControllerToken(uint64_t generation, FrontendControllerKind kind) noexcept
            : generation_(generation), kind_(kind)
        {
        }

        friend FrontendOperationStatus AcquireFrontendController(
            FrontendControllerKind kind, FrontendControllerToken *token);
        friend FrontendOperationStatus ReleaseFrontendController(FrontendControllerToken token);
        friend FrontendOperationStatus DispatchMainMenuSinglePlayer(FrontendControllerToken token);
        friend FrontendOperationStatus RequestFrontendSave(FrontendControllerToken token, const char *basename);
        friend FrontendOperationStatus ObserveFrontendSave(FrontendControllerToken token, struct FrontendSaveSnapshot *snapshot);
        friend FrontendOperationStatus DispatchFrontendControl(FrontendControllerToken token, const char *name);
    };

    struct FrontendSaveSnapshot
    {
        bool request_pending = false;
        bool completed = false;
        char selected_basename[260]{};
    };

    /** Installs the verified frontend and main-menu lifecycle hooks transactionally. */
    FrontendOperationStatus InstallFrontendAutomationHooks();
    /** Restores a frontend hook installation after a later startup step fails. */
    FrontendOperationStatus RollbackFrontendAutomationHooks();
    /** Registers the one bundled automation notification without exposing a controller address. */
    FrontendOperationStatus SetFrontendControllerCaptureCallback(FrontendControllerCaptureCallback callback);
    /** Makes lifecycle callbacks inert and invalidates every controller capability. */
    void DeactivateFrontendAutomation() noexcept;
    /** Copies a capability only while the matching controller capture remains current. */
    FrontendOperationStatus AcquireFrontendController(
        FrontendControllerKind kind, FrontendControllerToken *token);
    /** Invalidates a matching controller capture; stale capabilities are harmless. */
    FrontendOperationStatus ReleaseFrontendController(FrontendControllerToken token);
    /** Resolves and emits the verified main-menu Single Player control signal. */
    FrontendOperationStatus DispatchMainMenuSinglePlayer(FrontendControllerToken token);
    /** Selects an allowed basename and requests native save loading with readback. */
    FrontendOperationStatus RequestFrontendSave(FrontendControllerToken token, const char *basename);
    /** Copies the native save request/completion flags after controller and thread validation. */
    FrontendOperationStatus ObserveFrontendSave(FrontendControllerToken token, FrontendSaveSnapshot *snapshot);
    /** Resolves and emits one verified frontend control signal. */
    FrontendOperationStatus DispatchFrontendControl(FrontendControllerToken token, const char *name);

    enum class ObserverObservationStatus
    {
        completed,
        outside_campaign,
        invalid_state,
        signature_mismatch,
        not_found,
    };

    enum class ObserverOperationStatus
    {
        completed,
        outside_campaign,
        invalid_state,
        signature_mismatch,
        unavailable,
        precondition_failed,
        command_failed,
        readback_failed,
    };

    struct ObserverTag
    {
        char value[4]{};
        int32_t ordinal = -1;

        bool normalized_candidate() const noexcept;
        const char *str() const noexcept { return value; }
    };

    struct ObserverCountrySnapshot
    {
        ObserverTag tag{};
        bool exists = false;
        bool human_controlled = false;
        bool has_ai = false;
        bool ai_scheduled = false;

        bool healthy_ai() const noexcept
        {
            return exists && !human_controlled && has_ai && ai_scheduled;
        }
    };

    struct ObserverStateSnapshot
    {
        ObserverCountrySnapshot view_country{};
        uint32_t country_count = 0;
        uint32_t country_ai_count = 0;
        bool human_control_present = false;
        bool full_map_visibility_enabled = false;
    };

    struct CampaignConsoleCommandResult
    {
        bool success = false;
        bool message_available = false;
        char message[128]{};
    };

    enum class CampaignConsoleCaptureStatus
    {
        observer_disabled,
        completed,
        already_configured,
        command_conflict,
        native_tag_unavailable,
    };

    enum class CampaignConsoleCommand : uint8_t
    {
        native_tag,
        observer_switch,
    };

    /** Checked, copied console input. No engine-owned argument storage escapes the runtime. */
    struct CampaignConsoleArguments
    {
        bool valid = false;
        uint32_t count = 0;
        char first[128]{};
    };

    struct CampaignConsoleResponse
    {
        bool success = false;
        char message[128]{};

        CampaignConsoleResponse() = default;
        CampaignConsoleResponse(const char *text, bool result = true) noexcept;
    };

    using CampaignAnnexationCallback = void (__stdcall *)(int32_t annexed_ordinal);
    using CampaignConsoleCaptureCallback = void (__stdcall *)(CampaignConsoleCaptureStatus status);
    using CampaignConsoleCallback = CampaignConsoleResponse (__stdcall *)(
        CampaignConsoleCommand command, const CampaignConsoleArguments &arguments);

    struct CampaignAutomationCallbacks
    {
        CampaignAnnexationCallback annexation = nullptr;
        CampaignConsoleCaptureCallback console_capture = nullptr;
        CampaignConsoleCallback console = nullptr;
    };

    /** Copies the observer view, ownership, scheduler, and FOW state without retaining game pointers. */
    ObserverObservationStatus ReadObserverState(ObserverStateSnapshot *snapshot);
    /** Resolves one country database ordinal into a copied observer snapshot. */
    ObserverObservationStatus ReadObserverCountry(int32_t ordinal, ObserverCountrySnapshot *snapshot);
    /** Resolves a normalized three-character tag into a copied observer snapshot. */
    ObserverObservationStatus ResolveObserverCountry(const char tag[4], ObserverCountrySnapshot *snapshot);
    /** Selects the first living, scheduled-AI country other than the excluded ordinal. */
    ObserverObservationStatus FindHealthyObserverCountry(int32_t excluded_ordinal, ObserverCountrySnapshot *snapshot);
    /** Returns one currently human-controlled country to AI and verifies its restored AI state. */
    ObserverOperationStatus ReturnObserverCountryToAI(
        const ObserverCountrySnapshot &country, ObserverStateSnapshot *after = nullptr);
    /** Changes only the observer camera tag and verifies ownership and scheduler counts are unchanged. */
    ObserverOperationStatus SetObserverViewCountry(
        const ObserverCountrySnapshot &country, ObserverStateSnapshot *after = nullptr);
    /** Installs annex and message hooks transactionally and retains their raw ownership in the runtime. */
    CampaignOperationStatus InstallCampaignAutomationHooks(CampaignAutomationCallbacks callbacks);
    /** Enables observer-only command replacement and resets the copied popup counter. */
    void SetCampaignObserverMode(bool enabled) noexcept;
    /** Registers runtime-owned console capture; no engine console object crosses into the plugin. */
    bool RegisterCampaignConsoleCapture(Plugin *owner);
    void UnregisterCampaignConsoleCapture(Plugin *owner) noexcept;
    /** Makes callbacks inert, restores/removes observer commands, and disables popup suppression. */
    void DeactivateCampaignAutomation() noexcept;
    bool IsCampaignObserverConsoleReady() noexcept;
    /** Invokes the captured native asynchronous tag operation with a copied tag argument. */
    ObserverOperationStatus StartNativeObserverTagSwitch(
        const ObserverTag &tag, CampaignConsoleCommandResult *result = nullptr);
    /** Validates and invokes the native debug fow command, then requires visibility readback. */
    ObserverOperationStatus EnableObserverFullMapVisibility();
    void SetCampaignMessagePopupSuppression(bool enabled) noexcept;
    int32_t CampaignSuppressedMessageCount() noexcept;

    /**
     * Pauses an active CInGameIdler through the verified native operation.
     * The call checks executable identity, object readability and RTTI, code
     * bytes, and paused readback; it retains no game pointer.
     */
    PauseOperationStatus PauseGame();
    bool IsPauseOperationAvailable();

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

        friend PopInterestMutationStatus ApplyPopInterestBatch(
            DailyInterestAccess &access, PopInterestBatchEntry *entries, uint32_t entry_count,
            PopInterestBatchResult *result);
    };

    class BankInterestAccess
    {
    public:
        BankInterestAccess(const BankInterestAccess &) = delete;
        BankInterestAccess &operator=(const BankInterestAccess &) = delete;
        BankInterestAccess(BankInterestAccess &&) = default;
        BankInterestAccess &operator=(BankInterestAccess &&) = default;

        static BankInterestAccess FromEvent(events::BankInterestEvent &event);

        GameStateRef game_state() const noexcept { return game_state_; }
        CountryRef country() const noexcept { return country_; }
        uint64_t session_epoch() const noexcept { return session_epoch_; }
        bool after() const noexcept { return after_; }
        bool first_country() const noexcept { return first_country_; }

    private:
        BankInterestAccess(GameSession session, CountryRef country, const void *bank,
                           bool after, bool first_country, uint64_t generation) noexcept;
        PopInterestMutationStatus CheckMutationAccess(bool require_after) const;
        PopInterestMutationStatus CheckPreparedMutationAccess() const;
        PopInterestMutationStatus CheckSignature(bool recheck = false);
        bool ContainsPreparedState(const StateInterestCandidate &state) const;

        GameStateRef game_state_{};
        CountryRef country_{};
        const void *bank_ = nullptr;
        std::thread::id thread_{};
        uint64_t generation_ = 0;
        uint64_t session_epoch_ = 0;
        bool after_ = false;
        bool first_country_ = false;
        bool signature_checked_ = false;
        PopInterestMutationStatus signature_status_ = PopInterestMutationStatus::unavailable;
        std::array<uintptr_t, 512> prepared_state_addresses_{};
        uint32_t prepared_state_count_ = 0;

        friend PopInterestMutationStatus DiscardStateInterestPools(
            BankInterestAccess &access, StateInterestInitializationResult *result);
        friend PopInterestMutationStatus PrepareCountryStateInterestPayouts(
            BankInterestAccess &access, const StateInterestCandidate *states, uint32_t state_count);
        friend PopInterestMutationStatus ApplyStateInterestPayout(
            BankInterestAccess &access, const StateInterestCandidate &state,
            PopInterestBatchEntry *entries, uint32_t entry_count,
            PopInterestBatchResult *result);
    };

    /** Checks the complete verified POP money write span without writing it. */
    bool IsPopInterestWritable(PopRef pop);
    /**
     * Preflights every positive payout before the first write, then invokes the
     * native operation in entry order with immediate per-POP postconditions.
     * Session, phase, signature, and page checks are amortized across the
     * synchronous batch. write_count includes a call whose postcondition fails;
     * verified_count includes only calls with successful postconditions.
     */
    PopInterestMutationStatus ApplyPopInterestBatch(
        DailyInterestAccess &access, PopInterestBatchEntry *entries, uint32_t entry_count,
        PopInterestBatchResult *result);
    /** Discards serialized orphan pools once before a campaign begins paying new interest. */
    PopInterestMutationStatus DiscardStateInterestPools(
        BankInterestAccess &access, StateInterestInitializationResult *result);
    /** Validates one complete country state snapshot for subsequent payouts in the same callback. */
    PopInterestMutationStatus PrepareCountryStateInterestPayouts(
        BankInterestAccess &access, const StateInterestCandidate *states, uint32_t state_count);
    /** Pays one unchanged state pool and clears it only after every POP postcondition succeeds. */
    PopInterestMutationStatus ApplyStateInterestPayout(
        BankInterestAccess &access, const StateInterestCandidate &state,
        PopInterestBatchEntry *entries, uint32_t entry_count,
        PopInterestBatchResult *result);
}
