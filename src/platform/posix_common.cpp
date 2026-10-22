// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// File helpers shared by both platforms (the PS5 libc is POSIX enough).
#include "platform/platform.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
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
    int fd = ::fileno(f);
    if (fd >= 0) {
        ok = (::fsync(fd) == 0) && ok;
    }
    ok = (std::fclose(f) == 0) && ok;
    if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
        int err = errno;
        std::remove(tmp.c_str());
        errno = err;
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

std::string localIPv4() {
    std::string candidate;
    ifaddrs* ifa = nullptr;
    if (::getifaddrs(&ifa) == 0) {
        for (ifaddrs* i = ifa; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
            if ((i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP)) continue;
            char ip[INET_ADDRSTRLEN];
            auto* sa = reinterpret_cast<sockaddr_in*>(i->ifa_addr);
            if (::inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip))) {
                std::string s = ip;
                if (s != "127.0.0.1" && s.rfind("169.254.", 0) != 0) {
                    if (s.rfind("192.168.", 0) == 0 || s.rfind("10.", 0) == 0 || s.rfind("172.", 0) == 0) {
                        candidate = s;
                        break;
                    }
                    if (candidate.empty()) candidate = s;
                }
            }
        }
        ::freeifaddrs(ifa);
    }
    if (!candidate.empty()) return candidate;

    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s >= 0) {
        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(80);
        ::inet_pton(AF_INET, "8.8.8.8", &dst.sin_addr);
        if (::connect(s, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) == 0) {
            sockaddr_in local{};
            socklen_t len = sizeof(local);
            if (::getsockname(s, reinterpret_cast<sockaddr*>(&local), &len) == 0) {
                char ip[INET_ADDRSTRLEN];
                if (::inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip))) {
                    candidate = ip;
                }
            }
        }
        ::close(s);
    }
    return candidate.empty() ? "127.0.0.1" : candidate;
}

}  // namespace xc::platform
