// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "platform/platform.h"
#include "util/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#include <stdlib.h> // arc4random_buf

#include <arpa/inet.h>
#include <atomic>
#include <netinet/in.h>
#include <sys/socket.h>

extern "C" {
int sceSystemServiceLoadExec(const char* path, char* const argv[]);
int sceKernelSendNotificationRequest(int device, void* request, size_t size, int blocking);
int sceNetInit(void);
int sceNetPoolCreate(const char* name, int size, int flags);
int sceNetResolverCreate(const char* name, int memid, int flags);
int sceNetResolverStartNtoa(int rid, const char* hostname, uint32_t* addr, int timeout, int retry, int flags);
int sceNetResolverDestroy(int rid);
}

namespace xc::platform {

namespace {
std::string g_dataDir = "/data/homebrew/PPSA99810";
std::atomic<int> g_netPool{-1};
}

bool init() {
    // If running in /data/homebrew/PPSA99810 or /app0
    struct stat st{};
    if (::stat(g_dataDir.c_str(), &st) != 0) {
        ::mkdir(g_dataDir.c_str(), 0777);
    }
    // libSceNet's resolver: 32 KiB pool.
    int rc = sceNetInit();
    if (rc < 0) XC_LOGW("sceNetInit: 0x%08x", static_cast<unsigned>(rc));
    int pool = sceNetPoolCreate("xcloud", 32 * 1024, 0);
    if (pool < 0) XC_LOGE("sceNetPoolCreate: 0x%08x (host names will not resolve)", static_cast<unsigned>(pool));
    g_netPool = pool;
    return true;
}

void shutdown() {}

std::string dataDir() {
    return g_dataDir;
}

std::string assetDir() { return "/app0/assets"; }

std::string caBundlePath() {
    // Shipped in /app0/assets/cacert.pem
    return "/app0/assets/cacert.pem";
}

bool randomBytes(void* out, size_t len) {
    arc4random_buf(out, len);
    return true;
}

bool resolveIPv4(const std::string& host, uint32_t& addr, std::string& err) {
    in_addr literal{};
    if (::inet_pton(AF_INET, host.c_str(), &literal) == 1) {
        addr = literal.s_addr;
        return true;
    }
    int pool = g_netPool.load();
    if (pool < 0) {
        err = "DNS lookup failed for " + host + ": networking not initialised";
        return false;
    }
    int rid = sceNetResolverCreate("xcloud", pool, 0);
    if (rid < 0) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "sceNetResolverCreate 0x%08x", static_cast<unsigned>(rid));
        err = "DNS lookup failed for " + host + ": " + buf;
        return false;
    }
    uint32_t a = 0;
    // timeout/retry 0: the library defaults.
    int rc = sceNetResolverStartNtoa(rid, host.c_str(), &a, 0, 0, 0);
    sceNetResolverDestroy(rid);
    if (rc < 0) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "0x%08x", static_cast<unsigned>(rc));
        err = "DNS lookup failed for " + host + " (" + buf + ")";
        return false;
    }
    addr = a;  // network byte order
    return true;
}

} // namespace xc::platform

// For the getaddrinfo replacement in libc_compat.c.
extern "C" int xc_resolve_ipv4(const char* host, uint32_t* addr) {
    std::string err;
    if (xc::platform::resolveIPv4(host, *addr, err)) return 0;
    XC_LOGW("%s", err.c_str());
    return -1;
}

namespace xc::platform {

bool restartApp() {
    // Replaces this process with /app0/eboot.bin (the console showed it
    // takes under a second, and the new eboot.bin is the one that starts).
    char* argv[] = {nullptr};
    int rc = sceSystemServiceLoadExec("/app0/eboot.bin", argv);
    XC_LOGE("restart: sceSystemServiceLoadExec returned 0x%08x", static_cast<unsigned>(rc));
    return false;
}

void notify(const std::string& text) { notify(text, text); }

void notify(const std::string& text, const std::string& forLog) {
    XC_LOGI("NOTIFY: %s", forLog.c_str());
    // Same request layout as PS5_Vulkan's demo renderer.
    struct NotificationRequest {
        uint8_t reserved[45];
        char message[3075];
    } req{};
    std::snprintf(req.message, sizeof req.message, "%s", text.c_str());
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

} // namespace xc::platform
