#define SMEDLEY_PLUGIN_BUILD
#include <smedley/event_api.h>
#include <smedley/plugin_abi.h>

#include <windows.h>

typedef struct FixtureState {
    uint32_t created;
    uint32_t loaded;
    SmedleyEventApiV1 event_api;
    SmedleyEventRegistration daily_registration;
    uint32_t daily_callbacks;
} FixtureState;

static SmedleyEventCallbackResult SMEDLEY_EVENT_CALL on_daily(
    void *context, const SmedleyDailyEventV1 *event)
{
    FixtureState *state = (FixtureState *)context;
    if (state == 0 || event == 0 || event->struct_size != sizeof(SmedleyDailyEventV1)
        || event->version != SMEDLEY_DAILY_EVENT_VERSION_V1) {
        return SMEDLEY_EVENT_CALLBACK_DISABLE;
    }
    state->daily_callbacks++;
    return SMEDLEY_EVENT_CALLBACK_CONTINUE;
}

static SmedleyPluginResult SMEDLEY_PLUGIN_CALL create_fixture(void *instance, uint32_t size)
{
    FixtureState *state = (FixtureState *)instance;
    if (state == 0 || size != sizeof(FixtureState)) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
    state->created = 1;
    return SMEDLEY_PLUGIN_SUCCESS;
}

static SmedleyPluginResult SMEDLEY_PLUGIN_CALL load_fixture(void *instance)
{
    FixtureState *state = (FixtureState *)instance;
    HMODULE kernel;
    SmedleyGetEventApiV1Fn get_event_api;
    if (state == 0 || state->created != 1) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
    kernel = GetModuleHandleW(L"smedley_kernel.dll");
    if (kernel == 0) return SMEDLEY_PLUGIN_FAILURE;
    get_event_api = (SmedleyGetEventApiV1Fn)GetProcAddress(kernel, SMEDLEY_EVENT_GET_API_V1_SYMBOL);
    if (get_event_api == 0) return SMEDLEY_PLUGIN_FAILURE;
    state->event_api.struct_size = sizeof(SmedleyEventApiV1);
    state->event_api.version = SMEDLEY_EVENT_API_VERSION_V1;
    if (get_event_api(&state->event_api) != SMEDLEY_EVENT_SUCCESS) return SMEDLEY_PLUGIN_FAILURE;
    if (state->event_api.register_daily(&on_daily, state, &state->daily_registration) != SMEDLEY_EVENT_SUCCESS) {
        return SMEDLEY_PLUGIN_FAILURE;
    }
    state->loaded = 1;
    return SMEDLEY_PLUGIN_SUCCESS;
}

static SmedleyPluginResult SMEDLEY_PLUGIN_CALL unload_fixture(void *instance)
{
    FixtureState *state = (FixtureState *)instance;
    if (state == 0 || state->loaded != 1) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
    if (state->event_api.unregister(state->daily_registration) != SMEDLEY_EVENT_SUCCESS) {
        return SMEDLEY_PLUGIN_FAILURE;
    }
    state->daily_registration = 0;
    state->loaded = 0;
    return SMEDLEY_PLUGIN_SUCCESS;
}

static void SMEDLEY_PLUGIN_CALL destroy_fixture(void *instance)
{
    FixtureState *state = (FixtureState *)instance;
    if (state != 0) state->created = 0;
}

SMEDLEY_PLUGIN_EXPORT SmedleyPluginResult SMEDLEY_PLUGIN_CALL
SmedleyPluginGetApiV1(SmedleyPluginApiV1 *api)
{
    if (api == 0 || api->struct_size != sizeof(SmedleyPluginApiV1)
        || api->version != SMEDLEY_PLUGIN_ABI_VERSION_V1) {
        return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
    }
    api->instance_size = sizeof(FixtureState);
    api->instance_alignment = 4;
    api->reserved[0] = 0;
    api->reserved[1] = 0;
    api->reserved[2] = 0;
    api->reserved[3] = 0;
    api->create = &create_fixture;
    api->load = &load_fixture;
    api->unload = &unload_fixture;
    api->destroy = &destroy_fixture;
    return SMEDLEY_PLUGIN_SUCCESS;
}
