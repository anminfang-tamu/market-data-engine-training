#pragma once

#include <atomic>
#include <cstddef>

namespace containers
{
    // Single-producer / single-consumer ring buffer with power-of-two capacity.
    template <typename T, size_t Capacity>
    class RingBuffer
    {
        static_assert(Capacity > 1, "Capacity must be greater than 1");
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

    public:
        bool empty() const noexcept;
        bool full() const noexcept;
        void reset() noexcept;

        bool push(const T &data) noexcept;
        bool pop(T &data) noexcept;

    private:
        alignas(64) std::atomic<size_t> head_{0}; // next index to read
        alignas(64) std::atomic<size_t> tail_{0}; // next index to write

        static constexpr size_t mask_ = Capacity - 1;

        alignas(64) T buffer_[Capacity];
    };

    template <typename T, size_t Capacity>
    bool RingBuffer<T, Capacity>::empty() const noexcept
    {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        return h == t;
    }

    template <typename T, size_t Capacity>
    bool RingBuffer<T, Capacity>::full() const noexcept
    {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        return (t - h) == Capacity;
    }

    template <typename T, size_t Capacity>
    void RingBuffer<T, Capacity>::reset() noexcept
    {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    template <typename T, size_t Capacity>
    bool RingBuffer<T, Capacity>::push(const T &data) noexcept
    {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_relaxed);

        if ((t - h) == Capacity)
        {
            return false; // full
        }

        buffer_[t & mask_] = data;
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    template <typename T, size_t Capacity>
    bool RingBuffer<T, Capacity>::pop(T &data) noexcept
    {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t t = tail_.load(std::memory_order_acquire);

        if (h == t)
        {
            return false; // empty
        }

        data = buffer_[h & mask_];
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

} // namespace containers
