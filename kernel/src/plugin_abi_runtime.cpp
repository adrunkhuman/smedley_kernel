#include "plugin_abi_runtime.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <malloc.h>

namespace smedley
{
    namespace
    {
        bool IsPowerOfTwo(uint32_t value)
        {
            return value != 0 && (value & (value - 1)) == 0;
        }

        void AddError(std::vector<std::string> *errors, const char *message) noexcept
        {
            if (errors == nullptr) return;
            try {
                errors->emplace_back(message);
            } catch (...) {
            }
        }

        void AddResultError(std::vector<std::string> *errors, const char *message, SmedleyPluginResult result) noexcept
        {
            if (errors == nullptr) return;
            try {
                errors->push_back(std::string(message) + std::to_string(result));
            } catch (...) {
            }
        }

        void AppendCleanupErrors(std::string *error, const std::vector<std::string> &cleanup_errors)
        {
            for (const auto &cleanup_error : cleanup_errors) {
                *error += "; rollback: " + cleanup_error;
            }
        }
    }

    bool ValidatePluginApiV1(const SmedleyPluginApiV1 &api, std::string *error)
    {
        if (api.struct_size != sizeof(SmedleyPluginApiV1)) {
            *error = "plugin ABI v1 structure size does not match";
            return false;
        }
        if (api.version != SMEDLEY_PLUGIN_ABI_VERSION_V1) {
            *error = "plugin ABI v1 version does not match";
            return false;
        }
        if (api.instance_size == 0 || api.instance_size > SMEDLEY_PLUGIN_MAX_INSTANCE_BYTES) {
            *error = "plugin ABI v1 instance size is outside the supported range";
            return false;
        }
        if (!IsPowerOfTwo(api.instance_alignment)
            || api.instance_alignment > SMEDLEY_PLUGIN_MAX_INSTANCE_ALIGNMENT) {
            *error = "plugin ABI v1 instance alignment is not a supported power of two";
            return false;
        }
        if (std::any_of(std::begin(api.reserved), std::end(api.reserved), [](uint32_t value) { return value != 0; })) {
            *error = "plugin ABI v1 reserved fields must be zero";
            return false;
        }
        if (api.create == nullptr || api.load == nullptr || api.unload == nullptr || api.destroy == nullptr) {
            *error = "plugin ABI v1 lifecycle callbacks must not be null";
            return false;
        }
        return true;
    }

    PluginAbiV1Instance::PluginAbiV1Instance(SmedleyPluginApiV1 api) : api_(api) {}

    PluginAbiV1Instance::~PluginAbiV1Instance()
    {
        Stop();
    }

    bool PluginAbiV1Instance::Start(std::string *error)
    {
        if (started_) return true;
        if (!ValidatePluginApiV1(api_, error)) return false;
        const size_t alignment = (std::max)(static_cast<size_t>(api_.instance_alignment), sizeof(void *));
        storage_ = _aligned_malloc(api_.instance_size, alignment);
        if (storage_ == nullptr) {
            *error = "could not allocate plugin ABI v1 instance storage";
            return false;
        }
        std::memset(storage_, 0, api_.instance_size);
        SmedleyPluginResult result = SMEDLEY_PLUGIN_FAILURE;
        try {
            result = api_.create(storage_, api_.instance_size);
        } catch (...) {
            *error = "plugin ABI v1 create callback threw an exception";
            _aligned_free(storage_);
            storage_ = nullptr;
            return false;
        }
        if (result != SMEDLEY_PLUGIN_SUCCESS) {
            *error = "plugin ABI v1 create callback failed with result " + std::to_string(result);
            _aligned_free(storage_);
            storage_ = nullptr;
            return false;
        }
        created_ = true;
        load_attempted_ = true;
        try {
            result = api_.load(storage_);
        } catch (...) {
            *error = "plugin ABI v1 load callback threw an exception";
            std::vector<std::string> cleanup_errors;
            CleanupAfterLoadAttempt(&cleanup_errors);
            AppendCleanupErrors(error, cleanup_errors);
            return false;
        }
        if (result != SMEDLEY_PLUGIN_SUCCESS) {
            *error = "plugin ABI v1 load callback failed with result " + std::to_string(result);
            std::vector<std::string> cleanup_errors;
            CleanupAfterLoadAttempt(&cleanup_errors);
            AppendCleanupErrors(error, cleanup_errors);
            return false;
        }
        started_ = true;
        return true;
    }

    void PluginAbiV1Instance::CleanupAfterLoadAttempt(std::vector<std::string> *errors) noexcept
    {
        if (load_attempted_) {
            try {
                const auto result = api_.unload(storage_);
                if (result != SMEDLEY_PLUGIN_SUCCESS) {
                    AddResultError(errors, "plugin ABI v1 unload callback failed with result ", result);
                }
            } catch (...) {
                AddError(errors, "plugin ABI v1 unload callback threw an exception");
            }
        }
        if (created_) {
            try {
                api_.destroy(storage_);
            } catch (...) {
                AddError(errors, "plugin ABI v1 destroy callback threw an exception");
            }
        }
        _aligned_free(storage_);
        storage_ = nullptr;
        created_ = false;
        load_attempted_ = false;
        started_ = false;
    }

    void PluginAbiV1Instance::Stop(std::vector<std::string> *errors) noexcept
    {
        if (storage_ == nullptr) return;
        CleanupAfterLoadAttempt(errors);
    }
}
