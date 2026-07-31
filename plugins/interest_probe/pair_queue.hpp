#pragma once

#include "probe_core.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace interest_probe
{
    struct SamplePair
    {
        Sample before;
        Sample after;
    };

    template <size_t Capacity>
    class PairQueue
    {
        static_assert(Capacity > 1);

    public:
        bool TryPush(const SamplePair &pair) noexcept
        {
            const uint32_t write = write_.load(std::memory_order_relaxed);
            const uint32_t next = (write + 1) % Capacity;
            if (next == read_.load(std::memory_order_acquire)) return false;
            pairs_[write] = pair;
            write_.store(next, std::memory_order_release);
            return true;
        }

        bool TryPop(SamplePair *pair) noexcept
        {
            const uint32_t read = read_.load(std::memory_order_relaxed);
            if (read == write_.load(std::memory_order_acquire)) return false;
            *pair = pairs_[read];
            read_.store((read + 1) % Capacity, std::memory_order_release);
            return true;
        }

        bool Empty() const noexcept
        {
            return read_.load(std::memory_order_relaxed) == write_.load(std::memory_order_acquire);
        }

    private:
        std::array<SamplePair, Capacity> pairs_{};
        std::atomic<uint32_t> write_{0};
        std::atomic<uint32_t> read_{0};
    };
}
