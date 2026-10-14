// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/crash_guard.h"

#include "platform/platform.h"
#include "util/log.h"

#include <atomic>
#include <csignal>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

namespace xc::app::crash {

namespace {

char g_marker[512] = {};
char g_stage[96] = "starting";
std::atomic<uint64_t> g_beat{0};

const char* signalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGABRT: return "SIGABRT";
    default: return "?";
    }
}

// Async-signal-safe: open / write / close only, no allocation.
void writeMarker(const char* kind) {
    if (!g_marker[0]) return;
    int fd = ::open(g_marker, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return;
    auto put = [fd](const char* s) { (void)!::write(fd, s, std::strlen(s)); };
    put(kind);
    put("\n");
    put(g_stage);
    put("\n");
    ::close(fd);
}

void onSignal(int sig) {
    char kind[48] = "signal ";
    char num[8];
    int n = 0, v = sig;
    do num[n++] = static_cast<char>('0' + v % 10), v /= 10; while (v && n < 7);
    size_t k = std::strlen(kind);
    while (n) kind[k++] = num[--n];
    kind[k] = 0;
    std::strcat(kind, " (");
    std::strcat(kind, signalName(sig));
    std::strcat(kind, ")");
    writeMarker(kind);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

}  // namespace

void install(const std::string& markerPath) {
    std::strncpy(g_marker, markerPath.c_str(), sizeof g_marker - 1);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) std::signal(sig, onSignal);
    std::set_terminate([] {
        writeMarker("uncaught exception");
        std::abort();
    });
}

void setStage(const char* stage) {
    // Written before a crash can read it half-copied: the end first.
    char tmp[sizeof g_stage];
    std::strncpy(tmp, stage, sizeof tmp - 1);
    tmp[sizeof tmp - 1] = 0;
    std::memcpy(g_stage, tmp, sizeof tmp);
}

void heartbeat() { g_beat = platform::nowMs(); }

void startWatchdog(uint32_t stallMs) {
    heartbeat();
    std::thread([stallMs] {
        bool stalled = false;
        for (;;) {
            platform::sleepMs(1000);
            uint64_t idle = platform::nowMs() - g_beat.load();
            if (!stalled && idle > stallMs) {
                stalled = true;
                XC_LOGW("crash guard: the main loop has not run for %llu ms (%s)", static_cast<unsigned long long>(idle), g_stage);
                writeMarker("stalled");
            } else if (stalled && idle < 1000) {
                stalled = false;
                XC_LOGI("crash guard: the main loop is back");
                ::unlink(g_marker);
            }
        }
    }).detach();
}

Previous takePrevious(const std::string& markerPath) {
    Previous p;
    std::string text;
    if (!platform::readFile(markerPath, text)) return p;
    ::unlink(markerPath.c_str());
    size_t nl = text.find('\n');
    p.found = true;
    p.kind = text.substr(0, nl);
    if (nl != std::string::npos) {
        p.stage = text.substr(nl + 1);
        while (!p.stage.empty() && (p.stage.back() == '\n' || p.stage.back() == '\r')) p.stage.pop_back();
    }
    return p;
}

}  // namespace xc::app::crash
