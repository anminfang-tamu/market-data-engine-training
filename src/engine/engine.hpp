#pragma once
#include <cstdint>
#include <functional>
#include <thread>

#include "protocol/md_message.hpp"
#include "containers/blocked_bounded_queue.hpp"

namespace engine
{
    class Engine
    {
    public:
        Engine() = default;
        ~Engine();

        bool run();
        bool stop();

        void process_loop();

    private:
        bool running_{false};

        int listen_fd_{-1};

        int64_t received_{0};
        int64_t processed_{0};
        int64_t decoded_err_{0};
        int64_t drops_{0};

        std::thread processor_;

        std::thread reporter_;

        containers::BlockedBoundedQueue<protocol::MarketDataMsg> queue_;

        void on_message(const void *data, size_t len);
    };
}