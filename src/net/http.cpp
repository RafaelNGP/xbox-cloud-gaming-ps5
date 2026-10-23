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
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <ctime>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>

// From mbedtls/net_sockets.h; that module is not built for the PS5 (we bring
// our own sockets), so only its error values are borrowed.
#ifndef MBEDTLS_ERR_NET_SEND_FAILED
#define MBEDTLS_ERR_NET_SEND_FAILED -0x004E
#endif
#ifndef MBEDTLS_ERR_NET_RECV_FAILED
#define MBEDTLS_ERR_NET_RECV_FAILED -0x004C
#endif

// The CA bundle built in (cmake/embed_file.cmake from assets/cacert.pem).
extern const unsigned char xc_builtin_ca_bundle[];
extern const std::size_t xc_builtin_ca_bundle_size;

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
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;  // a connection the server dropped: an error, not SIGPIPE
#endif
    ssize_t n = ::send(fd, buf, len, flags);
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

// Each request's own time limits (a kept-alive socket keeps the last ones).
void setTimeouts(int fd, int timeoutMs) {
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

int connectTcp(const std::string& host, int port, int timeoutMs, std::string& err) {
    uint32_t ip = 0;
    if (!platform::resolveIPv4(host, ip, err)) return -1;
    uint32_t hostOrderIp = ntohl(ip);
    char ipStr[32];
    std::snprintf(ipStr, sizeof ipStr, "%u.%u.%u.%u",
                  (hostOrderIp >> 24) & 0xFF, (hostOrderIp >> 16) & 0xFF,
                  (hostOrderIp >> 8) & 0xFF, hostOrderIp & 0xFF);
    if (host != "localhost" && host != "127.0.0.1" &&
        (hostOrderIp == 0 || (hostOrderIp >> 24) == 127)) {
        err = "DNS lookup failed for " + host + " (sinkhole IP " + ipStr + ")";
        return -1;
    }
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(port));
    sa.sin_addr.s_addr = ip;

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        err = "socket() failed (errno " + std::to_string(errno) + ")";
        return -1;
    }
    setTimeouts(fd, timeoutMs);
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    // A larger window: catalog responses are hundreds of KB from far away.
    int buf = 1 << 20;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof buf);

    int on = 1;
    ::ioctl(fd, FIONBIO, &on);
    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
    if (rc != 0) {
        if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
            struct pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLOUT;
            int connectTimeout = (timeoutMs > 0) ? std::min(timeoutMs, 10000) : 10000;
            int pr = ::poll(&pfd, 1, connectTimeout);
            if (pr <= 0) {
                err = (pr == 0) ? "connect timed out to " + host + ":" + std::to_string(port) + " [" + ipStr + "]"
                                : "poll failed connecting to " + host + ":" + std::to_string(port) + " [" + ipStr + "] (errno " + std::to_string(errno) + ")";
                ::close(fd);
                return -1;
            }
            int so_error = 0;
            socklen_t len = sizeof so_error;
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0) {
                err = "connect failed to " + host + ":" + std::to_string(port) + " [" + ipStr + "] (errno " + std::to_string(so_error ? so_error : errno) + ")";
                ::close(fd);
                return -1;
            }
        } else {
            err = "connect failed to " + host + ":" + std::to_string(port) + " [" + ipStr + "] (errno " + std::to_string(errno) + ")";
            ::close(fd);
            return -1;
        }
    }
    on = 0;
    ::ioctl(fd, FIONBIO, &on);
    return fd;
}

std::function<Response(const Request&)> g_testTransport;

}  // namespace

bool dechunk(const std::string& in, std::string& out) {
    size_t pos = 0;
    out.clear();
    for (;;) {
        size_t eol = in.find("\r\n", pos);
        if (eol == std::string::npos) return false;
        std::string line = in.substr(pos, eol - pos);
        char* end = nullptr;
        unsigned long long size = std::strtoull(line.c_str(), &end, 16);
        // A hexadecimal size, then nothing or an extension (";...").
        if (end == line.c_str() || (*end && *end != ';' && *end != ' ')) return false;
        pos = eol + 2;
        if (size == 0) return true;
        if (size > in.size() - pos || in.size() - pos - size < 2 || in.compare(pos + size, 2, "\r\n") != 0)
            return false;
        out.append(in, pos, size);
        pos += size + 2;
    }
}

long long chunkedLength(std::string_view in) {
    size_t pos = 0;
    for (;;) {
        size_t eol = in.find("\r\n", pos);
        if (eol == std::string_view::npos) return in.size() - pos > 1024 ? -1 : 0;  // no size line is that long
        std::string line(in.substr(pos, eol - pos));
        char* end = nullptr;
        unsigned long long size = std::strtoull(line.c_str(), &end, 16);
        if (end == line.c_str() || (*end && *end != ';' && *end != ' ')) return -1;
        if (size > (1ull << 31)) return -1;  // no answer here is that large
        pos = eol + 2;
        if (size == 0) {
            // Trailers, if any, then an empty line.
            for (;;) {
                size_t e = in.find("\r\n", pos);
                if (e == std::string_view::npos) return 0;
                if (e == pos) return static_cast<long long>(pos + 2);
                pos = e + 2;
            }
        }
        if (in.size() - pos < size + 2) return 0;
        if (in.compare(pos + size, 2, "\r\n") != 0) return -1;
        pos += size + 2;
    }
}

bool keepAliveAfter(int status, bool http10, const std::string& connection, long long bodyEnd, size_t received) {
    if (status <= 0 || bodyEnd < 0 || static_cast<unsigned long long>(bodyEnd) != received) return false;
    std::string c = lower(connection);
    if (c.find("close") != std::string::npos) return false;
    return !http10 || c.find("keep-alive") != std::string::npos;
}

void setTestTransport(std::function<Response(const Request&)> transport) { g_testTransport = std::move(transport); }

bool HostPacer::paced(const std::string& host) {
    auto endsWith = [&](const char* tail) {
        size_t n = std::strlen(tail);
        return host.size() >= n && host.compare(host.size() - n, n, tail) == 0;
    };
    return endsWith(".xboxlive.com") || host == "login.live.com";
}

uint64_t HostPacer::reserve(const std::string& host, uint64_t nowMs) {
    constexpr uint64_t kSpacingMs = 200;  // five a second
    if (!paced(host)) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t& next = next_[host];
    uint64_t at = std::max(next, nowMs);
    next = at + kSpacingMs;
    return at - nowMs;
}

void HostPacer::throttled(const std::string& host, int seconds, uint64_t nowMs) {
    if (!paced(host)) return;
    uint64_t waitMs = static_cast<uint64_t>(std::clamp(seconds > 0 ? seconds : 5, 1, 30)) * 1000u;
    std::lock_guard<std::mutex> lock(mutex_);
    next_[host] = std::max(next_[host], nowMs + waitMs);
}



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

namespace {
std::string g_initError;
bool g_builtinCa = false;
}

std::atomic<int> g_tlsProblem{0};  // TlsProblem

std::atomic<unsigned> g_clockRefusals{0};

TlsProblem lastTlsProblem() { return static_cast<TlsProblem>(g_tlsProblem.load()); }
unsigned clockRefusals() { return g_clockRefusals; }

bool microsoftHost(const std::string& host) {
    for (const char* domain : {"xboxlive.com", "microsoft.com", "s-microsoft.com", "live.com", "xbox.com", "gamepass.com"}) {
        std::string d = domain;
        if (host == d || (host.size() > d.size() && host.compare(host.size() - d.size() - 1, d.size() + 1, "." + d) == 0))
            return true;
    }
    return false;
}

std::string todayText() {
    std::time_t now = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&now, &utc);
    char text[16];
    std::strftime(text, sizeof text, "%Y-%m-%d", &utc);
    return text;
}

bool usingBuiltinCa() {
    std::lock_guard<std::mutex> lock(g.mutex);
    return g_builtinCa;
}

std::string tlsInitError() {
    std::lock_guard<std::mutex> lock(g.mutex);
    return g_initError;
}

bool initTls(const std::string& caBundlePath) {
    std::lock_guard<std::mutex> lock(g.mutex);
    if (g.ready) return true;
    auto fail = [](std::string why) {
        XC_LOGE("TLS setup: %s", why.c_str());
        g_initError = std::move(why);
        return false;
    };
#if defined(MBEDTLS_PSA_CRYPTO_C)
    // Gathers entropy from the system (getrandom / kern.arandom) first.
    if (psa_status_t st = psa_crypto_init(); st != PSA_SUCCESS)
        return fail("crypto init (system random source?): PSA error " + std::to_string(static_cast<int>(st)));
#endif
    mbedtls_entropy_init(&g.entropy);
    mbedtls_ctr_drbg_init(&g.drbg);
    mbedtls_x509_crt_init(&g.ca);
    mbedtls_entropy_add_source(&g.entropy, platformEntropy, nullptr, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);
    const char* pers = "xcloud-ps5";
    int rc = mbedtls_ctr_drbg_seed(&g.drbg, mbedtls_entropy_func, &g.entropy,
                                   reinterpret_cast<const unsigned char*>(pers), std::strlen(pers));
    if (rc != 0) return fail("random generator (system random source?): " + tlsError(rc));
    rc = mbedtls_x509_crt_parse_file(&g.ca, caBundlePath.c_str());
    if (rc < 0) {
        // The file is missing or unreadable: the same bundle, built in. The
        // install stays broken (other files may be missing too): logged.
        XC_LOGW("TLS setup: certificates %s: %s; using the built-in copy (reinstall the app folder)",
                caBundlePath.c_str(), tlsError(rc).c_str());
        mbedtls_x509_crt_free(&g.ca);
        mbedtls_x509_crt_init(&g.ca);
        rc = mbedtls_x509_crt_parse(&g.ca, xc_builtin_ca_bundle, xc_builtin_ca_bundle_size);
        if (rc < 0) return fail("certificates " + caBundlePath + " and the built-in copy: " + tlsError(rc));
        g_builtinCa = true;
    }
    if (rc > 0) XC_LOGW("CA bundle: %d certificates could not be parsed (ignored)", rc);
    g.ready = true;
    return true;
}

bool addTrustedCa(const std::string& pemPath) {
    std::lock_guard<std::mutex> lock(g.mutex);
    if (!g.ready) return false;
    int rc = mbedtls_x509_crt_parse_file(&g.ca, pemPath.c_str());
    XC_LOGI("extra CA %s: %s", pemPath.c_str(), rc == 0 ? "trusted" : tlsError(rc).c_str());
    return rc == 0;
}

namespace {

// A TLS connection, kept open between requests to the same host.
struct PooledConn {
    std::string host;
    int port = 443;
    int fd = -1;
    bool tls = false;  // ssl and conf initialised
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    uint64_t lastUsedMs = 0;

    PooledConn() = default;
    PooledConn(const PooledConn&) = delete;
    PooledConn& operator=(const PooledConn&) = delete;
    // No close_notify: a write that could block (or hit a dropped peer) for
    // nothing; the server is fine with the TCP close.
    ~PooledConn() {
        if (tls) {
            mbedtls_ssl_free(&ssl);
            mbedtls_ssl_config_free(&conf);
        }
        if (fd >= 0) ::close(fd);
    }
};
using Conns = std::vector<std::unique_ptr<PooledConn>>;

// Idle kept-alive connections. The ones it closes are collected under its
// lock and closed after it, so a slow close never holds up other threads.
class ConnectionPool {
public:
    // Never destroyed: detached threads may still be in a request at exit.
    static ConnectionPool& instance() {
        static ConnectionPool* pool = new ConnectionPool;
        return *pool;
    }

    std::unique_ptr<PooledConn> acquire(const std::string& host, int port) {
        Conns dead;  // closed after the lock (declared before it)
        std::unique_ptr<PooledConn> found;
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t now = platform::nowMs();
        for (auto it = pool_.begin(); it != pool_.end();) {
            bool mine = !found && (*it)->host == host && (*it)->port == port;
            bool expired = now - (*it)->lastUsedMs > kIdleMs;
            if (mine && !expired) {
                // Readable while idle: the server closed it (or sent
                // something unasked); not usable either way.
                struct pollfd pfd{(*it)->fd, POLLIN, 0};
                if (::poll(&pfd, 1, 0) == 0) {
                    found = std::move(*it);
                    it = pool_.erase(it);
                    continue;
                }
                expired = true;
            }
            if (expired) {
                dead.push_back(std::move(*it));
                it = pool_.erase(it);
                continue;
            }
            ++it;
        }
        return found;
    }

    void release(std::unique_ptr<PooledConn> conn) {
        if (!conn) return;
        Conns dead;
        std::lock_guard<std::mutex> lock(mutex_);
        if (pool_.size() >= kMaxIdle) {  // the oldest makes room
            dead.push_back(std::move(pool_.front()));
            pool_.erase(pool_.begin());
        }
        conn->lastUsedMs = platform::nowMs();
        pool_.push_back(std::move(conn));
    }

    // A kept-alive connection to this host turned out dead: the others,
    // idle as long or longer, likely are too.
    void dropHost(const std::string& host, int port) {
        Conns dead;
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = pool_.begin(); it != pool_.end();) {
            if ((*it)->host == host && (*it)->port == port) {
                dead.push_back(std::move(*it));
                it = pool_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void clear() {
        Conns dead;
        std::lock_guard<std::mutex> lock(mutex_);
        dead.swap(pool_);
    }

private:
    static constexpr uint64_t kIdleMs = 15000;
    static constexpr size_t kMaxIdle = 8;
    std::mutex mutex_;
    Conns pool_;
};

}  // namespace

void shutdownTls() {
    std::lock_guard<std::mutex> lock(g.mutex);
    if (!g.ready) return;
    ConnectionPool::instance().clear();
    mbedtls_x509_crt_free(&g.ca);
    mbedtls_ctr_drbg_free(&g.drbg);
    mbedtls_entropy_free(&g.entropy);
    g.ready = false;
}

namespace {

struct VerifyChainCtx {
    mbedtls_x509_crt* ca = nullptr;
    const char* host = nullptr;
    uint32_t deferredFlags = 0;
};

// Some regional Azure load balancers (e.g. CIA.core.gssv-play-prod.xboxlive.com
// in Central India) deliver intermediate certificates out of RFC 5246 order
// ([leaf, root, intermediate2, intermediate1] where intermediate1 signs leaf
// and intermediate2 signs intermediate1). Stock mbedTLS only searches
// child->next for each parent, failing with MBEDTLS_X509_BADCERT_NOT_TRUSTED.
// When that happens on a multi-certificate peer chain, reconstruct the
// issuer-ordered chain from the full peer list and re-verify it against `ca`.
int verifyPeerChainCallback(void* data, mbedtls_x509_crt* crt, int depth, uint32_t* flags) {
    auto* ctx = static_cast<VerifyChainCtx*>(data);
    if (!ctx || !ctx->ca || !crt || !flags) return 0;
    if (depth > 0) {
        if (*flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) {
            ctx->deferredFlags |= (*flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED);
            *flags &= ~static_cast<uint32_t>(MBEDTLS_X509_BADCERT_NOT_TRUSTED);
        }
        return 0;
    }
    *flags |= ctx->deferredFlags;
    ctx->deferredFlags = 0;
    if (!(*flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) || !crt->next) return 0;

    std::vector<const mbedtls_x509_crt*> all;
    for (const mbedtls_x509_crt* c = crt; c != nullptr && all.size() < 16; c = c->next)
        all.push_back(c);

    std::vector<const mbedtls_x509_crt*> ordered;
    ordered.push_back(crt);
    bool orderChanged = false;
    while (ordered.size() < all.size()) {
        const mbedtls_x509_crt* child = ordered.back();
        const mbedtls_x509_crt* nextParent = nullptr;
        for (const mbedtls_x509_crt* cand : all) {
            if (std::find(ordered.begin(), ordered.end(), cand) != ordered.end()) continue;
            if (child->issuer_raw.len == cand->subject_raw.len &&
                std::memcmp(child->issuer_raw.p, cand->subject_raw.p, child->issuer_raw.len) == 0) {
                nextParent = cand;
                break;
            }
        }
        if (!nextParent) break;
        if (nextParent != child->next) orderChanged = true;
        ordered.push_back(nextParent);
    }
    if (!orderChanged) return 0;

    mbedtls_x509_crt reordered;
    mbedtls_x509_crt_init(&reordered);
    bool parsedOk = true;
    for (const mbedtls_x509_crt* c : ordered) {
        if (mbedtls_x509_crt_parse_der(&reordered, c->raw.p, c->raw.len) != 0) {
            parsedOk = false;
            break;
        }
    }
    if (parsedOk) {
        uint32_t newFlags = 0;
        int vrc = mbedtls_x509_crt_verify(&reordered, ctx->ca, nullptr, ctx->host, &newFlags, nullptr, nullptr);
        if (vrc == 0) {
            *flags = 0;
        } else if (vrc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
            *flags = newFlags;
        }
    }
    mbedtls_x509_crt_free(&reordered);
    return 0;
}

// One request on one connection: a kept-alive one from the pool when
// `reuse` and there is one, else a new one. `stale` is set (and nothing
// else is to be trusted in the answer) when a kept-alive connection failed
// before a single byte of the answer came: the server had closed it, never
// saw the request, and it can be sent again on a new connection, whatever
// its method. A time-out is not that (the server may be working on it).
Response exchange(const Request& req, const Url& url, bool reuse, bool& stale) {
    Response resp;
    stale = false;
    const uint64_t t0 = platform::nowMs();
    auto conn = reuse ? ConnectionPool::instance().acquire(url.host, url.port) : nullptr;
    const bool isReused = conn != nullptr;
    if (isReused) setTimeouts(conn->fd, req.timeoutMs);
    uint64_t tConnected = t0;
    uint64_t tHandshake = t0;

    bool canKeepAlive = false;
    auto finish = [&](std::string err) {
        if (!err.empty()) resp.error = std::move(err);
        uint64_t total = platform::nowMs() - t0;
        if (total > 1500)
            XC_LOGI("slow request %s%s: connect %llu ms, TLS %llu ms, total %llu ms", url.host.c_str(),
                    url.path.substr(0, 40).c_str(), static_cast<unsigned long long>(tConnected - t0),
                    static_cast<unsigned long long>(tHandshake - tConnected), static_cast<unsigned long long>(total));
        if (conn && canKeepAlive) ConnectionPool::instance().release(std::move(conn));
        conn.reset();
        return resp;
    };
    auto staleReuse = [&] {
        stale = true;
        conn.reset();
        return resp;
    };

    if (!conn) {
        conn = std::make_unique<PooledConn>();
        conn->host = url.host;
        conn->port = url.port;
        conn->fd = connectTcp(url.host, url.port, req.timeoutMs, resp.error);
        if (conn->fd < 0) return resp;
        tConnected = platform::nowMs();
        tHandshake = tConnected;

        mbedtls_ssl_init(&conn->ssl);
        mbedtls_ssl_config_init(&conn->conf);
        conn->tls = true;

        int rc = mbedtls_ssl_config_defaults(&conn->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                             MBEDTLS_SSL_PRESET_DEFAULT);
        if (rc) return finish("ssl_config_defaults: " + tlsError(rc));
        mbedtls_ssl_conf_authmode(&conn->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&conn->conf, &g.ca, nullptr);
        VerifyChainCtx verifyCtx{&g.ca, url.host.c_str(), 0};
        mbedtls_ssl_conf_verify(&conn->conf, verifyPeerChainCallback, &verifyCtx);
        mbedtls_ssl_conf_rng(&conn->conf, mbedtls_ctr_drbg_random, &g.drbg);
        if ((rc = mbedtls_ssl_setup(&conn->ssl, &conn->conf))) return finish("ssl_setup: " + tlsError(rc));
        if ((rc = mbedtls_ssl_set_hostname(&conn->ssl, url.host.c_str()))) return finish("set_hostname: " + tlsError(rc));
        mbedtls_ssl_set_bio(&conn->ssl, &conn->fd, bioSend, bioRecv, nullptr);

        while ((rc = mbedtls_ssl_handshake(&conn->ssl)) != 0) {
            if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (rc != MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
                return finish("TLS handshake with " + url.host + ": " + tlsError(rc));
            uint32_t flags = mbedtls_ssl_get_verify_result(&conn->ssl);
            TlsProblem problem = flags & (MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE)
                                     ? TlsProblem::Clock
                                 : flags & MBEDTLS_X509_BADCERT_CN_MISMATCH ? TlsProblem::NameMismatch
                                 : flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED ? TlsProblem::Untrusted
                                                                            : TlsProblem::Other;
            g_tlsProblem = static_cast<int>(problem);
            if (problem == TlsProblem::Clock && microsoftHost(url.host)) ++g_clockRefusals;
            char info[512];
            int n = mbedtls_x509_crt_verify_info(info, sizeof info, "", flags);
            std::string why = n > 0 ? std::string(info, static_cast<size_t>(n)) : "flags " + std::to_string(flags);
            for (auto& c : why)
                if (c == '\n') c = ';';
            while (!why.empty() && (why.back() == ';' || why.back() == ' ')) why.pop_back();
            return finish("TLS handshake with " + url.host + ": certificate refused (" + why + "; console date " +
                          todayText() + ") " + tlsError(rc));
        }
        g_tlsProblem = static_cast<int>(TlsProblem::None);
        tHandshake = platform::nowMs();
    }

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
    head += "Connection: keep-alive\r\n\r\n";
    std::string out = head + req.body;

    size_t sent = 0;
    while (sent < out.size()) {
        int rc = mbedtls_ssl_write(&conn->ssl, reinterpret_cast<const unsigned char*>(out.data() + sent), out.size() - sent);
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (rc < 0) {
            if (isReused) return staleReuse();  // the server never got the whole request
            return finish("TLS write: " + tlsError(rc));
        }
        sent += static_cast<size_t>(rc);
    }

    // Response: read until the body is complete or the peer closes.
    std::string raw;
    size_t headerEnd = std::string::npos;
    long contentLength = -1;
    bool chunked = false;
    bool http10 = false;
    long long chunkedEnd = 0;  // chunkedLength() of the body so far
    bool streaming = false;  // the body goes to req.onBody
    size_t streamed = 0;
    unsigned char buf[16384];
    for (;;) {
        int rc = mbedtls_ssl_read(&conn->ssl, buf, sizeof buf);
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
#if defined(MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
        if (rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
#endif
        // A kept-alive connection closed (or reset) before any answer.
        if (isReused && raw.empty() && !streaming && rc <= 0 && rc != MBEDTLS_ERR_SSL_TIMEOUT) return staleReuse();
        if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) break;
        if (rc < 0) {
            if (headerEnd != std::string::npos && contentLength < 0 && !chunked) break;  // EOF-delimited
            return finish("TLS read: " + tlsError(rc));
        }
        if (streaming) {
            if (!req.onBody(reinterpret_cast<char*>(buf), static_cast<size_t>(rc))) return finish("download stopped");
            streamed += static_cast<size_t>(rc);
            if (req.onProgress) req.onProgress(streamed, contentLength);
            if (contentLength >= 0 && streamed >= static_cast<size_t>(contentLength)) break;
            continue;
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
            http10 = status.rfind("HTTP/1.0", 0) == 0;
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
            if (req.method == "HEAD" || resp.status == 204 || resp.status == 304) contentLength = 0, chunked = false;  // no body
            if (req.onBody && resp.ok() && !chunked) {
                // What came with the headers, then each read as it arrives.
                streaming = true;
                streamed = raw.size() - (headerEnd + 4);
                if (streamed && !req.onBody(raw.data() + headerEnd + 4, streamed)) return finish("download stopped");
                raw.resize(headerEnd + 4);
                if (req.onProgress) req.onProgress(streamed, contentLength);
                if (contentLength >= 0 && streamed >= static_cast<size_t>(contentLength)) break;
                continue;
            }
        }
        size_t bodyLen = raw.size() - (headerEnd + 4);
        if (chunked) {
            // The framing itself says where the body ends (not a guess on
            // the last bytes read).
            chunkedEnd = chunkedLength(std::string_view(raw).substr(headerEnd + 4));
            if (chunkedEnd != 0) break;  // complete, or broken
        } else if (contentLength >= 0 && bodyLen >= static_cast<size_t>(contentLength)) {
            break;
        }
    }

    if (headerEnd == std::string::npos) return finish("no HTTP response from " + url.host);
    if (streaming) {
        if (contentLength >= 0 && streamed < static_cast<size_t>(contentLength)) {
            resp.status = 0;
            return finish("download cut short");
        }
        canKeepAlive = keepAliveAfter(resp.status, http10, resp.header("connection"), contentLength, streamed);
        return finish({});
    }
    std::string body = raw.substr(headerEnd + 4);
    const size_t received = body.size();
    long long bodyEnd = -1;  // where the framing ends the body; -1: the server's closing did
    if (chunked) {
        if (chunkedEnd <= 0 || !dechunk(body.substr(0, static_cast<size_t>(chunkedEnd)), resp.body)) {
            resp.status = 0;  // not an answer (Response::ok() false)
            resp.body.clear();
            return finish(chunkedEnd == 0 ? "response cut short" : "malformed chunked body");
        }
        bodyEnd = chunkedEnd;
    } else {
        resp.body = std::move(body);
        // The connection closed before all of it came: not an answer.
        if (contentLength >= 0 && resp.body.size() < static_cast<size_t>(contentLength)) {
            resp.status = 0;
            resp.body.clear();
            return finish("response cut short");
        }
        if (contentLength >= 0 && resp.body.size() > static_cast<size_t>(contentLength))
            resp.body.resize(static_cast<size_t>(contentLength));
        bodyEnd = contentLength;
    }
    canKeepAlive = keepAliveAfter(resp.status, http10, resp.header("connection"), bodyEnd, received);
    return finish({});
}

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
    bool stale = false;
    resp = exchange(req, url, true, stale);
    if (stale) {
        // Once, on a new connection, for any method (the request never got
        // there); the other idle connections to the host go too.
        XC_LOGI("%s: kept-alive connection closed by the server; sending again on a new one", url.host.c_str());
        ConnectionPool::instance().dropHost(url.host, url.port);
        resp = exchange(req, url, false, stale);
    }
    return resp;
}

Response performRetrying(const Request& req) {
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

}  // namespace

namespace {
HostPacer g_pacer;

Response performPaced(const Request& req) {
    Url url;
    if (!Url::parse(req.url, url)) return performRetrying(req);
    if (uint64_t waitMs = g_pacer.reserve(url.host, platform::nowMs())) {
        if (waitMs >= 1000) XC_LOGI("%s: waiting %llu ms (paced)", url.host.c_str(), static_cast<unsigned long long>(waitMs));
        platform::sleepMs(static_cast<uint32_t>(waitMs));
    }
    Response resp = performRetrying(req);
    if (resp.status == 429) {
        int seconds = std::atoi(resp.header("retry-after").c_str());
        XC_LOGW("%s: HTTP 429, nothing more to it for %d s", url.host.c_str(), std::clamp(seconds > 0 ? seconds : 5, 1, 30));
        g_pacer.throttled(url.host, seconds, platform::nowMs());
    } else if (resp.status >= 500) {
        XC_LOGW("%s: HTTP %d (%s)", url.host.c_str(), resp.status, req.method.empty() ? "GET" : req.method.c_str());
    }
    return resp;
}
}  // namespace

Response perform(const Request& req) {
    if (g_testTransport) return g_testTransport(req);
    Response resp = performPaced(req);
    Request next = req;
    for (int hops = 0; req.followRedirects && hops < 5; ++hops) {
        int s = resp.status;
        std::string location = resp.header("location");
        if ((s != 301 && s != 302 && s != 303 && s != 307 && s != 308) || location.empty()) break;
        Url from, to;
        Url::parse(next.url, from);
        if (location[0] == '/') {
            bool defaultPort = from.port == (from.scheme == "https" ? 443 : 80);
            location = from.scheme + "://" + from.host + (defaultPort ? "" : ":" + std::to_string(from.port)) + location;
        }
        if (!Url::parse(location, to)) break;
        if (to.host != from.host)
            for (auto it = next.headers.begin(); it != next.headers.end();)
                it = lower(it->first) == "authorization" ? next.headers.erase(it) : it + 1;
        if (s == 303) {
            next.method = "GET";
            next.body.clear();
        }
        next.url = location;
        resp = performPaced(next);
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
