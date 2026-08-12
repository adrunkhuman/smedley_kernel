#include <smedley/logging_api.h>

#include "log.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    SmedleyLoggingApiV1 Api()
    {
        SmedleyLoggingApiV1 api{};
        api.struct_size = sizeof(api);
        api.version = SMEDLEY_LOGGING_API_VERSION_V1;
        EXPECT_EQ(SmedleyGetLoggingApiV1(&api), SMEDLEY_LOGGING_SUCCESS);
        return api;
    }
}

TEST(LoggingApiV1Test, ValidatesDiscoveryAndWriteArguments)
{
    EXPECT_EQ(SmedleyGetLoggingApiV1(nullptr), SMEDLEY_LOGGING_INVALID_ARGUMENT);

    SmedleyLoggingApiV1 malformed{};
    malformed.struct_size = sizeof(malformed);
    malformed.version = SMEDLEY_LOGGING_API_VERSION_V1;
    malformed.reserved[0] = 1;
    EXPECT_EQ(SmedleyGetLoggingApiV1(&malformed), SMEDLEY_LOGGING_INVALID_ARGUMENT);

    auto api = Api();
    smedley::ConfigureServiceLogPath("");
    EXPECT_EQ(api.write(SMEDLEY_LOG_INFO, "test", 4, "message", 7), SMEDLEY_LOGGING_UNAVAILABLE);
    EXPECT_EQ(api.write(SMEDLEY_LOG_CRITICAL + 1, "test", 4, "message", 7),
        SMEDLEY_LOGGING_INVALID_ARGUMENT);
    EXPECT_EQ(api.write(SMEDLEY_LOG_INFO, nullptr, 4, "message", 7), SMEDLEY_LOGGING_INVALID_ARGUMENT);
    EXPECT_EQ(api.write(SMEDLEY_LOG_INFO, "test", 0, "message", 7), SMEDLEY_LOGGING_INVALID_ARGUMENT);
    EXPECT_EQ(api.write(SMEDLEY_LOG_INFO, "test", SMEDLEY_LOGGING_MAX_COMPONENT_BYTES + 1, "message", 7),
        SMEDLEY_LOGGING_INVALID_ARGUMENT);
    EXPECT_EQ(api.write(SMEDLEY_LOG_INFO, "test", 4, nullptr, 7), SMEDLEY_LOGGING_INVALID_ARGUMENT);
    EXPECT_EQ(api.write(SMEDLEY_LOG_INFO, "test", 4, "message", 0), SMEDLEY_LOGGING_INVALID_ARGUMENT);
    EXPECT_EQ(api.write(SMEDLEY_LOG_INFO, "test", 4, "message", SMEDLEY_LOGGING_MAX_MESSAGE_BYTES + 1),
        SMEDLEY_LOGGING_INVALID_ARGUMENT);

    smedley::ConfigureServiceLogPath("?:\\invalid\\smedley.log");
    EXPECT_EQ(api.write(SMEDLEY_LOG_INFO, "test", 4, "message", 7), SMEDLEY_LOGGING_WRITE_FAILED);
}

TEST(LoggingApiV1Test, WritesExactByteSlices)
{
    const auto path = std::filesystem::temp_directory_path() / "smedley_logging_api_test.log";
    std::filesystem::remove(path);
    smedley::ConfigureServiceLogPath(path.string());
    auto api = Api();

    ASSERT_EQ(api.write(SMEDLEY_LOG_WARN, "component-ignored", 9, "message-ignored", 7),
        SMEDLEY_LOGGING_SUCCESS);

    std::ifstream input(path);
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    EXPECT_NE(contents.find("[WARN] [component] message"), std::string::npos);
    EXPECT_EQ(contents.find("ignored"), std::string::npos);
    input.close();
    smedley::ConfigureServiceLogPath("");
    std::filesystem::remove(path);
}
