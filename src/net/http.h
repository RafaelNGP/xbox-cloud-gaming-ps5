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
// request; returns false when the file is missing or holds no certificates.
bool initTls(const std::string& caBundlePath);
// Trusts the certificates in this PEM file too (the update test's local
// server); false when it holds none.
bool addTrustedCa(const std::string& pemPath);
// Why initTls() failed (the step and Mbed TLS's message); empty otherwise.
std::string tlsInitError();
void shutdownTls();

Response perform(const Request& req);

// Helpers.
std::string urlEncode(const std::string& s);
std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields);

}  // namespace xc::net
