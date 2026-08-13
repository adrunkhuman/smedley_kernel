#include <smedley/logging_api.h>

#include "log.hpp"

#include <memory>
#include <mutex>

namespace smedley
{
    namespace
    {
        std::mutex service_log_mutex;
        std::unique_ptr<FileLogger> service_logger;

        SmedleyLoggingResult SMEDLEY_LOGGING_CALL WriteLog(SmedleyLogLevel level,
            const char *component, uint32_t component_bytes, const char *message, uint32_t message_bytes)
        {
            if (level > SMEDLEY_LOG_CRITICAL || component == nullptr || message == nullptr
                || component_bytes == 0 || component_bytes > SMEDLEY_LOGGING_MAX_COMPONENT_BYTES
                || message_bytes == 0 || message_bytes > SMEDLEY_LOGGING_MAX_MESSAGE_BYTES) {
                return SMEDLEY_LOGGING_INVALID_ARGUMENT;
            }
            std::lock_guard lock(service_log_mutex);
            if (service_logger == nullptr) return SMEDLEY_LOGGING_UNAVAILABLE;
            try {
                service_logger->SetPrefix(std::string(component, component_bytes));
                service_logger->Log(static_cast<Logger::Level>(level), std::string(message, message_bytes));
                return service_logger->Good() ? SMEDLEY_LOGGING_SUCCESS : SMEDLEY_LOGGING_WRITE_FAILED;
            } catch (...) {
                return SMEDLEY_LOGGING_WRITE_FAILED;
            }
        }
    }

    void ConfigureServiceLogPath(const std::string &path)
    {
        std::lock_guard lock(service_log_mutex);
        service_logger = path.empty() ? nullptr : std::make_unique<FileLogger>(path, "", true);
    }
}

SMEDLEY_LOGGING_EXPORT SmedleyLoggingResult SMEDLEY_LOGGING_CALL SmedleyGetLoggingApiV1(SmedleyLoggingApiV1 *api)
{
    if (api == nullptr || api->struct_size != sizeof(SmedleyLoggingApiV1)
        || api->version != SMEDLEY_LOGGING_API_VERSION_V1 || api->reserved[0] != 0 || api->reserved[1] != 0) {
        return SMEDLEY_LOGGING_INVALID_ARGUMENT;
    }
    api->write = &smedley::WriteLog;
    return SMEDLEY_LOGGING_SUCCESS;
}

static_assert(sizeof(SmedleyLoggingApiV1) == 20, "logging API v1 layout changed");
