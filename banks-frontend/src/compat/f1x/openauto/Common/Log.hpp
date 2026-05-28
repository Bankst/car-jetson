#pragma once

// Local shim — replaces upstream openauto Log.hpp which pulls boost::log.
// Routes OPENAUTO_LOG(sev) << ... through spdlog.

#include <spdlog/spdlog.h>
#include <sstream>

namespace banks_log_compat {

inline spdlog::level::level_enum sevToLevel(const char* sev) {
    // Severities used by openauto: trace, debug, info, warning, error, fatal
    switch (sev[0]) {
        case 't': return spdlog::level::trace;
        case 'd': return spdlog::level::debug;
        case 'i': return spdlog::level::info;
        case 'w': return spdlog::level::warn;
        case 'e': return spdlog::level::err;
        case 'f': return spdlog::level::critical;
        default:  return spdlog::level::info;
    }
}

class LogStream {
public:
    explicit LogStream(spdlog::level::level_enum lvl) : m_lvl(lvl) {}
    ~LogStream() { spdlog::log(m_lvl, "{}", m_stream.str()); }
    template <typename T> LogStream& operator<<(const T& v) { m_stream << v; return *this; }
private:
    std::ostringstream m_stream;
    spdlog::level::level_enum m_lvl;
};

} // namespace banks_log_compat

#define OPENAUTO_LOG_CONTEXT ""
#define OPENAUTO_LOG(severity) \
    ::banks_log_compat::LogStream(::banks_log_compat::sevToLevel(#severity)) << "[OpenAuto] "
