#include "EraShift/Core/Log.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <ostream>
#include <iomanip>
#include <sstream>
#include <system_error>

#include <unistd.h>

namespace EraShift::Core {
namespace {

std::string timestampUtc()
{
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto tt   = clock::to_time_t(now);
    const auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif

    // 64 bytes is comfortably more than the 25 the format can produce; the
    // smaller buffer made the compiler warn about a truncation that cannot
    // actually happen.
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%03dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));

    return std::string(buffer);
}

/// Last path component of "Namespace/File.cpp:42" style locations.
std::string_view basename(std::string_view path)
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

const char* colourFor(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Trace: return "\033[37m";
        case LogLevel::Debug: return "\033[36m";
        case LogLevel::Info:  return "\033[0m";
        case LogLevel::Warn:  return "\033[33m";
        case LogLevel::Error: return "\033[31m";
        case LogLevel::Fatal: return "\033[1;31m";
        case LogLevel::Off:   return "";
    }
    return "";
}

constexpr std::string_view kResetColour = "\033[0m";

} // namespace

// ---------------------------------------------------------------------------
// LogLevel
// ---------------------------------------------------------------------------
std::string_view logLevelName(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        case LogLevel::Off:   return "OFF  ";
    }
    return "?????";
}

std::ostream& operator<<(std::ostream& os, LogLevel level)
{
    return os << logLevelName(level);
}

bool parseLogLevel(std::string_view text, LogLevel& out) noexcept
{
    std::string upper;
    upper.reserve(text.size());
    for (const char c : text) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }

    if (upper == "TRACE")      { out = LogLevel::Trace; return true; }
    if (upper == "DEBUG")      { out = LogLevel::Debug; return true; }
    if (upper == "INFO")       { out = LogLevel::Info;  return true; }
    if (upper == "WARN" || upper == "WARNING") { out = LogLevel::Warn;  return true; }
    if (upper == "ERROR")      { out = LogLevel::Error; return true; }
    if (upper == "FATAL")      { out = LogLevel::Fatal; return true; }
    if (upper == "OFF" || upper == "NONE") { out = LogLevel::Off; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// LogSink
// ---------------------------------------------------------------------------
LogSink::~LogSink() = default;

// ---------------------------------------------------------------------------
// ConsoleLogSink
// ---------------------------------------------------------------------------
ConsoleLogSink::ConsoleLogSink(bool useColours)
    : m_useColours(useColours && ::isatty(fileno(stdout)) == 1)
{
}

void ConsoleLogSink::write(LogLevel level, std::string_view message)
{
    std::FILE* out = (level >= LogLevel::Error) ? stderr : stdout;

    std::string line;
    line.reserve(message.size() + 48);
    line.append(timestampUtc());
    line.append("  ");
    line.append(logLevelName(level));
    line.append("  ");
    if (m_useColours) {
        line.append(colourFor(level));
    }
    line.append(message);
    if (m_useColours) {
        line.append(kResetColour);
    }
    line.push_back('\n');

    std::fwrite(line.data(), 1, line.size(), out);
}

void ConsoleLogSink::flush()
{
    std::fflush(stdout);
    std::fflush(stderr);
}

// ---------------------------------------------------------------------------
// FileLogSink
// ---------------------------------------------------------------------------
FileLogSink::FileLogSink(std::string path, LogLevel minLevel)
    : m_path(std::move(path)), m_minLevel(minLevel)
{
    auto* file = std::fopen(m_path.c_str(), "a");
    if (file == nullptr) {
        std::fprintf(stderr, "[LOG] unable to open log file '%s' for writing\n", m_path.c_str());
        return;
    }
    m_file = file;
}

FileLogSink::~FileLogSink()
{
    FileLogSink::flush();
    if (m_file != nullptr) {
        std::fclose(static_cast<std::FILE*>(m_file));
        m_file = nullptr;
    }
}

void FileLogSink::write(LogLevel level, std::string_view message)
{
    if (level < m_minLevel || m_file == nullptr) {
        return;
    }

    const std::lock_guard<std::mutex> lock(m_mutex);

    std::string line;
    line.reserve(message.size() + 48);
    line.append(timestampUtc());
    line.append("  ");
    line.append(logLevelName(level));
    line.append("  ");
    line.append(message);
    line.push_back('\n');

    auto* file = static_cast<std::FILE*>(m_file);
    std::fwrite(line.data(), 1, line.size(), file);
}

void FileLogSink::flush()
{
    if (m_file == nullptr) {
        return;
    }
    const std::lock_guard<std::mutex> lock(m_mutex);
    std::fflush(static_cast<std::FILE*>(m_file));
}

// ---------------------------------------------------------------------------
// MemoryLogSink
// ---------------------------------------------------------------------------
MemoryLogSink::MemoryLogSink(std::size_t capacity)
    : m_capacity(capacity == 0 ? 1 : capacity)
{
    m_records.reserve(std::min(m_capacity, std::size_t{256}));
}

void MemoryLogSink::write(LogLevel level, std::string_view message)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_records.push_back(Record{level, std::string(message)});
    if (m_records.size() > m_capacity) {
        m_records.erase(m_records.begin(),
                        m_records.begin() + static_cast<std::ptrdiff_t>(m_records.size() - m_capacity));
    }
}

std::vector<MemoryLogSink::Record> MemoryLogSink::records() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_records;
}

std::size_t MemoryLogSink::count() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_records.size();
}

void MemoryLogSink::clear()
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_records.clear();
}

// ---------------------------------------------------------------------------
// Logger
// ---------------------------------------------------------------------------
Logger::Logger(LogLevel minLevel, bool colouredOutput)
    : m_minLevel(minLevel)
{
    addSink(std::make_shared<ConsoleLogSink>(colouredOutput));
}

void Logger::addSink(std::shared_ptr<LogSink> sink)
{
    if (sink == nullptr) {
        return;
    }
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_sinks.push_back(std::move(sink));
}

void Logger::clearSinks()
{
    std::vector<std::shared_ptr<LogSink>> doomed;
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        doomed.swap(m_sinks);
    }
    for (auto& sink : doomed) {
        sink->flush();
    }
}

std::size_t Logger::sinkCount() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_sinks.size();
}

void Logger::setMinLevel(LogLevel level) noexcept
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_minLevel = level;
}

LogLevel Logger::minLevel() const noexcept
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_minLevel;
}

bool Logger::isEnabled(LogLevel level) const noexcept
{
    return level >= m_minLevel && m_minLevel != LogLevel::Off;
}

void Logger::write(LogLevel level, std::string_view where, std::string_view message)
{
    if (!isEnabled(level)) {
        return;
    }

    std::string line;
    if (!where.empty()) {
        line.reserve(where.size() + message.size() + 4);
        line.append(basename(where));
        line.append(": ");
        line.append(message);
    } else {
        line.assign(message);
    }

    const std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& sink : m_sinks) {
        sink->write(level, line);
    }
}

} // namespace EraShift::Core
