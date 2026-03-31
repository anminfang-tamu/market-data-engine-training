#include <iostream>
#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>
#include <pthread.h>

#include "engine/v2/engine.hpp"
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

    engine::v2::Engine engine;
    net::udp::v2::ReceiverConfig cfg{};
    cfg.local_ip = "0.0.0.0";
    cfg.local_port = 8888;
    cfg.rcvbuf_bytes = 4 * 1024 * 1024;
    cfg.connect_socket = false;

    if (!engine.receive(cfg))
    {
        LOG_ERROR("Failed to start engine UDP receiver on ", cfg.local_ip, ":", cfg.local_port);
        return 1;
    }

    LOG_INFO("Engine UDP receiver is running on ", cfg.local_ip, ":", cfg.local_port);

    // Wait until a termination signal arrives.
    int sig = 0;
    sigwait(&set, &sig);

    engine.stop();
    LOG_INFO("Engine stopped!!!");
    return 0;
}
