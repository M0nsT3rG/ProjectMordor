#pragma once
// Structured JSON-lines logger. One object per line: t_ms, lvl, comp, evt, then fields.
// Logs never contain player names, file paths or remote IP addresses (sanitized by convention).
#include <chrono>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>

namespace mordor {

enum class LogLevel { Debug, Info, Warn, Error };

struct LogField {
    std::string_view key;
    std::string value;
    bool quoted;
};

inline LogField kv(std::string_view k, std::string_view v) { return {k, std::string(v), true}; }
inline LogField kv(std::string_view k, const char* v) { return kv(k, std::string_view(v)); }  // else const char* -> bool wins
inline LogField kv(std::string_view k, bool v) { return {k, v ? "true" : "false", false}; }
template <class T>
    requires std::is_arithmetic_v<T>
LogField kv(std::string_view k, T v) {
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(v)) return {k, "null", false};
    }
    return {k, std::to_string(v), false};
}

class Logger {
public:
    // sink == nullptr disables output (used by tests).
    explicit Logger(std::string component, std::FILE* sink = stderr, LogLevel min_level = LogLevel::Info);
    void set_level(LogLevel l) { level_ = l; }
    void log(LogLevel level, std::string_view event, std::initializer_list<LogField> fields = {});
    void debug(std::string_view e, std::initializer_list<LogField> f = {}) { log(LogLevel::Debug, e, f); }
    void info(std::string_view e, std::initializer_list<LogField> f = {}) { log(LogLevel::Info, e, f); }
    void warn(std::string_view e, std::initializer_list<LogField> f = {}) { log(LogLevel::Warn, e, f); }
    void error(std::string_view e, std::initializer_list<LogField> f = {}) { log(LogLevel::Error, e, f); }

private:
    std::string component_;
    std::FILE* sink_;
    LogLevel level_;
    std::chrono::steady_clock::time_point start_;
};

}  // namespace mordor
