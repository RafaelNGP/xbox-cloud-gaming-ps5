// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Linux host build: used to develop and validate auth, catalog and streaming
// on a PC before deploying to the console.
#include "platform/platform.h"

#include <cstdio>
#include <cstdlib>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>

namespace xc::platform {

namespace {
std::string g_dataDir;
}

bool init() {
    const char* env = std::getenv("XCLOUD_DATA_DIR");
    if (env && *env) {
        g_dataDir = env;
    } else {
        const char* home = std::getenv("HOME");
        g_dataDir = std::string(home ? home : ".") + "/.local/share/xcloud-ps5";
    }
    std::string cmd = g_dataDir;
    // mkdir -p
    for (size_t i = 1; i <= cmd.size(); ++i) {
        if (i == cmd.size() || cmd[i] == '/') ::mkdir(cmd.substr(0, i).c_str(), 0700);
    }
    return true;
}

void shutdown() {}

std::string dataDir() { return g_dataDir; }

std::string assetDir() {
    if (const char* env = std::getenv("XCLOUD_ASSETS")) return env;
    return "assets";
}

std::string caBundlePath() {
    if (const char* env = std::getenv("XCLOUD_CA_BUNDLE")) return env;
    static const char* candidates[] = {
        "/etc/pki/tls/certs/ca-bundle.crt",       // Fedora / RHEL
        "/etc/ssl/certs/ca-certificates.crt",     // Debian / Ubuntu / Arch
        "/etc/ssl/cert.pem",                      // Alpine / macOS
    };
    for (const char* c : candidates) {
        struct stat st{};
        if (::stat(c, &st) == 0) return c;
    }
    return "assets/cacert.pem";
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

void notify(const std::string& text) { std::printf("\n>>> %s\n\n", text.c_str()); }

bool systemKeyboardAvailable() { return false; }
bool openSystemKeyboard(const std::string&, const std::string&, size_t, KeyboardKind) { return false; }
KeyboardStatus pollSystemKeyboard(std::string&) { return KeyboardStatus::Closed; }

}  // namespace xc::platform
