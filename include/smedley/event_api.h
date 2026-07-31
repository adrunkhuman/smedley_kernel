#ifndef SMEDLEY_EVENT_API_H
#define SMEDLEY_EVENT_API_H

#include <stdint.h>

#ifdef _WIN32
#define SMEDLEY_EVENT_CALL __cdecl
#ifdef SMEDLEY_EVENT_BUILD
#define SMEDLEY_EVENT_EXPORT __declspec(dllexport)
#else
#define SMEDLEY_EVENT_EXPORT
#endif
#else
#define SMEDLEY_EVENT_CALL
#define SMEDLEY_EVENT_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SMEDLEY_EVENT_API_VERSION_V1 UINT32_C(1)
#define SMEDLEY_DAILY_EVENT_VERSION_V1 UINT32_C(1)
#define SMEDLEY_EVENT_GET_API_V1_SYMBOL "SmedleyGetEventApiV1"
#define SMEDLEY_EVENT_MAX_DAILY_REGISTRATIONS UINT32_C(64)

typedef uint32_t SmedleyEventResult;
enum {
    SMEDLEY_EVENT_SUCCESS = 0,
    SMEDLEY_EVENT_INVALID_ARGUMENT = 1,
    SMEDLEY_EVENT_CAPACITY = 2,
    SMEDLEY_EVENT_NOT_FOUND = 3,
    SMEDLEY_EVENT_BUSY = 4
};

typedef uint32_t SmedleyEventCallbackResult;
enum {
    SMEDLEY_EVENT_CALLBACK_CONTINUE = 0,
    SMEDLEY_EVENT_CALLBACK_DISABLE = 1
};

typedef uint64_t SmedleyEventRegistration;

typedef struct SmedleyDailyEventV1 {
    uint32_t struct_size;
    uint32_t version;
    int64_t treasury_raw;
    int32_t game_date_raw;
    uint32_t country_slot_count;
    uint32_t ai_scheduler_entry_count;
    char country_tag[4];
    uint32_t has_owned_province;
    uint32_t human_control_present;
    uint32_t reserved[4];
} SmedleyDailyEventV1;

/* The snapshot pointer is valid only for this call. Callbacks run on
 * Victoria II's country-update thread and must not block, allocate, perform I/O,
 * throw, retain the snapshot, or call unregister. */
typedef SmedleyEventCallbackResult (SMEDLEY_EVENT_CALL *SmedleyDailyEventCallbackV1Fn)(
    void *context, const SmedleyDailyEventV1 *event);

typedef SmedleyEventResult (SMEDLEY_EVENT_CALL *SmedleyRegisterDailyEventV1Fn)(
    SmedleyDailyEventCallbackV1Fn callback, void *context, SmedleyEventRegistration *registration);
typedef SmedleyEventResult (SMEDLEY_EVENT_CALL *SmedleyUnregisterEventV1Fn)(
    SmedleyEventRegistration registration);

typedef struct SmedleyEventApiV1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t reserved[4];
    SmedleyRegisterDailyEventV1Fn register_daily;
    SmedleyUnregisterEventV1Fn unregister;
} SmedleyEventApiV1;

typedef SmedleyEventResult (SMEDLEY_EVENT_CALL *SmedleyGetEventApiV1Fn)(SmedleyEventApiV1 *api);

SMEDLEY_EVENT_EXPORT SmedleyEventResult SMEDLEY_EVENT_CALL SmedleyGetEventApiV1(SmedleyEventApiV1 *api);

#ifdef __cplusplus
}
#endif

#endif
