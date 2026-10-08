// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Thin platform layer: everything that differs between the Linux host build
// and the PS5 build lives behind these functions (see src/platform/host and
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

// Writable directory for tokens, caches and logs (no trailing slash).
std::string dataDir();
// Read-only directory with the bundled assets (fonts, certificates).
std::string assetDir();
// Read-only PEM bundle of trusted root certificates.
std::string caBundlePath();

bool randomBytes(void* out, size_t len);

// Host name -> IPv4 address (network byte order). The PS5 app sandbox has no
// resolv.conf, so the console goes through sceNetResolver instead of
// getaddrinfo. Returns false (with a message in `err`) on failure.
bool resolveIPv4(const std::string& host, uint32_t& addr, std::string& err);
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

// Short on-screen message (PS5 system notification / host stdout).
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

bool readFile(const std::string& path, std::string& out);
bool writeFileAtomic(const std::string& path, const std::string& data);

}  // namespace xc::platform
