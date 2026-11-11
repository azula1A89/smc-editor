#include <log.hpp>

#include <atomic>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void test_records_and_capacity() {
    DiagnosticLog log(2, false);
    const auto before = std::chrono::system_clock::now();
    log.write(LogLevel::debug, "Parser", "first");
    const auto after = std::chrono::system_clock::now();
    auto first = log.snapshot();
    require(first.records.size() == 1 && first.records[0].timestamp >= before &&
                first.records[0].timestamp <= after && first.records[0].level == LogLevel::debug &&
                first.records[0].source == "Parser" && first.records[0].detail == "first",
            "Log record fields are incorrect");
    require(!log.snapshot_if_changed(first.version), "Unchanged snapshot was copied");
    log.write(LogLevel::warning, "File", "second\nline");
    log.write(LogLevel::error, "Layout", "third");
    auto retained = log.snapshot_if_changed(first.version);
    require(retained && retained->records.size() == 2 &&
                retained->records[0].detail == "second\nline" &&
                retained->records[1].detail == "third",
            "Capacity did not discard the oldest record");
    require(format_log_timestamp(first.records[0].timestamp).size() == 19,
            "Timestamp format is incorrect");
    log.clear();
    require(log.snapshot().records.empty() &&
                log.snapshot_if_changed(retained->version).has_value(),
            "Clear did not invalidate the snapshot");
    bool threw = false;
    try { DiagnosticLog invalid(0); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "Zero capacity was accepted");
}

void test_concurrent_access() {
    constexpr int threads = 4;
    constexpr int writes = 1000;
    DiagnosticLog log(threads * writes, false);
    std::atomic<int> done = 0;
    std::atomic<bool> invalid = false;
    std::vector<std::thread> writers;
    for (int i = 0; i < threads; ++i) {
        writers.emplace_back([&, i] {
            for (int j = 0; j < writes; ++j)
                log.write(LogLevel::info, std::to_string(i), std::to_string(j));
            ++done;
        });
    }
    std::thread reader([&] {
        while (done.load() != threads) {
            const auto snapshot = log.snapshot();
            if (snapshot.records.size() > threads * writes)
                invalid = true;
            for (const auto& record : snapshot.records)
                if (record.source.empty() || record.detail.empty())
                    invalid = true;
        }
    });
    for (auto& writer : writers)
        writer.join();
    reader.join();
    require(!invalid && log.snapshot().records.size() == threads * writes,
            "Concurrent writes or snapshots lost records");

    done = 0;
    std::thread writer([&] {
        for (int i = 0; i < writes; ++i)
            log.write(LogLevel::info, "writer", std::to_string(i));
        ++done;
    });
    std::thread clearer([&] {
        while (done.load() == 0) {
            log.clear();
            if (log.snapshot().records.size() > threads * writes)
                invalid = true;
        }
    });
    writer.join();
    clearer.join();
    require(!invalid, "Concurrent clear or snapshot violated capacity");
}

void test_stderr() {
    std::ostringstream output;
    auto* previous = std::cerr.rdbuf(output.rdbuf());
    DiagnosticLog log;
    log.write(LogLevel::warning, "Settings", "logged warning");
    write_log(nullptr, LogLevel::error, "File", "fallback error");
    std::cerr.rdbuf(previous);
    require(output.str() == "logged warning\nfallback error\n",
            "Standard error output was lost or duplicated");

    std::ostringstream concurrent_output;
    previous = std::cerr.rdbuf(concurrent_output.rdbuf());
    std::vector<std::thread> writers;
    for (int i = 0; i < 4; ++i) {
        writers.emplace_back([i] {
            for (int j = 0; j < 100; ++j)
                write_log(nullptr, LogLevel::warning, "test",
                          std::to_string(i) + ":" + std::to_string(j));
        });
    }
    for (auto& writer : writers)
        writer.join();
    std::cerr.rdbuf(previous);
    std::istringstream lines(concurrent_output.str());
    std::set<std::string> entries;
    std::string line;
    while (std::getline(lines, line))
        entries.insert(line);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 100; ++j)
            require(entries.contains(std::to_string(i) + ":" + std::to_string(j)),
                    "Concurrent standard error output was interleaved or lost");
    require(entries.size() == 400, "Unexpected concurrent standard error output");
}
}

int main() {
    try {
        test_records_and_capacity();
        test_concurrent_access();
        test_stderr();
    } catch (const std::exception& error) {
        std::cerr << "log_tests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
