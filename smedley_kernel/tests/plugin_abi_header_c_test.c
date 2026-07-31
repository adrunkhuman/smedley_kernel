#include <smedley/plugin_abi.h>

static SmedleyPluginResult SMEDLEY_PLUGIN_CALL create_instance(void *instance, uint32_t size)
{
    return instance != 0 && size != 0 ? SMEDLEY_PLUGIN_SUCCESS : SMEDLEY_PLUGIN_INVALID_ARGUMENT;
}

static SmedleyPluginResult SMEDLEY_PLUGIN_CALL load_instance(void *instance)
{
    return instance != 0 ? SMEDLEY_PLUGIN_SUCCESS : SMEDLEY_PLUGIN_INVALID_ARGUMENT;
}

static void SMEDLEY_PLUGIN_CALL destroy_instance(void *instance)
{
    (void)instance;
}

void compile_plugin_abi_header_as_c(void)
{
    SmedleyPluginApiV1 api = {0};
    api.create = create_instance;
    api.load = load_instance;
    api.unload = load_instance;
    api.destroy = destroy_instance;
}
