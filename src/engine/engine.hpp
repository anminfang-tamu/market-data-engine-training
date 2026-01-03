#pragma once
#include <cstdint>
#include <functional>

#include "protocol/md_message.hpp"

namespace engine
{
    class Engine
    {
    public:
        Engine() = default;
        ~Engine();

        bool run();
        bool stop();

    private:
        bool running_{false};
        int64_t incoming_msg_count_{0};
        int64_t invalid_msg_count_{0};

        void on_message(const void *data, size_t len);
    };
}