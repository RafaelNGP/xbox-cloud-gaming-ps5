// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/web_server.h"
#include "app/report.h"
#include "app/settings.h"
#include "input/key_map.h"
#include "input/pad_map.h"
#include "platform/platform.h"
#include "ui/app_ui.h"
#include "util/json.h"
#include "util/log.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace xc::app {

static WebServerHooks s_hooks;

namespace {

std::string extractJsonField(const std::string& body, const std::string& field) {
    auto j = json::parse(body);
    if (j && j->isObject()) {
        auto val = (*j)[field];
        if (val.isString()) return val.str();
    }
    // Fallback: simple urlencoded or key=value
    std::string needle = field + "=";
    auto pos = body.find(needle);
    if (pos != std::string::npos) {
        auto end = body.find('&', pos);
        if (end == std::string::npos) end = body.size();
        return body.substr(pos + needle.size(), end - (pos + needle.size()));
    }
    return "";
}

}  // namespace

std::unique_ptr<WebServer> g_webServer;

WebServer::WebServer() = default;

WebServer::~WebServer() {
    stop();
}

std::string WebServer::localIp() const {
    return platform::localIPv4();
}

std::string WebServer::url() const {
    return "http://" + localIp() + ":" + std::to_string(port_);
}

bool WebServer::start(uint16_t port) {
#ifdef SIGPIPE
    ::signal(SIGPIPE, SIG_IGN);
#endif
    if (running_.load()) return true;
    port_ = port;

    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        XC_LOGW("web server: socket() failed (errno %d)", errno);
        return false;
    }

    int one = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        XC_LOGW("web server: bind(%u) failed (errno %d)", port_, errno);
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    if (::listen(listenFd_, 8) < 0) {
        XC_LOGW("web server: listen() failed (errno %d)", errno);
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    running_.store(true);
    if (!platform::startThread(thread_, [this] { threadLoop(); })) {
        XC_LOGW("web server: failed to start thread");
        running_.store(false);
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    XC_LOGI("web server: running at %s", url().c_str());
    return true;
}

void WebServer::stop() {
    if (!running_.exchange(false)) return;
    if (listenFd_ >= 0) {
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
        listenFd_ = -1;
    }
    {
        std::lock_guard<std::mutex> lock(wsMutex_);
        for (int fd : wsClients_) {
            ::shutdown(fd, SHUT_RDWR);
        }
        wsClients_.clear();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    XC_LOGI("web server: stopped");
}

void WebServer::threadLoop() {
    while (running_.load()) {
        pollfd pfd{listenFd_, POLLIN, 0};
        int pr = ::poll(&pfd, 1, 250);
        if (pr <= 0) continue;
        if (pfd.revents & POLLIN) {
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            int clientFd = ::accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
            if (clientFd >= 0) {
                std::thread([this, clientFd]() {
                    handleClient(clientFd);
                    ::close(clientFd);
                }).detach();
            }
        }
    }
}

void WebServer::handleClient(int clientFd) {
    timeval tv{2, 0};  // 2s timeout prevents hung clients from deadlocking server thread
    ::setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(clientFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#ifdef SO_NOSIGPIPE
    int nosig = 1;
    ::setsockopt(clientFd, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof(nosig));
#endif

    std::string req;
    char buf[8192];
    size_t contentLength = 0;
    bool headersComplete = false;

    while (req.size() < 65536) {
        ssize_t n = ::recv(clientFd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, static_cast<size_t>(n));

        if (!headersComplete) {
            auto hdrEnd = req.find("\r\n\r\n");
            if (hdrEnd != std::string::npos) {
                headersComplete = true;
                std::string lowerHdr = req.substr(0, hdrEnd);
                for (char& c : lowerHdr) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                auto clPos = lowerHdr.find("content-length:");
                if (clPos != std::string::npos) {
                    size_t valStart = clPos + 15;
                    while (valStart < hdrEnd && (lowerHdr[valStart] == ' ' || lowerHdr[valStart] == '\t')) valStart++;
                    size_t valEnd = lowerHdr.find("\r\n", valStart);
                    if (valEnd != std::string::npos) {
                        try {
                            contentLength = std::stoul(lowerHdr.substr(valStart, valEnd - valStart));
                        } catch (...) {}
                    }
                }
            }
        }

        if (headersComplete) {
            auto hdrEnd = req.find("\r\n\r\n");
            size_t bodyReceived = req.size() - (hdrEnd + 4);
            if (bodyReceived >= contentLength) {
                break;
            }
        }
    }

    if (req.empty()) return;

    auto lineEnd = req.find("\r\n");
    if (lineEnd == std::string::npos) lineEnd = req.find('\n');
    if (lineEnd == std::string::npos) return;

    std::string firstLine = req.substr(0, lineEnd);
    auto sp1 = firstLine.find(' ');
    auto sp2 = firstLine.rfind(' ');
    if (sp1 == std::string::npos || sp2 == std::string::npos || sp1 >= sp2) return;

    std::string method = firstLine.substr(0, sp1);
    std::string path = firstLine.substr(sp1 + 1, sp2 - (sp1 + 1));

    // Strip query string if any from path
    auto qpos = path.find('?');
    if (qpos != std::string::npos) path = path.substr(0, qpos);

    // WebSocket upgrade check
    auto hdrEnd = req.find("\r\n\r\n");
    if (hdrEnd != std::string::npos) {
        std::string lowerHdr = req.substr(0, hdrEnd);
        for (char& c : lowerHdr) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        bool isWs = (lowerHdr.find("upgrade: websocket") != std::string::npos ||
                     (lowerHdr.find("upgrade:") != std::string::npos && lowerHdr.find("websocket") != std::string::npos));
        if (isWs && (path == "/ws" || path == "/api/ws")) {
            auto keyPos = lowerHdr.find("sec-websocket-key:");
            if (keyPos != std::string::npos) {
                size_t valStart = keyPos + 18;
                while (valStart < hdrEnd && (req[valStart] == ' ' || req[valStart] == '\t')) valStart++;
                size_t valEnd = req.find("\r\n", valStart);
                if (valEnd != std::string::npos) {
                    std::string secKey = req.substr(valStart, valEnd - valStart);
                    std::string accept = ws::computeAccept(secKey);
                    std::string resp = "HTTP/1.1 101 Switching Protocols\r\n"
                                       "Upgrade: websocket\r\n"
                                       "Connection: Upgrade\r\n"
                                       "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
                    ::send(clientFd, resp.data(), resp.size(), MSG_NOSIGNAL);
                    handleWebSocket(clientFd);
                    return;
                }
            }
        }
    }

    // Find body if present
    std::string body;
    if (hdrEnd != std::string::npos) {
        body = req.substr(hdrEnd + 4);
        if (contentLength > 0 && body.size() > contentLength) {
            body.resize(contentLength);
        }
    }

    HttpResponse res = handleRequest(method, path, body);

    std::string statusText = res.status == 200   ? "200 OK"
                             : res.status == 204 ? "204 No Content"
                             : res.status == 400 ? "400 Bad Request"
                             : res.status == 404 ? "404 Not Found"
                                                 : "500 Internal Server Error";

    std::string response = "HTTP/1.1 " + statusText + "\r\n";
    response += "Content-Type: " + res.contentType + "\r\n";
    response += "Content-Length: " + std::to_string(res.body.size()) + "\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    response += "Access-Control-Allow-Headers: Content-Type\r\n";
    if (!res.extraHeaders.empty()) response += res.extraHeaders;
    response += "Connection: close\r\n\r\n";
    if (method != "HEAD") {
        response += res.body;
    }

    size_t sent = 0;
    while (sent < response.size()) {
        ssize_t w = ::send(clientFd, response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
        if (w <= 0) break;
        sent += static_cast<size_t>(w);
    }
}

void WebServer::handleWebSocket(int wsFd) {
    int nodelay = 1;
    ::setsockopt(wsFd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    {
        std::lock_guard<std::mutex> lock(wsMutex_);
        wsClients_.push_back(wsFd);
    }

    std::vector<uint8_t> inBuf;
    uint8_t recvBuf[4096];
    uint64_t lastTelMs = 0;

    while (running_.load()) {
        pollfd pfd{wsFd, POLLIN, 0};
        int pr = ::poll(&pfd, 1, 30);  // ~33 Hz tick
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            break;
        }
        if (pfd.revents & POLLIN) {
            ssize_t n = ::recv(wsFd, recvBuf, sizeof(recvBuf), 0);
            if (n <= 0) break;
            if (inBuf.size() + n > 65536) inBuf.clear();
            inBuf.insert(inBuf.end(), recvBuf, recvBuf + n);

            ws::Frame frame;
            size_t consumed = 0;
            while (ws::parseFrame(inBuf.data(), inBuf.size(), frame, consumed)) {
                inBuf.erase(inBuf.begin(), inBuf.begin() + consumed);
                if (frame.opcode == ws::Opcode::Close) {
                    auto closeResp = ws::makeCloseFrame();
                    ::send(wsFd, closeResp.data(), closeResp.size(), MSG_NOSIGNAL);
                    goto done;
                } else if (frame.opcode == ws::Opcode::Ping) {
                    auto pong = ws::makePongFrame(frame.payload);
                    ::send(wsFd, pong.data(), pong.size(), MSG_NOSIGNAL);
                } else if (frame.opcode == ws::Opcode::Text) {
                    std::string jsonStr(reinterpret_cast<const char*>(frame.payload.data()), frame.payload.size());
                    auto j = json::parse(jsonStr);
                    if (j && j->isObject()) {
                        int slot = 0;
                        input::ControllerState s{};
                        if (parseGamepadJson(*j, slot, s)) {
                            if ((*j).has("activeSwitch") && (*j)["activeSwitch"].asBool()) {
                                switchVirtualGamepadSlot(slot);
                            }
                            setVirtualGamepad(slot, s);
                            if (s_hooks.injectGamepad) s_hooks.injectGamepad(slot, s);
                        }
                    }
                } else if (frame.opcode == ws::Opcode::Binary) {
                    if (frame.payload.size() >= 13) {
                        int slot = frame.payload[0] & 0x03;
                        uint16_t btns = static_cast<uint16_t>(frame.payload[1]) | (static_cast<uint16_t>(frame.payload[2]) << 8);
                        input::ControllerState s{};
                        s.connected = true;
                        s.btnA = (btns & 0x0001) != 0;
                        s.btnB = (btns & 0x0002) != 0;
                        s.btnX = (btns & 0x0004) != 0;
                        s.btnY = (btns & 0x0008) != 0;
                        s.dpadUp = (btns & 0x0010) != 0;
                        s.dpadDown = (btns & 0x0020) != 0;
                        s.dpadLeft = (btns & 0x0040) != 0;
                        s.dpadRight = (btns & 0x0080) != 0;
                        s.btnL1 = (btns & 0x0100) != 0;
                        s.btnR1 = (btns & 0x0200) != 0;
                        s.btnL3 = (btns & 0x0400) != 0;
                        s.btnR3 = (btns & 0x0800) != 0;
                        s.btnTouchpad = (btns & 0x1000) != 0;
                        s.btnOptions = (btns & 0x2000) != 0;
                        s.btnNexus = (btns & 0x4000) != 0;
                        s.triggerL2 = static_cast<float>(frame.payload[3]) / 255.0f;
                        s.triggerR2 = static_cast<float>(frame.payload[4]) / 255.0f;
                        int16_t lx = static_cast<int16_t>(frame.payload[5] | (frame.payload[6] << 8));
                        int16_t ly = static_cast<int16_t>(frame.payload[7] | (frame.payload[8] << 8));
                        int16_t rx = static_cast<int16_t>(frame.payload[9] | (frame.payload[10] << 8));
                        int16_t ry = static_cast<int16_t>(frame.payload[11] | (frame.payload[12] << 8));
                        s.leftStickX = std::clamp(static_cast<float>(lx) / 32768.0f, -1.0f, 1.0f);
                        s.leftStickY = std::clamp(static_cast<float>(ly) / 32768.0f, -1.0f, 1.0f);
                        s.rightStickX = std::clamp(static_cast<float>(rx) / 32768.0f, -1.0f, 1.0f);
                        s.rightStickY = std::clamp(static_cast<float>(ry) / 32768.0f, -1.0f, 1.0f);
                        s.rawLeftX = s.leftStickX;
                        s.rawLeftY = s.leftStickY;
                        s.rawRightX = s.rightStickX;
                        s.rawRightY = s.rightStickY;
                        setVirtualGamepad(slot, s);
                        if (s_hooks.injectGamepad) s_hooks.injectGamepad(slot, s);
                    }
                }
            }
        }

        uint64_t now = platform::nowMs();
        if (now - lastTelMs >= 30) {  // ~33 fps telemetry
            lastTelMs = now;
            json::Value tel = json::Value::object();
            tel.set("type", "telemetry");
            json::Value stats = json::Value::object();
            if (s_hooks.getTelemetry) s_hooks.getTelemetry(stats);
            tel.set("stats", stats);
            auto f = ws::makeTextFrame(tel.dump());
            ssize_t sent = ::send(wsFd, f.data(), f.size(), MSG_NOSIGNAL);
            if (sent < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
                break;
            }
        }
    }

done:
    {
        std::lock_guard<std::mutex> lock(wsMutex_);
        auto it = std::find(wsClients_.begin(), wsClients_.end(), wsFd);
        if (it != wsClients_.end()) wsClients_.erase(it);
    }
}

void setWebServerHooks(WebServerHooks hooks) {
    s_hooks = std::move(hooks);
}

WebServer::HttpResponse WebServer::handleRequest(const std::string& method, const std::string& path,
                                                 const std::string& body) {
    if (method == "OPTIONS") {
        return {204, "text/plain", "", ""};
    }

    if (method == "GET" || method == "HEAD") {
        if (path == "/" || path == "/index.html" || path == "/boost" || path == "/boost/" || path == "/gamepad" || path == "/gamepad/") {
            return {200, "text/html; charset=utf-8", "", webServerIndexHtml()};
        }
        if (path == "/api/settings" || path == "/api/status" || path == "/api/state") {
            json::Value root = json::Value::object();
            root.set("ok", true);
            bool streaming = s_hooks.isStreaming ? s_hooks.isStreaming() : false;
            root.set("streaming", streaming);
            if (s_hooks.getSettings) {
                s_hooks.getSettings(root);
            } else {
                root.set("language", "en");
                root.set("resolution", "1080p");
                root.set("padMap", input::padMapJson(input::defaultPadMap()));
                root.set("keyMap", keyMapToJson(input::defaultKeyMap()));
                root.set("touchpadShortcuts", input::touchpadShortcutsJson(input::defaultTouchpadShortcuts()));
            }
            if (s_hooks.getSearchParams) {
                std::string q, tab;
                json::Value filters = json::Value::object();
                s_hooks.getSearchParams(q, tab, filters);
                root.set("query", q);
                root.set("tab", tab);
                root.set("filters", filters);
            } else if (s_hooks.getSearchQuery) {
                root.set("query", s_hooks.getSearchQuery());
                root.set("tab", "gamepass");
                root.set("filters", json::Value::object());
            } else {
                root.set("query", "");
                root.set("tab", "gamepass");
                root.set("filters", json::Value::object());
            }
            return {200, "application/json", "", root.dump()};
        }
        if (path == "/api/log" || path == "/api/log/prev") {
            bool prev = (path == "/api/log/prev");
            std::string filename = prev ? "xcloud.prev.log" : "xcloud.log";
            std::string rawLog;
            platform::readFile(platform::dataDir() + "/" + filename, rawLog);
            if (rawLog.empty() && prev) {
                platform::readFile(platform::dataDir() + "/xcloud.log", rawLog);
            }
            std::string clean = report::sanitize(rawLog, {});
            return {200, "text/plain; charset=utf-8", "Content-Disposition: attachment; filename=\"" + filename + "\"\r\n", clean};
        }
        if (path == "/api/report-issue" || path.rfind("/api/report-issue?", 0) == 0) {
            std::string rawLog;
            platform::readFile(platform::dataDir() + "/xcloud.log", rawLog);
            std::vector<std::string> privWords;
            for (int i = 0; i < input::kMaxPads; ++i) {
                std::string u = input::padUserName(i);
                if (!u.empty()) privWords.push_back(std::move(u));
            }

            report::Info info;
            info.title = "Relatório via WebUI";
            info.what = "Enviado através do painel WebUI do PSBox";
            info.release = XC_APP_VERSION;
            info.firmware = platform::firmwareVersion();
            info.platform = platform::platformName();
            std::string platHeader = "[Platform: " + info.platform + "]\n";
            info.log = platHeader + report::sanitize(report::logExcerpt(rawLog, false), privWords);
            std::string url = report::issueUrl(info);

            json::Value resp = json::Value::object();
            resp.set("ok", true);
            resp.set("url", url);
            resp.set("title", info.title);
            resp.set("platform", info.platform);
            resp.set("release", info.release);
            resp.set("firmware", info.firmware);
            resp.set("excerpt", info.log);
            return {200, "application/json; charset=utf-8", "", resp.dump()};
        }
        if (path == "/api/stats") {
            json::Value stats = json::Value::object();
            if (s_hooks.getTelemetry) {
                s_hooks.getTelemetry(stats);
            }
            return {200, "application/json", "", stats.dump()};
        }
        if (path == "/api/stream") {
            json::Value menu = json::Value::object();
            if (s_hooks.getStreamMenuState) {
                s_hooks.getStreamMenuState(menu);
            }
            return {200, "application/json", "", menu.dump()};
        }
        if (path == "/ws" || path == "/api/ws") {
            return {101, "application/json", "", "{\"ok\":true,\"endpoint\":\"websocket\"}"};
        }
    }

    if (method == "POST") {
        if (path == "/api/report-issue") {
            std::string title = "Relatório via WebUI";
            std::string what = "Enviado através do painel WebUI do PSBox";
            auto j = json::parse(body);
            if (j && j->isObject()) {
                if ((*j).has("title") && (*j)["title"].isString() && !(*j)["title"].asString().empty()) {
                    title = (*j)["title"].asString();
                }
                if ((*j).has("what") && (*j)["what"].isString() && !(*j)["what"].asString().empty()) {
                    what = (*j)["what"].asString();
                }
            }
            std::string rawLog;
            platform::readFile(platform::dataDir() + "/xcloud.log", rawLog);

            std::vector<std::string> privWords;
            for (int i = 0; i < input::kMaxPads; ++i) {
                std::string u = input::padUserName(i);
                if (!u.empty()) privWords.push_back(std::move(u));
            }

            report::Info info;
            info.title = report::shorten(title, 70);
            info.what = report::sanitize(what, privWords);
            info.release = XC_APP_VERSION;
            info.firmware = platform::firmwareVersion();
            info.platform = platform::platformName();
            std::string platHeader = "[Platform: " + info.platform + "]\n";
            info.log = platHeader + report::sanitize(report::logExcerpt(rawLog, false), privWords);
            std::string url = report::issueUrl(info);

            json::Value resp = json::Value::object();
            resp.set("ok", true);
            resp.set("url", url);
            resp.set("title", info.title);
            resp.set("platform", info.platform);
            resp.set("release", info.release);
            resp.set("firmware", info.firmware);
            resp.set("excerpt", info.log);
            return {200, "application/json; charset=utf-8", "", resp.dump()};
        }
        if (path == "/api/gamepad" || path == "/api/input") {
            auto j = json::parse(body);
            if (!j || !j->isObject()) {
                return {400, "application/json", "", "{\"ok\":false,\"error\":\"invalid json\"}"};
            }
            int slot = 0;
            input::ControllerState s{};
            if (!parseGamepadJson(*j, slot, s)) {
                return {400, "application/json", "", "{\"ok\":false,\"error\":\"invalid state\"}"};
            }

            if ((*j).has("activeSwitch") && (*j)["activeSwitch"].asBool()) {
                switchVirtualGamepadSlot(slot);
            }
            setVirtualGamepad(slot, s);
            if (s_hooks.injectGamepad) {
                s_hooks.injectGamepad(slot, s);
            }

            json::Value res = json::Value::object();
            res.set("ok", true);
            res.set("player", slot + 1);
            return {200, "application/json", "", res.dump()};
        }
        if (path == "/api/search") {
            bool streaming = s_hooks.isStreaming ? s_hooks.isStreaming() : false;
            if (streaming) {
                json::Value res = json::Value::object();
                res.set("ok", false);
                res.set("error", "streaming_active");
                res.set("message", "Desativado enquanto jogando");
                return {409, "application/json", "", res.dump()};
            }
            auto j = json::parse(body);
            std::string q;
            std::string tab = "gamepass";
            json::Value filtersObj = json::Value::object();
            bool hasQuery = false;
            bool hasTab = false;
            bool hasFilters = false;

            if (j && j->isObject()) {
                if ((*j).has("query") && (*j)["query"].isString()) {
                    q = (*j)["query"].str();
                    hasQuery = true;
                }
                if ((*j).has("tab") && (*j)["tab"].isString()) {
                    tab = (*j)["tab"].str();
                    hasTab = true;
                }
                if ((*j).has("filters") && (*j)["filters"].isObject()) {
                    filtersObj = (*j)["filters"];
                    hasFilters = true;
                }
                for (const char* k : {"wishlist", "free", "cheapest", "mouseKeyboard", "plan", "console", "mode", "language",
                                      "filterWishlist", "filterFree", "filterCheapest", "filterMouseKeyboard", "filterPlan", "filterConsole",
                                      "filterMode", "filterLanguage"}) {
                    if ((*j).has(k)) {
                        filtersObj.set(k, (*j)[k]);
                        hasFilters = true;
                    }
                }
            } else {
                q = extractJsonField(body, "query");
                hasQuery = !q.empty() || body.find("query=") != std::string::npos;
                std::string t = extractJsonField(body, "tab");
                if (!t.empty()) {
                    tab = t;
                    hasTab = true;
                }
            }

            if (s_hooks.setSearchParams) {
                s_hooks.setSearchParams(hasQuery ? &q : nullptr, hasTab ? &tab : nullptr, hasFilters ? &filtersObj : nullptr);
            } else if (s_hooks.setSearchQuery && hasQuery) {
                s_hooks.setSearchQuery(q);
            }

            if (!hasTab && s_hooks.getSearchParams) {
                std::string curQ, curTab;
                json::Value curF = json::Value::object();
                s_hooks.getSearchParams(curQ, curTab, curF);
                if (!curTab.empty()) tab = curTab;
            }

            json::Value res = json::Value::object();
            res.set("ok", true);
            res.set("query", q);
            res.set("tab", tab);
            res.set("filters", filtersObj);
            return {200, "application/json", "", res.dump()};
        }
        if (path == "/api/settings") {
            auto j = json::parse(body);
            if (!j || !j->isObject()) {
                return {400, "application/json", "", "{\"ok\":false,\"error\":\"invalid json\"}"};
            }
            bool ok = true;
            if (s_hooks.updateSettings) {
                ok = s_hooks.updateSettings(*j);
            }
            return {ok ? 200 : 400, "application/json", "", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"update failed\"}"};
        }
        if (path == "/api/stream") {
            auto j = json::parse(body);
            if (!j || !j->isObject()) {
                return {400, "application/json", "", "{\"ok\":false,\"error\":\"invalid json\"}"};
            }
            if ((*j).has("action") && (*j)["action"].isString()) {
                std::string act = (*j)["action"].str();
                if (s_hooks.streamAction) {
                    s_hooks.streamAction(act);
                }
            }
            bool ok = true;
            if (s_hooks.updateStreamMenu) {
                ok = s_hooks.updateStreamMenu(*j);
            }
            json::Value res = json::Value::object();
            res.set("ok", ok);
            return {ok ? 200 : 400, "application/json", "", res.dump()};
        }
    }

    return {404, "text/plain", "", "404 Not Found\n"};
}

void startWebServer(uint16_t port) {
    if (!g_webServer) {
        g_webServer = std::make_unique<WebServer>();
    }
    g_webServer->start(port);
}

void stopWebServer() {
    if (g_webServer) {
        g_webServer->stop();
    }
}

std::string webServerUrl() {
    return g_webServer ? g_webServer->url() : "http://" + platform::localIPv4() + ":8080";
}

}  // namespace xc::app
