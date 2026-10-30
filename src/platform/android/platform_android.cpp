// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The Android platform layer (psbox): app data directory in internal storage,
// assets from package, POSIX threads and networking.
#include "platform/platform.h"
#include "util/log.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace xc::platform {

namespace {
std::string g_dataDir;
std::string g_dataDirNote;

bool isDir(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void makeDirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            ::mkdir(path.substr(0, i).c_str(), 0700);
        }
    }
}
}  // namespace

bool init() {
    const char* env = std::getenv("XCLOUD_DATA_DIR");
    if (env && *env) {
        g_dataDir = env;
    } else {
        g_dataDir = "/data/data/com.rafaelngp.psbox/files";
    }
    makeDirs(g_dataDir);
    return true;
}

void shutdown() {}

std::string dataDir() { return g_dataDir; }
std::string dataDirNote() { return g_dataDirNote; }
bool dataDirWritable() { return true; }
int dataDirErrno() { return 0; }
std::string appDir() { return g_dataDir; }
std::string assetDir() { return g_dataDir + "/assets"; }
std::string caBundlePath() { return assetDir() + "/cacert.pem"; }

bool randomBytes(void* out, size_t len) {
    int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    auto* p = static_cast<unsigned char*>(out);
    while (len > 0) {
        ssize_t n = ::read(fd, p, len);
        if (n <= 0) {
            ::close(fd);
            return false;
        }
        p += n;
        len -= static_cast<size_t>(n);
    }
    ::close(fd);
    return true;
}

bool resolveIPv4(const std::string& host, uint32_t& addr, std::string& err) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (rc != 0 || !res) {
        err = "DNS lookup failed for " + host + ": " + gai_strerror(rc);
        return false;
    }
    addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr.s_addr;
    ::freeaddrinfo(res);
    return true;
}

void probeNetworking() {}

void notify(const std::string& text) { notify(text, text); }

void notify(const std::string& text, const std::string& forLog) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "PSBox", "%s", text.c_str());
#else
    std::printf("[Android Notify] %s (%s)\n", text.c_str(), forLog.c_str());
#endif
}

void makePrivate(const std::string& path) { ::chmod(path.c_str(), 0600); }

bool systemKeyboardAvailable() { return false; }
bool openSystemKeyboard(const std::string&, const std::string&, size_t, KeyboardKind) { return false; }
KeyboardStatus pollSystemKeyboard(std::string&) { return KeyboardStatus::Closed; }

bool restartApp() { return false; }

std::string firmwareVersion() { return {}; }
std::string diagnostics() { return {}; }
std::string platformName() { return "Android"; }
size_t imageCacheBudget() { return 96u << 20; }

void setThreadAffinity(ThreadRole, const char*) {}
void setThreadRealtimePriority() {}

}  // namespace xc::platform
