#ifndef SMEDLEY_EVENT_SERVICES_API_H
#define SMEDLEY_EVENT_SERVICES_API_H

#include <stdint.h>

#ifdef _WIN32
#define SMEDLEY_EVENT_SERVICES_CALL __cdecl
#ifdef SMEDLEY_EVENT_SERVICES_BUILD
#define SMEDLEY_EVENT_SERVICES_EXPORT __declspec(dllexport)
#else
#define SMEDLEY_EVENT_SERVICES_EXPORT
#endif
#else
#define SMEDLEY_EVENT_SERVICES_CALL
#define SMEDLEY_EVENT_SERVICES_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SMEDLEY_EVENT_SERVICES_API_VERSION_V1 UINT32_C(1)
#define SMEDLEY_BANK_INTEREST_EVENT_VERSION_V1 UINT32_C(1)
#define SMEDLEY_CAMPAIGN_CONSOLE_INPUT_VERSION_V1 UINT32_C(1)
#define SMEDLEY_CAMPAIGN_CONSOLE_RESULT_VERSION_V1 UINT32_C(1)
#define SMEDLEY_EVENT_SERVICES_GET_API_V1_SYMBOL "SmedleyGetEventServicesApiV1"
#define SMEDLEY_EVENT_SERVICES_MAX_BANK_INTEREST_REGISTRATIONS UINT32_C(32)
#define SMEDLEY_EVENT_SERVICES_MAX_CAMPAIGN_CONSOLE_REGISTRATIONS UINT32_C(16)
#define SMEDLEY_CAMPAIGN_CONSOLE_MAX_ARGUMENT_BYTES UINT32_C(128)
#define SMEDLEY_CAMPAIGN_CONSOLE_MAX_RESULT_BYTES UINT32_C(128)

typedef uint32_t SmedleyEventServicesResult;
enum {
    SMEDLEY_EVENT_SERVICES_SUCCESS = 0,
    SMEDLEY_EVENT_SERVICES_INVALID_ARGUMENT = 1,
    SMEDLEY_EVENT_SERVICES_CAPACITY = 2,
    SMEDLEY_EVENT_SERVICES_NOT_FOUND = 3,
    SMEDLEY_EVENT_SERVICES_BUSY = 4
};

typedef uint32_t SmedleyEventServicesCallbackResult;
enum {
    SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE = 0,
    SMEDLEY_EVENT_SERVICES_CALLBACK_DISABLE = 1
};

typedef uint64_t SmedleyEventServicesRegistration;
typedef uint64_t SmedleyBankInterestAuthority;

enum {
    SMEDLEY_BANK_INTEREST_BEFORE = 0,
    SMEDLEY_BANK_INTEREST_AFTER = 1
};

typedef struct SmedleyBankInterestEventV1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t phase;
    uint32_t country_index;
    uint32_t distributes_to_states;
    uint32_t reserved[5];
    SmedleyBankInterestAuthority authority;
} SmedleyBankInterestEventV1;

enum {
    SMEDLEY_CAMPAIGN_CONSOLE_NATIVE_TAG = 0,
    SMEDLEY_CAMPAIGN_CONSOLE_OBSERVER_SWITCH = 1
};

typedef struct SmedleyCampaignConsoleInputV1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t command;
    uint32_t argument_count;
    uint32_t arguments_valid;
    char first_argument[SMEDLEY_CAMPAIGN_CONSOLE_MAX_ARGUMENT_BYTES];
    uint32_t reserved[3];
} SmedleyCampaignConsoleInputV1;

typedef struct SmedleyCampaignConsoleResultV1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t handled;
    uint32_t success;
    uint32_t message_bytes;
    char message[SMEDLEY_CAMPAIGN_CONSOLE_MAX_RESULT_BYTES];
    uint32_t reserved[3];
} SmedleyCampaignConsoleResultV1;

/* Callbacks run synchronously in game hook paths. Records and bank authority
 * tokens are valid only for the call. Do not block, allocate, perform I/O,
 * retain pointers or tokens, throw, or call unregister from a callback. */
typedef SmedleyEventServicesCallbackResult (SMEDLEY_EVENT_SERVICES_CALL *SmedleyBankInterestCallbackV1Fn)(
    void *context, const SmedleyBankInterestEventV1 *event);
typedef SmedleyEventServicesCallbackResult (SMEDLEY_EVENT_SERVICES_CALL *SmedleyCampaignConsoleCallbackV1Fn)(
    void *context, const SmedleyCampaignConsoleInputV1 *input, SmedleyCampaignConsoleResultV1 *result);

typedef SmedleyEventServicesResult (SMEDLEY_EVENT_SERVICES_CALL *SmedleyRegisterBankInterestV1Fn)(
    SmedleyBankInterestCallbackV1Fn callback, void *context, SmedleyEventServicesRegistration *registration);
typedef SmedleyEventServicesResult (SMEDLEY_EVENT_SERVICES_CALL *SmedleyRegisterCampaignConsoleV1Fn)(
    SmedleyCampaignConsoleCallbackV1Fn callback, void *context, SmedleyEventServicesRegistration *registration);
typedef SmedleyEventServicesResult (SMEDLEY_EVENT_SERVICES_CALL *SmedleyUnregisterEventServicesV1Fn)(
    SmedleyEventServicesRegistration registration);

typedef struct SmedleyEventServicesApiV1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t reserved[3];
    SmedleyRegisterBankInterestV1Fn register_bank_interest;
    SmedleyRegisterCampaignConsoleV1Fn register_campaign_console;
    SmedleyUnregisterEventServicesV1Fn unregister;
} SmedleyEventServicesApiV1;

typedef SmedleyEventServicesResult (SMEDLEY_EVENT_SERVICES_CALL *SmedleyGetEventServicesApiV1Fn)(
    SmedleyEventServicesApiV1 *api);

SMEDLEY_EVENT_SERVICES_EXPORT SmedleyEventServicesResult SMEDLEY_EVENT_SERVICES_CALL
SmedleyGetEventServicesApiV1(SmedleyEventServicesApiV1 *api);

#ifdef __cplusplus
}
#endif

#endif
