// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Thin platform layer: everything that differs between the desktop builds
// and the PS5 build lives behind these functions (see src/platform/linux and
// src/platform/ps5).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace xc::platform {

// Called once at start-up, before anything else.
bool init();
void shutdown();

// Writable directory for tokens, caches and logs (no trailing slash). On the
// PS5 outside the app's folder, so uninstalling, reinstalling or a store
// update keeps them; on Linux $XCLOUD_DATA_DIR, else $XDG_DATA_HOME/psbox
// (~/.local/share/psbox).
std::string dataDir();
// What start-up did about the data folder (moved from the app's folder,
// fell back...), for the log once it is open; empty when nothing.
std::string dataDirNote();
// Whether the data folder is writable (false when permission or filesystem error occurred).
bool dataDirWritable();
// If dataDirWritable() is false, the errno from the write probe.
int dataDirErrno();
// The folder the app runs from (updates are installed there).
std::string appDir();
// Read-only directory with the bundled assets (fonts, certificates).
std::string assetDir();
// Read-only PEM bundle of trusted root certificates.
std::string caBundlePath();

bool randomBytes(void* out, size_t len);

// Host name -> IPv4 address (network byte order). The PS5 app sandbox has no
// resolv.conf, so the console goes through sceNetResolver instead of
// getaddrinfo. Returns false (with a message in `err`) on failure.
bool resolveIPv4(const std::string& host, uint32_t& addr, std::string& err);
// Returns the active local IPv4 address (e.g. "192.168.1.50") or "127.0.0.1".
std::string localIPv4();
uint64_t nowMs();  // monotonic
uint64_t nowUs();  // monotonic
void sleepMs(uint32_t ms);

// Logs which socket operations work (PS5 diagnostics; no-op on the host).
void probeNetworking();

// Starts a joinable thread with an explicit stack size (the console's default
// thread stack is too small for the H.264 decoder). Returns false on failure.
struct Thread {
    void* handle = nullptr;
    bool joinable() const { return handle != nullptr; }
    void join();
};
bool startThread(Thread& t, std::function<void()> fn, size_t stackBytes = 8u << 20);

// Hardware core pinning and real-time scheduling for low latency and zero stutter.
enum class ThreadRole {
    Main,        // Core 0: Main thread, UI, Vulkan command submission
    Network,     // Core 1: WebRTC I/O, SRTP decryption, Jitter buffer
    VideoDecode, // Core 2: Video decoder (FFmpeg / libavcodec)
    Audio,       // Core 3: Audio decode & libSceAudioOut playback (Realtime priority)
    Worker,      // Core 4..7: Background tasks, library hydration, image decoding
};
void setThreadAffinity(ThreadRole role, const char* name = nullptr);
void setThreadRealtimePriority();

// Short on-screen message: the PS5's system notification; in the desktop
// app a toast in the window (platform/toasts.h); stdout for the tools.
void notify(const std::string& text);
// The same, with `forLog` written to the log instead of `text` (which may
// name the user: the log gets attached to public bug reports).
void notify(const std::string& text, const std::string& forLog);

// The console's own on-screen keyboard (sceImeDialog), one at a time.
// openSystemKeyboard() is false where there is none (the host, or a console
// that refuses the module); pollSystemKeyboard() then reports Closed.
enum class KeyboardStatus { Closed, Open, Accepted, Cancelled };
enum class KeyboardKind { Text, Password, Number, Email, Url };
bool systemKeyboardAvailable();
bool openSystemKeyboard(const std::string& title, const std::string& text, size_t maxLength,
                        KeyboardKind kind = KeyboardKind::Text);
// Accepted/Cancelled once, when the player closes it; `text` (UTF-8) on Accepted.
KeyboardStatus pollSystemKeyboard(std::string& text);

// Starts the app again from its eboot.bin (after an update); returns only
// when it can't (false; the host never can).
bool restartApp();

// The console's system software ("13.00"); empty where unknown.
std::string firmwareVersion();

// Platform description ("PlayStation 4", "PlayStation 5", "Linux", etc.).
std::string platformName();

bool readFile(const std::string& path, std::string& out);
bool writeFileAtomic(const std::string& path, const std::string& data);
// A file holding the sign-in, once written: readable by this user only on a
// desktop (mode 0600); nothing on the PS5, which has no other users.
void makePrivate(const std::string& path);

}  // namespace xc::platform
