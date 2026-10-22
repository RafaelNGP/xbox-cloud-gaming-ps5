// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The Windows platform layer (psbox): WinSock2 networking, AppData storage,
// QueryPerformanceCounter timers, and thread pinning via SetThreadAffinityMask.
#include "platform/platform.h"
#include "platform/toasts.h"
#include "util/log.h"

#if defined(_WIN32) || defined(__CYGWIN__)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <direct.h>
#include <io.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace xc::platform {

namespace {

std::string g_dataDir;
std::string g_dataDirNote;

#if defined(_WIN32) || defined(__CYGWIN__)
void makeDirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/' || path[i] == '\\') {
            std::string sub = path.substr(0, i);
            _mkdir(sub.c_str());
        }
    }
}

std::string getExeDir() {
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return ".";
    std::string path(buf, len);
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return ".";
    return path.substr(0, slash);
}
#endif

}  // namespace

bool init() {
#if defined(_WIN32) || defined(__CYGWIN__)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }

    const char* env = std::getenv("XCLOUD_DATA_DIR");
    if (env && *env) {
        g_dataDir = env;
    } else {
        char appData[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
            g_dataDir = std::string(appData) + "\\PSBox";
        } else {
            g_dataDir = ".\\psbox-data";
        }
    }
    makeDirs(g_dataDir);
#else
    const char* env = std::getenv("XCLOUD_DATA_DIR");
    g_dataDir = (env && *env) ? env : "./psbox-data";
#endif
    return true;
}

void shutdown() {
#if defined(_WIN32) || defined(__CYGWIN__)
    WSACleanup();
#endif
}

std::string dataDir() { return g_dataDir; }
std::string dataDirNote() { return g_dataDirNote; }
bool dataDirWritable() { return true; }
int dataDirErrno() { return 0; }

std::string appDir() {
#if defined(_WIN32) || defined(__CYGWIN__)
    return getExeDir();
#else
    return ".";
#endif
}

std::string assetDir() {
    if (const char* env = std::getenv("XCLOUD_ASSETS"); env && *env) return env;
    std::string dir = appDir() + "/assets";
#if defined(_WIN32) || defined(__CYGWIN__)
    DWORD attr = GetFileAttributesA(dir.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return dir;
    }
#endif
    return "assets";
}

std::string caBundlePath() {
    if (const char* env = std::getenv("XCLOUD_CA_BUNDLE"); env && *env) return env;
    return assetDir() + "/cacert.pem";
}

bool randomBytes(void* out, size_t len) {
    if (!out || len == 0) return true;
#if defined(_WIN32) || defined(__CYGWIN__)
    HCRYPTPROV prov = 0;
    if (CryptAcquireContextA(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        BOOL ok = CryptGenRandom(prov, static_cast<DWORD>(len), static_cast<BYTE*>(out));
        CryptReleaseContext(prov, 0);
        if (ok) return true;
    }
#endif
    auto* p = static_cast<uint8_t*>(out);
    for (size_t i = 0; i < len; ++i) {
        p[i] = static_cast<uint8_t>(std::rand() & 0xFF);
    }
    return true;
}

bool resolveIPv4(const std::string& host, uint32_t& addr, std::string& err) {
#if defined(_WIN32) || defined(__CYGWIN__)
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    int rc = getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (rc != 0 || !res) {
        err = "DNS lookup failed for " + host + " (error " + std::to_string(WSAGetLastError()) + ")";
        return false;
    }
    addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(res);
    return true;
#else
    (void)host;
    (void)addr;
    err = "resolveIPv4 not implemented on this host";
    return false;
#endif
}

std::string localIPv4() {
#if defined(_WIN32) || defined(__CYGWIN__)
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s != INVALID_SOCKET) {
        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(80);
        dst.sin_addr.s_addr = inet_addr("8.8.8.8");
        if (connect(s, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) == 0) {
            sockaddr_in local{};
            int len = sizeof(local);
            if (getsockname(s, reinterpret_cast<sockaddr*>(&local), &len) == 0) {
                char ip[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip))) {
                    closesocket(s);
                    return ip;
                }
            }
        }
        closesocket(s);
    }
#endif
    return "127.0.0.1";
}

uint64_t nowMs() {
#if defined(_WIN32) || defined(__CYGWIN__)
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    return static_cast<uint64_t>((count.QuadPart * 1000) / freq.QuadPart);
#else
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}

uint64_t nowUs() {
#if defined(_WIN32) || defined(__CYGWIN__)
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    return static_cast<uint64_t>((count.QuadPart * 1000000) / freq.QuadPart);
#else
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}

void sleepMs(uint32_t ms) {
#if defined(_WIN32) || defined(__CYGWIN__)
    Sleep(ms);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}

void probeNetworking() {}

namespace {
struct ThreadArgs {
    std::function<void()> fn;
};

#if defined(_WIN32) || defined(__CYGWIN__)
DWORD WINAPI winThreadEntry(LPVOID param) {
    auto* args = static_cast<ThreadArgs*>(param);
    if (args) {
        if (args->fn) args->fn();
        delete args;
    }
    return 0;
}
#endif
}  // namespace

bool startThread(Thread& t, std::function<void()> fn, size_t stackBytes) {
#if defined(_WIN32) || defined(__CYGWIN__)
    auto* args = new ThreadArgs{std::move(fn)};
    HANDLE h = CreateThread(nullptr, stackBytes, winThreadEntry, args, 0, nullptr);
    if (!h) {
        delete args;
        return false;
    }
    t.handle = h;
    return true;
#else
    (void)stackBytes;
    auto* th = new std::thread(std::move(fn));
    t.handle = th;
    return true;
#endif
}

void Thread::join() {
    if (!handle) return;
#if defined(_WIN32) || defined(__CYGWIN__)
    HANDLE h = static_cast<HANDLE>(handle);
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
    handle = nullptr;
#else
    auto* th = static_cast<std::thread*>(handle);
    if (th->joinable()) th->join();
    delete th;
    handle = nullptr;
#endif
}

void setThreadAffinity(ThreadRole role, const char* name) {
#if defined(_WIN32) || defined(__CYGWIN__)
    (void)name;
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    DWORD numCores = sysInfo.dwNumberOfProcessors;
    if (numCores < 4) return;

    DWORD_PTR mask = 0;
    switch (role) {
    case ThreadRole::Main:
        mask = 1ULL << 0;
        break;
    case ThreadRole::Network:
        mask = 1ULL << (min(1UL, numCores - 1));
        break;
    case ThreadRole::VideoDecode:
        mask = 1ULL << (min(2UL, numCores - 1));
        break;
    case ThreadRole::Audio:
        mask = 1ULL << (min(3UL, numCores - 1));
        break;
    case ThreadRole::Worker:
        for (DWORD c = 4; c < numCores; ++c) mask |= (1ULL << c);
        if (numCores <= 4) mask = 1ULL << (numCores - 1);
        break;
    }
    if (mask) {
        SetThreadAffinityMask(GetCurrentThread(), mask);
    }
#else
    (void)role;
    (void)name;
#endif
}

void setThreadRealtimePriority() {
#if defined(_WIN32) || defined(__CYGWIN__)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
}

void notify(const std::string& text) { notify(text, text); }

void notify(const std::string& text, const std::string& forLog) {
    if (toastsEnabled()) {
        XC_LOGI("NOTIFY: %s", forLog.c_str());
        toasts().push(text, nowMs());
        return;
    }
    std::printf("\n>>> %s\n\n", text.c_str());
}

bool systemKeyboardAvailable() { return false; }
bool openSystemKeyboard(const std::string&, const std::string&, size_t, KeyboardKind) { return false; }
KeyboardStatus pollSystemKeyboard(std::string&) { return KeyboardStatus::Closed; }

bool restartApp() { return false; }
std::string firmwareVersion() { return {}; }
std::string platformName() { return "Windows"; }

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
    std::fclose(f);
    if (!ok) {
        std::remove(tmp.c_str());
        return false;
    }
#if defined(_WIN32) || defined(__CYGWIN__)
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        std::remove(tmp.c_str());
        return false;
    }
#else
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
#endif
    return true;
}

void makePrivate(const std::string&) {}

}  // namespace xc::platform
