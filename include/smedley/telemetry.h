#ifndef SMEDLEY_TELEMETRY_H
#define SMEDLEY_TELEMETRY_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define SMEDLEY_TELEMETRY_CALL __cdecl
#ifdef SMEDLEY_TELEMETRY_BUILD
#define SMEDLEY_TELEMETRY_EXPORT __declspec(dllexport)
#else
#define SMEDLEY_TELEMETRY_EXPORT
#endif
#else
#define SMEDLEY_TELEMETRY_CALL
#define SMEDLEY_TELEMETRY_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SMEDLEY_TELEMETRY_ABI_VERSION_V1 UINT32_C(1)
#define SMEDLEY_TELEMETRY_MAX_IDENTIFIER_BYTES UINT32_C(48)
#define SMEDLEY_TELEMETRY_MAX_STRING_BYTES UINT32_C(128)
/* Maximum entity and payload fields combined in one record. */
#define SMEDLEY_TELEMETRY_MAX_FIELDS UINT32_C(8)
#define SMEDLEY_TELEMETRY_RECORD_HAS_GAME_DATE UINT32_C(1)
#define SMEDLEY_TELEMETRY_EMIT_V1_SYMBOL "SmedleyTelemetryEmitV1"
#define SMEDLEY_TELEMETRY_EMIT_RELIABLE_V1_SYMBOL "SmedleyTelemetryEmitReliableV1"
#define SMEDLEY_TELEMETRY_DRAIN_V1_SYMBOL "SmedleyTelemetryDrainV1"

typedef uint32_t SmedleyTelemetryResult;
enum {
    SMEDLEY_TELEMETRY_UNAVAILABLE = 0,
    SMEDLEY_TELEMETRY_FILTERED = 1,
    SMEDLEY_TELEMETRY_ACCEPTED = 2,
    SMEDLEY_TELEMETRY_DROPPED = 3,
    SMEDLEY_TELEMETRY_INVALID = 4
};

typedef uint32_t SmedleyTelemetryDrainResult;
/* Unavailable means no active drain-capable sink; busy rejects a concurrent
 * call immediately. Timeout and failed do not permit a coordinated quit. */
enum {
    SMEDLEY_TELEMETRY_DRAIN_UNAVAILABLE = 0,
    SMEDLEY_TELEMETRY_DRAIN_COMPLETED = 1,
    SMEDLEY_TELEMETRY_DRAIN_BUSY = 2,
    SMEDLEY_TELEMETRY_DRAIN_TIMEOUT = 3,
    SMEDLEY_TELEMETRY_DRAIN_FAILED = 4
};

typedef uint32_t SmedleyTelemetryScalarType;
enum {
    SMEDLEY_TELEMETRY_NULL = 0,
    SMEDLEY_TELEMETRY_BOOL = 1,
    SMEDLEY_TELEMETRY_INT64 = 2,
    SMEDLEY_TELEMETRY_DOUBLE = 3,
    SMEDLEY_TELEMETRY_UTF8_STRING = 4
};

typedef struct SmedleyTelemetryUtf8V1 {
    const char *data;
    uint32_t length;
    uint32_t reserved;
} SmedleyTelemetryUtf8V1;

typedef union SmedleyTelemetryScalarValueV1 {
    uint32_t bool_value;
    int64_t int64_value;
    double double_value;
    SmedleyTelemetryUtf8V1 string_value;
} SmedleyTelemetryScalarValueV1;

typedef struct SmedleyTelemetryFieldV1 {
    uint32_t struct_size;
    uint32_t version;
    const char *key;
    uint32_t key_length;
    uint32_t type;
    uint32_t reserved;
    SmedleyTelemetryScalarValueV1 value;
} SmedleyTelemetryFieldV1;

typedef struct SmedleyTelemetryRecordV1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t flags;
    uint32_t reserved;
    const char *event_type;
    uint32_t event_type_length;
    const char *category;
    uint32_t category_length;
    const char *mapping_id;
    uint32_t mapping_id_length;
    const char *quality;
    uint32_t quality_length;
    int32_t game_date_raw;
    uint32_t reserved_date;
    const SmedleyTelemetryFieldV1 *entity_fields;
    uint32_t entity_field_count;
    const SmedleyTelemetryFieldV1 *payload_fields;
    uint32_t payload_field_count;
    uint32_t reserved_tail[4];
} SmedleyTelemetryRecordV1;

/* The sink copies all pointed-to data before this call returns. It is thread-safe,
 * but callers must not infer or require a particular execution thread. */
typedef SmedleyTelemetryResult (SMEDLEY_TELEMETRY_CALL *SmedleyTelemetryEmitV1Fn)(
    const SmedleyTelemetryRecordV1 *record);
typedef SmedleyTelemetryDrainResult (SMEDLEY_TELEMETRY_CALL *SmedleyTelemetryDrainV1Fn)(uint32_t timeout_ms);

SMEDLEY_TELEMETRY_EXPORT SmedleyTelemetryResult SMEDLEY_TELEMETRY_CALL
SmedleyTelemetryEmitV1(const SmedleyTelemetryRecordV1 *record);

/* Low-frequency lifecycle producers may use this symbol to wait for bounded
 * in-memory sink-lifetime, serialization, and queue locks. It may still report
 * dropped when the queue is full or stopped. Do not use it from hot hooks. */
SMEDLEY_TELEMETRY_EXPORT SmedleyTelemetryResult SMEDLEY_TELEMETRY_CALL
SmedleyTelemetryEmitReliableV1(const SmedleyTelemetryRecordV1 *record);

/* timeout_ms is milliseconds; UINT32_MAX waits indefinitely. One monotonic
 * deadline covers sink ownership, producer quiescence, queue drain, worker
 * join, telemetry.summary, final flush, and close. Once shutdown begins,
 * ingress remains stopped; timeout never detaches the worker. A later call
 * retries coordination or waits for the same in-progress drain. */
SMEDLEY_TELEMETRY_EXPORT SmedleyTelemetryDrainResult SMEDLEY_TELEMETRY_CALL
SmedleyTelemetryDrainV1(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
