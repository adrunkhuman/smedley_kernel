#include <smedley/logging_api.h>

typedef char assert_logging_api_v1_size[sizeof(SmedleyLoggingApiV1) == 20 ? 1 : -1];

void compile_logging_api_header_as_c(void)
{
    SmedleyLoggingApiV1 api = {0};
    api.struct_size = sizeof(api);
    api.version = SMEDLEY_LOGGING_API_VERSION_V1;
}
