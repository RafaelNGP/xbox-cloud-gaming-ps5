// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Blocking HTTPS/1.1 client over BSD sockets + mbedTLS.
//
// One code path for the Linux host build and the PS5: the payload SDK exposes
// FreeBSD sockets and getaddrinfo, and mbedTLS is plain C, so neither libcurl
// nor the console's libSceHttp/libSceSsl is required.
#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xc::net {

struct Request {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int timeoutMs = 20000;
    // Follows 301/302/303/307/308 (five at most), without the Authorization
    // header once on another host.
    bool followRedirects = false;
    // A successful (2xx) body goes here as it arrives instead of into
    // Response::body (a large download); returning false stops it. With
    // `onProgress`: bytes so far, and the total (-1 unknown).
    std::function<bool(const char* data, size_t len)> onBody;
    std::function<void(size_t got, long total)> onProgress;
};

struct Response {
    int status = 0;           // 0 = transport failure (see `error`)
    std::string error;
    std::map<std::string, std::string> headers;  // keys lower-cased
    std::string body;

    bool ok() const { return status >= 200 && status < 300; }
    std::string header(const std::string& lowerName) const {
        auto it = headers.find(lowerName);
        return it == headers.end() ? std::string() : it->second;
    }
};

struct Url {
    std::string scheme, host, path;
    int port = 0;
    static bool parse(const std::string& url, Url& out);
};

// Loads trusted roots from a PEM bundle. Must be called once before any
// request. When the file can't be read (an incomplete copy of the app), the
// copy built into the binary is used, and the log says so; false only when
// neither works.
bool initTls(const std::string& caBundlePath);
// True when initTls() fell back to the built-in copy.
bool usingBuiltinCa();

// Why the last secure connection refused the server's certificate, until a
// handshake succeeds again: the console's clock (the certificate isn't valid
// at its date: expired or not yet), a certificate no trusted authority signed,
// one for another name (both: something on the network answering for the
// site), or another reason.
enum class TlsProblem { None, Clock, Untrusted, NameMismatch, Other };
TlsProblem lastTlsProblem();
// Since start, a Microsoft service (xboxlive.com, (s-)microsoft.com, live.com,
// xbox.com, gamepass.com) refused for the clock: the console's date is
// surely wrong, even when signing in still works (certificates renewed after
// that date fail: prices, images, the user's consoles...). Kept for the whole
// session.
bool clockProblemSeen();
// The hosts that count for clockProblemSeen().
bool microsoftHost(const std::string& host);
// The console's date, "2026-10-14" (for the messages about its clock).
std::string todayText();
// Trusts the certificates in this PEM file too (the update test's local
// server); false when it holds none.
bool addTrustedCa(const std::string& pemPath);
// Why initTls() failed (the step and Mbed TLS's message); empty otherwise.
std::string tlsInitError();
void shutdownTls();

Response perform(const Request& req);

// Spaces the requests to Microsoft's services (*.xboxlive.com, login.live.com):
// five a second per host at most, and after an HTTP 429 none to that host
// until its Retry-After (30 s at most) — fewer bursts an account could be
// flagged for. Used by perform(); a class for the tests (`nowMs` passed in).
class HostPacer {
public:
    // How long to wait before sending to `host` (0: now); reserves its slot.
    uint64_t reserve(const std::string& host, uint64_t nowMs);
    // `host` answered 429 with Retry-After `seconds` (<= 0: unknown).
    void throttled(const std::string& host, int seconds, uint64_t nowMs);
    static bool paced(const std::string& host);

private:
    std::mutex mutex_;
    std::map<std::string, uint64_t> next_;  // host -> earliest next send (ms)
};

// --- For the tests -------------------------------------------------------------
// Answers every perform() instead of the network; empty: the network again.
void setTestTransport(std::function<Response(const Request&)> transport);
// A chunked body decoded; false on malformed framing.
bool dechunk(const std::string& in, std::string& out);

// Helpers.
std::string urlEncode(const std::string& s);
std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields);

}  // namespace xc::net
