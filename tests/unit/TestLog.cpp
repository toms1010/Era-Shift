// Era Shift - tests for the logging facility.

#include "EraShift/Core/Log.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace EraShift::Core;

TEST_CASE("LogLevel converts to and from a name")
{
    CHECK(logLevelName(LogLevel::Trace) == "TRACE");
    CHECK(logLevelName(LogLevel::Error) == "ERROR");
    CHECK(logLevelName(LogLevel::Off)   == "OFF  ");

    LogLevel level = LogLevel::Warn;
    CHECK(parseLogLevel("info", level));
    CHECK(level == LogLevel::Info);
    CHECK(parseLogLevel("WARNING", level));
    CHECK(level == LogLevel::Warn);
    CHECK(parseLogLevel("  Debug ", level) == false);   // no trimming by design
    CHECK(parseLogLevel("nonsense", level) == false);
}

TEST_CASE("MemoryLogSink captures records")
{
    MemoryLogSink sink(8);
    sink.write(LogLevel::Info, "hello");
    sink.write(LogLevel::Warn, "world");

    const auto records = sink.records();
    REQUIRE(records.size() == 2);
    CHECK(records[0].level == LogLevel::Info);
    CHECK(records[0].message == "hello");
    CHECK(records[1].message == "world");
    CHECK(sink.count() == 2);

    sink.clear();
    CHECK(sink.count() == 0);
}

TEST_CASE("MemoryLogSink is a bounded ring")
{
    MemoryLogSink sink(4);
    for (int i = 0; i < 10; ++i) {
        sink.write(LogLevel::Info, "line " + std::to_string(i));
    }

    const auto records = sink.records();
    REQUIRE(records.size() == 4);
    // The newest records are the ones kept.
    CHECK(records.back().message == "line 9");
    CHECK(records.front().message == "line 6");
}

TEST_CASE("Logger filters by level")
{
    auto sink = std::make_shared<MemoryLogSink>();
    Logger logger;
    logger.addSink(sink);
    logger.setMinLevel(LogLevel::Warn);

    logger.info("T", "info message");
    logger.debug("T", "debug message");
    logger.warn("T", "warn message");
    logger.error("T", "error message");

    const auto records = sink->records();
    REQUIRE(records.size() == 2);
    CHECK(records[0].message.find("warn message") != std::string::npos);
    CHECK(records[1].message.find("error message") != std::string::npos);
}

TEST_CASE("Logger::Off silences everything")
{
    auto sink = std::make_shared<MemoryLogSink>();
    Logger logger;
    logger.addSink(sink);
    logger.setMinLevel(LogLevel::Off);

    CHECK_FALSE(logger.isEnabled(LogLevel::Fatal));
    logger.fatal("T", "should not appear");
    CHECK(sink->count() == 0);
}

TEST_CASE("Logger prefixes the source location basename")
{
    auto sink = std::make_shared<MemoryLogSink>();
    Logger logger;
    logger.addSink(sink);
    logger.setMinLevel(LogLevel::Trace);

    logger.info("EraShift/Game/states/PlayingState.cpp", "entered Playing");

    const auto records = sink->records();
    REQUIRE(records.size() == 1);
    CHECK(records[0].message == "PlayingState.cpp: entered Playing");
}

TEST_CASE("Logger formats with std::format syntax")
{
    auto sink = std::make_shared<MemoryLogSink>();
    Logger logger;
    logger.addSink(sink);
    logger.setMinLevel(LogLevel::Trace);

    logger.info("T", "era={} health={} ready={}", "Future", 87, true);

    const auto records = sink->records();
    REQUIRE(records.size() == 1);
    // The logger prefixes the source location, so match the tail.
    CHECK(records[0].message == "T: era=Future health=87 ready=true");
}

TEST_CASE("Logger dispatches to every sink")
{
    auto first  = std::make_shared<MemoryLogSink>();
    auto second = std::make_shared<MemoryLogSink>();
    Logger logger;
    logger.addSink(first);
    logger.addSink(second);
    logger.setMinLevel(LogLevel::Info);

    CHECK(logger.sinkCount() == 2);
    logger.info("T", "broadcast");

    CHECK(first->count() == 1);
    CHECK(second->count() == 1);

    logger.clearSinks();
    CHECK(logger.sinkCount() == 0);

    logger.info("T", "after clear");
    CHECK(first->count() == 1);
}

TEST_CASE("Logger ignores null sinks")
{
    Logger logger;
    logger.addSink(nullptr);
    CHECK(logger.sinkCount() == 0);
}

TEST_CASE("Logger is safe to write from several threads")
{
    auto sink = std::make_shared<MemoryLogSink>(4096);
    Logger logger;
    logger.addSink(sink);
    logger.setMinLevel(LogLevel::Info);

    constexpr int kThreads = 4;
    constexpr int kPerThread = 250;

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&logger, t] {
            for (int i = 0; i < kPerThread; ++i) {
                logger.info("T", "thread {} message {}", t, i);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    CHECK(sink->count() == static_cast<std::size_t>(kThreads * kPerThread));
}
