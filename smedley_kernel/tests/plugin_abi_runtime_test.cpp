#include "plugin_abi_runtime.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <filesystem>
#include <new>
#include <stdexcept>

namespace
{
    int create_count = 0;
    int load_count = 0;
    int unload_count = 0;
    int destroy_count = 0;

    struct State
    {
        int create_count = 0;
        int load_count = 0;
        int unload_count = 0;
        int destroy_count = 0;
    };

    SmedleyPluginResult SMEDLEY_PLUGIN_CALL Create(void *storage, uint32_t size)
    {
        if (size != sizeof(State)) return SMEDLEY_PLUGIN_INVALID_ARGUMENT;
        auto *state = new (storage) State;
        state->create_count++;
        ++create_count;
        return SMEDLEY_PLUGIN_SUCCESS;
    }

    SmedleyPluginResult SMEDLEY_PLUGIN_CALL Load(void *storage)
    {
        static_cast<State *>(storage)->load_count++;
        ++load_count;
        return SMEDLEY_PLUGIN_SUCCESS;
    }

    SmedleyPluginResult SMEDLEY_PLUGIN_CALL Unload(void *storage)
    {
        static_cast<State *>(storage)->unload_count++;
        ++unload_count;
        return SMEDLEY_PLUGIN_SUCCESS;
    }

    void SMEDLEY_PLUGIN_CALL Destroy(void *storage)
    {
        auto *state = static_cast<State *>(storage);
        state->destroy_count++;
        ++destroy_count;
        state->~State();
    }

    SmedleyPluginApiV1 Api()
    {
        return {sizeof(SmedleyPluginApiV1), SMEDLEY_PLUGIN_ABI_VERSION_V1, sizeof(State), alignof(State), {0, 0, 0, 0},
                &Create, &Load, &Unload, &Destroy};
    }

    int load_fail_unloads = 0;
    int load_fail_destroys = 0;
    bool fail_unload = false;

    SmedleyPluginResult SMEDLEY_PLUGIN_CALL FailLoad(void *)
    {
        return SMEDLEY_PLUGIN_FAILURE;
    }

    SmedleyPluginResult SMEDLEY_PLUGIN_CALL CountUnload(void *)
    {
        ++load_fail_unloads;
        return fail_unload ? SMEDLEY_PLUGIN_FAILURE : SMEDLEY_PLUGIN_SUCCESS;
    }

    void SMEDLEY_PLUGIN_CALL CountDestroy(void *storage)
    {
        ++load_fail_destroys;
        static_cast<State *>(storage)->~State();
    }
}

TEST(PluginAbiV1Test, StartsAndStopsCompleteLifecycle)
{
    create_count = load_count = unload_count = destroy_count = 0;
    auto api = Api();
    std::string error;
    smedley::PluginAbiV1Instance instance(api);
    ASSERT_TRUE(instance.Start(&error)) << error;
    std::vector<std::string> errors;
    instance.Stop(&errors);
    EXPECT_TRUE(errors.empty());
    EXPECT_EQ(create_count, 1);
    EXPECT_EQ(load_count, 1);
    EXPECT_EQ(unload_count, 1);
    EXPECT_EQ(destroy_count, 1);
}

TEST(PluginAbiV1Test, RollsBackFailedLoad)
{
    load_fail_unloads = 0;
    load_fail_destroys = 0;
    fail_unload = false;
    auto api = Api();
    api.load = &FailLoad;
    api.unload = &CountUnload;
    api.destroy = &CountDestroy;
    std::string error;
    smedley::PluginAbiV1Instance instance(api);
    EXPECT_FALSE(instance.Start(&error));
    EXPECT_NE(error.find("load callback failed"), std::string::npos);
    EXPECT_EQ(load_fail_unloads, 1);
    EXPECT_EQ(load_fail_destroys, 1);
}

TEST(PluginAbiV1Test, PreservesRollbackFailureAfterLoadFailure)
{
    load_fail_unloads = 0;
    load_fail_destroys = 0;
    fail_unload = true;
    auto api = Api();
    api.load = &FailLoad;
    api.unload = &CountUnload;
    api.destroy = &CountDestroy;
    std::string error;
    smedley::PluginAbiV1Instance instance(api);
    EXPECT_FALSE(instance.Start(&error));
    EXPECT_NE(error.find("load callback failed"), std::string::npos);
    EXPECT_NE(error.find("rollback: plugin ABI v1 unload callback failed"), std::string::npos);
    EXPECT_EQ(load_fail_unloads, 1);
    EXPECT_EQ(load_fail_destroys, 1);
    fail_unload = false;
}

TEST(PluginAbiV1Test, DestroysAfterUnloadFailureAndReportsIt)
{
    load_fail_unloads = 0;
    load_fail_destroys = 0;
    fail_unload = true;
    auto api = Api();
    api.unload = &CountUnload;
    api.destroy = &CountDestroy;
    std::string error;
    smedley::PluginAbiV1Instance instance(api);
    ASSERT_TRUE(instance.Start(&error)) << error;
    std::vector<std::string> errors;
    instance.Stop(&errors);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors.front().find("unload callback failed"), std::string::npos);
    EXPECT_EQ(load_fail_unloads, 1);
    EXPECT_EQ(load_fail_destroys, 1);
    fail_unload = false;
}

TEST(PluginAbiV1Test, RejectsMalformedStructures)
{
    auto api = Api();
    std::string error;
    api.reserved[2] = 1;
    EXPECT_FALSE(smedley::ValidatePluginApiV1(api, &error));
    api.reserved[2] = 0;
    api.instance_alignment = 3;
    EXPECT_FALSE(smedley::ValidatePluginApiV1(api, &error));
    api.instance_alignment = alignof(State);
    api.destroy = nullptr;
    EXPECT_FALSE(smedley::ValidatePluginApiV1(api, &error));
}

TEST(PluginAbiV1Test, ContainsExceptionsAcrossTheCBoundary)
{
    auto api = Api();
    api.load = [](void *) -> SmedleyPluginResult { throw std::runtime_error("failure"); };
    std::string error;
    smedley::PluginAbiV1Instance instance(api);
    EXPECT_FALSE(instance.Start(&error));
    EXPECT_NE(error.find("threw an exception"), std::string::npos);
}

TEST(PluginAbiV1Test, DiscoversAndRunsTheIndependentCDll)
{
    wchar_t executable[MAX_PATH];
    ASSERT_NE(GetModuleFileNameW(nullptr, executable, MAX_PATH), 0u);
    const auto fixture = std::filesystem::path(executable).parent_path() / L"smedley_plugin_abi_fixture.dll";
    HMODULE module = LoadLibraryW(fixture.c_str());
    ASSERT_NE(module, nullptr);
    const auto get_api = reinterpret_cast<SmedleyPluginGetApiV1Fn>(
        GetProcAddress(module, SMEDLEY_PLUGIN_GET_API_V1_SYMBOL));
    ASSERT_NE(get_api, nullptr);
    SmedleyPluginApiV1 api{};
    api.struct_size = sizeof(api);
    api.version = SMEDLEY_PLUGIN_ABI_VERSION_V1;
    ASSERT_EQ(get_api(&api), SMEDLEY_PLUGIN_SUCCESS);
    {
        std::string error;
        smedley::PluginAbiV1Instance instance(api);
        ASSERT_TRUE(instance.Start(&error)) << error;
        instance.Stop();
    }
    EXPECT_TRUE(FreeLibrary(module));
}
