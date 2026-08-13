#ifndef SMEDLEY_INTEREST_POOL_API_H
#define SMEDLEY_INTEREST_POOL_API_H

#include <stdint.h>
#include <smedley/event_services_api.h>

#ifdef _WIN32
#define SMEDLEY_INTEREST_POOL_CALL __cdecl
#ifdef SMEDLEY_INTEREST_POOL_BUILD
#define SMEDLEY_INTEREST_POOL_EXPORT __declspec(dllexport)
#else
#define SMEDLEY_INTEREST_POOL_EXPORT
#endif
#else
#define SMEDLEY_INTEREST_POOL_CALL
#define SMEDLEY_INTEREST_POOL_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SMEDLEY_INTEREST_POOL_API_VERSION_V1 UINT32_C(1)
#define SMEDLEY_INTEREST_POOL_GET_API_V1_SYMBOL "SmedleyGetInterestPoolApiV1"
#define SMEDLEY_INTEREST_POOL_MAX_STATES UINT32_C(512)
#define SMEDLEY_INTEREST_POOL_MAX_POPS UINT32_C(100000)
typedef uint32_t SmedleyInterestPoolResult;
enum { SMEDLEY_INTEREST_POOL_SUCCESS = 0, SMEDLEY_INTEREST_POOL_INVALID_ARGUMENT = 1,
    SMEDLEY_INTEREST_POOL_UNAVAILABLE = 2, SMEDLEY_INTEREST_POOL_STALE_AUTHORITY = 3,
    SMEDLEY_INTEREST_POOL_PRECONDITION_FAILED = 4, SMEDLEY_INTEREST_POOL_PARTIAL_MUTATION = 5 };
typedef uint64_t SmedleyInterestState;
typedef uint64_t SmedleyInterestPop;
typedef struct SmedleyInterestStateSnapshotV1 {
    uint32_t struct_size, version;
    SmedleyInterestState state;
    int32_t state_id;
    uint32_t first_pop, pop_count, province_count, reserved[3];
    int64_t savings_raw, interest_raw;
} SmedleyInterestStateSnapshotV1;
typedef struct SmedleyInterestPopSnapshotV1 {
    uint32_t struct_size, version;
    SmedleyInterestPop pop;
    int64_t savings_raw;
    uint32_t reserved[3];
} SmedleyInterestPopSnapshotV1;
typedef struct SmedleyInterestPayoutV1 {
    uint32_t struct_size, version;
    SmedleyInterestPop pop;
    int64_t amount_raw;
    uint32_t reserved[3];
} SmedleyInterestPayoutV1;
typedef struct SmedleyInterestPayoutResultV1 {
    uint32_t struct_size, version, failed_index, write_count, verified_count, reserved[3];
} SmedleyInterestPayoutResultV1;
typedef struct SmedleyInterestInitializationV1 {
    uint32_t struct_size, version, country_count, state_count, cleared_state_count, flags, reserved[3];
    int64_t discarded_raw;
} SmedleyInterestInitializationV1;

typedef SmedleyInterestPoolResult (SMEDLEY_INTEREST_POOL_CALL *SmedleyCollectInterestPoolsV1Fn)(SmedleyBankInterestAuthority authority, SmedleyInterestStateSnapshotV1 *states, uint32_t state_capacity, uint32_t *state_count, SmedleyInterestPopSnapshotV1 *pops, uint32_t pop_capacity, uint32_t *pop_count, uint32_t *flags);
typedef SmedleyInterestPoolResult (SMEDLEY_INTEREST_POOL_CALL *SmedleyPrepareInterestPayoutsV1Fn)(SmedleyBankInterestAuthority authority, const SmedleyInterestStateSnapshotV1 *states, uint32_t state_count);
typedef SmedleyInterestPoolResult (SMEDLEY_INTEREST_POOL_CALL *SmedleyApplyInterestPayoutV1Fn)(SmedleyBankInterestAuthority authority, const SmedleyInterestStateSnapshotV1 *state, const SmedleyInterestPayoutV1 *payouts, uint32_t payout_count, SmedleyInterestPayoutResultV1 *result);
typedef SmedleyInterestPoolResult (SMEDLEY_INTEREST_POOL_CALL *SmedleyDiscardInterestPoolsV1Fn)(SmedleyBankInterestAuthority authority, SmedleyInterestInitializationV1 *result);
typedef struct SmedleyInterestPoolApiV1 {
    uint32_t struct_size, version, reserved[2];
    SmedleyCollectInterestPoolsV1Fn collect;
    SmedleyPrepareInterestPayoutsV1Fn prepare;
    SmedleyApplyInterestPayoutV1Fn apply;
    SmedleyDiscardInterestPoolsV1Fn discard;
} SmedleyInterestPoolApiV1;
typedef SmedleyInterestPoolResult (SMEDLEY_INTEREST_POOL_CALL *SmedleyGetInterestPoolApiV1Fn)(SmedleyInterestPoolApiV1 *api);
SMEDLEY_INTEREST_POOL_EXPORT SmedleyInterestPoolResult SMEDLEY_INTEREST_POOL_CALL SmedleyGetInterestPoolApiV1(SmedleyInterestPoolApiV1 *api);

#ifdef __cplusplus
}
#endif
#endif
