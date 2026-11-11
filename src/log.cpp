#include <log.hpp>

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <syncstream>
#include <utility>

namespace {
void output_stderr(const LogRecord& record) {
    std::osyncstream(std::cerr) << record.detail << '\n';
}
}

DiagnosticLog::DiagnosticLog(std::size_t capacity, bool stderr_output)
    : capacity_(capacity), stderr_output_(stderr_output) {
    if (capacity == 0)
        throw std::invalid_argument("Log capacity must be positive");
}

void DiagnosticLog::write(LogLevel level, std::string source, std::string detail) {
    publish({std::chrono::system_clock::now(), level, std::move(source), std::move(detail)});
}

void DiagnosticLog::publish(LogRecord record) {
    log_level_name(record.level);
    std::lock_guard lock(mutex_);
    if (stderr_output_)
        output_stderr(record);
    if (records_.size() == capacity_)
        records_.pop_front();
    records_.push_back(std::move(record));
    ++version_;
}

void DiagnosticLog::clear() {
    std::lock_guard lock(mutex_);
    records_.clear();
    ++version_;
}

LogSnapshot DiagnosticLog::snapshot() const {
    std::lock_guard lock(mutex_);
    return {version_, {records_.begin(), records_.end()}};
}

std::optional<LogSnapshot> DiagnosticLog::snapshot_if_changed(std::uint64_t version) const {
    std::lock_guard lock(mutex_);
    if (version == version_)
        return std::nullopt;
    return LogSnapshot{version_, {records_.begin(), records_.end()}};
}

void write_log(DiagnosticLog* log, LogLevel level, std::string source, std::string detail) {
    if (log)
        log->write(level, std::move(source), std::move(detail));
    else {
        log_level_name(level);
        output_stderr({std::chrono::system_clock::now(), level, std::move(source),
                       std::move(detail)});
    }
}

const char* log_level_name(LogLevel level) {
    switch (level) {
    case LogLevel::debug: return "Debug";
    case LogLevel::info: return "Info";
    case LogLevel::warning: return "Warning";
    case LogLevel::error: return "Error";
    }
    throw std::invalid_argument("Unknown log level");
}

std::string format_log_timestamp(std::chrono::system_clock::time_point timestamp) {
    const auto time = std::chrono::system_clock::to_time_t(timestamp);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &time) != 0)
#else
    if (!localtime_r(&time, &local))
#endif
        throw std::runtime_error("Cannot format log timestamp");
    std::ostringstream result;
    result << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return result.str();
}
