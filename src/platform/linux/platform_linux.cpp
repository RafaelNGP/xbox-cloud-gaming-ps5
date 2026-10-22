// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The Linux desktop's platform layer (psbox, psbox-headless, xcloud-cli,
// xcloud-tests): the data folder where XDG puts it, the assets beside the
// executable, and notifications as toasts inside the desktop app (or on
// stdout for the tools).
#include "platform/platform.h"

#include "platform/toasts.h"
#include "util/log.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace xc::platform {

namespace {

std::string g_dataDir, g_dataDirNote;

bool isDir(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// mkdir -p; the folders it makes are the user's alone (the sign-in is kept there).
void makeDirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i)
        if (i == path.size() || path[i] == '/') ::mkdir(path.substr(0, i).c_str(), 0700);
}

// The folder of the running executable; empty when /proc doesn't say.
const std::string& exeDir() {
    static const std::string dir = [] {
        char buf[4096];
        ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
        if (n <= 0) return std::string();
        std::string path(buf, static_cast<size_t>(n));
        size_t slash = path.rfind('/');
        if (slash == std::string::npos) return std::string();
        return slash ? path.substr(0, slash) : std::string("/");
    }();
    return dir;
}

}  // namespace

bool init() {
    // A server closing a connection while it is written to: an error to
    // handle, not the end of the app (the default for SIGPIPE).
    std::signal(SIGPIPE, SIG_IGN);
    const char* env = std::getenv("XCLOUD_DATA_DIR");
    if (env && *env) {
        g_dataDir = env;
    } else {
        const char* home = std::getenv("HOME");
        const std::string homeDir = home && *home ? home : ".";
        // XDG: a relative XDG_DATA_HOME is to be ignored.
        const char* xdg = std::getenv("XDG_DATA_HOME");
        const std::string base = xdg && *xdg == '/' ? std::string(xdg) : homeDir + "/.local/share";
        g_dataDir = base + "/psbox";
        // Before the desktop app the folder was ~/.local/share/xcloud-ps5:
        // moved once, the first time the new one is missing.
        const std::string old = homeDir + "/.local/share/xcloud-ps5";
        if (!isDir(g_dataDir) && isDir(old)) {
            makeDirs(base);
            if (std::rename(old.c_str(), g_dataDir.c_str()) == 0) {
                g_dataDirNote = "moved from " + old;
            } else {
                g_dataDirNote = "the old folder kept (" + old + " could not be moved: " + std::strerror(errno) + ")";
                g_dataDir = old;
            }
        }
    }
    makeDirs(g_dataDir);
    return true;
}

void shutdown() {}

std::string dataDir() { return g_dataDir; }
std::string dataDirNote() { return g_dataDirNote; }
bool dataDirWritable() { return true; }
int dataDirErrno() { return 0; }
// Where the executable is (an update would go there): never the data folder.
std::string appDir() { return exeDir().empty() ? std::string(".") : exeDir(); }

std::string assetDir() {
    if (const char* env = std::getenv("XCLOUD_ASSETS"); env && *env) return env;
    // The desktop app's build copies assets/ next to it; the tools run from
    // the source tree.
    if (!exeDir().empty() && isDir(exeDir() + "/assets")) return exeDir() + "/assets";
    return "assets";
}

std::string caBundlePath() {
    if (const char* env = std::getenv("XCLOUD_CA_BUNDLE"); env && *env) return env;
    static const char* candidates[] = {
        "/etc/pki/tls/certs/ca-bundle.crt",       // Fedora / RHEL
        "/etc/ssl/certs/ca-certificates.crt",     // Debian / Ubuntu / Arch
        "/etc/ssl/cert.pem",                      // Alpine / macOS
    };
    for (const char* c : candidates) {
        struct stat st {};
        if (::stat(c, &st) == 0) return c;
    }
    return assetDir() + "/cacert.pem";
}

bool randomBytes(void* out, size_t len) {
    auto* p = static_cast<unsigned char*>(out);
    while (len) {
        ssize_t n = ::getrandom(p, len, 0);
        if (n <= 0) return false;
        p += n;
        len -= static_cast<size_t>(n);
    }
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
    if (toastsEnabled()) {
        // The desktop app: a toast in the window. The log gets `forLog`, as
        // on the PS5 (`text` may name the user).
        XC_LOGI("NOTIFY: %s", forLog.c_str());
        toasts().push(text, nowMs());
        return;
    }
    std::printf("\n>>> %s\n\n", text.c_str());
}

void makePrivate(const std::string& path) { ::chmod(path.c_str(), 0600); }

bool systemKeyboardAvailable() { return false; }
bool openSystemKeyboard(const std::string&, const std::string&, size_t, KeyboardKind) { return false; }
KeyboardStatus pollSystemKeyboard(std::string&) { return KeyboardStatus::Closed; }

bool restartApp() { return false; }

std::string firmwareVersion() { return {}; }
std::string platformName() { return "Linux"; }

void setThreadAffinity(ThreadRole role, const char* name) {
    if (name && *name) {
        pthread_setname_np(pthread_self(), name);
    }
    long numCores = sysconf(_SC_NPROCESSORS_ONLN);
    if (numCores < 4) return;  // Do not restrict on systems with few cores

    cpu_set_t mask;
    CPU_ZERO(&mask);
    switch (role) {
    case ThreadRole::Main:
        CPU_SET(0, &mask);
        break;
    case ThreadRole::Network:
        CPU_SET(std::min(1L, numCores - 1), &mask);
        break;
    case ThreadRole::VideoDecode:
        CPU_SET(std::min(2L, numCores - 1), &mask);
        break;
    case ThreadRole::Audio:
        CPU_SET(std::min(3L, numCores - 1), &mask);
        break;
    case ThreadRole::Worker:
        for (long c = 4; c < numCores; ++c) CPU_SET(c, &mask);
        if (numCores <= 4) CPU_SET(numCores - 1, &mask);
        break;
    }
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &mask);
}

void setThreadRealtimePriority() {
    struct sched_param param{};
    int maxPrio = sched_get_priority_max(SCHED_RR);
    param.sched_priority = maxPrio > 0 ? maxPrio : 1;
    pthread_setschedparam(pthread_self(), SCHED_RR, &param);
}

}  // namespace xc::platform
