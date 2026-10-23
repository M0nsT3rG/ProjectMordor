#include "common/log.hpp"

namespace mordor {
namespace {

std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

const char* level_name(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warn: return "warn";
        case LogLevel::Error: return "error";
    }
    return "?";
}

}  // namespace

Logger::Logger(std::string component, std::FILE* sink, LogLevel min_level)
    : component_(std::move(component)), sink_(sink), level_(min_level), start_(std::chrono::steady_clock::now()) {}

void Logger::log(LogLevel level, std::string_view event, std::initializer_list<LogField> fields) {
    if (!sink_ || level < level_) return;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_).count();
    std::string line = "{\"t_ms\":" + std::to_string(ms) + ",\"lvl\":\"" + level_name(level) + "\",\"comp\":\"" +
                       escape(component_) + "\",\"evt\":\"" + escape(event) + "\"";
    for (const auto& f : fields) {
        line += ",\"" + escape(f.key) + "\":";
        line += f.quoted ? "\"" + escape(f.value) + "\"" : f.value;
    }
    line += "}\n";
    std::fputs(line.c_str(), sink_);
    std::fflush(sink_);
}

}  // namespace mordor
