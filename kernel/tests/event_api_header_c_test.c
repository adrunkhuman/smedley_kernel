#include <smedley/event_api.h>

typedef char assert_daily_event_v1_size[sizeof(SmedleyDailyEventV1) == 56 ? 1 : -1];
typedef char assert_event_api_v1_size[sizeof(SmedleyEventApiV1) == 32 ? 1 : -1];

static SmedleyEventCallbackResult SMEDLEY_EVENT_CALL daily_callback(
    void *context, const SmedleyDailyEventV1 *event)
{
    return context != 0 && event != 0 ? SMEDLEY_EVENT_CALLBACK_CONTINUE : SMEDLEY_EVENT_CALLBACK_DISABLE;
}

void compile_event_api_header_as_c(void)
{
    SmedleyEventApiV1 api = {0};
    SmedleyDailyEventCallbackV1Fn callback = daily_callback;
    api.struct_size = sizeof(api);
    (void)callback;
}
