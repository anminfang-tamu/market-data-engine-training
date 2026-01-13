// Basic thread-safe singleton logger for early-stage observability.
#include "common/logging/logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <thread>

namespace
{
    std::string timestamp_now()
    {
        using namespace std::chrono;

        const auto now = system_clock::now();
        const auto secs = system_clock::to_time_t(now);
        const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

        std::tm tm{};
        localtime_r(&secs, &tm);

        std::ostringstream oss;
        oss << std::put_time(&tm, "%F %T") << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }
}

namespace logging
{
    Logger &Logger::instance()
    {
        static Logger logger;
        return logger;
    }

    Logger::~Logger()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open())
        {
            file_.flush();
        }
    }

    void Logger::set_level(Level level)
    {
        level_.store(level, std::memory_order_relaxed);
    }

    Level Logger::level() const
    {
        return level_.load(std::memory_order_relaxed);
    }

    void Logger::enable_console(bool enable)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        console_enabled_ = enable;
    }

    bool Logger::set_log_file(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        file_.close();
        file_.clear();
        file_.open(path, std::ios::app);
        return file_.is_open();
    }

    void Logger::write(Level level, std::string_view message)
    {
        const auto now = timestamp_now();

        std::ostringstream oss;
        oss << "[" << now << "][" << level_to_string(level) << "][tid=" << std::this_thread::get_id() << "] " << message;
        const auto line = oss.str();

        std::lock_guard<std::mutex> lock(mutex_);
        if (console_enabled_)
        {
            std::clog << line << '\n';
            std::clog.flush();
        }
        if (file_.is_open())
        {
            file_ << line << '\n';
            file_.flush();
        }
    }

    const char *Logger::level_to_string(Level level)
    {
        switch (level)
        {
        case Level::TRACE:
            return "TRACE";
        case Level::DEBUG:
            return "DEBUG";
        case Level::INFO:
            return "INFO";
        case Level::WARN:
            return "WARN";
        case Level::ERROR:
            return "ERROR";
        case Level::FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
        }
    }
}
