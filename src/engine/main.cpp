#include <iostream>
#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>
#include <pthread.h>

#include "engine/engine.hpp"
#include "common/logging/logger.hpp"

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

    // Block SIGINT/SIGTERM in this thread and wait on them explicitly.
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, nullptr);

    engine::Engine engine;
    std::thread runner([&]()
                       { engine.run(); });

    LOG_INFO("Engine UDP receiver is running on 127.0.0.1:", 8888);

    // Wait until a termination signal arrives.
    int sig = 0;
    sigwait(&set, &sig);

    bool stopped = engine.stop();
    if (stopped)
    {
        LOG_INFO("Engine stopped!!!");
    }
    if (runner.joinable())
    {
        runner.join();
    }
}
