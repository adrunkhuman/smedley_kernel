#define SMEDLEY_PLUGIN_BUILD
#include <smedley/plugin_abi.h>

typedef struct FixtureState {
    uint32_t created;
    uint32_t loaded;
} FixtureState;

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
    if (state == 0 || state->created != 1) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
    state->loaded = 1;
    return SMEDLEY_PLUGIN_SUCCESS;
}

static SmedleyPluginResult SMEDLEY_PLUGIN_CALL unload_fixture(void *instance)
{
    FixtureState *state = (FixtureState *)instance;
    if (state == 0 || state->loaded != 1) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
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
