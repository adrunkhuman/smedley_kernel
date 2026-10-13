#pragma once

#include <smedley/executable_identity.hpp>
#include <smedley/memory.hpp>

#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <Windows.h>

namespace smedley::game_state
{
    inline bool ResolveSupportedGameAddress(uintptr_t rva, uintptr_t *address, std::string *error) noexcept
    {
        const uintptr_t base = smedley::memory::Map::base_addr;
        if (!smedley::IsCurrentExecutableSupported() || base == 0
            || rva > (std::numeric_limits<uintptr_t>::max)() - base || address == nullptr) {
            if (error != nullptr) *error = "supported game module is unavailable";
            return false;
        }
        *address = base + rva;
        return true;
    }

    inline bool MatchesReadableBytes(uintptr_t address, const void *expected, size_t size) noexcept
    {
        return smedley::memory::MatchesReadableBytes(address, expected, size);
    }

    class ScopedThreadQuiescence
    {
    public:
        explicit ScopedThreadQuiescence(std::string *error) : error_(error)
        {
            if (!inner_ && error_ != nullptr) *error_ = inner_.error();
        }

        ~ScopedThreadQuiescence()
        {
            if (!inner_.Release()) {
                TerminateProcess(GetCurrentProcess(), ERROR_OPERATION_ABORTED);
                std::terminate();
            }
            if (pending_error_ != nullptr && error_ != nullptr) *error_ = pending_error_;
        }

        ScopedThreadQuiescence(const ScopedThreadQuiescence &) = delete;
        ScopedThreadQuiescence &operator=(const ScopedThreadQuiescence &) = delete;

        explicit operator bool() const noexcept { return static_cast<bool>(inner_); }

        bool AnyInstructionPointerIn(uintptr_t address, size_t size, bool *found,
                                     std::string *) noexcept
        {
            if (inner_.AnyInstructionPointerIn(address, size, found)) return true;
            pending_error_ = inner_.error();
            return false;
        }

        bool InstructionPointerCountIn(uintptr_t address, size_t size, size_t *count,
                                       std::string *) noexcept
        {
            if (inner_.InstructionPointerCountIn(address, size, count)) return true;
            pending_error_ = inner_.error();
            return false;
        }

        bool Release(std::string *error)
        {
            if (inner_.Release()) {
                if (pending_error_ != nullptr && error != nullptr) *error = pending_error_;
                return true;
            }
            TerminateProcess(GetCurrentProcess(), ERROR_OPERATION_ABORTED);
            std::terminate();
        }

    private:
        smedley::memory::ScopedThreadQuiescence inner_;
        std::string *error_;
        const char *pending_error_ = nullptr;
    };
}
