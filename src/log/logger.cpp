#include <cstdarg>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <format>
#include <chrono>

#include "log/logger.hpp"
#include "proactor/time.hpp"

namespace Sage
{

using namespace std::string_view_literals;

namespace Logger
{

// Formatter control

constexpr auto FORMAT_END{ "\x1B[00m"sv };
constexpr auto FORMAT_BOLD{ "\x1B[01m"sv };
constexpr auto FORMAT_DISABLED{ "\x1B[02m"sv };
constexpr auto FORMAT_ITALIC{ "\x1B[03m"sv };
constexpr auto FORMAT_URL{ "\x1B[04m"sv };
constexpr auto FORMAT_BLINK{ "\x1B[05m"sv };
constexpr auto FORMAT_BLINK2{ "\x1B[06m"sv };
constexpr auto FORMAT_SELECTED{ "\x1B[07m"sv };
constexpr auto FORMAT_INVISIBLE{ "\x1B[08m"sv };
constexpr auto FORMAT_STRIKE{ "\x1B[09m"sv };
constexpr auto FORMAT_DOUBLE_UNDERLINE{ "\x1B[21m"sv };

// Dark Colours

constexpr auto DARK_BLACK{ "\x1B[30m"sv };
constexpr auto DARK_RED{ "\x1B[31m"sv };
constexpr auto DARK_GREEN{ "\x1B[32m"sv };
constexpr auto DARK_YELLOW{ "\x1B[33m"sv };
constexpr auto DARK_BLUE{ "\x1B[34m"sv };
constexpr auto DARK_VIOLET{ "\x1B[35m"sv };
constexpr auto DARK_BEIGE{ "\x1B[36m"sv };
constexpr auto DARK_WHITE{ "\x1B[37m"sv };

// Light Colours

constexpr auto LIGHT_GREY{ "\x1B[90m"sv };
constexpr auto LIGHT_RED{ "\x1B[91m"sv };
constexpr auto LIGHT_GREEN{ "\x1B[92m"sv };
constexpr auto LIGHT_YELLOW{ "\x1B[93m"sv };
constexpr auto LIGHT_BLUE{ "\x1B[94m"sv };
constexpr auto LIGHT_VIOLET{ "\x1B[95m"sv };
constexpr auto LIGHT_BEIGE{ "\x1B[96m"sv };
constexpr auto LIGHT_WHITE{ "\x1B[97m"sv };

// Helper classes / structs

class LogStreamer
{
public:
    using Stream = std::ostream;

    LogStreamer() = default;

    void Setup(const std::string& filename, Level level)
    {
        m_logFilename = filename;
        m_logLevel = level;

        if (m_logFilename.empty())
        {
            SetStreamToConsole();
            return;
        }

        // try setup a file logger if specified
        try
        {
            if (std::filesystem::exists(m_logFilename) and not std::filesystem::is_regular_file(m_logFilename))
            {
                throw std::runtime_error("cannot write to non regular file '" + m_logFilename + "'");
            }

            std::ofstream file{ m_logFilename , std::ios::out | std::ios::ate | std::ios::app };
            std::filesystem::permissions(
                m_logFilename,
                std::filesystem::perms::owner_write | std::filesystem::perms::group_read,
                std::filesystem::perm_options::add
            );

            if (file.fail())
            {
                throw std::runtime_error("unable to open file '" + m_logFilename + "' for writing");
            }

            SetStreamToFile(std::move(file));
        }
        catch (const std::exception& e)
        {
            LOG_CRITICAL("==== failed to setup file logger. what: %s ====", e.what());
        }
    }

    Level GetLogLevel() const noexcept { return m_logLevel; }

private:
    // Nothing in here is movable or copyable
    LogStreamer(const LogStreamer&) = delete;
    LogStreamer(LogStreamer&&) = delete;
    LogStreamer& operator=(const LogStreamer&) = delete;
    LogStreamer& operator=(LogStreamer&&) = delete;

    void EnsureLogFileWriteable()
    {
        if (m_logFilename.empty())
        {
            return;
        }

        try
        {
            // everything is okay
            if (std::filesystem::exists(m_logFilename) and std::filesystem::is_regular_file(m_logFilename))
            {
                return;
            }

            // No longer writing to log file

            m_lostLogTime += s_logFileCreatorPeriod.count();

            if (std::filesystem::exists(m_logFilename) and not std::filesystem::is_regular_file(m_logFilename))
            {
                std::fprintf(::stderr, "cannot write to non regular file '%s'", m_logFilename.c_str());
                return;
            }

            std::ofstream file{ m_logFilename , std::ios::out | std::ios::ate | std::ios::app };
            std::filesystem::permissions(
                m_logFilename,
                std::filesystem::perms::owner_write | std::filesystem::perms::group_read,
                std::filesystem::perm_options::add
            );

            if (file.fail())
            {
                std::fprintf(::stderr, "unable to open file '%s' for writing", m_logFilename.c_str());
                return;
            }

            SetStreamToFile(std::move(file));

            std::ostringstream lostLogTimeOSS;
            lostLogTimeOSS << static_cast<decltype(s_logFileCreatorPeriod)>(m_lostLogTime);
            LOG_CRITICAL("lost %s worth of logs", lostLogTimeOSS.str().c_str());
            m_lostLogTime = {};
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            std::fprintf(::stderr, "file system error when attempting to recreate file logger. e: %s", e.what());
        }
    }

    void SetStreamToConsole()
    {
        m_logFileStream = {};
        m_streamRef = s_consoleStream;
    }

    void SetStreamToFile(std::ofstream fileStream)
    {
        m_logFileStream = std::move(fileStream);
        m_streamRef = m_logFileStream;
    }

private:
    static constexpr auto s_logFileCreatorPeriod{ 60s };
    static constexpr std::reference_wrapper<Stream> s_consoleStream{ std::clog };

    std::reference_wrapper<Stream> m_streamRef{ s_consoleStream };
    std::string m_logFilename{};
    Level m_logLevel{ Level::Info };
    size_t m_lostLogTime{ 0 };
    std::ofstream m_logFileStream{};

    friend void LogToStream(Level level, const char* fmt, va_list args);
    friend void EnsureLogFileExist();
};

struct LogTimestamp
{
    // e.g "01 - 09 - 2023 00:42 : 19"
    using SecondsBuffer = char[26];
    // :%03u requires 7 bytes max
    using MilliSecBuffer = char[7];

    LogTimestamp() noexcept
    {
        std::timespec_get(&m_timeSpec, TIME_UTC);

        uint16_t millisec = static_cast<uint16_t>(m_timeSpec.tv_nsec / 1'000'000U);
        // incase of overflow
        if (millisec >= 1000U)
        {
            millisec = static_cast<uint16_t>(millisec - 1000U);
            m_timeSpec.tv_sec++;
        }

        std::tm localTimeRes{};
        std::strftime(
            m_secondsBuffer,
            sizeof(m_secondsBuffer),
            "%d-%m-%Y %H:%M:%S",
            ::localtime_r(&m_timeSpec.tv_sec, &localTimeRes)
        );
        snprintf(m_msSecBuff, sizeof(m_msSecBuff), ":%03u", millisec);
    }

    const SecondsBuffer& getSecondsBuffer() const noexcept { return m_secondsBuffer; };

    const MilliSecBuffer& getMilliSecBuffer() const noexcept { return m_msSecBuff; };

private:
    SecondsBuffer m_secondsBuffer;
    MilliSecBuffer m_msSecBuff;
    timespec m_timeSpec;
};

// Global variables

constexpr std::array LEVEL_COLOURS
{
    LIGHT_GREEN,    // Level::Trace
    DARK_BLUE,      // Level::Debug
    DARK_WHITE,     // Level::Info
    LIGHT_YELLOW,   // Level::Warning
    LIGHT_RED,      // Level::Error
    DARK_RED,       // Level::Critical
};

constexpr std::array LEVEL_NAMES
{
    "TRACE"sv,        // Level::Trace
    "DEBUG"sv,        // Level::Debug
    "INFO "sv,        // Level::Info
    "WARN "sv,        // Level::Warning
    "ERROR"sv,        // Level::Error
    "CRIT "sv,        // Level::Critical
};

/**
 * Intentionally leaking here.
 * Logging in global object destructor's may cause issue if we destroy the logger
*/
LogStreamer* const g_logStreamer{ new LogStreamer };

// Functions

void SetupLogger(Level logLevel, const std::string& filename)
{
    g_logStreamer->Setup(filename, logLevel);
}

constexpr std::string_view GetLevelFormatter(Level level) noexcept { return LEVEL_COLOURS[level]; }

constexpr std::string_view GetLevelName(Level level) noexcept { return LEVEL_NAMES[level]; }

void LogToStream(Level level, const char* fmt, va_list args)
{
    using MsgBuffer = char[1024];

    if (not Internal::ShouldLog(level))
        return;

    LogTimestamp ts;
    MsgBuffer msgBuff;
    vsnprintf(msgBuff, sizeof(msgBuff), fmt, args);

    {
        LogStreamer::Stream& stream{ g_logStreamer->m_streamRef.get() };
        stream
            << GetLevelFormatter(level)
            << '[' << ts.getSecondsBuffer() << ts.getMilliSecBuffer() << "] "
            << '[' << GetLevelName(level) << "] "
            << msgBuff
            << FORMAT_END << '\n';
        std::flush(stream);
    }
}

void EnsureLogFileExist()
{
    g_logStreamer->EnsureLogFileWriteable();
}

namespace Internal
{

void Trace(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Logger::LogToStream(Logger::Trace, fmt, args);
    va_end(args);
}

void Debug(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Logger::LogToStream(Logger::Debug, fmt, args);
    va_end(args);
}

void Info(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Logger::LogToStream(Logger::Info, fmt, args);
    va_end(args);
}

void Warning(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Logger::LogToStream(Logger::Warning, fmt, args);
    va_end(args);
}

void Error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Logger::LogToStream(Logger::Error, fmt, args);
    va_end(args);
}

void Critical(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Logger::LogToStream(Logger::Critical, fmt, args);
    va_end(args);
}

bool ShouldLog(Level level) noexcept { return level >= g_logStreamer->GetLogLevel(); }

} // namespace Internal

} // namespace Logger

} // namespace Sage
