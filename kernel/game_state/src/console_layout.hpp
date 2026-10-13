#pragma once

#include "engine_string_layout.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace smedley::game_state::console_layout
{
    struct Arguments
    {
        const EngineString *begin;
        const EngineString *end;
        const EngineString *capacity;
        uint32_t allocator;
    };

    static_assert(sizeof(Arguments) == 0x10);

    struct Result
    {
        bool success;
        uint8_t padding[3];
        EngineString message;

        explicit Result(const char *value, bool succeeded = true) : success(succeeded), padding{}, message{}
        {
            // The native console caller owns and releases any transferred heap string.
            AssignTransferredEngineString(&message, value);
        }
    };

    static_assert(sizeof(Result) == 0x20);

    struct Command
    {
        using Handler = Result (*)(const Arguments &arguments);

        bool is_allowed;
        uint8_t padding[3];
        const char *name;
        int32_t alias_count;
        const char *aliases[3];
        const char *description;
        Handler handler;
        int32_t argument_count;
        const char *argument_names[10];
    };

    static_assert(sizeof(Command) == 0x4c);
    static_assert(offsetof(Command, handler) == 0x1c);

    struct CommandVector
    {
        Command **begin;
        Command **end;
        Command **capacity;
        uint32_t allocator;
    };

    static_assert(sizeof(CommandVector) == 0x10);

    class SingleArgument
    {
    public:
        explicit SingleArgument(const char *value)
        {
            if (value == nullptr) return;
            const size_t size = std::strlen(value);
            if (size > 0xf) return;
            argument_ = {};
            std::memcpy(argument_.storage.inline_buffer, value, size + 1);
            argument_.size = static_cast<uint32_t>(size);
            argument_.capacity = 0xf;
            arguments_ = {&argument_, &argument_ + 1, &argument_ + 1, 0};
            valid_ = true;
        }

        const Arguments &arguments() const noexcept { return arguments_; }
        bool valid() const noexcept { return valid_; }

    private:
        EngineString argument_{};
        Arguments arguments_{};
        bool valid_ = false;
    };
}
