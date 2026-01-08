#include <iostream>
#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>

#include "engine/engine.hpp"

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
    std::cout << "<======== Engine ========>" << std::endl;

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    engine::Engine engine;
    std::thread runner([&]()
                       { engine.run(); });

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
