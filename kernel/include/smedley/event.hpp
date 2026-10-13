#pragma once

namespace smedley
{

    /**
     * Represents a snapshot of an in-game event or state change. Some events
     * can be canceled.
     */
    class Event
    {
        bool _cancelable;
        bool _is_cancelled;
    public:
        Event(bool cancelable) : _cancelable(cancelable), _is_cancelled(false) {}
        virtual ~Event() {}

        /// @brief Cancels the event if possible.
        void Cancel();
        /// @brief Returns whether the event can be canceled.
        inline bool can_cancel() { return _cancelable; }
        /// @brief Returns whether the event has been canceled.
        inline bool is_cancelled() { return _is_cancelled; }
    };

}
