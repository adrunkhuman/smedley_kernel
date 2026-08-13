#include <gtest/gtest.h>
#include <smedley/plugin_abi_runtime.hpp>

#include <filesystem>
#include <windows.h>

TEST(TelemetryPluginTest, StartsThroughTheProductionLifecycleAllocatorContract)
{
    wchar_t executable[MAX_PATH];
    ASSERT_NE(GetModuleFileNameW(nullptr, executable, MAX_PATH), 0u);
    const auto module_path = std::filesystem::path(executable).parent_path() / L"telemetry.dll";
    HMODULE module = LoadLibraryW(module_path.c_str());
    ASSERT_NE(module, nullptr);

    const auto get_api = reinterpret_cast<SmedleyPluginGetApiV1Fn>(
        GetProcAddress(module, SMEDLEY_PLUGIN_GET_API_V1_SYMBOL));
    ASSERT_NE(get_api, nullptr);
    SmedleyPluginApiV1 api{};
    api.struct_size = sizeof(api);
    api.version = SMEDLEY_PLUGIN_ABI_VERSION_V1;
    ASSERT_EQ(get_api(&api), SMEDLEY_PLUGIN_SUCCESS);

    {
        smedley::PluginAbiV1Instance instance(api);
        std::string error;
        ASSERT_TRUE(instance.Start(&error)) << error;
        std::vector<std::string> errors;
        instance.Stop(&errors);
        EXPECT_TRUE(errors.empty());
    }

    EXPECT_TRUE(FreeLibrary(module));
}
