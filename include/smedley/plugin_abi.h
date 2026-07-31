#ifndef SMEDLEY_PLUGIN_ABI_H
#define SMEDLEY_PLUGIN_ABI_H

#include <stdint.h>

#ifdef _WIN32
#define SMEDLEY_PLUGIN_CALL __cdecl
#ifdef SMEDLEY_PLUGIN_BUILD
#define SMEDLEY_PLUGIN_EXPORT __declspec(dllexport)
#else
#define SMEDLEY_PLUGIN_EXPORT
#endif
#else
#define SMEDLEY_PLUGIN_CALL
#define SMEDLEY_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SMEDLEY_PLUGIN_ABI_VERSION_V1 UINT32_C(1)
#define SMEDLEY_PLUGIN_GET_API_V1_SYMBOL "SmedleyPluginGetApiV1"
#define SMEDLEY_PLUGIN_MAX_INSTANCE_BYTES UINT32_C(1048576)
#define SMEDLEY_PLUGIN_MAX_INSTANCE_ALIGNMENT UINT32_C(4096)

typedef uint32_t SmedleyPluginResult;
enum {
    SMEDLEY_PLUGIN_SUCCESS = 0,
    SMEDLEY_PLUGIN_INVALID_ARGUMENT = 1,
    SMEDLEY_PLUGIN_FAILURE = 2
};

/* Instance storage is allocated, zeroed, and freed by the host. The plugin may
 * construct state there, but must release every plugin-owned resource before
 * destroy returns. No callback may retain the API structure pointer. */
typedef SmedleyPluginResult (SMEDLEY_PLUGIN_CALL *SmedleyPluginCreateV1Fn)(
    void *instance, uint32_t instance_size);
typedef SmedleyPluginResult (SMEDLEY_PLUGIN_CALL *SmedleyPluginLoadV1Fn)(void *instance);
typedef SmedleyPluginResult (SMEDLEY_PLUGIN_CALL *SmedleyPluginUnloadV1Fn)(void *instance);
typedef void (SMEDLEY_PLUGIN_CALL *SmedleyPluginDestroyV1Fn)(void *instance);

typedef struct SmedleyPluginApiV1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t instance_size;
    uint32_t instance_alignment;
    uint32_t reserved[4];
    SmedleyPluginCreateV1Fn create;
    SmedleyPluginLoadV1Fn load;
    SmedleyPluginUnloadV1Fn unload;
    SmedleyPluginDestroyV1Fn destroy;
} SmedleyPluginApiV1;

/* The host initializes struct_size and version before calling this export.
 * A v1 plugin validates those inputs, fills the remaining fields, and returns
 * an explicit result. Advertising this symbol disables legacy fallback. */
typedef SmedleyPluginResult (SMEDLEY_PLUGIN_CALL *SmedleyPluginGetApiV1Fn)(
    SmedleyPluginApiV1 *api);

SMEDLEY_PLUGIN_EXPORT SmedleyPluginResult SMEDLEY_PLUGIN_CALL
SmedleyPluginGetApiV1(SmedleyPluginApiV1 *api);

#ifdef __cplusplus
}
#endif

#endif
