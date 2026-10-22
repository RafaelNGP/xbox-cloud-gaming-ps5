// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Lightweight embedded HTTP server (port 8080) for remote configuration,
// live search typing, and log download via mobile browser / QR code.
#pragma once

#include "input/controller.h"
#include "platform/platform.h"
#include "util/json.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace xc::app {

struct WebServerHooks {
    std::function<void(json::Value& root)> getSettings;
    std::function<bool(const json::Value& root)> updateSettings;
    std::function<std::string()> getSearchQuery;
    std::function<void(const std::string& query)> setSearchQuery;
    std::function<void(const std::string* query, const std::string* tab, const json::Value* filters)> setSearchParams;
    std::function<void(std::string& outQuery, std::string& outTab, json::Value& outFilters)> getSearchParams;
    std::function<bool()> isStreaming;
    std::function<void(int playerIndex, const input::ControllerState& state)> injectGamepad;
};
void setWebServerHooks(WebServerHooks hooks);

// Virtual gamepad input query:
// playerIndex: 0 = Player 1, 1 = Player 2
// outState: contains current active buttons/axes (resets to neutral after ~600ms of inactivity)
// isConnected: true if any report/keepalive received within ~3000ms
bool virtualGamepadState(int playerIndex, input::ControllerState& outState, bool& isConnected);
void setVirtualGamepad(int playerIndex, const input::ControllerState& state);
void resetVirtualGamepads();

class WebServer {
public:
    WebServer();
    ~WebServer();

    bool start(uint16_t port = 8080);
    void stop();
    bool isRunning() const { return running_.load(); }
    uint16_t port() const { return port_; }
    std::string localIp() const;
    std::string url() const;

    // Direct request handler for testing without networking
    struct HttpResponse {
        int status = 200;
        std::string contentType;
        std::string extraHeaders;
        std::string body;
    };
    HttpResponse handleRequest(const std::string& method, const std::string& path, const std::string& body);

private:
    void threadLoop();
    void handleClient(int clientFd);

    std::atomic<bool> running_{false};
    uint16_t port_ = 8080;
    int listenFd_ = -1;
    platform::Thread thread_{};
};

extern std::unique_ptr<WebServer> g_webServer;
void startWebServer(uint16_t port = 8080);
void stopWebServer();
std::string webServerUrl();

}  // namespace xc::app
