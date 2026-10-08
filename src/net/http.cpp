// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "net/http.h"

#include "platform/platform.h"
#include "util/log.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#if defined(MBEDTLS_PSA_CRYPTO_C)
#include <psa/crypto.h>
#endif

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstring>
#include <mutex>

// From mbedtls/net_sockets.h; that module is not built for the PS5 (we bring
// our own sockets), so only its error values are borrowed.
#ifndef MBEDTLS_ERR_NET_SEND_FAILED
#define MBEDTLS_ERR_NET_SEND_FAILED -0x004E
#endif
#ifndef MBEDTLS_ERR_NET_RECV_FAILED
#define MBEDTLS_ERR_NET_RECV_FAILED -0x004C
#endif

namespace xc::net {

namespace {

struct TlsGlobals {
    std::mutex mutex;
    bool ready = false;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt ca;
};
TlsGlobals g;

int platformEntropy(void*, unsigned char* out, size_t len, size_t* olen) {
    if (!platform::randomBytes(out, len)) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen = len;
    return 0;
}

std::string tlsError(int rc) {
    char buf[160];
    mbedtls_strerror(rc, buf, sizeof buf);
    return std::string(buf) + " (" + std::to_string(rc) + ")";
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int bioSend(void* ctx, const unsigned char* buf, size_t len) {
    int fd = *static_cast<int*>(ctx);
    ssize_t n = ::send(fd, buf, len, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_TIMEOUT;
        if (errno == EINTR) return MBEDTLS_ERR_SSL_WANT_WRITE;
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return static_cast<int>(n);
}

int bioRecv(void* ctx, unsigned char* buf, size_t len) {
    int fd = *static_cast<int*>(ctx);
    ssize_t n = ::recv(fd, buf, len, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_TIMEOUT;
        if (errno == EINTR) return MBEDTLS_ERR_SSL_WANT_READ;
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return static_cast<int>(n);  // 0 = peer closed
}

int connectTcp(const std::string& host, int port, int timeoutMs, std::string& err) {
    uint32_t ip = 0;
    if (!platform::resolveIPv4(host, ip, err)) return -1;
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(port));
    sa.sin_addr.s_addr = ip;

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        err = "socket() failed (errno " + std::to_string(errno) + ")";
        return -1;
    }
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    // A larger window: catalog responses are hundreds of KB from far away.
    int buf = 1 << 20;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof buf);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa) != 0) {
        err = "connect failed to " + host + ":" + std::to_string(port) + " (errno " + std::to_string(errno) + ")";
        ::close(fd);
        return -1;
    }
    return fd;
}

// Decodes a chunked body in place. Returns false on malformed framing.
bool dechunk(const std::string& in, std::string& out) {
    size_t pos = 0;
    out.clear();
    for (;;) {
        size_t eol = in.find("\r\n", pos);
        if (eol == std::string::npos) return false;
        size_t size = std::strtoul(in.substr(pos, eol - pos).c_str(), nullptr, 16);
        pos = eol + 2;
        if (size == 0) return true;
        if (pos + size > in.size()) return false;
        out.append(in, pos, size);
        pos += size + 2;  // skip trailing CRLF
    }
}

}  // namespace

bool Url::parse(const std::string& url, Url& out) {
    size_t s = url.find("://");
    if (s == std::string::npos) return false;
    out.scheme = lower(url.substr(0, s));
    size_t hostStart = s + 3;
    size_t pathStart = url.find('/', hostStart);
    std::string hostPort = url.substr(hostStart, pathStart == std::string::npos ? std::string::npos : pathStart - hostStart);
    out.path = pathStart == std::string::npos ? "/" : url.substr(pathStart);
    size_t colon = hostPort.rfind(':');
    if (colon != std::string::npos && hostPort.find(']') == std::string::npos) {
        out.host = hostPort.substr(0, colon);
        out.port = std::atoi(hostPort.c_str() + colon + 1);
    } else {
        out.host = hostPort;
        out.port = out.scheme == "https" ? 443 : 80;
    }
    return !out.host.empty() && (out.scheme == "https" || out.scheme == "http");
}

bool initTls(const std::string& caBundlePath) {
    std::lock_guard<std::mutex> lock(g.mutex);
    if (g.ready) return true;
#if defined(MBEDTLS_PSA_CRYPTO_C)
    if (psa_crypto_init() != PSA_SUCCESS) {
        XC_LOGE("psa_crypto_init failed");
        return false;
    }
#endif
    mbedtls_entropy_init(&g.entropy);
    mbedtls_ctr_drbg_init(&g.drbg);
    mbedtls_x509_crt_init(&g.ca);
    mbedtls_entropy_add_source(&g.entropy, platformEntropy, nullptr, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);
    const char* pers = "xcloud-ps5";
    int rc = mbedtls_ctr_drbg_seed(&g.drbg, mbedtls_entropy_func, &g.entropy,
                                   reinterpret_cast<const unsigned char*>(pers), std::strlen(pers));
    if (rc != 0) {
        XC_LOGE("ctr_drbg_seed: %s", tlsError(rc).c_str());
        return false;
    }
    rc = mbedtls_x509_crt_parse_file(&g.ca, caBundlePath.c_str());
    if (rc < 0) {
        XC_LOGE("CA bundle %s: %s", caBundlePath.c_str(), tlsError(rc).c_str());
        return false;
    }
    if (rc > 0) XC_LOGW("CA bundle: %d certificates could not be parsed (ignored)", rc);
    g.ready = true;
    return true;
}

void shutdownTls() {
    std::lock_guard<std::mutex> lock(g.mutex);
    if (!g.ready) return;
    mbedtls_x509_crt_free(&g.ca);
    mbedtls_ctr_drbg_free(&g.drbg);
    mbedtls_entropy_free(&g.entropy);
    g.ready = false;
}

namespace {

Response performOnce(const Request& req) {
    Response resp;
    Url url;
    if (!Url::parse(req.url, url)) {
        resp.error = "bad URL: " + req.url;
        return resp;
    }
    if (url.scheme != "https") {
        resp.error = "only https is supported";
        return resp;
    }
    if (!g.ready) {
        resp.error = "TLS not initialised";
        return resp;
    }

    const uint64_t t0 = platform::nowMs();
    int fd = connectTcp(url.host, url.port, req.timeoutMs, resp.error);
    if (fd < 0) return resp;
    const uint64_t tConnected = platform::nowMs();
    uint64_t tHandshake = tConnected;

    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);

    auto finish = [&](std::string err) {
        if (!err.empty()) resp.error = std::move(err);
        uint64_t total = platform::nowMs() - t0;
        if (total > 1500)
            XC_LOGI("slow request %s%s: connect %llu ms, TLS %llu ms, total %llu ms", url.host.c_str(),
                    url.path.substr(0, 40).c_str(), static_cast<unsigned long long>(tConnected - t0),
                    static_cast<unsigned long long>(tHandshake - tConnected), static_cast<unsigned long long>(total));
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        ::close(fd);
        return resp;
    };

    int rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc) return finish("ssl_config_defaults: " + tlsError(rc));
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &g.ca, nullptr);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &g.drbg);
    if ((rc = mbedtls_ssl_setup(&ssl, &conf))) return finish("ssl_setup: " + tlsError(rc));
    if ((rc = mbedtls_ssl_set_hostname(&ssl, url.host.c_str()))) return finish("set_hostname: " + tlsError(rc));
    mbedtls_ssl_set_bio(&ssl, &fd, bioSend, bioRecv, nullptr);

    while ((rc = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE)
            return finish("TLS handshake with " + url.host + ": " + tlsError(rc));
    }
    tHandshake = platform::nowMs();

    // Request.
    std::string head = req.method + " " + url.path + " HTTP/1.1\r\n";
    head += "Host: " + url.host + "\r\n";
    bool hasUA = false, hasAccept = false;
    for (const auto& [k, v] : req.headers) {
        std::string lk = lower(k);
        hasUA |= lk == "user-agent";
        hasAccept |= lk == "accept";
        head += k + ": " + v + "\r\n";
    }
    if (!hasUA) head += "User-Agent: xCloud-PS5/0.1\r\n";
    if (!hasAccept) head += "Accept: application/json\r\n";
    if (!req.body.empty() || req.method == "POST" || req.method == "PUT")
        head += "Content-Length: " + std::to_string(req.body.size()) + "\r\n";
    head += "Connection: close\r\n\r\n";
    std::string out = head + req.body;

    size_t sent = 0;
    while (sent < out.size()) {
        rc = mbedtls_ssl_write(&ssl, reinterpret_cast<const unsigned char*>(out.data() + sent), out.size() - sent);
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (rc < 0) return finish("TLS write: " + tlsError(rc));
        sent += static_cast<size_t>(rc);
    }

    // Response: read until the body is complete or the peer closes.
    std::string raw;
    size_t headerEnd = std::string::npos;
    long contentLength = -1;
    bool chunked = false;
    unsigned char buf[16384];
    for (;;) {
        rc = mbedtls_ssl_read(&ssl, buf, sizeof buf);
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
#if defined(MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
        if (rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
#endif
        if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) break;
        if (rc < 0) {
            if (headerEnd != std::string::npos && contentLength < 0 && !chunked) break;  // EOF-delimited
            return finish("TLS read: " + tlsError(rc));
        }
        raw.append(reinterpret_cast<char*>(buf), static_cast<size_t>(rc));

        if (headerEnd == std::string::npos) {
            headerEnd = raw.find("\r\n\r\n");
            if (headerEnd == std::string::npos) continue;
            // Status line + headers.
            size_t lineEnd = raw.find("\r\n");
            std::string status = raw.substr(0, lineEnd);
            size_t sp = status.find(' ');
            resp.status = sp == std::string::npos ? 0 : std::atoi(status.c_str() + sp + 1);
            size_t pos = lineEnd + 2;
            while (pos < headerEnd) {
                size_t e = raw.find("\r\n", pos);
                std::string line = raw.substr(pos, e - pos);
                pos = e + 2;
                size_t c = line.find(':');
                if (c == std::string::npos) continue;
                std::string key = lower(line.substr(0, c));
                std::string val = line.substr(c + 1);
                while (!val.empty() && val.front() == ' ') val.erase(0, 1);
                resp.headers[key] = val;
            }
            if (resp.headers.count("content-length")) contentLength = std::atol(resp.headers["content-length"].c_str());
            chunked = lower(resp.header("transfer-encoding")).find("chunked") != std::string::npos;
            if (req.method == "HEAD" || resp.status == 204 || resp.status == 304) contentLength = 0;
        }
        size_t bodyLen = raw.size() - (headerEnd + 4);
        if (contentLength >= 0 && bodyLen >= static_cast<size_t>(contentLength)) break;
        if (chunked && raw.size() >= 5 && raw.compare(raw.size() - 5, 5, "0\r\n\r\n") == 0) break;
    }
    mbedtls_ssl_close_notify(&ssl);

    if (headerEnd == std::string::npos) return finish("no HTTP response from " + url.host);
    std::string body = raw.substr(headerEnd + 4);
    if (chunked) {
        if (!dechunk(body, resp.body)) return finish("malformed chunked body");
    } else {
        resp.body = std::move(body);
        if (contentLength >= 0 && resp.body.size() > static_cast<size_t>(contentLength))
            resp.body.resize(static_cast<size_t>(contentLength));
    }
    return finish({});
}

}  // namespace

Response perform(const Request& req) {
    Response resp = performOnce(req);
    // A GET that got no answer at all (the connection or the handshake
    // failed, or timed out) is asked once more: they are seldom and
    // passing, and a GET can be repeated safely.
    if (resp.status == 0 && (req.method.empty() || req.method == "GET") && resp.error.rfind("bad URL", 0) != 0 &&
        resp.error.rfind("only https", 0) != 0 && resp.error != "TLS not initialised") {
        Url url;
        Url::parse(req.url, url);
        XC_LOGW("%s: %s; asking again", url.host.c_str(), resp.error.c_str());
        platform::sleepMs(300);
        resp = performOnce(req);
    }
    return resp;
}

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string out;
    for (const auto& [k, v] : fields) {
        if (!out.empty()) out += '&';
        out += urlEncode(k) + "=" + urlEncode(v);
    }
    return out;
}

}  // namespace xc::net
