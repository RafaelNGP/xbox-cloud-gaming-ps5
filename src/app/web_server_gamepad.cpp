// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/web_server.h"
#include "platform/platform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>
#include <mutex>

namespace xc::app {

namespace ws {

std::string computeAccept(const std::string& key) {
    std::string trimmed = key;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t' || trimmed.front() == '\r' || trimmed.front() == '\n')) {
        trimmed.erase(trimmed.begin());
    }
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' || trimmed.back() == '\r' || trimmed.back() == '\n')) {
        trimmed.pop_back();
    }
    trimmed += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char sha[20];
    mbedtls_sha1(reinterpret_cast<const unsigned char*>(trimmed.data()), trimmed.size(), sha);
    unsigned char b64[64];
    size_t olen = 0;
    mbedtls_base64_encode(b64, sizeof(b64), &olen, sha, sizeof(sha));
    return std::string(reinterpret_cast<char*>(b64), olen);
}

std::vector<uint8_t> makeFrame(Opcode opcode, const uint8_t* data, size_t len, bool fin) {
    std::vector<uint8_t> frame;
    uint8_t b0 = (fin ? 0x80 : 0x00) | (static_cast<uint8_t>(opcode) & 0x0F);
    frame.push_back(b0);
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 0xFFFF) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((static_cast<uint64_t>(len) >> (i * 8)) & 0xFF));
        }
    }
    if (data && len > 0) {
        frame.insert(frame.end(), data, data + len);
    }
    return frame;
}

std::vector<uint8_t> makeTextFrame(const std::string& text) {
    return makeFrame(Opcode::Text, reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

std::vector<uint8_t> makeBinaryFrame(const uint8_t* data, size_t len) {
    return makeFrame(Opcode::Binary, data, len);
}

std::vector<uint8_t> makePongFrame(const std::vector<uint8_t>& pingPayload) {
    return makeFrame(Opcode::Pong, pingPayload.data(), pingPayload.size());
}

std::vector<uint8_t> makeCloseFrame(uint16_t code) {
    uint8_t payload[2];
    payload[0] = static_cast<uint8_t>((code >> 8) & 0xFF);
    payload[1] = static_cast<uint8_t>(code & 0xFF);
    return makeFrame(Opcode::Close, payload, sizeof(payload));
}

bool parseFrame(const uint8_t* data, size_t len, Frame& outFrame, size_t& bytesConsumed) {
    if (!data || len < 2) return false;
    bool fin = (data[0] & 0x80) != 0;
    Opcode opcode = static_cast<Opcode>(data[0] & 0x0F);
    bool masked = (data[1] & 0x80) != 0;
    uint8_t lenField = data[1] & 0x7F;

    size_t headerSize = 2;
    uint64_t payloadLen = 0;
    if (lenField < 126) {
        payloadLen = lenField;
    } else if (lenField == 126) {
        if (len < headerSize + 2) return false;
        payloadLen = (static_cast<uint64_t>(data[2]) << 8) | data[3];
        headerSize += 2;
    } else {
        if (len < headerSize + 8) return false;
        payloadLen = 0;
        for (int i = 0; i < 8; ++i) {
            payloadLen = (payloadLen << 8) | data[headerSize + i];
        }
        headerSize += 8;
    }
    if (payloadLen > 1048576) return false;

    uint8_t maskKey[4] = {0, 0, 0, 0};
    if (masked) {
        if (len < headerSize + 4) return false;
        std::memcpy(maskKey, data + headerSize, 4);
        headerSize += 4;
    }

    if (len < headerSize + payloadLen) return false;

    outFrame.fin = fin;
    outFrame.opcode = opcode;
    outFrame.payload.resize(static_cast<size_t>(payloadLen));
    for (size_t i = 0; i < payloadLen; ++i) {
        uint8_t b = data[headerSize + i];
        if (masked) b ^= maskKey[i % 4];
        outFrame.payload[i] = b;
    }
    bytesConsumed = headerSize + static_cast<size_t>(payloadLen);
    return true;
}

}  // namespace ws

bool parseGamepadJson(const json::Value& val, int& outSlot, input::ControllerState& outState) {
    if (!val.isObject()) return false;
    int slot = 0;
    if (val.has("playerIndex")) {
        slot = static_cast<int>(val["playerIndex"].asInt(0));
    } else if (val.has("player")) {
        int p = static_cast<int>(val["player"].asInt(1));
        slot = p - 1;
    }
    slot = std::clamp(slot, 0, 3);
    outSlot = slot;

    outState = input::ControllerState{};
    outState.connected = true;

    auto readButton = [](const json::Value& b) -> bool {
        if (b.isBool()) return b.asBool();
        if (b.isNumber()) return b.asNumber() > 0.5;
        if (b.isObject()) {
            if (b.has("pressed")) return b["pressed"].asBool();
            if (b.has("value")) return b["value"].asNumber() > 0.5;
        }
        return false;
    };

    auto readTrigger = [](const json::Value& b) -> float {
        if (b.isNumber()) return static_cast<float>(b.asNumber(0.0));
        if (b.isBool()) return b.asBool() ? 1.0f : 0.0f;
        if (b.isObject() && b.has("value")) return static_cast<float>(b["value"].asNumber(0.0));
        if (b.isObject() && b.has("pressed")) return b["pressed"].asBool() ? 1.0f : 0.0f;
        return 0.0f;
    };

    // 1. W3C Gamepad API standard arrays
    if (val.has("buttons") && val["buttons"].isArray()) {
        const auto& arr = val["buttons"];
        size_t n = arr.size();
        if (n > 0) outState.btnA = readButton(arr[0]);
        if (n > 1) outState.btnB = readButton(arr[1]);
        if (n > 2) outState.btnX = readButton(arr[2]);
        if (n > 3) outState.btnY = readButton(arr[3]);
        if (n > 4) outState.btnL1 = readButton(arr[4]);
        if (n > 5) outState.btnR1 = readButton(arr[5]);
        if (n > 6) outState.triggerL2 = readTrigger(arr[6]);
        if (n > 7) outState.triggerR2 = readTrigger(arr[7]);
        if (n > 8) outState.btnTouchpad = readButton(arr[8]); // Select / View
        if (n > 9) outState.btnOptions = readButton(arr[9]);   // Start / Menu
        if (n > 10) outState.btnL3 = readButton(arr[10]);
        if (n > 11) outState.btnR3 = readButton(arr[11]);
        if (n > 12) outState.dpadUp = readButton(arr[12]);
        if (n > 13) outState.dpadDown = readButton(arr[13]);
        if (n > 14) outState.dpadLeft = readButton(arr[14]);
        if (n > 15) outState.dpadRight = readButton(arr[15]);
        if (n > 16) outState.btnNexus = readButton(arr[16]);   // Xbox / Guide
    }

    if (val.has("axes") && val["axes"].isArray()) {
        const auto& arr = val["axes"];
        size_t n = arr.size();
        if (n > 0) outState.leftStickX = static_cast<float>(arr[0].asNumber(0.0));
        if (n > 1) outState.leftStickY = static_cast<float>(arr[1].asNumber(0.0));
        if (n > 2) outState.rightStickX = static_cast<float>(arr[2].asNumber(0.0));
        if (n > 3) outState.rightStickY = static_cast<float>(arr[3].asNumber(0.0));
    }

    // 2. Named / object mappings and flat fields
    auto getBool = [&](const char* k1, const char* k2 = nullptr, const char* k3 = nullptr) -> bool {
        if (val.has(k1)) return val[k1].asBool(false);
        if (k2 && val.has(k2)) return val[k2].asBool(false);
        if (k3 && val.has(k3)) return val[k3].asBool(false);
        if (val.has("buttons") && val["buttons"].isObject()) {
            const auto& b = val["buttons"];
            if (b.has(k1)) return b[k1].asBool(false);
            if (k2 && b.has(k2)) return b[k2].asBool(false);
            if (k3 && b.has(k3)) return b[k3].asBool(false);
        }
        return false;
    };

    auto getFloat = [&](const char* k1, const char* k2 = nullptr) -> float {
        if (val.has(k1)) return static_cast<float>(val[k1].asNumber(0.0));
        if (k2 && val.has(k2)) return static_cast<float>(val[k2].asNumber(0.0));
        if (val.has("axes") && val["axes"].isObject()) {
            const auto& a = val["axes"];
            if (a.has(k1)) return static_cast<float>(a[k1].asNumber(0.0));
            if (k2 && a.has(k2)) return static_cast<float>(a[k2].asNumber(0.0));
        }
        return 0.0f;
    };

    if (getBool("btnA", "a")) outState.btnA = true;
    if (getBool("btnB", "b")) outState.btnB = true;
    if (getBool("btnX", "x")) outState.btnX = true;
    if (getBool("btnY", "y")) outState.btnY = true;

    if (getBool("dpadUp", "dpad_up", "up")) outState.dpadUp = true;
    if (getBool("dpadDown", "dpad_down", "down")) outState.dpadDown = true;
    if (getBool("dpadLeft", "dpad_left", "left")) outState.dpadLeft = true;
    if (getBool("dpadRight", "dpad_right", "right")) outState.dpadRight = true;

    if (getBool("btnL1", "lb", "l1")) outState.btnL1 = true;
    if (getBool("btnR1", "rb", "r1")) outState.btnR1 = true;
    if (getBool("btnL3", "ls", "l3")) outState.btnL3 = true;
    if (getBool("btnR3", "rs", "r3")) outState.btnR3 = true;

    if (getBool("btnTouchpad", "btnView", "view")) outState.btnTouchpad = true;
    if (getBool("btnOptions", "btnMenu", "menu")) outState.btnOptions = true;
    if (getBool("btnNexus", "xbox", "guide")) outState.btnNexus = true;

    float tL = getFloat("triggerL2", "lt");
    if (tL > 0.0f) outState.triggerL2 = tL;
    float tR = getFloat("triggerR2", "rt");
    if (tR > 0.0f) outState.triggerR2 = tR;

    if (val.has("leftStickX") || val.has("lx") || (val.has("axes") && val["axes"].isObject())) {
        float lx = getFloat("leftStickX", "lx");
        if (std::abs(lx) > 0.001f) outState.leftStickX = lx;
    }
    if (val.has("leftStickY") || val.has("ly") || (val.has("axes") && val["axes"].isObject())) {
        float ly = getFloat("leftStickY", "ly");
        if (std::abs(ly) > 0.001f) outState.leftStickY = ly;
    }
    if (val.has("rightStickX") || val.has("rx") || (val.has("axes") && val["axes"].isObject())) {
        float rx = getFloat("rightStickX", "rx");
        if (std::abs(rx) > 0.001f) outState.rightStickX = rx;
    }
    if (val.has("rightStickY") || val.has("ry") || (val.has("axes") && val["axes"].isObject())) {
        float ry = getFloat("rightStickY", "ry");
        if (std::abs(ry) > 0.001f) outState.rightStickY = ry;
    }

    outState.rawLeftX = outState.leftStickX;
    outState.rawLeftY = outState.leftStickY;
    outState.rawRightX = outState.rightStickX;
    outState.rawRightY = outState.rightStickY;
    return true;
}

namespace {
struct VirtualGamepadSlot {
    input::ControllerState state{};
    uint64_t lastSeenMs = 0;
    uint64_t lastInputMs = 0;
};
std::mutex s_vpadMutex;
VirtualGamepadSlot s_virtualPads[4];

bool hasGamepadInput(const input::ControllerState& s) {
    return s.btnA || s.btnB || s.btnX || s.btnY ||
           s.dpadUp || s.dpadDown || s.dpadLeft || s.dpadRight ||
           s.btnL1 || s.btnR1 || s.btnL3 || s.btnR3 ||
           s.btnTouchpad || s.btnOptions || s.btnNexus ||
           s.triggerL2 > 0.05f || s.triggerR2 > 0.05f ||
           std::abs(s.leftStickX) > 0.08f || std::abs(s.leftStickY) > 0.08f ||
           std::abs(s.rightStickX) > 0.08f || std::abs(s.rightStickY) > 0.08f;
}
}  // namespace

bool virtualGamepadState(int playerIndex, input::ControllerState& outState, bool& isConnected) {
    if (playerIndex < 0 || playerIndex >= 4) {
        isConnected = false;
        outState = {};
        return false;
    }
    std::lock_guard<std::mutex> lock(s_vpadMutex);
    uint64_t now = platform::nowMs();
    uint64_t diff = now > s_virtualPads[playerIndex].lastSeenMs ? now - s_virtualPads[playerIndex].lastSeenMs : 0;
    isConnected = (s_virtualPads[playerIndex].lastSeenMs > 0 && diff < 4000);
    if (diff < 2000) {
        outState = s_virtualPads[playerIndex].state;
    } else {
        outState = {};
    }
    return isConnected;
}

void setVirtualGamepad(int playerIndex, const input::ControllerState& state) {
    if (playerIndex < 0 || playerIndex >= 4) return;
    std::lock_guard<std::mutex> lock(s_vpadMutex);
    uint64_t now = platform::nowMs();
    bool firstSeen = (s_virtualPads[playerIndex].lastSeenMs == 0);
    s_virtualPads[playerIndex].state = state;
    s_virtualPads[playerIndex].lastSeenMs = now;
    if (firstSeen || hasGamepadInput(state)) {
        s_virtualPads[playerIndex].lastInputMs = now;
    }
}

void touchVirtualGamepad(int playerIndex) {
    if (playerIndex < 0 || playerIndex >= 4) return;
    std::lock_guard<std::mutex> lock(s_vpadMutex);
    uint64_t now = platform::nowMs();
    s_virtualPads[playerIndex].lastSeenMs = now;
    s_virtualPads[playerIndex].lastInputMs = now;
}

void switchVirtualGamepadSlot(int newSlot) {
    if (newSlot < 0 || newSlot >= 4) return;
    std::lock_guard<std::mutex> lock(s_vpadMutex);
    for (int i = 0; i < 4; ++i) {
        if (i != newSlot) {
            s_virtualPads[i] = {};
        }
    }
    uint64_t now = platform::nowMs();
    s_virtualPads[newSlot].lastSeenMs = now;
    s_virtualPads[newSlot].lastInputMs = now;
}

uint64_t virtualGamepadActiveAt(int playerIndex) {
    if (playerIndex < 0 || playerIndex >= 4) return 0;
    std::lock_guard<std::mutex> lock(s_vpadMutex);
    return s_virtualPads[playerIndex].lastInputMs;
}

void resetVirtualGamepads() {
    std::lock_guard<std::mutex> lock(s_vpadMutex);
    for (auto& slot : s_virtualPads) slot = {};
}

}  // namespace xc::app
