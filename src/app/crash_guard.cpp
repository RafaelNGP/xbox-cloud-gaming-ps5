// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/crash_guard.h"

#include "platform/platform.h"
#include "util/log.h"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <thread>
#include <ucontext.h>
#include <unistd.h>

#if defined(XCLOUD_PS5)
#include <ps5platform/context.h>
extern "C" {
extern const char __text_start[];
extern const char __text_end[];
struct SceModuleInfoForUnwind {
    size_t st_size;
    char name[256];
    uintptr_t eh_frame_hdr_addr;
    uintptr_t eh_frame_addr;
    size_t eh_frame_size;
    uintptr_t seg0_addr;
    size_t seg0_size;
};
int sceKernelGetModuleInfoForUnwind(uintptr_t addr, int flags, SceModuleInfoForUnwind* info);
}
#else
#include <link.h>
#endif

namespace xc::app::crash {

namespace {

char g_marker[512] = {};
char g_stage[96] = "starting";
std::atomic<uint64_t> g_beat{0};
uintptr_t g_ebootBase = 0;
uintptr_t g_ebootEnd = 0;

void initBase() {
#if defined(XCLOUD_PS5)
    g_ebootBase = reinterpret_cast<uintptr_t>(__text_start);
    g_ebootEnd = reinterpret_cast<uintptr_t>(__text_end);
#else
    dl_iterate_phdr(
        [](struct dl_phdr_info* info, size_t, void*) -> int {
            g_ebootBase = static_cast<uintptr_t>(info->dlpi_addr);
            uintptr_t end = g_ebootBase;
            for (int i = 0; i < info->dlpi_phnum; ++i) {
                if (info->dlpi_phdr[i].p_type != PT_LOAD) continue;
                uintptr_t segEnd = g_ebootBase + info->dlpi_phdr[i].p_vaddr + info->dlpi_phdr[i].p_memsz;
                if (segEnd > end) end = segEnd;
            }
            g_ebootEnd = end;
            return 1;  // first module is the main executable
        },
        nullptr);
#endif
}

const char* signalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGABRT: return "SIGABRT";
    default: return "?";
    }
}

void appendStr(char* buf, size_t cap, size_t& len, const char* s) {
    if (!s) return;
    while (*s && len + 1 < cap) buf[len++] = *s++;
    buf[len] = 0;
}

void appendDec(char* buf, size_t cap, size_t& len, int val) {
    char tmp[16];
    int n = 0;
    unsigned int v = val < 0 ? static_cast<unsigned int>(-val) : static_cast<unsigned int>(val);
    if (val < 0 && len + 1 < cap) buf[len++] = '-';
    do {
        tmp[n++] = static_cast<char>('0' + (v % 10));
        v /= 10;
    } while (v && n < 15);
    while (n > 0 && len + 1 < cap) buf[len++] = tmp[--n];
    buf[len] = 0;
}

void appendHex(char* buf, size_t cap, size_t& len, uintptr_t val) {
    appendStr(buf, cap, len, "0x");
    char tmp[20];
    int n = 0;
    do {
        unsigned int d = static_cast<unsigned int>(val & 0xf);
        tmp[n++] = static_cast<char>(d < 10 ? ('0' + d) : ('a' + d - 10));
        val >>= 4;
    } while (val && n < 16);
    while (n > 0 && len + 1 < cap) buf[len++] = tmp[--n];
    buf[len] = 0;
}

const char* baseName(const char* path) {
    if (!path) return "";
    const char* last = path;
    for (const char* p = path; *p; ++p)
        if (*p == '/') last = p + 1;
    return last;
}

void appendCodeAddr(char* buf, size_t cap, size_t& len, uintptr_t addr) {
    if (addr != 0 && g_ebootEnd > g_ebootBase && addr >= g_ebootBase && addr < g_ebootEnd) {
        appendStr(buf, cap, len, "eboot+");
        appendHex(buf, cap, len, addr - g_ebootBase);
        return;
    }
    if (addr != 0) {
#if defined(XCLOUD_PS5)
        SceModuleInfoForUnwind mod{};
        mod.st_size = sizeof(mod);
        if (sceKernelGetModuleInfoForUnwind(addr, 1, &mod) == 0 && mod.name[0] && addr >= mod.seg0_addr) {
            const char* name = baseName(mod.name);
            if (std::strcmp(name, "eboot.bin") == 0 || std::strcmp(name, "eboot.elf") == 0) name = "eboot";
            appendStr(buf, cap, len, name);
            appendStr(buf, cap, len, "+");
            appendHex(buf, cap, len, addr - mod.seg0_addr);
            return;
        }
#else
        struct Lookup {
            uintptr_t addr;
            const char* name;
            uintptr_t base;
            bool found;
        } q{addr, nullptr, 0, false};
        dl_iterate_phdr(
            [](struct dl_phdr_info* info, size_t, void* data) -> int {
                auto* l = static_cast<Lookup*>(data);
                uintptr_t base = static_cast<uintptr_t>(info->dlpi_addr);
                for (int i = 0; i < info->dlpi_phnum; ++i) {
                    if (info->dlpi_phdr[i].p_type != PT_LOAD) continue;
                    uintptr_t lo = base + info->dlpi_phdr[i].p_vaddr;
                    uintptr_t hi = lo + info->dlpi_phdr[i].p_memsz;
                    if (l->addr >= lo && l->addr < hi) {
                        l->name = info->dlpi_name;
                        l->base = base;
                        l->found = true;
                        return 1;
                    }
                }
                return 0;
            },
            &q);
        if (q.found) {
            const char* name = (q.name && q.name[0]) ? baseName(q.name) : "eboot";
            appendStr(buf, cap, len, name);
            appendStr(buf, cap, len, "+");
            appendHex(buf, cap, len, addr - q.base);
            return;
        }
#endif
    }
    appendHex(buf, cap, len, addr);
}

// Async-signal-safe: open / write / close only, no allocation.
void writeMarker(const char* kind, const char* crashLine = nullptr, uintptr_t rbp = 0, uintptr_t rsp = 0) {
    if (!g_marker[0]) return;
    int fd = ::open(g_marker, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return;
    auto put = [fd](const char* s) { (void)!::write(fd, s, std::strlen(s)); };
    put(kind);
    put("\n");
    put(g_stage);
    put("\n");
    if (crashLine && crashLine[0]) {
        put(crashLine);
        // Optional caller return addresses via frame pointers, written after
        // the main crash line is already flushed to the descriptor.
        if (rsp != 0 && (rbp & (sizeof(uintptr_t) - 1)) == 0 && rbp >= rsp && rbp - rsp <= (256u << 10)) {
            uintptr_t fp = rbp;
            bool any = false;
            for (int depth = 0; depth < 6; ++depth) {
                const auto* frame = reinterpret_cast<const uintptr_t*>(fp);
                uintptr_t nextFp = frame[0];
                uintptr_t ret = frame[1];
                if (ret == 0) break;
                char item[96];
                size_t len = 0;
                appendStr(item, sizeof item, len, any ? "," : " bt=");
                appendCodeAddr(item, sizeof item, len, ret - 1);
                put(item);
                any = true;
                if ((nextFp & (sizeof(uintptr_t) - 1)) != 0 || nextFp <= fp || nextFp - fp > (64u << 10)) break;
                fp = nextFp;
            }
        }
        put("\n");
    }
    ::close(fd);
}

void onSignal(int sig, siginfo_t* info, void* uctx) {
    char kind[48];
    size_t klen = 0;
    appendStr(kind, sizeof kind, klen, "signal ");
    appendDec(kind, sizeof kind, klen, sig);
    appendStr(kind, sizeof kind, klen, " (");
    appendStr(kind, sizeof kind, klen, signalName(sig));
    appendStr(kind, sizeof kind, klen, ")");

    uintptr_t pc = 0, rbp = 0, rsp = 0;
#if !defined(__ORBIS__) && !defined(__PS4__)
    if (uctx) {
        const auto* uc = static_cast<const ucontext_t*>(uctx);
#if defined(__FreeBSD__) || defined(__PROSPERO__) || defined(XCLOUD_PS5)
        pc = static_cast<uintptr_t>(uc->uc_mcontext.mc_rip);
        rbp = static_cast<uintptr_t>(uc->uc_mcontext.mc_rbp);
        rsp = static_cast<uintptr_t>(uc->uc_mcontext.mc_rsp);
#elif defined(__x86_64__) && defined(__linux__)
        pc = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
        rbp = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RBP]);
        rsp = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RSP]);
#endif
    }
    uintptr_t faultAddr = info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0;
#if defined(__FreeBSD__) || defined(__PROSPERO__) || defined(XCLOUD_PS5)
    if ((sig == SIGSEGV || sig == SIGBUS) && faultAddr == 0 && uctx) {
        const auto* uc = static_cast<const ucontext_t*>(uctx);
        faultAddr = static_cast<uintptr_t>(uc->uc_mcontext.mc_addr);
    }
#endif
#else
    uintptr_t faultAddr = info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0;
    if (uctx) {
        // FreeBSD amd64 ucontext: 16-byte sigset, then mcontext of 8-byte slots.
        const auto* mc = reinterpret_cast<const uint64_t*>(static_cast<const char*>(uctx) + 16);
        rbp = static_cast<uintptr_t>(mc[9]);
        pc = static_cast<uintptr_t>(mc[20]);
        rsp = static_cast<uintptr_t>(mc[23]);
        if (faultAddr == 0) faultAddr = static_cast<uintptr_t>(mc[17]);
        // Calling a null function pointer leaves the caller's return address at [rsp].
        if (pc == 0 && rsp != 0 && (rsp & 7) == 0) {
            pc = *reinterpret_cast<const uintptr_t*>(rsp);
        }
    }
#endif

    char crashLine[192];
    size_t clen = 0;
    appendStr(crashLine, sizeof crashLine, clen, "crash: ");
    appendStr(crashLine, sizeof crashLine, clen, signalName(sig));
    appendStr(crashLine, sizeof crashLine, clen, " pc=");
    appendCodeAddr(crashLine, sizeof crashLine, clen, pc);
    appendStr(crashLine, sizeof crashLine, clen, " addr=");
    appendHex(crashLine, sizeof crashLine, clen, faultAddr);

    writeMarker(kind, crashLine, rbp, rsp);

    struct sigaction dfl{};
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    ::sigaction(sig, &dfl, nullptr);
    ::raise(sig);
}

}  // namespace

void install(const std::string& markerPath) {
    initBase();
    std::strncpy(g_marker, markerPath.c_str(), sizeof g_marker - 1);
    g_marker[sizeof g_marker - 1] = 0;
    struct sigaction sa{};
    sa.sa_sigaction = onSignal;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) ::sigaction(sig, &sa, nullptr);
    std::set_terminate([] {
        uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
        char crashLine[160];
        size_t clen = 0;
        appendStr(crashLine, sizeof crashLine, clen, "crash: terminate pc=");
        appendCodeAddr(crashLine, sizeof crashLine, clen, caller ? caller - 1 : 0);
        appendStr(crashLine, sizeof crashLine, clen, " addr=0x0");
#if defined(__ORBIS__)
        if (auto ep = std::current_exception()) {
            try {
                std::rethrow_exception(ep);
            } catch (const std::exception& e) {
                appendStr(crashLine, sizeof crashLine, clen, " what=");
                appendStr(crashLine, sizeof crashLine, clen, e.what());
            } catch (...) {
                appendStr(crashLine, sizeof crashLine, clen, " what=<non-std>");
            }
        }
#endif
        writeMarker("uncaught exception", crashLine);
        struct sigaction dfl{};
        dfl.sa_handler = SIG_DFL;
        sigemptyset(&dfl.sa_mask);
        ::sigaction(SIGABRT, &dfl, nullptr);
        std::abort();
    });
}

void setStage(const char* stage) {
    // Written before a crash can read it half-copied: the end first.
    char tmp[sizeof g_stage];
    std::strncpy(tmp, stage, sizeof tmp - 1);
    tmp[sizeof tmp - 1] = 0;
    std::memcpy(g_stage, tmp, sizeof tmp);
}

void heartbeat() { g_beat = platform::nowMs(); }

void startWatchdog(uint32_t stallMs) {
    heartbeat();
    std::thread([stallMs] {
        bool stalled = false;
        uint64_t last = platform::nowMs();
        for (;;) {
            platform::sleepMs(1000);
            uint64_t now = platform::nowMs();
            // This second took far longer: the whole app was suspended (the
            // console in rest mode), not the main loop stuck. A fresh start
            // for the measure, no marker.
            if (now - last > 5000) {
                XC_LOGI("crash guard: the app was suspended for %llu ms (rest mode)",
                        static_cast<unsigned long long>(now - last));
                heartbeat();
                last = now;
                continue;
            }
            last = now;
            // The main loop may beat between our two readings: never negative.
            uint64_t beat = g_beat.load();
            uint64_t idle = now > beat ? now - beat : 0;
            if (!stalled && idle > stallMs) {
                stalled = true;
                XC_LOGW("crash guard: the main loop has not run for %llu ms (%s)", static_cast<unsigned long long>(idle), g_stage);
                writeMarker("stalled");
            } else if (stalled && idle < 1000) {
                stalled = false;
                XC_LOGI("crash guard: the main loop is back");
                ::unlink(g_marker);
            }
        }
    }).detach();
}

Previous takePrevious(const std::string& markerPath) {
    Previous p;
    std::string text;
    if (!platform::readFile(markerPath, text)) return p;
    ::unlink(markerPath.c_str());
    auto trimCr = [](std::string& s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    };
    size_t nl1 = text.find('\n');
    p.found = true;
    p.kind = text.substr(0, nl1);
    trimCr(p.kind);
    if (nl1 != std::string::npos) {
        size_t nl2 = text.find('\n', nl1 + 1);
        p.stage = text.substr(nl1 + 1, nl2 == std::string::npos ? std::string::npos : nl2 - (nl1 + 1));
        trimCr(p.stage);
        if (nl2 != std::string::npos) {
            p.crash = text.substr(nl2 + 1);
            trimCr(p.crash);
        }
    }
    return p;
}

}  // namespace xc::app::crash
