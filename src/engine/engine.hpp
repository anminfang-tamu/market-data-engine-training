#pragma once
#include <cstdint>
#include <functional>
#include <thread>

#include "protocol/md_message.hpp"
#include "containers/blocked_bounded_queue.hpp"
#include "common/metrics/metrics.hpp"

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

        metrics::Metrics m_;

        std::thread processor_;

        std::thread reporter_;

        containers::BlockedBoundedQueue<protocol::MarketDataMsg> queue_;

        void on_message(const void *data, size_t len);
    };
}