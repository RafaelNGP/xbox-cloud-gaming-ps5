#include "util/log.h"

#include <cstdio>
#include <ctime>
#include <mutex>

namespace xc::log {

namespace {
std::mutex g_mutex;
Level g_level = Level::Info;
FILE* g_file = nullptr;

const char* tag(Level l) {
    switch (l) {
        case Level::Debug: return "D";
        case Level::Info: return "I";
        case Level::Warn: return "W";
        case Level::Error: return "E";
    }
    return "?";
}
}  // namespace

void setLevel(Level level) { g_level = level; }

void setFile(const char* path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) std::fclose(g_file);
    g_file = path ? std::fopen(path, "a") : nullptr;
}

void write(Level level, const char* fmt, ...) {
    if (level < g_level) return;
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    std::time_t now = std::time(nullptr);
    char ts[32];
    std::strftime(ts, sizeof ts, "%H:%M:%S", std::localtime(&now));

    std::lock_guard<std::mutex> lock(g_mutex);
    std::fprintf(stderr, "%s [%s] %s\n", ts, tag(level), msg);
    if (g_file) {
        std::fprintf(g_file, "%s [%s] %s\n", ts, tag(level), msg);
        std::fflush(g_file);
    }
}

}  // namespace xc::log
