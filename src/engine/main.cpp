#include <iostream>
#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>

#include "engine/engine.hpp"
#include "common/logging/logger.hpp"

namespace
{
    std::atomic<bool> stop_flag{false};

    void handle_signal(int)
    {
        stop_flag.store(true, std::memory_order_relaxed);
    }
}

int main()
{
    auto &logger = log::Logger::instance();
    if (!logger.set_log_file("engine_app.log"))
    {
        LOG_ERROR("Failed to open engine_app.log for writing");
        return 1;
    }
    logger.set_level(Level::DEBUG);
    LOG_INFO("<======== Engine ========>");

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    engine::Engine engine;
    std::thread runner([&]()
                       { engine.run(); });

    LOG_INFO("Engine Server is running on port: ", 8888);

    while (!stop_flag.load(std::memory_order_relaxed))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    engine.stop();
    if (runner.joinable())
    {
        runner.join();
    }
}
