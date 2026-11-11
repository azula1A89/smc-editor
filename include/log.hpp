#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

enum class LogLevel { debug, info, warning, error };

struct LogRecord {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string source;
    std::string detail;
};

struct LogSnapshot {
    std::uint64_t version;
    std::vector<LogRecord> records;
};

class DiagnosticLog {
public:
    static constexpr std::size_t default_capacity = 10000;

    explicit DiagnosticLog(std::size_t capacity = default_capacity, bool stderr_output = true);
    void write(LogLevel level, std::string source, std::string detail);
    void publish(LogRecord record);
    void clear();
    LogSnapshot snapshot() const;
    std::optional<LogSnapshot> snapshot_if_changed(std::uint64_t version) const;

private:
    const std::size_t capacity_;
    const bool stderr_output_;
    mutable std::mutex mutex_;
    std::deque<LogRecord> records_;
    std::uint64_t version_ = 0;
};

void write_log(DiagnosticLog* log, LogLevel level, std::string source, std::string detail);
const char* log_level_name(LogLevel level);
std::string format_log_timestamp(std::chrono::system_clock::time_point timestamp);
