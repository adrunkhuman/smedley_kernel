#ifndef SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_API_H
#define SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_API_H

#include <stdint.h>

#ifdef _WIN32
#define SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALL __cdecl
#ifdef SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_BUILD
#define SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_EXPORT __declspec(dllexport)
#else
#define SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_EXPORT
#endif
#else
#define SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALL
#define SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_API_VERSION_V1 UINT32_C(1)
#define SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_EVENT_VERSION_V1 UINT32_C(1)
#define SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_GET_API_V1_SYMBOL "SmedleyGetProvinceModifierReconciliationApiV1"
#define SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_REGISTRATIONS UINT32_C(16)
#define SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_KEYS UINT32_C(16)
#define SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_KEY_BYTES UINT32_C(64)
#define SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_PROVINCES UINT32_C(4096)
#define SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_MAX_COUNTRIES UINT32_C(1024)

typedef uint32_t SmedleyProvinceModifierReconciliationResult;
enum {
    SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_SUCCESS = 0,
    SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_INVALID_ARGUMENT = 1,
    SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNAVAILABLE = 2,
    SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_STALE_AUTHORITY = 3,
    SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_PRECONDITION_FAILED = 4,
    SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_PARTIAL_MUTATION = 5,
    SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_DEFERRED = 6,
    SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CAPACITY = 7,
    SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_UNSUPPORTED = 8,
    SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_NOT_FOUND = 9,
    SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_BUSY = 10
};

typedef uint32_t SmedleyProvinceModifierReconciliationCallbackResult;
enum {
    SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALLBACK_CONTINUE = 0,
    SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALLBACK_DISABLE = 1
};

typedef uint64_t SmedleyProvinceModifierReconciliationAuthority;
typedef uint64_t SmedleyProvinceModifierReconciliationRegistration;
typedef uint64_t SmedleyReconciliationProvince;
typedef uint64_t SmedleyReconciliationCountry;

typedef struct SmedleyProvinceModifierReconciliationEventV1 {
    uint32_t struct_size, version;
    SmedleyProvinceModifierReconciliationAuthority authority;
    uint32_t reserved[4];
} SmedleyProvinceModifierReconciliationEventV1;

typedef struct SmedleyReconciliationKeyV1 {
    uint32_t struct_size, version, bytes;
    char value[SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_KEY_BYTES];
    uint32_t reserved[3];
} SmedleyReconciliationKeyV1;

typedef struct SmedleyReconciliationProvinceSnapshotV1 {
    uint32_t struct_size, version;
    SmedleyReconciliationProvince province;
    SmedleyReconciliationCountry owner;
    uint32_t present_modifier_mask;
    uint32_t reserved[3];
} SmedleyReconciliationProvinceSnapshotV1;

typedef struct SmedleyReconciliationCountrySnapshotV1 {
    uint32_t struct_size, version;
    SmedleyReconciliationCountry country;
    uint32_t owner_option_selected;
    uint32_t reserved[3];
} SmedleyReconciliationCountrySnapshotV1;

typedef struct SmedleyReconciliationRemovalV1 {
    uint32_t struct_size, version;
    SmedleyReconciliationProvince province;
    SmedleyReconciliationCountry expected_owner;
    uint32_t expected_present_modifier_mask;
    uint32_t remove_modifier_mask;
    uint32_t expected_owner_option_selected;
    uint32_t reserved[3];
} SmedleyReconciliationRemovalV1;

typedef struct SmedleyReconciliationBatchResultV1 {
    uint32_t struct_size, version;
    uint32_t failed_index, write_count, verified_count, deferred_count;
    uint32_t reserved[3];
} SmedleyReconciliationBatchResultV1;

/* failed_index identifies the zero-based removal request that stopped, or
 * removal_count on success. write_count counts returned native remover calls,
 * including a call whose postcondition later fails. verified_count counts
 * calls whose immediate postconditions passed, so PARTIAL_MUTATION may report
 * write_count greater than verified_count. deferred_count is one when an
 * iteration guard defers the batch; deferred batches perform no writes. */

typedef SmedleyProvinceModifierReconciliationCallbackResult
(SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALL *SmedleyProvinceModifierReconciliationCallbackV1Fn)(
    void *context, const SmedleyProvinceModifierReconciliationEventV1 *event);

typedef SmedleyProvinceModifierReconciliationResult
(SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALL *SmedleyRegisterProvinceModifierReconciliationV1Fn)(
    SmedleyProvinceModifierReconciliationCallbackV1Fn callback, void *context,
    SmedleyProvinceModifierReconciliationRegistration *registration);

typedef SmedleyProvinceModifierReconciliationResult
(SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALL *SmedleyUnregisterProvinceModifierReconciliationV1Fn)(
    SmedleyProvinceModifierReconciliationRegistration registration);

typedef SmedleyProvinceModifierReconciliationResult
(SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALL *SmedleyCollectProvinceModifierReconciliationV1Fn)(
    SmedleyProvinceModifierReconciliationAuthority authority,
    const SmedleyReconciliationKeyV1 *modifier_keys,
    uint32_t modifier_key_count,
    const SmedleyReconciliationKeyV1 *owner_option_key,
    SmedleyReconciliationProvinceSnapshotV1 *provinces,
    uint32_t province_capacity,
    uint32_t *province_count,
    SmedleyReconciliationCountrySnapshotV1 *countries,
    uint32_t country_capacity,
    uint32_t *country_count);

typedef SmedleyProvinceModifierReconciliationResult
(SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALL *SmedleyApplyProvinceModifierReconciliationV1Fn)(
    SmedleyProvinceModifierReconciliationAuthority authority,
    const SmedleyReconciliationRemovalV1 *removals,
    uint32_t removal_count,
    SmedleyReconciliationBatchResultV1 *result);

typedef struct SmedleyProvinceModifierReconciliationApiV1 {
    uint32_t struct_size, version, reserved[2];
    SmedleyRegisterProvinceModifierReconciliationV1Fn register_callback;
    SmedleyUnregisterProvinceModifierReconciliationV1Fn unregister_callback;
    SmedleyCollectProvinceModifierReconciliationV1Fn collect;
    SmedleyApplyProvinceModifierReconciliationV1Fn apply;
} SmedleyProvinceModifierReconciliationApiV1;

/* Registered callbacks run synchronously at a reconciliation-owned safe
 * boundary on the game thread. Event records and authorities are valid only
 * for the call. Do not block, allocate, perform I/O, retain pointers or tokens,
 * throw, or call unregister_callback from a callback. Returning DISABLE or an
 * unknown result makes the callback inert until its owner unregisters it.
 *
 * collect resolves the supplied generic keys and copies every province that
 * currently has at least one requested modifier plus each referenced owner's
 * current political-option selection. Handles are valid only for the current
 * reconciliation callback and only for apply on the same API table and game
 * thread. collect and apply must run synchronously within that callback;
 * the authority and all returned handles expire when the callback returns.
 * Province and country handles are domain-tagged and are not interchangeable.
 * CAPACITY reports required counts without partially writing output records.
 *
 * apply preflights the complete batch before its first native call. It returns
 * DEFERRED without mutation if any target province is being iterated by the
 * engine. PARTIAL_MUTATION means at least one native call may have occurred;
 * callers must stop automation. Successful calls verify removed-modifier
 * absence and unchanged owner, policy, and non-target modifier bits. */

typedef SmedleyProvinceModifierReconciliationResult
(SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALL *SmedleyGetProvinceModifierReconciliationApiV1Fn)(
    SmedleyProvinceModifierReconciliationApiV1 *api);

SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_EXPORT SmedleyProvinceModifierReconciliationResult
SMEDLEY_PROVINCE_MODIFIER_RECONCILIATION_CALL
SmedleyGetProvinceModifierReconciliationApiV1(SmedleyProvinceModifierReconciliationApiV1 *api);

#ifdef __cplusplus
}
#endif

#endif
