// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "platform/platform.h"
#include "platform/toasts.h"
#include "util/log.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <orbis/NetCtl.h>

#include <arpa/inet.h>
#include <atomic>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <pthread_np.h>
#include <cstdarg>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>

extern "C" {
int sceNetIoctl(int s, unsigned long cmd, void* data);
int sceNetSocket(const char* name, int domain, int type, int protocol);
int sceNetBind(int s, const void* addr, unsigned int addrlen);
int sceNetGetsockname(int s, void* addr, unsigned int* addrlen);
int sceNetSendto(int s, const void* msg, size_t len, int flags, const void* to, unsigned int tolen);
int sceNetRecvfrom(int s, void* buf, size_t len, int flags, void* from, unsigned int* fromlen);
int sceNetSend(int s, const void* msg, size_t len, int flags);
int sceNetRecv(int s, void* buf, size_t len, int flags);
int sceNetConnect(int s, const void* addr, unsigned int addrlen);
int sceNetListen(int s, int backlog);
int sceNetAccept(int s, void* addr, unsigned int* addrlen);
int sceNetSetsockopt(int s, int level, int optname, const void* optval, unsigned int optlen);
int sceNetGetsockopt(int s, int level, int optname, void* optval, unsigned int* optlen);
int sceNetSocketClose(int s);
int* sceNetErrnoLoc(void);
int _ioctl(int fd, unsigned long request, void* arg);
int _close(int fd);
int sceSysmoduleLoadModule(uint32_t id);
int sceSysmoduleLoadModuleInternal(uint32_t id);
int sceRandomGetRandomNumber(void* buf, size_t size);
int scePthreadCreate(pthread_t* thread, const pthread_attr_t* attr, void* (*entry)(void*), void* arg, const char* name);
pthread_t scePthreadSelf(void);
int scePthreadSetaffinity(pthread_t thread, uint64_t mask);
int scePthreadSetprio(pthread_t thread, int prio);
int sceKernelAvailableFlexibleMemorySize(size_t* sizeOut);

static std::mutex s_sockMutex;
static std::unordered_map<int, uint16_t> s_boundPorts;
static std::unordered_set<int> s_openSockets;

static bool isSocket(int fd) {
    if (fd < 0) return false;
    std::lock_guard<std::mutex> lock(s_sockMutex);
    return s_openSockets.find(fd) != s_openSockets.end();
}

static inline int getNetErrno(int ret) {
    uint32_t uret = static_cast<uint32_t>(ret);
    if ((uret & 0xFFFFFE00) == 0x80410000) {
        return static_cast<int>(uret & 0xFF);
    }
    int* err = sceNetErrnoLoc();
    if (err && *err > 0) {
        return *err;
    }
    if (ret < 0 && ret > -256) {
        return -ret;
    }
    return ret < 0 ? -ret : 0;
}

static inline int sanitizeNetFlags(int flags) {
    return flags & 0xC3;
}

static int setSocketNonBlock(int fd, int on) {
    if (int* err = sceNetErrnoLoc()) *err = 0;
    int rcNbio = sceNetSetsockopt(fd, 0xffff /*SOL_SOCKET*/, 0x1200 /*SO_NBIO*/, &on, sizeof(on));
    int errNbio = (rcNbio < 0) ? getNetErrno(rcNbio) : 0;

    if (int* err = sceNetErrnoLoc()) *err = 0;
    int rcIoctl = sceNetIoctl(fd, 0x8004667eUL /*FIONBIO*/, &on);
    int errIoctl = (rcIoctl < 0) ? getNetErrno(rcIoctl) : 0;

    if (rcNbio < 0 && rcIoctl < 0) {
        if (errNbio == EBADF || errNbio == ENOTSOCK ||
            errIoctl == EBADF || errIoctl == ENOTSOCK) {
            if (int* err = sceNetErrnoLoc()) *err = 0;
            return -1;
        }
    }
    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        XC_LOGI("net: setSocketNonBlock(fd=%d, on=%d): SO_NBIO=0x%08x, sceNetIoctl=0x%08x",
                fd, on, static_cast<unsigned>(rcNbio), static_cast<unsigned>(rcIoctl));
    }
    if (rcNbio >= 0 || rcIoctl >= 0) {
        if (int* err = sceNetErrnoLoc()) *err = 0;
        return 0;
    }
    if (on != 0) {
        int us = 50000;
        if (sceNetSetsockopt(fd, 0xffff /*SOL_SOCKET*/, 0x1006 /*SO_RCVTIMEO*/, &us, sizeof(us)) < 0) {
            struct timeval tv{0, 50000};
            sceNetSetsockopt(fd, 0xffff /*SOL_SOCKET*/, 0x1006 /*SO_RCVTIMEO*/, &tv, sizeof(tv));
        }
    }
    if (int* err = sceNetErrnoLoc()) *err = 0;
    return 0;
}

int ioctl(int fd, unsigned long request, ...) {
    void* arg = nullptr;
    va_list ap;
    va_start(ap, request);
    arg = va_arg(ap, void*);
    va_end(ap);

    if (request == 0x5421 || request == 0x8004667eUL || request == FIONBIO) {
        int on = arg ? (*reinterpret_cast<const int*>(arg) ? 1 : 0) : 1;
        setSocketNonBlock(fd, on);
        return 0;
    }

    // On PS4/shadPS4, socket ioctls like FIONREAD must be routed to sceNetIoctl.
    int rc = sceNetIoctl(fd, request, arg);
    if (rc >= 0) {
        return rc;
    }
    if (int* err = sceNetErrnoLoc()) *err = 0;
    return _ioctl(fd, request, arg);
}

int fcntl(int fd, int cmd, ...) {
    void* arg = nullptr;
    va_list ap;
    va_start(ap, cmd);
    arg = va_arg(ap, void*);
    va_end(ap);

    if (cmd == F_SETFL) {
        long flags = reinterpret_cast<intptr_t>(arg);
        int on = (flags & O_NONBLOCK) ? 1 : 0;
        if (isSocket(fd)) {
            return setSocketNonBlock(fd, on);
        }
    } else if (cmd == F_GETFL) {
        typedef int (*RealFcntlFn)(int, int, ...);
        static RealFcntlFn real_fcntl = nullptr;
        if (!real_fcntl) {
            real_fcntl = reinterpret_cast<RealFcntlFn>(dlsym(RTLD_NEXT, "fcntl"));
        }
        if (real_fcntl) {
            int ret = real_fcntl(fd, cmd);
            if (ret >= 0) return ret;
        }
        return 0;
    }

    typedef int (*RealFcntlFn)(int, int, ...);
    static RealFcntlFn real_fcntl = nullptr;
    if (!real_fcntl) {
        real_fcntl = reinterpret_cast<RealFcntlFn>(dlsym(RTLD_NEXT, "fcntl"));
    }
    if (real_fcntl) {
        return real_fcntl(fd, cmd, arg);
    }
    return 0;
}


// The OpenOrbis headers carry musl's _SC_* numbering, but the only sysconf() the linker
// finds is Sony's libkernel one (FreeBSD numbering). libc++'s hardware_concurrency()
// calls sysconf(84): musl's _SC_NPROCESSORS_ONLN, FreeBSD's _SC_THREAD_CPUTIME. Whatever
// libkernel answers then becomes the size of libdatachannel's ThreadPool (rtc::impl::Init
// spawns max(hardware_concurrency(), MIN_THREADPOOL_SIZE) threads at the first
// PeerConnection). 4XCLOUD links musl's own sysconf for this; here the musl numbers are
// answered directly. hardware_concurrency() is the only caller in the binary.
long sysconf(int name) {
    switch (name) {
    case 83:  // musl _SC_NPROCESSORS_CONF
    case 84:  // musl _SC_NPROCESSORS_ONLN
        return 6;  // cores 0-5 are the title's (the affinity masks use 0x3F)
    case 30:  // musl _SC_PAGESIZE
        return 16384;
    case 2:  // musl _SC_CLK_TCK
        return 100;
    case 4:  // musl _SC_OPEN_MAX
        return 1024;
    default:
        errno = EINVAL;
        return -1;
    }
}

static std::atomic<int> s_threadsCreated{0};
static std::atomic<int> s_threadsLive{0};
static std::atomic<int> s_threadFailures{0};

// Counts the threads still running: the key's destructor runs when a thread ends,
// whether it returns or calls pthread_exit.
static pthread_key_t s_liveKey;
static std::once_flag s_liveKeyOnce;
static bool s_liveKeyOk = false;

struct ThreadStart {
    void* (*fn)(void*);
    void* arg;
};

static void* threadTrampoline(void* p) {
    ThreadStart start = *static_cast<ThreadStart*>(p);
    delete static_cast<ThreadStart*>(p);
    if (s_liveKeyOk) {
        s_threadsLive.fetch_add(1, std::memory_order_relaxed);
        pthread_setspecific(s_liveKey, reinterpret_cast<void*>(1));
    }
    return start.fn(start.arg);
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*start_routine)(void*), void* arg) {
    if (!thread || !start_routine) {
        return EINVAL;
    }
    std::call_once(s_liveKeyOnce, [] {
        s_liveKeyOk = pthread_key_create(&s_liveKey, [](void*) {
            s_threadsLive.fetch_sub(1, std::memory_order_relaxed);
        }) == 0;
    });

    pthread_attr_t customAttr;
    bool createdAttr = false;

    if (!attr) {
        if (pthread_attr_init(&customAttr) == 0) {
            pthread_attr_setstacksize(&customAttr, 1024 * 1024); // 1 MB stack
            attr = &customAttr;
            createdAttr = true;
        }
    }

    typedef int (*RealPthreadCreateFn)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
    static RealPthreadCreateFn real_pthread_create = nullptr;
    static bool s_resolved = false;
    if (!s_resolved) {
        s_resolved = true;
        void* sym = dlsym(RTLD_NEXT, "pthread_create");
        if (sym && sym != reinterpret_cast<void*>(&pthread_create)) {
            real_pthread_create = reinterpret_cast<RealPthreadCreateFn>(sym);
        }
        XC_LOGI("threads: created with %s, %ld cores reported, default stack 1 MB",
                real_pthread_create ? "libkernel pthread_create (dlsym)" : "scePthreadCreate",
                sysconf(84 /*musl _SC_NPROCESSORS_ONLN*/));
    }

    size_t stackBytes = 0;
    if (attr) pthread_attr_getstacksize(attr, &stackBytes);

    auto* start = new ThreadStart{start_routine, arg};
    int rc = -1;
    if (real_pthread_create) {
        rc = real_pthread_create(thread, attr, threadTrampoline, start);
    }
    int realRc = rc;
    int sceRc = 0;
    if (rc != 0) {
        rc = scePthreadCreate(thread, attr, threadTrampoline, start, "xcloud_worker");
        sceRc = rc;
        if (rc < 0) {
            uint32_t uret = static_cast<uint32_t>(rc);
            rc = ((uret & 0xFFFF0000) == 0x80020000) ? static_cast<int>(uret & 0xFFFF) : EAGAIN;
        }
    }

    if (createdAttr) {
        pthread_attr_destroy(&customAttr);
    }
    if (rc == 0) {
        s_threadsCreated.fetch_add(1, std::memory_order_relaxed);
    } else {
        delete start;
        // The first few, then every 100th: a runaway loop must not flood the log.
        int n = s_threadFailures.fetch_add(1) + 1;
        if (n <= 3 || n % 100 == 0) {
            XC_LOGE("pthread_create failed #%d: rc=%d %s (sce 0x%08x, pthread_create %d), stack %zu KB; %s", n, rc,
                    std::strerror(rc), static_cast<unsigned>(sceRc), realRc, stackBytes >> 10,
                    xc::platform::diagnostics().c_str());
        }
    }
    return rc;
}

int socket(int domain, int type, int protocol) {
    if (protocol == 17) {
        protocol = 0;
    }
    int baseType = type & 0xF;
    int s = sceNetSocket("", domain, baseType, protocol);
    if (s < 0) {
        errno = getNetErrno(s);
        XC_LOGW("net: sceNetSocket(domain=%d, type=0x%x, proto=%d) failed: 0x%08x (errno=%d)",
                domain, type, protocol, static_cast<unsigned>(s), errno);
        return -1;
    }
    {
        std::lock_guard<std::mutex> lock(s_sockMutex);
        s_openSockets.insert(s);
    }
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 04000
#endif
    if ((type & SOCK_NONBLOCK) || (type & 04000) || (type & 0x800) || (type & 0x4000) || (type & 0x20000000)) {
        setSocketNonBlock(s, 1);
    }
    return s;
}

int bind(int fd, const struct sockaddr* addr, socklen_t len) {
    if (!addr || len < sizeof(struct sockaddr_in)) {
        int rc = sceNetBind(fd, addr, len);
        if (rc < 0) { errno = getNetErrno(rc); return -1; }
        return rc;
    }
    const auto* raw = reinterpret_cast<const uint8_t*>(addr);
    uint16_t port = 0;
    std::memcpy(&port, raw + 2, sizeof(port));
    port = ntohs(port);

    struct sockaddr_in boundAddr{};
    std::memcpy(&boundAddr, addr, std::min<size_t>(len, sizeof(boundAddr)));
    if (boundAddr.sin_family == 0 && raw[0] == AF_INET) {
        boundAddr.sin_family = AF_INET;
        boundAddr.sin_len = sizeof(struct sockaddr_in);
    } else if (boundAddr.sin_len == 0 || boundAddr.sin_len < sizeof(struct sockaddr_in)) {
        boundAddr.sin_len = sizeof(struct sockaddr_in);
    }
    if (boundAddr.sin_family == 0) boundAddr.sin_family = AF_INET;

    // Port 0 lets the stack pick a free ephemeral port. Forcing a fixed high
    // range (old 50100+) collides with ports the PS4 network stack reserves.
    int rc = sceNetBind(fd, &boundAddr, sizeof(boundAddr));
    if (rc < 0) {
        errno = getNetErrno(rc);
        XC_LOGW("net: sceNetBind(fd=%d, port=%u) failed: 0x%08x (errno=%d)",
                fd, static_cast<unsigned>(port), static_cast<unsigned>(rc), errno);
        return -1;
    }
    if (port != 0) {
        std::lock_guard<std::mutex> lock(s_sockMutex);
        s_boundPorts[fd] = port;
    } else {
        struct sockaddr_in actual{};
        unsigned int actualLen = sizeof(actual);
        if (sceNetGetsockname(fd, &actual, &actualLen) == 0 && actual.sin_port != 0) {
            std::lock_guard<std::mutex> lock(s_sockMutex);
            s_boundPorts[fd] = ntohs(actual.sin_port);
        } else {
            std::lock_guard<std::mutex> lock(s_sockMutex);
            if (s_boundPorts.find(fd) == s_boundPorts.end()) {
                s_boundPorts[fd] = static_cast<uint16_t>(50100 + (fd % 1000));
            }
        }
    }
    return 0;
}

int getsockname(int fd, struct sockaddr* addr, socklen_t* len) {
    if (!addr || !len || *len < sizeof(struct sockaddr_in)) return -1;
    {
        unsigned int nlen = static_cast<unsigned int>(*len);
        if (sceNetGetsockname(fd, addr, &nlen) == 0) {
            auto* sin = reinterpret_cast<struct sockaddr_in*>(addr);
            const auto* raw = reinterpret_cast<const uint8_t*>(addr);
            if (raw[0] == AF_INET && raw[1] == 0) {
                sin->sin_len = sizeof(struct sockaddr_in);
                sin->sin_family = AF_INET;
            } else if (sin->sin_family == AF_INET || sin->sin_family == AF_INET6) {
                if (sin->sin_len == 0) sin->sin_len = sizeof(struct sockaddr_in);
            }
            if (sin->sin_port == 0) {
                std::lock_guard<std::mutex> lock(s_sockMutex);
                auto it = s_boundPorts.find(fd);
                if (it != s_boundPorts.end()) {
                    sin->sin_port = htons(it->second);
                } else {
                    uint16_t fallbackPort = static_cast<uint16_t>(50100 + (fd % 1000));
                    s_boundPorts[fd] = fallbackPort;
                    sin->sin_port = htons(fallbackPort);
                }
            } else {
                std::lock_guard<std::mutex> lock(s_sockMutex);
                s_boundPorts[fd] = ntohs(sin->sin_port);
            }
            *len = nlen;
            return 0;
        }
    }
    uint16_t port = 0;
    {
        std::lock_guard<std::mutex> lock(s_sockMutex);
        auto it = s_boundPorts.find(fd);
        if (it != s_boundPorts.end()) {
            port = it->second;
        } else {
            port = static_cast<uint16_t>(50100 + (fd % 1000));
            s_boundPorts[fd] = port;
        }
    }
    auto* sin = reinterpret_cast<struct sockaddr_in*>(addr);
    std::memset(sin, 0, sizeof(*sin));
    sin->sin_len = sizeof(struct sockaddr_in);
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);
    sin->sin_addr.s_addr = 0; // INADDR_ANY so addr_is_any returns true
    *len = sizeof(struct sockaddr_in);
    return 0;
}

ssize_t sendto(int fd, const void* buf, size_t len, int flags,
               const struct sockaddr* dest_addr, socklen_t addrlen) {
    flags = sanitizeNetFlags(flags);
    if (!dest_addr || addrlen < 2) {
        int ret = sceNetSendto(fd, buf, len, flags, nullptr, 0);
        if (ret < 0) {
            errno = getNetErrno(ret);
            return -1;
        }
        return ret;
    }

    struct sockaddr_storage fixedAddr{};
    std::memcpy(&fixedAddr, dest_addr, std::min<size_t>(addrlen, sizeof(fixedAddr)));
    if (fixedAddr.ss_family == 0 && reinterpret_cast<const uint8_t*>(dest_addr)[0] == AF_INET) {
        fixedAddr.ss_family = AF_INET;
        fixedAddr.ss_len = sizeof(struct sockaddr_in);
    } else if (fixedAddr.ss_len == 0) {
        fixedAddr.ss_len = (fixedAddr.ss_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
    }

    int ret = sceNetSendto(fd, buf, len, flags, &fixedAddr, addrlen);
    if (ret < 0) {
        errno = getNetErrno(ret);
        return -1;
    }
    return ret;
}

ssize_t recvfrom(int fd, void* buf, size_t len, int flags,
                 struct sockaddr* src_addr, socklen_t* addrlen) {
    flags = sanitizeNetFlags(flags);
    unsigned int slen = addrlen ? static_cast<unsigned int>(*addrlen) : 0;
    int ret = sceNetRecvfrom(fd, buf, len, flags, src_addr, addrlen ? &slen : nullptr);
    if (ret < 0) {
        int err = getNetErrno(ret);
        errno = (err == ETIMEDOUT) ? EAGAIN : err;
        return -1;
    }
    if (addrlen) *addrlen = slen;
    if (src_addr && slen >= sizeof(struct sockaddr_in)) {
        auto* sin = reinterpret_cast<struct sockaddr_in*>(src_addr);
        if (sin->sin_len == 0) sin->sin_len = sizeof(struct sockaddr_in);
        if (sin->sin_family == 0) sin->sin_family = AF_INET;
    }
    return ret;
}

ssize_t send(int fd, const void* buf, size_t len, int flags) {
    int ret = sceNetSend(fd, buf, len, sanitizeNetFlags(flags));
    if (ret < 0) {
        errno = getNetErrno(ret);
        return -1;
    }
    return ret;
}

ssize_t recv(int fd, void* buf, size_t len, int flags) {
    int ret = sceNetRecv(fd, buf, len, sanitizeNetFlags(flags));
    if (ret < 0) {
        int err = getNetErrno(ret);
        errno = (err == ETIMEDOUT) ? EAGAIN : err;
        return -1;
    }
    return ret;
}

int connect(int fd, const struct sockaddr* addr, socklen_t len) {
    if (!addr) return -1;
    struct sockaddr_storage fixedAddr{};
    std::memcpy(&fixedAddr, addr, std::min<size_t>(len, sizeof(fixedAddr)));
    if (fixedAddr.ss_family == 0 && reinterpret_cast<const uint8_t*>(addr)[0] == AF_INET) {
        fixedAddr.ss_family = AF_INET;
        fixedAddr.ss_len = sizeof(struct sockaddr_in);
    } else if (fixedAddr.ss_len == 0) {
        fixedAddr.ss_len = (fixedAddr.ss_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
    }
    int ret = sceNetConnect(fd, &fixedAddr, len);
    if (ret < 0) {
        errno = getNetErrno(ret);
        return -1;
    }
    return ret;
}

int listen(int fd, int backlog) {
    int ret = sceNetListen(fd, backlog);
    if (ret < 0) {
        errno = getNetErrno(ret);
        return -1;
    }
    return ret;
}

int accept(int fd, struct sockaddr* addr, socklen_t* addrlen) {
    unsigned int slen = addrlen ? static_cast<unsigned int>(*addrlen) : 0;
    int ret = sceNetAccept(fd, addr, addrlen ? &slen : nullptr);
    if (ret < 0) {
        errno = getNetErrno(ret);
        return -1;
    }
    {
        std::lock_guard<std::mutex> lock(s_sockMutex);
        s_openSockets.insert(ret);
    }
    if (addrlen) *addrlen = slen;
    return ret;
}

int setsockopt(int fd, int level, int optname, const void* optval, socklen_t optlen) {
    int ret = sceNetSetsockopt(fd, level, optname, optval, optlen);
    if (ret < 0) {
        errno = getNetErrno(ret);
        return -1;
    }
    return 0;
}

int getsockopt(int fd, int level, int optname, void* optval, socklen_t* optlen) {
    unsigned int olen = optlen ? static_cast<unsigned int>(*optlen) : 0;
    int ret = sceNetGetsockopt(fd, level, optname, optval, optlen ? &olen : nullptr);
    if (ret < 0) {
        errno = getNetErrno(ret);
        return -1;
    }
    if (optlen) *optlen = olen;
    return 0;
}

int close(int fd) {
    {
        std::lock_guard<std::mutex> lock(s_sockMutex);
        s_boundPorts.erase(fd);
        s_openSockets.erase(fd);
    }
    int netRet = sceNetSocketClose(fd);
    if (netRet >= 0) {
        return 0;
    }
    if (int* err = sceNetErrnoLoc()) *err = 0;
    return _close(fd);
}
}

extern "C" void arc4random_buf(void* buf, size_t n) {
    if (buf && n > 0) {
        sceRandomGetRandomNumber(buf, n);
    }
}

extern "C" int mbedtls_hardware_poll(void* /*data*/, unsigned char* output, size_t len, size_t* olen) {
    arc4random_buf(output, len);
    *olen = len;
    return 0;
}

extern "C" uint32_t ntohl(uint32_t netlong) {
    return __builtin_bswap32(netlong);
}

extern "C" uint32_t htonl(uint32_t hostlong) {
    return __builtin_bswap32(hostlong);
}

extern "C" uint16_t ntohs(uint16_t netshort) {
    return __builtin_bswap16(netshort);
}

extern "C" uint16_t htons(uint16_t hostshort) {
    return __builtin_bswap16(hostshort);
}

extern "C" int inet_pton(int af, const char* src, void* dst) {
    if (af != AF_INET || !src || !dst) return 0;
    unsigned int a, b, c, d;
    char tail = 0;
    if (std::sscanf(src, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4) {
        if (a <= 255 && b <= 255 && c <= 255 && d <= 255) {
            uint8_t* p = static_cast<uint8_t*>(dst);
            p[0] = static_cast<uint8_t>(a);
            p[1] = static_cast<uint8_t>(b);
            p[2] = static_cast<uint8_t>(c);
            p[3] = static_cast<uint8_t>(d);
            return 1;
        }
    }
    return 0;
}

extern "C" const char* inet_ntop(int af, const void* src, char* dst, socklen_t size) {
    if (!src || !dst) return nullptr;
    if (af == AF_INET) {
        if (size < 16) return nullptr;
        const auto* p = static_cast<const uint8_t*>(src);
        int n = std::snprintf(dst, size, "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
        if (n < 0 || static_cast<socklen_t>(n) >= size) return nullptr;
        return dst;
    }
    if (af == AF_INET6) {
        if (size < 46) return nullptr;
        const auto* p = static_cast<const uint8_t*>(src);
        uint16_t words[8];
        for (int i = 0; i < 8; ++i) {
            uint16_t w = 0;
            std::memcpy(&w, p + i * 2, sizeof(w));
            words[i] = ntohs(w);
        }
        int n = std::snprintf(dst, size, "%x:%x:%x:%x:%x:%x:%x:%x",
                              words[0], words[1], words[2], words[3],
                              words[4], words[5], words[6], words[7]);
        if (n < 0 || static_cast<socklen_t>(n) >= size) return nullptr;
        return dst;
    }
    return nullptr;
}

extern "C" int getnameinfo(const struct sockaddr* sa, socklen_t salen,
                           char* host, socklen_t hostlen,
                           char* serv, socklen_t servlen,
                           int flags) {
    (void)salen;
    (void)flags;
    if (!sa) return EAI_FAIL;

    const auto* raw = reinterpret_cast<const uint8_t*>(sa);
    int family = -1;
    if (raw[0] == AF_INET || raw[1] == AF_INET) {
        family = AF_INET;
    } else if (raw[0] == 28 || raw[1] == 28 || raw[0] == 10 || raw[1] == 10) {
        family = AF_INET6;
    } else {
        return EAI_FAMILY;
    }

    int port = 0;
    if (family == AF_INET) {
        uint16_t netport = 0;
        std::memcpy(&netport, raw + 2, sizeof(netport));
        port = ntohs(netport);

        if (host && hostlen > 0) {
            int n = std::snprintf(host, hostlen, "%u.%u.%u.%u", raw[4], raw[5], raw[6], raw[7]);
            if (n < 0 || static_cast<socklen_t>(n) >= hostlen) return EAI_OVERFLOW;
        }
    } else {
        uint16_t netport = 0;
        std::memcpy(&netport, raw + 2, sizeof(netport));
        port = ntohs(netport);

        if (host && hostlen > 0) {
            uint16_t words[8];
            for (int i = 0; i < 8; ++i) {
                uint16_t w = 0;
                std::memcpy(&w, raw + 8 + i * 2, sizeof(w));
                words[i] = ntohs(w);
            }
            int n = std::snprintf(host, hostlen, "%x:%x:%x:%x:%x:%x:%x:%x",
                                  words[0], words[1], words[2], words[3],
                                  words[4], words[5], words[6], words[7]);
            if (n < 0 || static_cast<socklen_t>(n) >= hostlen) return EAI_OVERFLOW;
        }
    }

    if (serv && servlen > 0) {
        int n = std::snprintf(serv, servlen, "%d", port);
        if (n < 0 || static_cast<socklen_t>(n) >= servlen) return EAI_OVERFLOW;
    }
    return 0;
}

extern "C" int poll(struct pollfd* fds, nfds_t nfds, int timeout) {
    fd_set readfds, writefds, exceptfds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    int maxfd = -1;
    for (nfds_t i = 0; i < nfds; ++i) {
        fds[i].revents = 0;
        if (fds[i].fd < 0 || fds[i].fd >= FD_SETSIZE) continue;
        if (fds[i].events & (POLLIN | POLLPRI)) FD_SET(fds[i].fd, &readfds);
        if (fds[i].events & POLLOUT) FD_SET(fds[i].fd, &writefds);
        FD_SET(fds[i].fd, &exceptfds);
        if (fds[i].fd > maxfd) maxfd = fds[i].fd;
    }
    struct timeval tv{};
    struct timeval* ptv = nullptr;
    if (timeout >= 0) {
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;
        ptv = &tv;
    }
    if (maxfd < 0) {
        if (timeout > 0) usleep(static_cast<useconds_t>(timeout) * 1000);
        return 0;
    }
    int rc = select(maxfd + 1, &readfds, &writefds, &exceptfds, ptv);
    if (rc <= 0) return rc;
    int count = 0;
    for (nfds_t i = 0; i < nfds; ++i) {
        if (fds[i].fd < 0 || fds[i].fd >= FD_SETSIZE) continue;
        short rev = 0;
        if (FD_ISSET(fds[i].fd, &readfds)) rev |= POLLIN;
        if (FD_ISSET(fds[i].fd, &writefds)) rev |= POLLOUT;
        if (FD_ISSET(fds[i].fd, &exceptfds)) rev |= POLLERR;
        fds[i].revents = rev;
        if (rev) count++;
    }
    return count;
}

extern "C" int __cxa_thread_atexit_impl(void (*)(void*), void*, void*) {
    return 0;
}

extern "C" int getifaddrs(struct ifaddrs** ifap) {
    if (!ifap) return -1;
    *ifap = nullptr;

    struct sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_len = sizeof(struct sockaddr_in);

    struct sockaddr_in mask{};
    mask.sin_family = AF_INET;
    mask.sin_len = sizeof(struct sockaddr_in);
    mask.sin_addr.s_addr = htonl(0xFFFFFF00);

    const char* src = "none";
    // 1. Query sceNetCtlGetInfo from libSceNetCtl
    OrbisNetCtlInfo info{};
    int ctlRc = sceNetCtlGetInfo(14 /*ORBIS_NET_CTL_INFO_IP_ADDRESS*/, &info);
    if (ctlRc != 0) {
        sceNetCtlInit();
        ctlRc = sceNetCtlGetInfo(14, &info);
    }
    if (ctlRc != 0 || info.ip_address[0] == '\0') {
        ctlRc = sceNetCtlGetInfo(16 /*SCE_NET_CTL_INFO_IP_ADDRESS fallback*/, &info);
    }
    if (ctlRc == 0 && info.ip_address[0] != '\0') {
        info.ip_address[sizeof(info.ip_address) - 1] = '\0';
        inet_pton(AF_INET, info.ip_address, &local.sin_addr);
        src = "sceNetCtlGetInfo";

        OrbisNetCtlInfo maskInfo{};
        if (sceNetCtlGetInfo(15 /*ORBIS_NET_CTL_INFO_NETMASK*/, &maskInfo) == 0 && maskInfo.netmask[0] != '\0') {
            maskInfo.netmask[sizeof(maskInfo.netmask) - 1] = '\0';
            inet_pton(AF_INET, maskInfo.netmask, &mask.sin_addr);
        }
    }

    // 2. Fallback: Connect UDP socket to 8.8.8.8:53 to query kernel routing
    if (local.sin_addr.s_addr == 0) {
        int s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s >= 0) {
            struct sockaddr_in probe{};
            probe.sin_len = sizeof(probe);
            probe.sin_family = AF_INET;
            probe.sin_port = htons(53);
            probe.sin_addr.s_addr = htonl(0x08080808); // 8.8.8.8
            if (connect(s, reinterpret_cast<struct sockaddr*>(&probe), sizeof(probe)) == 0) {
                socklen_t localLen = sizeof(local);
                getsockname(s, reinterpret_cast<struct sockaddr*>(&local), &localLen);
                if (local.sin_addr.s_addr != 0) src = "udp-route-probe";
            }
            close(s);
        }
    }

    // 3. Fallback: /data/psbox/local_ip.txt override file
    if (local.sin_addr.s_addr == 0) {
        FILE* fp = std::fopen("/data/psbox/local_ip.txt", "r");
        if (fp) {
            char buf[64]{};
            if (std::fgets(buf, sizeof(buf), fp)) {
                char* nl = std::strpbrk(buf, "\r\n ");
                if (nl) *nl = '\0';
                inet_pton(AF_INET, buf, &local.sin_addr);
                if (local.sin_addr.s_addr != 0) src = "local_ip.txt";
            }
            std::fclose(fp);
        }
    }

    // 4. Default fallback: ensure libjuice never receives an empty interface list
    if (local.sin_addr.s_addr == 0) {
        inet_pton(AF_INET, "127.0.0.1", &local.sin_addr);
        src = "loopback-default";
    }
    static const char* s_lastIfSrc = nullptr;
    if (s_lastIfSrc != src) {
        s_lastIfSrc = src;
        XC_LOGI("net: getifaddrs resolved eth0 via %s (ctlRc=0x%08x)", src, static_cast<unsigned>(ctlRc));
    }
    local.sin_family = AF_INET;
    local.sin_len = sizeof(struct sockaddr_in);

    struct Entry {
        struct ifaddrs ifa;
        struct sockaddr_in addr;
        struct sockaddr_in mask;
        char name[16];
    };

    auto* e = static_cast<Entry*>(std::calloc(1, sizeof(Entry)));
    if (!e) return -1;

    std::strncpy(e->name, "eth0", sizeof(e->name) - 1);
    e->addr = local;
    e->mask = mask;

    e->ifa.ifa_next = nullptr;
    e->ifa.ifa_name = e->name;
    e->ifa.ifa_flags = IFF_UP | IFF_RUNNING | IFF_BROADCAST | IFF_MULTICAST;
    e->ifa.ifa_addr = reinterpret_cast<struct sockaddr*>(&e->addr);
    e->ifa.ifa_netmask = reinterpret_cast<struct sockaddr*>(&e->mask);

    *ifap = &e->ifa;
    return 0;
}

extern "C" void freeifaddrs(struct ifaddrs* ifa) {
    while (ifa) {
        struct ifaddrs* next = ifa->ifa_next;
        std::free(ifa);
        ifa = next;
    }
}

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
int sceKernelAvailableDirectMemorySize(off_t start, off_t end, size_t align, off_t* startOut, size_t* sizeOut);
size_t sceKernelGetDirectMemorySize(void);
int sceKernelIsNeoMode(void);
}

namespace xc::platform {

namespace {
constexpr const char* kDataDir = "/data/psbox";
constexpr const char* kLegacyDir = "/data/homebrew/PSBX00001";
std::string g_dataDir = kDataDir;
std::string g_dataNote;
bool g_dataDirWritable = false;
int g_dataDirErrno = 0;
std::atomic<int> g_netPool{-1};

bool writable(const std::string& dir, int& errOut) {
    errOut = 0;
    struct stat st{};
    if (::stat(dir.c_str(), &st) != 0) {
        if (::mkdir(dir.c_str(), 0777) != 0) {
            errOut = errno;
            return false;
        }
    } else {
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

}  // namespace

bool init() {
    sceSysmoduleLoadModuleInternal(0x8000001C); // NET
    sceSysmoduleLoadModuleInternal(0x80000009); // NETCTL
    sceSysmoduleLoadModule(0x00BA);             // RANDOM

    int errPsbox = 0;
    int errLegacy = 0;
    if (writable(kDataDir, errPsbox)) {
        g_dataDirWritable = true;
        migrateLegacy();
    } else if (writable(kLegacyDir, errLegacy)) {
        g_dataDirWritable = true;
        g_dataDir = kLegacyDir;
        g_dataNote = std::string(kDataDir) + " not writable (errno " + std::to_string(errPsbox) + "); using " + kLegacyDir;
    } else {
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

    int rc = sceNetInit();
    if (rc < 0 && rc != static_cast<int>(0x80410101)) {
        XC_LOGW("sceNetInit: 0x%08x", static_cast<unsigned>(rc));
    }
    int ctlRc = sceNetCtlInit();
    if (ctlRc < 0 && ctlRc != static_cast<int>(0x80410101) && ctlRc != static_cast<int>(0x80412101)) {
        XC_LOGW("sceNetCtlInit: 0x%08x", static_cast<unsigned>(ctlRc));
    }
    int pool = sceNetPoolCreate("xcloud", 32 * 1024, 0);
    if (pool < 0) {
        XC_LOGE("sceNetPoolCreate: 0x%08x (host names will not resolve)", static_cast<unsigned>(pool));
    }
    g_netPool = pool;
    return true;
}

void shutdown() {
    sceNetCtlTerm();
}

std::string appDir() { return "/app0"; }
std::string dataDir() { return g_dataDir; }
std::string dataDirNote() { return g_dataNote; }
bool dataDirWritable() { return g_dataDirWritable; }
int dataDirErrno() { return g_dataDirErrno; }
std::string assetDir() { return "/app0/assets"; }
std::string caBundlePath() { return "/app0/assets/cacert.pem"; }

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
    int rc = sceNetResolverStartNtoa(rid, host.c_str(), &a, 5000000, 2, 0);
    sceNetResolverDestroy(rid);
    if (rc < 0) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "0x%08x", static_cast<unsigned>(rc));
        err = "DNS lookup failed for " + host + " (" + buf + ")";
        return false;
    }
    addr = a;
    return true;
}

void probeNetworking() {}


bool restartApp() {
    char* argv[] = {nullptr};
    int rc = sceSystemServiceLoadExec("/app0/eboot.bin", argv);
    XC_LOGE("restart: sceSystemServiceLoadExec returned 0x%08x", static_cast<unsigned>(rc));
    return false;
}

std::string firmwareVersion() {
    SceKernelSwVersion v{};
    v.size = sizeof v;
    if (sceKernelGetSystemSwVersion(&v) != 0) return {};
    v.versionString[sizeof v.versionString - 1] = 0;
    std::string s = v.versionString;
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    return s;
}

std::string platformName() {
    return "PlayStation 4";
}

size_t imageCacheBudget() {
    return 48u << 20;
}

std::string diagnostics() {
    size_t flex = 0;
    sceKernelAvailableFlexibleMemorySize(&flex);
    off_t start = 0;
    size_t direct = 0;
    sceKernelAvailableDirectMemorySize(0, static_cast<off_t>(sceKernelGetDirectMemorySize()), 16384, &start, &direct);
    char text[192];
    std::snprintf(text, sizeof text, "%s, flexible %zu MB free, direct %zu MB free (largest block), threads %d live / %d created",
                  sceKernelIsNeoMode() ? "PS4 Pro" : "PS4", flex >> 20, direct >> 20,
                  s_threadsLive.load(), s_threadsCreated.load());
    return text;
}

void makePrivate(const std::string&) {}

void notify(const std::string& text) { notify(text, text); }

void notify(const std::string& text, const std::string& forLog) {
    XC_LOGI("NOTIFY: %s", forLog.c_str());
    if (toastsEnabled() && forLog != "signed in (notification)") toasts().push(text, nowMs());
    struct NotificationRequest {
        uint8_t reserved[45];
        char message[3075];
    } req{};
    std::snprintf(req.message, sizeof req.message, "%s", text.c_str());
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

void setThreadAffinity(ThreadRole role, const char* name) {
    if (name && *name) {
        pthread_set_name_np(pthread_self(), name);
    }
    static std::once_flag s_maskOnce;
    static int s_allowedCpus[32];
    static int s_numAllowedCpus = 0;

    std::call_once(s_maskOnce, [&]() {
        cpu_set_t procMask;
        CPU_ZERO(&procMask);
        int grc = pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &procMask);
        if (grc == 0) {
            for (int i = 0; i < 32; ++i) {
                if (CPU_ISSET(i, &procMask)) {
                    s_allowedCpus[s_numAllowedCpus++] = i;
                }
            }
        }
        // Fallback to 6 Jaguar cores (mask 0x3F: cores 0..5) on PS4 if not detected
        if (s_numAllowedCpus < 2) {
            s_numAllowedCpus = 0;
            for (int i = 0; i < 6; ++i) {
                s_allowedCpus[s_numAllowedCpus++] = i;
            }
        }
        XC_LOGI("ps4 cpuset: %d allowed cpus detected (grc=%d)", s_numAllowedCpus, grc);
        for (int i = 0; i < s_numAllowedCpus; ++i) {
            XC_LOGI("  cpu slot %d: logical core %d", i, s_allowedCpus[i]);
        }
    });

    if (s_numAllowedCpus <= 1) {
        return;
    }

    uint64_t orbisMask = 0;
    auto setSlot = [&](int slotIndex) {
        int idx = s_allowedCpus[slotIndex % s_numAllowedCpus];
        if (idx >= 0 && idx < 64) {
            orbisMask |= (UINT64_C(1) << idx);
        }
    };

    switch (role) {
    case ThreadRole::Main:
        setSlot(0);
        break;
    case ThreadRole::Network:
        setSlot(1);
        break;
    case ThreadRole::VideoDecode:
        setSlot(2);
        if (s_numAllowedCpus >= 4) setSlot(3);
        break;
    case ThreadRole::Audio:
        setSlot(s_numAllowedCpus >= 5 ? 4 : 1);
        break;
    case ThreadRole::Worker:
        if (s_numAllowedCpus >= 6) {
            setSlot(5);
        } else {
            setSlot(s_numAllowedCpus - 1);
        }
        break;
    }
    int rc = scePthreadSetaffinity(scePthreadSelf(), orbisMask);
    if (rc != 0) {
        XC_LOGW("scePthreadSetaffinity(%s, 0x%llx): 0x%08x", name ? name : "thread",
                static_cast<unsigned long long>(orbisMask), static_cast<unsigned>(rc));
    } else {
        XC_LOGI("thread %s pinned to role %d (mask 0x%llx)", name ? name : "unknown",
                static_cast<int>(role), static_cast<unsigned long long>(orbisMask));
    }
}

void setThreadRealtimePriority() {
    int rc = scePthreadSetprio(scePthreadSelf(), 256);
    if (rc != 0) {
        XC_LOGW("scePthreadSetprio(256): 0x%08x (the audio thread keeps the default)", static_cast<unsigned>(rc));
    } else {
        XC_LOGI("thread elevated to realtime priority (scePthreadSetprio 256)");
    }
}

}  // namespace xc::platform

extern "C" int xc_resolve_ipv4(const char* host, uint32_t* addr) {
    std::string err;
    if (xc::platform::resolveIPv4(host, *addr, err)) return 0;
    XC_LOGW("%s", err.c_str());
    return -1;
}

// IPv4-only getaddrinfo. The OpenOrbis musl version fails on PS4 even for
// numeric literals, which kept every remote ICE candidate unresolved.
namespace {
struct Ps4AddrInfo {
    struct addrinfo ai;
    struct sockaddr_in sin;
};
}  // namespace

extern "C" int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints,
                           struct addrinfo** res) {
    if (!res) return EAI_FAIL;
    *res = nullptr;
    if (!node && !service) return EAI_NONAME;
    const int family = hints ? hints->ai_family : AF_UNSPEC;
    const int flags = hints ? hints->ai_flags : 0;
    if (family != AF_UNSPEC && family != AF_INET) return EAI_FAMILY;

    uint16_t port = 0;
    if (service && *service) {
        char* end = nullptr;
        unsigned long p = std::strtoul(service, &end, 10);
        if (!end || *end != '\0' || p > 65535) return EAI_SERVICE;
        port = static_cast<uint16_t>(p);
    }

    in_addr addr{};
    if (!node) {
        addr.s_addr = (flags & AI_PASSIVE) ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK);
    } else if (::inet_pton(AF_INET, node, &addr) != 1) {
        in6_addr v6{};
        if (::inet_pton(AF_INET6, node, &v6) == 1) return EAI_FAMILY;  // no IPv6 on PS4
        if (flags & AI_NUMERICHOST) return EAI_NONAME;
        if (std::strcmp(node, "localhost") == 0) {
            addr.s_addr = htonl(INADDR_LOOPBACK);
        } else {
            uint32_t a = 0;
            if (xc_resolve_ipv4(node, &a) != 0) return EAI_NONAME;
            addr.s_addr = a;
        }
    }

    auto* r = static_cast<Ps4AddrInfo*>(std::calloc(1, sizeof(Ps4AddrInfo)));
    if (!r) return EAI_MEMORY;
    r->sin.sin_len = sizeof(struct sockaddr_in);
    r->sin.sin_family = AF_INET;
    r->sin.sin_port = htons(port);
    r->sin.sin_addr = addr;
    r->ai.ai_flags = flags;
    r->ai.ai_family = AF_INET;
    r->ai.ai_socktype = hints && hints->ai_socktype ? hints->ai_socktype : SOCK_STREAM;
    r->ai.ai_protocol = hints ? hints->ai_protocol : 0;
    r->ai.ai_addrlen = sizeof(struct sockaddr_in);
    r->ai.ai_addr = reinterpret_cast<struct sockaddr*>(&r->sin);
    r->ai.ai_canonname = nullptr;
    r->ai.ai_next = nullptr;
    *res = &r->ai;
    return 0;
}

extern "C" void freeaddrinfo(struct addrinfo* ai) {
    while (ai) {
        struct addrinfo* next = ai->ai_next;
        std::free(ai);  // ai is the first member of Ps4AddrInfo
        ai = next;
    }
}
