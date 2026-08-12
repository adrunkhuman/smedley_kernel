#include <smedley/campaign_control_api.h>

typedef char assert_campaign_snapshot_v1_size[sizeof(SmedleyCampaignSnapshotV1) == 32 ? 1 : -1];
typedef char assert_campaign_control_api_v1_size[sizeof(SmedleyCampaignControlApiV1) == 32 ? 1 : -1];

void compile_campaign_control_api_header_as_c(void)
{
    SmedleyCampaignControlApiV1 api = {0};
    api.struct_size = sizeof(api);
    api.version = SMEDLEY_CAMPAIGN_CONTROL_API_VERSION_V1;
}
