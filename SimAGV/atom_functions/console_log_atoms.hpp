#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace simagv::l4 {
namespace {

inline std::mutex& consoleLogMutex()
{
    static std::mutex mutex;
    return mutex;
}

inline std::string isoNowUtc()
{
    const auto now = std::chrono::system_clock::now();
    const auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const std::time_t sec = static_cast<std::time_t>(epochMs / 1000);
    const int ms = static_cast<int>(epochMs % 1000);
    std::tm tmUtc;
    ::gmtime_r(&sec, &tmUtc);
    std::ostringstream oss;
    oss << std::put_time(&tmUtc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms << 'Z';
    return oss.str();
}

inline void writeLogLine(std::ostream& os, const char* level, const std::string& message)
{
    std::lock_guard<std::mutex> lock(consoleLogMutex());
    os << isoNowUtc() << ' ' << level << ' ' << message << " tid=" << std::this_thread::get_id() << "\n";
}

}

inline void logInfo(const std::string& message) { writeLogLine(std::cout, "info:", message); }
inline void logWarn(const std::string& message) { writeLogLine(std::cerr, "warn:", message); }
inline void logError(const std::string& message) { writeLogLine(std::cerr, "error:", message); }

}
