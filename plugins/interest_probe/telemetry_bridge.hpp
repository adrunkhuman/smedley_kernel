#pragma once

#include <smedley/telemetry.h>

#include <cstdint>

namespace interest_probe
{
    SmedleyTelemetryFieldV1 TelemetryIntField(const char *key, int64_t value);
    SmedleyTelemetryFieldV1 TelemetryBoolField(const char *key, bool value);
    SmedleyTelemetryFieldV1 TelemetryStringField(const char *key, const char *value);

    class TelemetryBridge
    {
    public:
        SmedleyTelemetryResult Emit(const char *event_type, const char *quality, int32_t date_raw,
                                    const SmedleyTelemetryFieldV1 *entities, uint32_t entity_count,
                                    const SmedleyTelemetryFieldV1 *payload, uint32_t payload_count);

    private:
        SmedleyTelemetryEmitV1Fn Resolve();

        SmedleyTelemetryEmitV1Fn emit_ = nullptr;
        bool resolution_attempted_ = false;
    };
}
