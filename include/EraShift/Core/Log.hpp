// Era Shift - Core logging facility.
//
// The logger is an ordinary object owned by the engine. There is deliberately
// no global logger singleton: subsystems receive a `Logger&` through their
// constructor so that tests can capture output and the dependency graph stays
// visible.

#pragma once

#include <cstdint>
#include <format>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace EraShift::Core {

/// Severity of a log record.
enum class LogLevel : std::uint8_t {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Fatal = 5,
    Off   = 6,
};

/// Human readable name of a level, e.g. "WARN".
[[nodiscard]] std::string_view logLevelName(LogLevel level) noexcept;

/// Stream inserter so log levels print readably in test failures.
std::ostream& operator<<(std::ostream& os, LogLevel level);

/// Parses a level name, case-insensitively. Returns false when unknown.
[[nodiscard]] bool parseLogLevel(std::string_view text, LogLevel& out) noexcept;

/// Destination for formatted log records.
class LogSink {
public:
    LogSink() = default;
    virtual ~LogSink();

    LogSink(const LogSink&)            = delete;
    LogSink& operator=(const LogSink&) = delete;

    /// Writes a single already-formatted record. Implementations may assume the
    /// logger serialises calls, but should still be safe with their own locking.
    virtual void write(LogLevel level, std::string_view message) = 0;

    /// Called once when the sink is attached or detached so it can flush/close.
    virtual void flush() {}
};

/// Writes records to stdout/stderr. Records at Error and above go to stderr.
class ConsoleLogSink final : public LogSink {
public:
    explicit ConsoleLogSink(bool useColours = true);
    void write(LogLevel level, std::string_view message) override;
    void flush() override;

private:
    bool m_useColours;
};

/// Appends records to a file. Opening happens in the constructor so that a
/// failure is reported immediately rather than on the first log line.
class FileLogSink final : public LogSink {
public:
    /// @param path       Destination file, created/appended to.
    /// @param minLevel   Records below this level are discarded by the sink.
    FileLogSink(std::string path, LogLevel minLevel = LogLevel::Trace);
    ~FileLogSink() override;

    void write(LogLevel level, std::string_view message) override;
    void flush() override;

    [[nodiscard]] const std::string& path() const noexcept { return m_path; }
    [[nodiscard]] bool isOpen() const noexcept { return m_file != nullptr; }

private:
    std::string m_path;
    LogLevel    m_minLevel;
    void*       m_file;   // std::FILE*, hidden to keep <cstdio> out of the header
    std::mutex  m_mutex;
};

/// In-memory sink used by tests and by the on-screen log panel.
class MemoryLogSink final : public LogSink {
public:
    struct Record {
        LogLevel    level;
        std::string message;
    };

    explicit MemoryLogSink(std::size_t capacity = 512);

    void write(LogLevel level, std::string_view message) override;

    [[nodiscard]] std::vector<Record> records() const;
    [[nodiscard]] std::size_t count() const;
    void clear();

private:
    mutable std::mutex   m_mutex;
    std::size_t          m_capacity;
    std::vector<Record>  m_records;
};

/// Dispatches records to the attached sinks.
///
/// The logger is thread safe: `write()` takes an internal lock so background
/// jobs (asset streaming, save writing) can log safely.
class Logger {
public:
    Logger() = default;
    explicit Logger(LogLevel minLevel, bool colouredOutput = true);

    /// Attaches a sink. Ownership is shared because a sink may be observed by
    /// several components (for example the on-screen panel and the file sink).
    void addSink(std::shared_ptr<LogSink> sink);
    void clearSinks();

    [[nodiscard]] std::size_t sinkCount() const;

    void setMinLevel(LogLevel level) noexcept;
    [[nodiscard]] LogLevel minLevel() const noexcept;

    [[nodiscard]] bool isEnabled(LogLevel level) const noexcept;

    /// Emits a record. `where` is an optional source location.
    void write(LogLevel level, std::string_view where, std::string_view message);

    /// Formats and emits a record using std::format syntax.
    template <typename... Args>
    void log(LogLevel level, std::string_view where, std::format_string<Args...> fmt, Args&&... args)
    {
        if (!isEnabled(level)) {
            return;
        }
        write(level, where, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void trace(std::string_view where, std::format_string<Args...> fmt, Args&&... args)
    {
        log(LogLevel::Trace, where, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void debug(std::string_view where, std::format_string<Args...> fmt, Args&&... args)
    {
        log(LogLevel::Debug, where, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void info(std::string_view where, std::format_string<Args...> fmt, Args&&... args)
    {
        log(LogLevel::Info, where, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void warn(std::string_view where, std::format_string<Args...> fmt, Args&&... args)
    {
        log(LogLevel::Warn, where, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void error(std::string_view where, std::format_string<Args...> fmt, Args&&... args)
    {
        log(LogLevel::Error, where, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void fatal(std::string_view where, std::format_string<Args...> fmt, Args&&... args)
    {
        log(LogLevel::Fatal, where, fmt, std::forward<Args>(args)...);
    }

private:
    mutable std::mutex              m_mutex;
    LogLevel                        m_minLevel = LogLevel::Info;
    std::vector<std::shared_ptr<LogSink>> m_sinks;
};

} // namespace EraShift::Core
