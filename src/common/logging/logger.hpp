#pragma once

#include <atomic>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

enum Level
{
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

namespace logging
{
    class Logger
    {
    public:
        static Logger &instance();

        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;

        void set_level(Level level);
        Level level() const;

        void enable_console(bool enable);
        bool set_log_file(const std::string &path);

        template <typename... Args>
        void log(Level level, Args &&...args)
        {
            if (level < level_.load(std::memory_order_relaxed))
            {
                return;
            }

            std::ostringstream oss;
            (oss << ... << std::forward<Args>(args));
            write(level, oss.str());
        }

    private:
        Logger() = default;
        ~Logger();

        void write(Level level, std::string_view message);
        static const char *level_to_string(Level level);

        std::atomic<Level> level_{Level::INFO};
        bool console_enabled_{true};
        std::ofstream file_;
        std::mutex mutex_;
    };
}

namespace log = logging;

#define LOG_TRACE(...) log::Logger::instance().log(Level::TRACE, __VA_ARGS__)
#define LOG_DEBUG(...) log::Logger::instance().log(Level::DEBUG, __VA_ARGS__)
#define LOG_INFO(...) log::Logger::instance().log(Level::INFO, __VA_ARGS__)
#define LOG_WARN(...) log::Logger::instance().log(Level::WARN, __VA_ARGS__)
#define LOG_ERROR(...) log::Logger::instance().log(Level::ERROR, __VA_ARGS__)
#define LOG_FATAL(...) log::Logger::instance().log(Level::FATAL, __VA_ARGS__)
