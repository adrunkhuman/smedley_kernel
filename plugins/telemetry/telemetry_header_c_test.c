#include <smedley/telemetry.h>

static SmedleyTelemetryEmitV1Fn sink;
typedef char smedley_telemetry_result_is_u32[(sizeof(SmedleyTelemetryResult) == sizeof(uint32_t)) ? 1 : -1];
typedef char smedley_telemetry_scalar_is_u32[(sizeof(SmedleyTelemetryScalarType) == sizeof(uint32_t)) ? 1 : -1];
#define SMEDLEY_ASSERT_LAYOUT(type, member, offset) typedef char type##_##member##_offset[(offsetof(type, member) == offset) ? 1 : -1]
typedef char smedley_utf8_size[(sizeof(SmedleyTelemetryUtf8V1) == 12) ? 1 : -1];
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryUtf8V1, data, 0);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryUtf8V1, length, 4);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryUtf8V1, reserved, 8);
typedef char smedley_scalar_union_size[(sizeof(SmedleyTelemetryScalarValueV1) == 16) ? 1 : -1];
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryScalarValueV1, bool_value, 0);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryScalarValueV1, int64_value, 0);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryScalarValueV1, double_value, 0);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryScalarValueV1, string_value, 0);
typedef char smedley_field_size[(sizeof(SmedleyTelemetryFieldV1) == 40) ? 1 : -1];
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryFieldV1, struct_size, 0);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryFieldV1, version, 4);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryFieldV1, key, 8);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryFieldV1, key_length, 12);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryFieldV1, type, 16);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryFieldV1, reserved, 20);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryFieldV1, value, 24);
typedef char smedley_record_size[(sizeof(SmedleyTelemetryRecordV1) == 88) ? 1 : -1];
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, struct_size, 0);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, version, 4);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, flags, 8);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, reserved, 12);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, event_type, 16);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, event_type_length, 20);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, category, 24);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, category_length, 28);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, mapping_id, 32);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, mapping_id_length, 36);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, quality, 40);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, quality_length, 44);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, game_date_raw, 48);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, reserved_date, 52);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, entity_fields, 56);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, entity_field_count, 60);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, payload_fields, 64);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, payload_field_count, 68);
SMEDLEY_ASSERT_LAYOUT(SmedleyTelemetryRecordV1, reserved_tail, 72);
typedef char smedley_record_reserved_tail_size[(sizeof(((SmedleyTelemetryRecordV1 *)0)->reserved_tail) == 16) ? 1 : -1];

int smedley_telemetry_header_c_test(void)
{
    SmedleyTelemetryRecordV1 record = {0};
    record.struct_size = sizeof(record);
    record.version = SMEDLEY_TELEMETRY_ABI_VERSION_V1;
    return sink == 0 ? (int)record.version : (int)sink(&record);
}
