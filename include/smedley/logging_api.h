#ifndef SMEDLEY_LOGGING_API_H
#define SMEDLEY_LOGGING_API_H

#include <stdint.h>

#ifdef _WIN32
#define SMEDLEY_LOGGING_CALL __cdecl
#ifdef SMEDLEY_LOGGING_BUILD
#define SMEDLEY_LOGGING_EXPORT __declspec(dllexport)
#else
#define SMEDLEY_LOGGING_EXPORT
#endif
#else
#define SMEDLEY_LOGGING_CALL
#define SMEDLEY_LOGGING_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SMEDLEY_LOGGING_API_VERSION_V1 UINT32_C(1)
#define SMEDLEY_LOGGING_GET_API_V1_SYMBOL "SmedleyGetLoggingApiV1"
#define SMEDLEY_LOGGING_MAX_COMPONENT_BYTES UINT32_C(64)
#define SMEDLEY_LOGGING_MAX_MESSAGE_BYTES UINT32_C(4096)

typedef uint32_t SmedleyLoggingResult;
enum {
    SMEDLEY_LOGGING_SUCCESS = 0,
    SMEDLEY_LOGGING_INVALID_ARGUMENT = 1,
    SMEDLEY_LOGGING_UNAVAILABLE = 2,
    SMEDLEY_LOGGING_WRITE_FAILED = 3
};

typedef uint32_t SmedleyLogLevel;
enum {
    SMEDLEY_LOG_DEBUG = 0,
    SMEDLEY_LOG_INFO = 1,
    SMEDLEY_LOG_WARN = 2,
    SMEDLEY_LOG_FAILURE = 3,
    SMEDLEY_LOG_CRITICAL = 4
};

/* Component and message are explicit UTF-8 byte slices. V1 does not validate
 * encoding, and embedded NUL bytes are preserved. */
typedef SmedleyLoggingResult (SMEDLEY_LOGGING_CALL *SmedleyWriteLogV1Fn)(
    SmedleyLogLevel level, const char *component_utf8, uint32_t component_bytes,
    const char *message_utf8, uint32_t message_bytes);

typedef struct SmedleyLoggingApiV1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t reserved[2];
    SmedleyWriteLogV1Fn write;
} SmedleyLoggingApiV1;

typedef SmedleyLoggingResult (SMEDLEY_LOGGING_CALL *SmedleyGetLoggingApiV1Fn)(SmedleyLoggingApiV1 *api);

SMEDLEY_LOGGING_EXPORT SmedleyLoggingResult SMEDLEY_LOGGING_CALL
SmedleyGetLoggingApiV1(SmedleyLoggingApiV1 *api);

#ifdef __cplusplus
}
#endif

#endif
