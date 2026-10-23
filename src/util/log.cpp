// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "util/log.h"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

namespace xc::log {

namespace {
std::mutex g_mutex;
std::atomic<Level> g_level{Level::Info};
FILE* g_file = nullptr;
std::string g_recentLogs;
constexpr size_t kMaxRecentLogs = 64 * 1024;  // 64 KiB ring buffer

std::string g_lastMsg;
Level g_lastLevel = Level::Info;
bool g_hasLastMsg = false;
size_t g_repeatCount = 0;

const char* tag(Level l) {
    switch (l) {
        case Level::Debug: return "D";
        case Level::Info: return "I";
        case Level::Warn: return "W";
        case Level::Error: return "E";
    }
    return "?";
}

void appendRecentLocked(const char* line, size_t lineLen) {
    if (lineLen == 0) return;
    g_recentLogs.append(line, lineLen);
    if (g_recentLogs.size() > kMaxRecentLogs) {
        size_t excess = g_recentLogs.size() - kMaxRecentLogs;
        size_t nextNl = g_recentLogs.find('\n', excess);
        if (nextNl != std::string::npos && nextNl + 1 < g_recentLogs.size()) {
            g_recentLogs.erase(0, nextNl + 1);
        } else {
            g_recentLogs.erase(0, excess);
        }
    }
}

void flushRepeatsLocked() {
    if (g_repeatCount > 0) {
        char repLine[128];
        int len = std::snprintf(repLine, sizeof repLine, "   [last message repeated %zu %s]\n",
                                g_repeatCount, g_repeatCount == 1 ? "time" : "times");
        if (len > 0) {
            std::fprintf(stderr, "%s", repLine);
            if (g_file) {
                std::fputs(repLine, g_file);
                std::fflush(g_file);
            }
            appendRecentLocked(repLine, static_cast<size_t>(len));
        }
        g_repeatCount = 0;
    }
    g_hasLastMsg = false;
    g_lastMsg.clear();
}

struct LogCleanup {
    ~LogCleanup() {
        std::lock_guard<std::mutex> lock(g_mutex);
        flushRepeatsLocked();
        if (g_file) {
            std::fclose(g_file);
            g_file = nullptr;
        }
    }
};
static LogCleanup s_cleanup;

}  // namespace

void setLevel(Level level) { g_level = level; }

bool setFile(const char* path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    flushRepeatsLocked();
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
    if (!path) return true;
    g_file = std::fopen(path, "a");
    return g_file != nullptr;
}

std::string recentLogs() {
    std::lock_guard<std::mutex> lock(g_mutex);
    flushRepeatsLocked();
    return g_recentLogs;
}

void flush() {
    std::lock_guard<std::mutex> lock(g_mutex);
    flushRepeatsLocked();
}

void write(Level level, const char* fmt, ...) {
    if (level < g_level) return;
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);  // std::localtime's result is shared by every thread
    char ts[32];
    std::strftime(ts, sizeof ts, "%H:%M:%S", &local);

    char line[2304];
    int lineLen = std::snprintf(line, sizeof line, "%s [%s] %s\n", ts, tag(level), msg);

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_hasLastMsg && level == g_lastLevel && g_lastMsg == msg) {
        ++g_repeatCount;
        return;
    }
    flushRepeatsLocked();
    g_lastMsg = msg;
    g_lastLevel = level;
    g_hasLastMsg = true;

    std::fprintf(stderr, "%s", line);
    if (g_file) {
        std::fputs(line, g_file);
        std::fflush(g_file);
    }
    appendRecentLocked(line, static_cast<size_t>(lineLen));
}

}  // namespace xc::log
