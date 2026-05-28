// Replacement for aasdk's src/Common/ModernLogger.cpp. The original spins
// a worker thread and writes to stderr through its own ConsoleSink. We
// forward everything to spdlog instead — same singleton API, no extra
// thread, no duplicate output.

#include <aasdk/Common/ModernLogger.hpp>
#include <spdlog/spdlog.h>

namespace aasdk {
namespace common {

namespace {

spdlog::level::level_enum toSpd(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::TRACE: return spdlog::level::trace;
        case LogLevel::DEBUG: return spdlog::level::debug;
        case LogLevel::INFO:  return spdlog::level::info;
        case LogLevel::WARN:  return spdlog::level::warn;
        case LogLevel::ERROR: return spdlog::level::err;
        case LogLevel::FATAL: return spdlog::level::critical;
    }
    return spdlog::level::info;
}

} // namespace

ModernLogger& ModernLogger::getInstance() {
    static ModernLogger inst;
    return inst;
}

ModernLogger::ModernLogger()
    : globalLevel_(LogLevel::TRACE),
      async_(false),
      maxQueueSize_(0),
      shutdown_(false),
      droppedMessages_(0) {}

ModernLogger::~ModernLogger() = default;

void ModernLogger::setLevel(LogLevel level) { globalLevel_ = level; }
void ModernLogger::setCategoryLevel(LogCategory category, LogLevel level) {
    std::lock_guard<std::mutex> lk(mutex_);
    categoryLevels_[category] = level;
}
void ModernLogger::addSink(std::shared_ptr<LogSink>) {}
void ModernLogger::setFormatter(std::shared_ptr<LogFormatter>) {}
void ModernLogger::setAsync(bool) {}
void ModernLogger::setMaxQueueSize(size_t) {}

void ModernLogger::log(LogLevel level, LogCategory /*category*/,
                       const std::string& /*component*/,
                       const std::string& /*function*/,
                       const std::string& /*file*/, int /*line*/,
                       const std::string& message) {
    spdlog::log(toSpd(level), "{}", message);
}

void ModernLogger::logWithContext(LogLevel level, LogCategory category,
                                  const std::string& component,
                                  const std::string& function,
                                  const std::string& file, int line,
                                  const std::string& message,
                                  const std::map<std::string, std::string>& /*context*/) {
    log(level, category, component, function, file, line, message);
}

void ModernLogger::trace(LogCategory c, const std::string& comp, const std::string& f,
                         const std::string& fi, int li, const std::string& m) {
    log(LogLevel::TRACE, c, comp, f, fi, li, m);
}
void ModernLogger::debug(LogCategory c, const std::string& comp, const std::string& f,
                         const std::string& fi, int li, const std::string& m) {
    log(LogLevel::DEBUG, c, comp, f, fi, li, m);
}
void ModernLogger::info(LogCategory c, const std::string& comp, const std::string& f,
                        const std::string& fi, int li, const std::string& m) {
    log(LogLevel::INFO, c, comp, f, fi, li, m);
}
void ModernLogger::warn(LogCategory c, const std::string& comp, const std::string& f,
                        const std::string& fi, int li, const std::string& m) {
    log(LogLevel::WARN, c, comp, f, fi, li, m);
}
void ModernLogger::error(LogCategory c, const std::string& comp, const std::string& f,
                         const std::string& fi, int li, const std::string& m) {
    log(LogLevel::ERROR, c, comp, f, fi, li, m);
}
void ModernLogger::fatal(LogCategory c, const std::string& comp, const std::string& f,
                         const std::string& fi, int li, const std::string& m) {
    log(LogLevel::FATAL, c, comp, f, fi, li, m);
}

void ModernLogger::flush()    { spdlog::default_logger()->flush(); }
void ModernLogger::shutdown() {}

size_t ModernLogger::getQueueSize() const       { return 0; }
size_t ModernLogger::getDroppedMessages() const { return droppedMessages_.load(); }

bool ModernLogger::shouldLog(LogLevel level, LogCategory category) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = categoryLevels_.find(category);
    LogLevel threshold = (it != categoryLevels_.end()) ? it->second : globalLevel_;
    return static_cast<int>(level) >= static_cast<int>(threshold);
}

std::string ModernLogger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
    }
    return "INFO";
}

std::string ModernLogger::categoryToString(LogCategory /*category*/) { return ""; }

LogLevel ModernLogger::stringToLevel(const std::string& level) {
    if (level == "TRACE") return LogLevel::TRACE;
    if (level == "DEBUG") return LogLevel::DEBUG;
    if (level == "INFO")  return LogLevel::INFO;
    if (level == "WARN")  return LogLevel::WARN;
    if (level == "ERROR") return LogLevel::ERROR;
    if (level == "FATAL") return LogLevel::FATAL;
    return LogLevel::INFO;
}

LogCategory ModernLogger::stringToCategory(const std::string& /*category*/) {
    return LogCategory::GENERAL;
}

void ModernLogger::processLogs() {}

// Stubs for formatter/sink classes referenced in the header. Nothing
// instantiates them post-shim, but symbols may be required if other code
// touches the vtables.
AasdkConsoleFormatter::AasdkConsoleFormatter(bool useColors, bool showThreadId, bool showLocation)
    : useColors_(useColors), showThreadId_(showThreadId), showLocation_(showLocation) {}
std::string AasdkConsoleFormatter::format(const LogEntry&)               { return {}; }
std::string AasdkConsoleFormatter::getLevelColor(LogLevel) const         { return {}; }
std::string AasdkConsoleFormatter::getCategoryColor(LogCategory) const   { return {}; }
std::string AasdkConsoleFormatter::resetColor() const                    { return {}; }

JsonFormatter::JsonFormatter(bool prettyPrint) : prettyPrint_(prettyPrint) {}
std::string JsonFormatter::format(const LogEntry&) { return {}; }

FileFormatter::FileFormatter() {}
std::string FileFormatter::format(const LogEntry&) { return {}; }

ConsoleSink::ConsoleSink(bool useStderr) : useStderr_(useStderr) {}
void ConsoleSink::write(const std::string&) {}
void ConsoleSink::flush() {}

FileSink::FileSink(const std::string& filename, size_t maxSize, size_t maxFiles)
    : filename_(filename), maxSize_(maxSize), maxFiles_(maxFiles), currentSize_(0) {}
FileSink::~FileSink() = default;
void FileSink::write(const std::string&) {}
void FileSink::flush() {}
void FileSink::rotateFile() {}

RemoteSink::RemoteSink(const std::string& endpoint) : endpoint_(endpoint) {}
void RemoteSink::write(const std::string&) {}
void RemoteSink::flush() {}

} // namespace common
} // namespace aasdk
