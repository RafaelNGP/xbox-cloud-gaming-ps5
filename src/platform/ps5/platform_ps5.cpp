// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "platform/platform.h"
#include "util/log.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdlib.h> // arc4random_buf

#include <arpa/inet.h>
#include <atomic>
#include <netinet/in.h>
#include <sys/socket.h>

extern "C" {
struct SceKernelSwVersion {
    uint64_t size;
    char versionString[0x1C];
    uint32_t version;
};
int sceKernelGetSystemSwVersion(SceKernelSwVersion* version);
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
// User data (sign-in, settings, caches, log): its own folder, apart from the
// app's, which installers replace or delete. Before 0.9.7 it was the app's.
constexpr const char* kDataDir = "/data/psbox";
constexpr const char* kLegacyDir = "/data/homebrew/PPSA99810";
std::string g_dataDir = kDataDir;
std::string g_dataNote;
std::atomic<int> g_netPool{-1};
}

// True when files can be made in `dir` (made first if missing).
bool writable(const std::string& dir) {
    struct stat st{};
    if (::stat(dir.c_str(), &st) != 0 && ::mkdir(dir.c_str(), 0777) != 0) return false;
    std::string probe = dir + "/.write-test";
    if (!writeFileAtomic(probe, "ok")) return false;
    ::unlink(probe.c_str());
    return true;
}

// Once: the user's files of a version before 0.9.7, from the app's folder
// (copied, then its sign-in removed there: one copy of the token). Not
// again after (.migrated): a sign-out later must not bring them back.
void migrateLegacy() {
    const std::string data = kDataDir, legacy = kLegacyDir, done = data + "/.migrated";
    std::string text;
    if (readFile(done, text)) return;
    int moved = 0;
    for (const char* name : {"account.json", "settings.json", "library.json", "prices.json"}) {
        std::string mine;
        if (readFile(legacy + "/" + name, text) && !readFile(data + "/" + name, mine) &&
            writeFileAtomic(data + "/" + name, text))
            ++moved;
    }
    if (readFile(data + "/account.json", text)) ::unlink((legacy + "/account.json").c_str());
    writeFileAtomic(done, "1\n");
    if (moved) g_dataNote = "moved " + std::to_string(moved) + " file(s) from " + legacy + " to " + data;
}

bool init() {
    if (writable(kDataDir)) {
        migrateLegacy();
    } else if (writable(kLegacyDir)) {  // as before 0.9.7
        int err = errno;
        g_dataDir = kLegacyDir;
        g_dataNote = std::string(kDataDir) + " not writable (errno " + std::to_string(err) + "); using " + kLegacyDir;
    } else {
        g_dataNote = std::string("neither ") + kDataDir + " nor " + kLegacyDir + " is writable: nothing will be kept";
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

std::string dataDirNote() { return g_dataNote; }

std::string appDir() {
    // /app0 is the app's own folder; its real path where it has been
    // tested to take updates (renames over the running eboot.bin).
    struct stat legacy {}, running {};
    if (::stat((std::string(kLegacyDir) + "/eboot.bin").c_str(), &legacy) == 0 &&
        ::stat("/app0/eboot.bin", &running) == 0 && legacy.st_ino == running.st_ino)
        return kLegacyDir;
    return "/app0";
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

std::string firmwareVersion() {
    // The kernel's: 0x13600007 is 13.60 (the version the console's settings
    // show). sceKernelGetSystemSwVersion said "13.590.001" on a 13.60 console.
    uint32_t sdk = 0;
    size_t len = sizeof sdk;
    if (sysctlbyname("kern.sdk_version", &sdk, &len, nullptr, 0) == 0 && len == sizeof sdk && sdk) {
        char text[16];
        std::snprintf(text, sizeof text, "%x.%02x", sdk >> 24, (sdk >> 16) & 0xFF);
        return text;
    }
    SceKernelSwVersion v{};
    v.size = sizeof v;
    if (sceKernelGetSystemSwVersion(&v) != 0) return {};
    v.versionString[sizeof v.versionString - 1] = 0;
    std::string s = v.versionString;
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    return s;
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
