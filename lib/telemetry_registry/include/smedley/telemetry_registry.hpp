#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace smedley::telemetry
{
    enum class CaptureCadence : uint8_t
    {
        FixedDays,
        Daily,
        Weekly,
        Monthly,
        Yearly,
    };

    enum class MetricCostClass : uint8_t { Low, Medium, High };
    enum class MetricCollector : uint8_t { World, Country, Province, Population };
    enum class MetricAdmissionPriority : uint8_t { BestEffort, Important, ReliableTerminal };
    enum class MetricAdmission : uint8_t { BestEffort, Reliable };
    enum class MetricValidationError : uint8_t { None, DailyCadenceRequired, EntityFilterRequired };

    struct MetricEvent
    {
        std::string_view id;
        // Comma-separated required keys; '?' marks optional keys and '-' marks an empty object.
        std::string_view entity_schema;
        std::string_view payload_schema;
    };

    struct MetricRuleSelection
    {
        std::string_view family;
        // Null or empty selects every field. A nonzero count requires a non-null pointer.
        const std::string_view *fields = nullptr;
        size_t field_count = 0;
    };

    struct RuntimePlan
    {
        bool open_observation = false;
        bool install_factory_sales_hook = false;
        bool install_factory_flow_hook = false;
        bool install_artisan_flow_hook = false;
        bool install_pop_cashflow_hook = false;
    };

    struct MetricFamily
    {
        std::string_view id;
        const std::string_view *fields;
        size_t field_count;
        std::string_view event_prefix;
        std::string_view entity;
        std::string_view mapping_id;
        std::string_view quality;
        MetricCostClass cost_class;
        MetricCollector collector;
        MetricAdmissionPriority admission_priority;
        bool supports_country_filter;
        bool supports_province_filter;
        bool requires_entity_filter;
        bool daily_only;
        bool sales_daily_only;
        bool requires_gold_to_cash_rate;
        const MetricEvent *events = nullptr;
        size_t event_count = 0;
    };

    // Registry storage is immutable and remains valid for the process lifetime. count may be null.
    const MetricFamily *MetricFamilies(size_t *count);
    // Lookup results are borrowed process-lifetime pointers, or null when no entry matches.
    const MetricFamily *FindMetricFamily(std::string_view family);
    const MetricFamily *FindMetricFamilyForEvent(std::string_view event);
    const MetricEvent *FindMetricEvent(std::string_view event);
    // Checks required and allowed key membership, not ordering or duplicate keys. Nonzero counts require non-null pointers.
    bool MetricEventMatchesSchema(const MetricEvent &event,
                                  const std::string_view *entity_fields, size_t entity_field_count,
                                  const std::string_view *payload_fields, size_t payload_field_count);
    bool MetricFamilySupportsField(const MetricFamily &family, std::string_view field);
    // Checks cadence and required-filter policy only. Callers validate field names and supported filter kinds separately.
    // fixed_days is used only with FixedDays; a nonzero field_count requires a non-null fields pointer.
    MetricValidationError MetricFamilyValidate(const MetricFamily &family, CaptureCadence cadence, int fixed_days,
                                                const std::string_view *fields, size_t field_count,
                                                size_t country_filter_count, size_t province_filter_count);
    std::string_view MetricValidationErrorMessage(MetricValidationError error);
    bool MetricFamilyEmitsEvent(const MetricFamily &family, std::string_view event);
    // Important families are reliable only for 1-16 combined filters; ReliableTerminal is always reliable.
    MetricAdmission MetricFamilyAdmission(const MetricFamily &family, size_t country_filter_count,
                                           size_t province_filter_count);
    // Unknown families are ignored. A nonzero rule_count requires a non-null rules pointer.
    RuntimePlan BuildRuntimePlan(const MetricRuleSelection *rules, size_t rule_count);
    std::string_view CaptureCadenceNameView(CaptureCadence cadence);
}
