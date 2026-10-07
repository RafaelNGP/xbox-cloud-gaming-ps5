// Thin platform layer: everything that differs between the Linux host build
// and the PS5 build lives behind these functions (see src/platform/host and
// src/platform/ps5).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace xc::platform {

// Called once at start-up, before anything else.
bool init();
void shutdown();

// Writable directory for tokens, caches and logs (no trailing slash).
std::string dataDir();
// Read-only PEM bundle of trusted root certificates.
std::string caBundlePath();

bool randomBytes(void* out, size_t len);

// Host name -> IPv4 address (network byte order). The PS5 app sandbox has no
// resolv.conf, so the console goes through sceNetResolver instead of
// getaddrinfo. Returns false (with a message in `err`) on failure.
bool resolveIPv4(const std::string& host, uint32_t& addr, std::string& err);
uint64_t nowMs();  // monotonic
void sleepMs(uint32_t ms);

// Short on-screen message (PS5 system notification / host stdout).
void notify(const std::string& text);

bool readFile(const std::string& path, std::string& out);
bool writeFileAtomic(const std::string& path, const std::string& data);

}  // namespace xc::platform
