#pragma once

#include <utility>
#include <mutex>
#include <cstddef>
#include <condition_variable>

namespace containers
{
    static constexpr size_t kCapacity = 3000;

    template <typename T>
    class BlockedBoundedQueue
    {
    public:
        bool enqueue(const T &data);
        bool dequeue(T &out);

        bool isFull() const;
        bool isEmpty() const;

        size_t size() const;
        size_t capacity() const;

        void clear();
        void close();
        bool closed() const;

    private:
        mutable std::mutex mtx_;
        std::condition_variable cv_not_full_;
        std::condition_variable cv_not_empty_;

        T buffer[kCapacity];
        size_t head_{0};
        size_t tail_{0};
        size_t size_{0};

        bool closed_{false};
    };

    // tempalte method definitions
    template <typename T>
    bool BlockedBoundedQueue<T>::enqueue(const T &data)
    {
        std::unique_lock<std::mutex> lock(mtx_);

        cv_not_full_.wait(lock, [&]
                          { return size_ < kCapacity || closed_; });

        if (closed_)
        {
            return false;
        }

        buffer[tail_] = data;
        tail_ = (tail_ + 1) % kCapacity;
        ++size_;

        lock.unlock();
        cv_not_empty_.notify_one();

        return true;
    }

    template <typename T>
    bool BlockedBoundedQueue<T>::dequeue(T &out)
    {
        std::unique_lock<std::mutex> lock(mtx_);

        cv_not_empty_.wait(lock, [&]
                           { return size_ > 0 || closed_; });

        if (size_ == 0 && closed_)
        {
            return false;
        }

        out = buffer[head_];
        head_ = (head_ + 1) % kCapacity;
        --size_;

        lock.unlock();
        cv_not_full_.notify_one();

        return true;
    }

    template <typename T>
    bool BlockedBoundedQueue<T>::isFull() const
    {
        std::unique_lock<std::mutex> lock(mtx_);
        return size_ == kCapacity;
    }

    template <typename T>
    bool BlockedBoundedQueue<T>::isEmpty() const
    {
        std::unique_lock<std::mutex> lock(mtx_);
        return size_ == 0;
    }

    template <typename T>
    size_t BlockedBoundedQueue<T>::size() const
    {
        std::unique_lock<std::mutex> lock(mtx_);
        return size_;
    }

    template <typename T>
    size_t BlockedBoundedQueue<T>::capacity() const
    {
        return kCapacity - size_;
    }

    template <typename T>
    void BlockedBoundedQueue<T>::clear()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        head_ = 0;
        tail_ = 0;
        size_ = 0;
    }

    template <typename T>
    void BlockedBoundedQueue<T>::close()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        closed_ = true;
        lock.unlock();
        cv_not_full_.notify_all();
        cv_not_empty_.notify_all();
    }

    template <typename T>
    bool BlockedBoundedQueue<T>::closed() const
    {
        std::unique_lock<std::mutex> lock(mtx_);
        return closed_;
    }
}