#ifndef SMEDLEY_CAMPAIGN_AUTOMATION_API_H
#define SMEDLEY_CAMPAIGN_AUTOMATION_API_H

#include <stdint.h>
#include <smedley/campaign_runtime_api.h>

#ifdef _WIN32
#define SMEDLEY_CAMPAIGN_AUTOMATION_CALL __cdecl
#ifdef SMEDLEY_CAMPAIGN_AUTOMATION_BUILD
#define SMEDLEY_CAMPAIGN_AUTOMATION_EXPORT __declspec(dllexport)
#else
#define SMEDLEY_CAMPAIGN_AUTOMATION_EXPORT
#endif
#else
#define SMEDLEY_CAMPAIGN_AUTOMATION_CALL
#define SMEDLEY_CAMPAIGN_AUTOMATION_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SMEDLEY_CAMPAIGN_AUTOMATION_API_VERSION_V1 UINT32_C(1)
#define SMEDLEY_CAMPAIGN_AUTOMATION_GET_API_V1_SYMBOL "SmedleyGetCampaignAutomationApiV1"
#define SMEDLEY_CAMPAIGN_AUTOMATION_MAX_REGISTRATIONS UINT32_C(1)
#define SMEDLEY_CAMPAIGN_AUTOMATION_MESSAGE_BYTES UINT32_C(128)

typedef uint32_t SmedleyCampaignAutomationResult;
enum {
    SMEDLEY_CAMPAIGN_AUTOMATION_SUCCESS = 0,
    SMEDLEY_CAMPAIGN_AUTOMATION_INVALID_ARGUMENT = 1,
    SMEDLEY_CAMPAIGN_AUTOMATION_UNAVAILABLE = 2,
    SMEDLEY_CAMPAIGN_AUTOMATION_STALE_HANDLE = 3,
    SMEDLEY_CAMPAIGN_AUTOMATION_PRECONDITION_FAILED = 4,
    SMEDLEY_CAMPAIGN_AUTOMATION_READBACK_FAILED = 5,
    SMEDLEY_CAMPAIGN_AUTOMATION_WRONG_THREAD = 6,
    SMEDLEY_CAMPAIGN_AUTOMATION_CAPACITY = 7,
    SMEDLEY_CAMPAIGN_AUTOMATION_BUSY = 8
};

typedef uint32_t SmedleyCampaignAutomationCallbackResult;
enum {
    SMEDLEY_CAMPAIGN_AUTOMATION_CALLBACK_CONTINUE = 0,
    SMEDLEY_CAMPAIGN_AUTOMATION_CALLBACK_DISABLE = 1
};

typedef uint64_t SmedleyCampaignSession;
typedef uint64_t SmedleyCampaignAutomation;

enum {
    SMEDLEY_FRONTEND_CONTROLLER_FRONTEND = 0,
    SMEDLEY_FRONTEND_CONTROLLER_MAIN_MENU = 1,
    SMEDLEY_CAMPAIGN_CONSOLE_OBSERVER_DISABLED = 0,
    SMEDLEY_CAMPAIGN_CONSOLE_COMPLETED = 1,
    SMEDLEY_CAMPAIGN_CONSOLE_ALREADY_CONFIGURED = 2,
    SMEDLEY_CAMPAIGN_CONSOLE_COMMAND_CONFLICT = 3,
    SMEDLEY_CAMPAIGN_CONSOLE_NATIVE_TAG_UNAVAILABLE = 4
};

typedef struct SmedleyCampaignFrontendCaptureV1 {
    uint32_t struct_size, version, controller_kind, reserved[3];
    uint64_t epoch;
} SmedleyCampaignFrontendCaptureV1;
typedef struct SmedleyCampaignAnnexationV1 {
    uint32_t struct_size, version;
    int32_t annexed_ordinal;
    uint32_t reserved[3];
    uint64_t epoch;
} SmedleyCampaignAnnexationV1;
typedef struct SmedleyCampaignConsoleCaptureV1 {
    uint32_t struct_size, version, status, ready, reserved[3];
    uint64_t epoch;
} SmedleyCampaignConsoleCaptureV1;
typedef struct SmedleyCampaignTagV1 {
    uint32_t struct_size, version;
    char tag[4];
    int32_t ordinal;
    uint32_t reserved[3];
} SmedleyCampaignTagV1;
typedef struct SmedleyCampaignConsoleStateV1 {
    uint32_t struct_size, version, ready, reserved[3];
    uint64_t epoch;
} SmedleyCampaignConsoleStateV1;
typedef struct SmedleyCampaignConsoleCommandResultV1 {
    uint32_t struct_size, version, success, message_available, message_bytes;
    char message[SMEDLEY_CAMPAIGN_AUTOMATION_MESSAGE_BYTES];
    uint32_t reserved[3];
} SmedleyCampaignConsoleCommandResultV1;
typedef struct SmedleyCampaignPopupSnapshotV1 {
    uint32_t struct_size, version, suppression_enabled, reserved[3];
    uint64_t suppressed_count;
} SmedleyCampaignPopupSnapshotV1;
typedef struct SmedleyCampaignProcessMetricsV1 {
    uint32_t struct_size, version, availability_flags, reserved[3];
    int64_t process_cpu_us, working_set_bytes, private_bytes, peak_working_set_bytes;
} SmedleyCampaignProcessMetricsV1;

/* These callbacks run synchronously in engine hook paths. They receive copied
 * records only. Do not allocate, perform I/O, block, throw, call deactivation,
 * or retain the record pointer; enqueue copied work for a later owner-thread call. */
typedef SmedleyCampaignAutomationCallbackResult (SMEDLEY_CAMPAIGN_AUTOMATION_CALL *SmedleyCampaignFrontendCaptureCallbackV1Fn)(
    uint64_t context, const SmedleyCampaignFrontendCaptureV1 *event);
typedef SmedleyCampaignAutomationCallbackResult (SMEDLEY_CAMPAIGN_AUTOMATION_CALL *SmedleyCampaignAnnexationCallbackV1Fn)(
    uint64_t context, const SmedleyCampaignAnnexationV1 *event);
typedef SmedleyCampaignAutomationCallbackResult (SMEDLEY_CAMPAIGN_AUTOMATION_CALL *SmedleyCampaignConsoleCaptureCallbackV1Fn)(
    uint64_t context, const SmedleyCampaignConsoleCaptureV1 *event);

typedef struct SmedleyCampaignAutomationOptionsV1 {
    uint32_t struct_size, version, reserved[3];
    uint64_t context;
    SmedleyCampaignFrontendCaptureCallbackV1Fn frontend_capture;
    SmedleyCampaignAnnexationCallbackV1Fn annexation;
    SmedleyCampaignConsoleCaptureCallbackV1Fn console_capture;
} SmedleyCampaignAutomationOptionsV1;

typedef SmedleyCampaignAutomationResult (SMEDLEY_CAMPAIGN_AUTOMATION_CALL *SmedleyInstallCampaignAutomationV1Fn)(
    SmedleyCampaignSession session, const SmedleyCampaignAutomationOptionsV1 *options,
    SmedleyCampaignAutomation *automation);
typedef SmedleyCampaignAutomationResult (SMEDLEY_CAMPAIGN_AUTOMATION_CALL *SmedleyDeactivateCampaignAutomationV1Fn)(
    SmedleyCampaignAutomation automation);
typedef SmedleyCampaignAutomationResult (SMEDLEY_CAMPAIGN_AUTOMATION_CALL *SmedleySetCampaignObserverModeV1Fn)(
    SmedleyCampaignAutomation automation, uint32_t enabled);
typedef SmedleyCampaignAutomationResult (SMEDLEY_CAMPAIGN_AUTOMATION_CALL *SmedleyReadCampaignConsoleStateV1Fn)(
    SmedleyCampaignAutomation automation, SmedleyCampaignConsoleStateV1 *state);
typedef SmedleyCampaignAutomationResult (SMEDLEY_CAMPAIGN_AUTOMATION_CALL *SmedleyReadObserverCountryByTagV1Fn)(
    SmedleyCampaignSession session, const SmedleyCampaignTagV1 *tag, SmedleyObserverCountrySnapshotV1 *country_snapshot);
typedef SmedleyCampaignAutomationResult (SMEDLEY_CAMPAIGN_AUTOMATION_CALL *SmedleyStartObserverTagSwitchV1Fn)(
    SmedleyCampaignAutomation automation, const SmedleyCampaignTagV1 *tag,
    SmedleyCampaignConsoleCommandResultV1 *result);
typedef SmedleyCampaignAutomationResult (SMEDLEY_CAMPAIGN_AUTOMATION_CALL *SmedleySetCampaignPopupSuppressionV1Fn)(
    SmedleyCampaignAutomation automation, uint32_t enabled);
typedef SmedleyCampaignAutomationResult (SMEDLEY_CAMPAIGN_AUTOMATION_CALL *SmedleyReadCampaignPopupStateV1Fn)(
    SmedleyCampaignAutomation automation, SmedleyCampaignPopupSnapshotV1 *snapshot);
typedef SmedleyCampaignAutomationResult (SMEDLEY_CAMPAIGN_AUTOMATION_CALL *SmedleyReadCampaignProcessMetricsV1Fn)(
    SmedleyCampaignSession session, SmedleyCampaignProcessMetricsV1 *metrics);

typedef struct SmedleyCampaignAutomationApiV1 {
    uint32_t struct_size, version, reserved[2];
    SmedleyInstallCampaignAutomationV1Fn install;
    SmedleyDeactivateCampaignAutomationV1Fn deactivate;
    SmedleySetCampaignObserverModeV1Fn set_observer_mode;
    SmedleyReadCampaignConsoleStateV1Fn read_console_state;
    SmedleyReadObserverCountryByTagV1Fn read_observer_country_by_tag;
    SmedleyStartObserverTagSwitchV1Fn start_observer_tag_switch;
    SmedleySetCampaignPopupSuppressionV1Fn set_popup_suppression;
    SmedleyReadCampaignPopupStateV1Fn read_popup_state;
    SmedleyReadCampaignProcessMetricsV1Fn read_process_metrics;
} SmedleyCampaignAutomationApiV1;

typedef SmedleyCampaignAutomationResult (SMEDLEY_CAMPAIGN_AUTOMATION_CALL *SmedleyGetCampaignAutomationApiV1Fn)(
    SmedleyCampaignAutomationApiV1 *api);
SMEDLEY_CAMPAIGN_AUTOMATION_EXPORT SmedleyCampaignAutomationResult SMEDLEY_CAMPAIGN_AUTOMATION_CALL
SmedleyGetCampaignAutomationApiV1(SmedleyCampaignAutomationApiV1 *api);

#ifdef __cplusplus
}
#endif
#endif
