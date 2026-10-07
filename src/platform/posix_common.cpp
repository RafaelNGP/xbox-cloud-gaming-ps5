// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// File helpers shared by both platforms (the PS5 libc is POSIX enough).
#include "platform/platform.h"

#include <cstdio>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

namespace xc::platform {

bool readFile(const std::string& path, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool writeFileAtomic(const std::string& path, const std::string& data) {
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fflush(f) == 0) && ok;
    ok = (std::fclose(f) == 0) && ok;
    if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

uint64_t nowMs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec) / 1000000u;
}

uint64_t nowUs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000u + static_cast<uint64_t>(ts.tv_nsec) / 1000u;
}

namespace {
void* threadEntry(void* arg) {
    auto* fn = static_cast<std::function<void()>*>(arg);
    (*fn)();
    delete fn;
    return nullptr;
}
}  // namespace

bool startThread(Thread& t, std::function<void()> fn, size_t stackBytes) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stackBytes);
    auto* heap = new std::function<void()>(std::move(fn));
    auto* tid = new pthread_t;
    int rc = pthread_create(tid, &attr, threadEntry, heap);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        delete heap;
        delete tid;
        return false;
    }
    t.handle = tid;
    return true;
}

void Thread::join() {
    if (!handle) return;
    auto* tid = static_cast<pthread_t*>(handle);
    pthread_join(*tid, nullptr);
    delete tid;
    handle = nullptr;
}

void sleepMs(uint32_t ms) { usleep(static_cast<useconds_t>(ms) * 1000u); }

}  // namespace xc::platform
