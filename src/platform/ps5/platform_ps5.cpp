// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "platform/platform.h"
#include "platform/toasts.h"
#include "util/log.h"

#include <algorithm>
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
#include <pthread.h>
#include <pthread_np.h>
#include <sched.h>
#include <sys/cpuset.h>
#include <string>
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

struct SceNetInAddr {
    uint32_t s_addr;
};

union SceNetAddrUnion {
    SceNetInAddr addr;
    uint8_t addr6[16];
};

struct SceNetResolverAddr {
    SceNetAddrUnion u;
    uint32_t af;
    uint32_t reserved[3];
};

struct SceNetResolverInfo {
    SceNetResolverAddr addrs[10];
    uint32_t records;
    uint32_t recordsv4;
    uint32_t reserved[14];
};

int sceNetResolverStartNtoaMultipleRecords(int rid, const char* hostname, SceNetResolverInfo* info, int timeout, int retry, int flags);
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
bool g_dataDirWritable = false;
int g_dataDirErrno = 0;
std::atomic<int> g_netPool{-1};
}

// True when files can be made in `dir` (made first if missing).
bool writable(const std::string& dir, int& errOut) {
    errOut = 0;
    struct stat st{};
    if (::stat(dir.c_str(), &st) != 0) {
        if (::mkdir(dir.c_str(), 0777) != 0) {
            errOut = errno;
            return false;
        }
    } else {
        // Folder exists: ensure permissions allow writing by uid 1
        ::chmod(dir.c_str(), 0777);
    }
    std::string probe = dir + "/.write-test";
    if (!writeFileAtomic(probe, "ok")) {
        errOut = errno;
        return false;
    }
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
    for (const char* name : {"account.json", "settings.json", "library.json", "library_cache.json", "prices.json"}) {
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
    // 1. Initialize networking early so we can trigger early unjail via localhost (127.0.0.1:8084)
    int rc = sceNetInit();
    if (rc < 0) XC_LOGW("sceNetInit: 0x%08x", static_cast<unsigned>(rc));
    int pool = sceNetPoolCreate("xcloud", 32 * 1024, 0);
    if (pool < 0) XC_LOGE("sceNetPoolCreate: 0x%08x (host names will not resolve)", static_cast<unsigned>(pool));
    g_netPool = pool;

    // 2. Trigger early unjail via elfldr / payload manager (127.0.0.1:8084)
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s >= 0) {
        struct sockaddr_in sin{};
        sin.sin_family = AF_INET;
        sin.sin_port = htons(8084);
        sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        struct timeval tv{0, 250000}; // 250ms
        ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (::connect(s, reinterpret_cast<struct sockaddr*>(&sin), sizeof(sin)) == 0) {
            const char req[] = "GET /loadpayload:/data/pldmgr/payloads/patch_test/patch_test.elf HTTP/1.1\r\nHost: 127.0.0.1:8084\r\nConnection: close\r\n\r\n";
            ::send(s, req, sizeof(req) - 1, 0);
            char buf[128];
            ::recv(s, buf, sizeof(buf), 0);
        }
        ::close(s);
        ::usleep(150000); // 150ms for payload to unjail PID
    }

    int errPsbox = 0;
    int errLegacy = 0;
    bool ok = false;
    for (int retry = 0; retry < 30; ++retry) {
        if (writable(kDataDir, errPsbox)) {
            g_dataDir = kDataDir;
            g_dataDirWritable = true;
            migrateLegacy();
            ok = true;
            break;
        }
        ::usleep(100000); // 100ms * 30 = up to 3s wait for unjail
    }

    if (!ok) {
        if (writable(kLegacyDir, errLegacy)) {  // as before 0.9.7
            g_dataDirWritable = true;
            g_dataDir = kLegacyDir;
            g_dataNote = std::string(kDataDir) + " not writable (errno " + std::to_string(errPsbox) + "); using " + kLegacyDir;
        } else {
            // Try fallback directories on PS5 sandbox
            bool found = false;
            for (const char* fb : {"/download0/psbox", "/user/home/psbox", "/temp0/psbox"}) {
                int errFb = 0;
                if (writable(fb, errFb)) {
                    g_dataDir = fb;
                    g_dataDirWritable = true;
                    g_dataNote = std::string("neither ") + kDataDir + " (errno " + std::to_string(errPsbox) +
                                 ") nor " + kLegacyDir + " (errno " + std::to_string(errLegacy) +
                                 ") writable; fallback to " + fb;
                    found = true;
                    break;
                }
            }
            if (!found) {
                g_dataDirErrno = errPsbox ? errPsbox : errLegacy;
                g_dataDirWritable = false;
                g_dataNote = std::string("neither ") + kDataDir + " (errno " + std::to_string(errPsbox) +
                             ") nor " + kLegacyDir + " (errno " + std::to_string(errLegacy) +
                             ") is writable: nothing will be kept";
            }
        }
    }
    XC_LOGI("platform ps5: storage dir=%s (writable=%d, errno=%d, note=%s)",
            g_dataDir.c_str(), g_dataDirWritable ? 1 : 0, g_dataDirErrno, g_dataNote.c_str());
    return true;
}

void shutdown() {}

std::string dataDir() {
    return g_dataDir;
}

std::string dataDirNote() { return g_dataNote; }
bool dataDirWritable() { return g_dataDirWritable; }
int dataDirErrno() { return g_dataDirErrno; }

std::string appDir() {
    // /app0 is the app's own folder; its real path where it has been
    // tested to take updates (renames over the running eboot.bin).
    struct stat legacy {}, running {};
    if (::stat("/app0/eboot.bin", &running) == 0) {
        if (::stat((std::string(kLegacyDir) + "/eboot.bin").c_str(), &legacy) == 0 &&
            legacy.st_ino == running.st_ino)
            return kLegacyDir;
        return "/app0";
    }
    if (::stat((std::string(kLegacyDir) + "/eboot.bin").c_str(), &legacy) == 0)
        return kLegacyDir;
    return "/app0";
}

std::string assetDir() {
    struct stat st{};
    if (::stat("/app0/assets/fonts/Inter-Regular.ttf", &st) == 0) return "/app0/assets";
    std::string legacyAssets = std::string(kLegacyDir) + "/assets";
    if (::stat((legacyAssets + "/fonts/Inter-Regular.ttf").c_str(), &st) == 0) return legacyAssets;
    return appDir() + "/assets";
}

std::string caBundlePath() {
    struct stat st{};
    std::string p = assetDir() + "/cacert.pem";
    if (::stat(p.c_str(), &st) == 0) return p;
    return "/app0/assets/cacert.pem";
}

bool randomBytes(void* out, size_t len) {
    arc4random_buf(out, len);
    return true;
}

bool resolveIPv4(const std::string& host, std::vector<uint32_t>& addrs, std::string& err) {
    in_addr literal{};
    if (::inet_pton(AF_INET, host.c_str(), &literal) == 1) {
        addrs.push_back(literal.s_addr);
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
    SceNetResolverInfo info{};
    int rc = sceNetResolverStartNtoaMultipleRecords(rid, host.c_str(), &info, 5000000, 2, 0);
    if (rc >= 0) {
        uint32_t count = std::min<uint32_t>(info.records, 10);
        for (uint32_t i = 0; i < count; ++i) {
            if (info.addrs[i].af == AF_INET && info.addrs[i].u.addr.s_addr != 0) {
                uint32_t a = info.addrs[i].u.addr.s_addr;
                if (std::find(addrs.begin(), addrs.end(), a) == addrs.end()) {
                    addrs.push_back(a);
                }
            }
        }
    }
    if (addrs.empty()) {
        uint32_t a = 0;
        rc = sceNetResolverStartNtoa(rid, host.c_str(), &a, 5000000, 2, 0);
        if (rc >= 0 && a != 0) {
            addrs.push_back(a);
        }
    }
    sceNetResolverDestroy(rid);
    if (addrs.empty()) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "0x%08x", static_cast<unsigned>(rc));
        err = "DNS lookup failed for " + host + " (" + buf + ")";
        return false;
    }
    return true;
}

bool resolveIPv4(const std::string& host, uint32_t& addr, std::string& err) {
    std::vector<uint32_t> addrs;
    if (!resolveIPv4(host, addrs, err)) return false;
    addr = addrs[0];
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

std::string diagnostics() { return {}; }

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

std::string platformName() {
    return "PlayStation 5";
}

size_t imageCacheBudget() {
    return 160u << 20;
}

// One user per app sandbox: nothing to hide the sign-in from.
void makePrivate(const std::string&) {}

void notify(const std::string& text) { notify(text, text); }

void notify(const std::string& text, const std::string& forLog) {
    XC_LOGI("NOTIFY: %s", forLog.c_str());
    if (toastsEnabled() && forLog != "signed in (notification)") toasts().push(text, nowMs());
    // Same request layout as PS5_Vulkan's demo renderer.
    struct NotificationRequest {
        uint8_t reserved[45];
        char message[3075];
    } req{};
    std::snprintf(req.message, sizeof req.message, "%s", text.c_str());
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

namespace {
// The threads keep the scheduler's choice unless setNoThreadPinning(false):
// measured in Fortnite (2026-11-04), pinning by role did nothing for the
// video and made the sound run dry more often (the four audio threads then
// share one core). It never took before that day either: the cpuset query
// failed with the SDK's mask size.
std::atomic<bool> g_noThreadPinning{true};
}  // namespace

void setNoThreadPinning(bool on) { g_noThreadPinning = on; }
bool isHandheld() { return false; }
void forceHandheld(bool) {}

void setThreadAffinity(ThreadRole role, const char* name) {
    if (name && *name) {
        pthread_set_name_np(pthread_self(), name);
    }
    static cpuset_t s_procMask;
    static bool s_procMaskQueried = false;
    static int s_allowedCpus[32];
    static int s_numAllowedCpus = 0;
    // The kernel's cpuset is smaller than the SDK's (256 CPUs): the full
    // size gets ERANGE, so the sizes it may take are tried down to 64 CPUs.
    static size_t s_maskSize = sizeof(cpuset_t);

    if (!s_procMaskQueried) {
        s_procMaskQueried = true;
        int grc = -1;
        for (size_t size : {sizeof(cpuset_t), size_t{16}, size_t{8}}) {
            if (size > sizeof(cpuset_t)) continue;
            CPU_ZERO(&s_procMask);
            grc = pthread_getaffinity_np(pthread_self(), size, &s_procMask);
            if (grc == 0) {
                s_maskSize = size;
                break;
            }
        }
        if (grc == 0) {
            for (int i = 0; i < 32; ++i) {
                if (CPU_ISSET(i, &s_procMask)) {
                    s_allowedCpus[s_numAllowedCpus++] = i;
                }
            }
        }
        std::string list;
        for (int i = 0; i < s_numAllowedCpus; ++i) list += (i ? "," : "") + std::to_string(s_allowedCpus[i]);
        XC_LOGI("ps5 cpuset: %d allowed cpus (%s), mask of %zu bytes (grc=%d)%s", s_numAllowedCpus, list.c_str(),
                s_maskSize, grc, g_noThreadPinning ? "; threads left unpinned" : "");
    }

    if (s_numAllowedCpus <= 1 || g_noThreadPinning) {
        return;
    }

    cpuset_t mask;
    CPU_ZERO(&mask);
    auto setSlot = [&](int slotIndex) {
        int idx = s_allowedCpus[slotIndex % s_numAllowedCpus];
        CPU_SET(idx, &mask);
    };

    switch (role) {
    case ThreadRole::Main:
        setSlot(0);
        if (s_numAllowedCpus >= 8) setSlot(1);
        break;
    case ThreadRole::Network:
        setSlot(1);
        break;
    case ThreadRole::VideoDecode:
        setSlot(2);
        if (s_numAllowedCpus >= 4) setSlot(3);
        break;
    case ThreadRole::Audio:
        setSlot(s_numAllowedCpus >= 6 ? 4 : 1);
        break;
    case ThreadRole::Worker:
        if (s_numAllowedCpus >= 6) {
            setSlot(5);
            if (s_numAllowedCpus >= 7) setSlot(6);
        } else {
            setSlot(s_numAllowedCpus - 1);
        }
        break;
    }
    int rc = pthread_setaffinity_np(pthread_self(), s_maskSize, &mask);
    if (rc != 0) {
        XC_LOGW("pthread_setaffinity_np(%s): %d", name ? name : "thread", rc);
    } else {
        XC_LOGI("thread %s pinned to role %d", name ? name : "unknown", static_cast<int>(role));
    }
}

void setThreadRealtimePriority() {
    struct sched_param param{};
    param.sched_priority = sched_get_priority_max(SCHED_RR);
    if (param.sched_priority <= 0) param.sched_priority = 50;
    int rc = pthread_setschedparam(pthread_self(), SCHED_RR, &param);
    if (rc != 0) {
        XC_LOGW("pthread_setschedparam(SCHED_RR, prio=%d): %d", param.sched_priority, rc);
    } else {
        XC_LOGI("thread elevated to SCHED_RR (prio=%d)", param.sched_priority);
    }
}

} // namespace xc::platform
