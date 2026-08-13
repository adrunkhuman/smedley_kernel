#include <smedley/event_services_api.h>

typedef char assert_bank_interest_event_v1_size[sizeof(SmedleyBankInterestEventV1) == 48 ? 1 : -1];
typedef char assert_console_input_v1_size[sizeof(SmedleyCampaignConsoleInputV1) == 160 ? 1 : -1];
typedef char assert_console_result_v1_size[sizeof(SmedleyCampaignConsoleResultV1) == 160 ? 1 : -1];
typedef char assert_event_services_api_v1_size[sizeof(SmedleyEventServicesApiV1) == 32 ? 1 : -1];

static SmedleyEventServicesCallbackResult SMEDLEY_EVENT_SERVICES_CALL bank_callback(
    void *context, const SmedleyBankInterestEventV1 *event)
{
    return context != 0 && event != 0 ? SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE
                                      : SMEDLEY_EVENT_SERVICES_CALLBACK_DISABLE;
}

static SmedleyEventServicesCallbackResult SMEDLEY_EVENT_SERVICES_CALL console_callback(
    void *context, const SmedleyCampaignConsoleInputV1 *input, SmedleyCampaignConsoleResultV1 *result)
{
    if (context == 0 || input == 0 || result == 0) return SMEDLEY_EVENT_SERVICES_CALLBACK_DISABLE;
    result->handled = 1;
    return SMEDLEY_EVENT_SERVICES_CALLBACK_CONTINUE;
}

void compile_event_services_api_header_as_c(void)
{
    SmedleyEventServicesApiV1 api = {0};
    SmedleyBankInterestCallbackV1Fn bank = bank_callback;
    SmedleyCampaignConsoleCallbackV1Fn console = console_callback;
    api.struct_size = sizeof(api);
    (void)bank;
    (void)console;
}
