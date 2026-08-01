#pragma once


#include "event.hpp"
#include "apimacros.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace smedley
{

    class Plugin;

    enum class EventHandlerPriority : uint32_t
    {
        LOWEST = 0,
        LOW = 1 << 10,
        NORMAL = 1 << 15,
        HIGH = 1 << 20,
        HIGHEST = (std::numeric_limits<uint32_t>::max)(),
    };

    /**
     * Stores all listeners for a specific event type.
     */
    template <class Ev>
    class EventRegistry
    {
    protected:
        struct Handler
        {
            Plugin *key_plugin;
            std::string key_str;
            std::function<void(Ev &)> fn;
            EventHandlerPriority priority;
        };
    public:
        using Handlers = std::vector<Handler>;
    protected:
        inline static struct
        {
            inline bool operator()(Handler a, Handler b) const { return a.priority < b.priority; }
        } _handler_compare_lt{};
        SMEDLEY_API static uint32_t _notification_depth;

        // Defined for each event by template specialization.
        SMEDLEY_API static Handlers _handlers;
    public:
        /**
         * Adds an event handler to the registry.
         * 
         * @param plugin Plugin that registers the handler.
         * @param id Handler ID, namespaced within the plugin.
         * @param fn Event callback.
         * @param priority Handler callback priority.
         */
        static void Register(Plugin *plugin, const std::string &id, std::function<void(Ev &)> fn, EventHandlerPriority priority = EventHandlerPriority::LOWEST)
        {
            if (_notification_depth != 0) throw std::logic_error("cannot register an event handler during notification");
            auto handler_exists = [&plugin, &id](const Handler &h) { return h.key_plugin == plugin && h.key_str == id; };
            if (std::find_if(_handlers.begin(), _handlers.end(), handler_exists) == _handlers.end()) {
                Handler eh{plugin, id, fn, priority};
                _handlers.push_back(eh);
                std::sort(_handlers.begin(), _handlers.end(), _handler_compare_lt);
            }
        }

        /**
         * Removes an event handler from the registry.
         * 
         * @param plugin Plugin that registered the handler.
         * @param id Handler ID.
         */
        static void Unregister(Plugin *plugin, const std::string &id)
        {
            if (_notification_depth != 0) throw std::logic_error("cannot unregister an event handler during notification");
            auto handler_exists = [plugin, &id](const Handler &h) { return h.key_plugin == plugin && h.key_str == id; };
            auto iter = std::find_if(_handlers.begin(), _handlers.end(), handler_exists);
            if (iter != _handlers.end()) {
                _handlers.erase(iter);
            }
        }

        /**
         * Notifies handlers until the event is canceled.
         */
        static void Notify(Ev &e)
        {
            ++_notification_depth;
            try {
                for (auto iter = _handlers.begin(); iter != _handlers.end() && !e.is_cancelled(); iter++) {
                    (*iter).fn(e);
                }
            } catch (...) {
                --_notification_depth;
                throw;
            }
            --_notification_depth;
        }

        static uint32_t NotifyContained(Ev &e) noexcept
        {
            uint32_t failures = 0;
            ++_notification_depth;
            for (auto iter = _handlers.begin(); iter != _handlers.end() && !e.is_cancelled(); iter++) {
                try {
                    (*iter).fn(e);
                } catch (...) {
                    ++failures;
                }
            }
            --_notification_depth;
            return failures;
        }
    };

}
