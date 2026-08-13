#pragma once

#include "telemetry_core.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <smedley/telemetry_registry.hpp>

namespace telemetry_plugin::collectors
{
    struct FamilyStats
    {
        uint64_t polls_due = 0;
        uint64_t collection_attempts = 0;
        uint64_t accepted = 0;
        uint64_t filtered = 0;
        uint64_t dropped = 0;
        uint64_t invalid = 0;
        uint64_t collection_us = 0;
        uint64_t accepted_bytes = 0;
        uint64_t dropped_bytes = 0;
    };

    class CollectorRuntime
    {
    public:
        virtual ~CollectorRuntime() = default;

        virtual const smedley::telemetry::CaptureRule *DueRule(std::string_view family, int32_t date_raw,
                                                               size_t *rule_index) = 0;
        virtual const smedley::telemetry::CaptureRule *FindRule(std::string_view family,
                                                                  size_t *rule_index) const = 0;
        virtual size_t RuleCount() const = 0;
        virtual const smedley::telemetry::CaptureRule &RuleAt(size_t index) const = 0;
        virtual FamilyStats &Stats(size_t rule_index) = 0;
        virtual void Poll(size_t rule_index) = 0;
        virtual void PollOnce(size_t rule_index, int32_t date_raw) = 0;
        virtual void Attempt(size_t rule_index) = 0;
        virtual void Invalid(size_t rule_index) = 0;
        virtual void Invalid(size_t rule_index, uint64_t count) = 0;
        virtual void CollectionTime(size_t rule_index, uint64_t collection_us) = 0;
        virtual void Account(size_t rule_index, smedley::telemetry::PublicationResult result) = 0;
        virtual smedley::telemetry::PublicationResult EmitFamilyState(
            size_t rule_index, const char *event_type, int32_t date_raw,
            const SmedleyTelemetryFieldV1 *entities, uint32_t entity_count,
            const SmedleyTelemetryFieldV1 *payload, uint32_t payload_count, bool initial = false) = 0;
        smedley::telemetry::PublicationResult EmitState(
            const char *event_type, int32_t date_raw, const SmedleyTelemetryFieldV1 *entities,
            uint32_t entity_count, const SmedleyTelemetryFieldV1 *payload, uint32_t payload_count,
            bool reliability_hint = false, bool initial = false)
        {
            (void)reliability_hint;
            const auto *family = smedley::telemetry::FindMetricFamilyForEvent(event_type);
            size_t rule_index = 0;
            if (family == nullptr || FindRule(family->id, &rule_index) == nullptr) {
                return {SMEDLEY_TELEMETRY_INVALID, 0};
            }
            return EmitFamilyState(rule_index, event_type, date_raw, entities, entity_count, payload,
                                   payload_count, initial);
        }
    };
}
