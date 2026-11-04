// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// USB pads (pad_usb.h): their reports parsed, and their state kept for
// pollUsbPad(). The report layouts are those of the Linux xpad driver and
// OmniPad CE (usb_controllers.c).
#include "input/pad_usb.h"

#include "input/keyboard_nav.h"
#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <array>
#include <mutex>

namespace xc::input {

namespace {

int16_t le16(const uint8_t* p) { return static_cast<int16_t>(p[0] | (p[1] << 8)); }

float stick(int16_t v) { return std::clamp(static_cast<float>(v) / 32767.0f, -1.0f, 1.0f); }

// XInput and GIP sticks have +y up; ControllerState has +y down.
void setSticks(ControllerState& s, const uint8_t* p) {
    s.rawLeftX = s.leftStickX = stick(le16(p));
    s.rawLeftY = s.leftStickY = -stick(le16(p + 2));
    s.rawRightX = s.rightStickX = stick(le16(p + 4));
    s.rawRightY = s.rightStickY = -stick(le16(p + 6));
}

struct Slot {
    bool attached = false;
    std::string name;
    ControllerState state;  // as read; connected once its first input came
    int player = -1;        // -1: given one at the next poll
    uint64_t order = 0;     // when it came, for the players' order
    // The Xbox / Home button held: LB / RB move the pad to another player;
    // let go without them, it asks for the in-game menu. It never reaches
    // the game itself (the menu has an Xbox button item).
    bool guideHeld = false, comboUsed = false;
    bool waitRelease = false;  // out of the game until every button is let go
    uint64_t activeAt = 0;
};

std::mutex g_mutex;
std::array<Slot, kMaxUsbPads> g_slots;
std::vector<UsbPadEvent> g_events;
uint64_t g_attachCount = 0;

// The players for the pads that came since the last poll, in their order.
void assignPlayers() {
    for (;;) {
        Slot* next = nullptr;
        for (Slot& s : g_slots)
            if (s.attached && s.player < 0 && (!next || s.order < next->order)) next = &s;
        if (!next) return;
        next->player = 0;  // all taken: the first player
        for (int p = 0; p < kMaxPads; ++p) {
            bool taken = padConnected(p);
            for (const Slot& o : g_slots) taken |= o.attached && o.player == p && &o != next;
            if (!taken) {
                next->player = p;
                break;
            }
        }
        XC_LOGI("usb pad: %s plays as player %d", next->name.c_str(), next->player + 1);
        g_events.push_back({UsbPadEvent::Kind::Connected, next->player, next->name, static_cast<int>(next - g_slots.data())});
    }
}

// A pad's state for the game and the menus: the settings applied, LB / RB
// held back while the Xbox button is down, the Xbox button itself never.
ControllerState shown(const Slot& slot) {
    ControllerState s = slot.state;
    applyPadSettings(s);
    if (slot.guideHeld) s.btnL1 = s.btnR1 = false;
    s.btnNexus = false;
    return s;
}

void movePlayer(Slot& s, int slot, int player) {
    s.player = player;
    XC_LOGI("usb pad %d: %s moved to player %d", slot, s.name.c_str(), player + 1);
    g_events.push_back({UsbPadEvent::Kind::Moved, player, s.name, slot});
}

// `player` -1: every pad (the menus, so a pad waiting for its buttons to be
// let go too); otherwise those playing as it, but `except`.
bool pollWhere(int player, int except, ControllerState& out) {
    out = ControllerState{};
    bool any = false;
    std::lock_guard<std::mutex> lock(g_mutex);
    assignPlayers();
    for (size_t i = 0; i < g_slots.size(); ++i) {
        const Slot& slot = g_slots[i];
        if (!slot.attached || !slot.state.connected) continue;
        if (player >= 0 && (slot.player != player || slot.waitRelease || static_cast<int>(i) == except)) continue;
        ControllerState s = shown(slot);
        if (any) mergePads(out, s);
        else out = s;
        any = true;
    }
    return any;
}

}  // namespace

UsbPadProtocol usbPadProtocolFor(uint8_t ifaceClass, uint8_t ifaceSubclass, uint8_t ifaceProtocol) {
    if (ifaceClass != 0xFF) return UsbPadProtocol::None;
    if (ifaceSubclass == 0x5D && ifaceProtocol == 0x01) return UsbPadProtocol::XInput;
    if (ifaceSubclass == 0x47 && ifaceProtocol == 0xD0) return UsbPadProtocol::Gip;
    return UsbPadProtocol::None;
}

UsbPadInterface findUsbPadInterface(const uint8_t* d, size_t len) {
    UsbPadInterface pad;
    bool inPad = false;  // the endpoints that follow are the pad interface's
    for (size_t off = 0; d && off + 2 <= len; off += d[off]) {
        const size_t l = d[off];
        if (l < 2 || off + l > len) break;
        if (d[off + 1] == 0x04 && l >= 9) {  // an interface
            if (inPad && pad.epIn) return pad;
            pad = {usbPadProtocolFor(d[off + 5], d[off + 6], d[off + 7]), d[off + 2], 0, 0};
            inPad = pad.protocol != UsbPadProtocol::None && d[off + 3] == 0;
        } else if (d[off + 1] == 0x05 && l >= 7 && inPad && (d[off + 3] & 0x03) == 0x03) {  // an interrupt endpoint
            const uint8_t addr = d[off + 2];
            uint8_t& ep = (addr & 0x80) ? pad.epIn : pad.epOut;
            if (!ep) ep = addr;
        }
    }
    return inPad && pad.epIn ? pad : UsbPadInterface{};
}

std::vector<std::array<uint8_t, 13>> gipAcksFor(const uint8_t* data, size_t len) {
    std::vector<std::array<uint8_t, 13>> acks;
    for (size_t off = 0; data && off + 4 <= len;) {
        const uint8_t* m = data + off;
        const size_t payload = m[3];
        if ((m[3] & 0x80) || payload < 2 || payload > 60 || off + 4 + payload > len) break;
        off += 4 + payload;
        if (!(m[1] & 0x10)) continue;
        const auto client = static_cast<uint8_t>((m[1] & 0x0f) | 0x20);
        acks.push_back({0x01, client, m[2], 0x09, 0x00, m[0], client, static_cast<uint8_t>(payload), 0, 0, 0, 0, 0});
    }
    return acks;
}

// 00 14 | buttons (16 bits) | LT | RT | LX LY RX RY (16 bits each) | ...
// Clones fill the length byte in their own way; the type and the size
// tell an input report from the LED (01), rumble (03) and status ones.
bool parseXInputReport(const uint8_t* data, size_t len, ControllerState& s) {
    if (!data || len < 14 || data[0] != 0x00) return false;
    const unsigned b = static_cast<unsigned>(data[2] | (data[3] << 8));
    s.dpadUp = b & 0x0001;
    s.dpadDown = b & 0x0002;
    s.dpadLeft = b & 0x0004;
    s.dpadRight = b & 0x0008;
    s.btnOptions = b & 0x0010;   // Start (Menu)
    s.btnTouchpad = b & 0x0020;  // Back (View)
    s.btnL3 = b & 0x0040;
    s.btnR3 = b & 0x0080;
    s.btnL1 = b & 0x0100;
    s.btnR1 = b & 0x0200;
    s.btnNexus = b & 0x0400;  // Guide
    s.btnA = b & 0x1000;
    s.btnB = b & 0x2000;
    s.btnX = b & 0x4000;
    s.btnY = b & 0x8000;
    s.triggerL2 = static_cast<float>(data[4]) / 255.0f;
    s.triggerR2 = static_cast<float>(data[5]) / 255.0f;
    setSticks(s, data + 6);
    s.connected = true;
    return true;
}

// A GIP message: command | options | sequence | payload length | payload.
// 0x20, the input: buttons (16 bits) | LT RT (0..1023) | LX LY RX RY. The
// Xbox button comes by itself, in a 0x07 message (bit 0 of its payload).
bool parseGipReport(const uint8_t* data, size_t len, ControllerState& s) {
    if (!data || len < 5) return false;
    if (data[0] == 0x07) {
        s.btnNexus = data[4] & 0x01;
        s.connected = true;
        return true;
    }
    if (data[0] != 0x20 || len < 18) return false;
    const unsigned b = static_cast<unsigned>(data[4] | (data[5] << 8));
    s.btnOptions = b & 0x0004;   // Menu
    s.btnTouchpad = b & 0x0008;  // View
    s.btnA = b & 0x0010;
    s.btnB = b & 0x0020;
    s.btnX = b & 0x0040;
    s.btnY = b & 0x0080;
    s.dpadUp = b & 0x0100;
    s.dpadDown = b & 0x0200;
    s.dpadLeft = b & 0x0400;
    s.dpadRight = b & 0x0800;
    s.btnL1 = b & 0x1000;
    s.btnR1 = b & 0x2000;
    s.btnL3 = b & 0x4000;
    s.btnR3 = b & 0x8000;
    s.triggerL2 = std::min(static_cast<float>(static_cast<uint16_t>(le16(data + 6))) / 1023.0f, 1.0f);
    s.triggerR2 = std::min(static_cast<float>(static_cast<uint16_t>(le16(data + 8))) / 1023.0f, 1.0f);
    setSticks(s, data + 10);
    s.connected = true;
    return true;
}

bool parseUsbPadReport(UsbPadProtocol protocol, const uint8_t* data, size_t len, ControllerState& state) {
    switch (protocol) {
        case UsbPadProtocol::XInput: return parseXInputReport(data, len, state);
        case UsbPadProtocol::Gip: return parseGipReport(data, len, state);
        case UsbPadProtocol::None: break;
    }
    return false;
}

void usbPadAttached(int slot, const std::string& name) {
    if (slot < 0 || slot >= kMaxUsbPads) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_slots[slot] = Slot{};
    g_slots[slot].attached = true;
    g_slots[slot].name = name;
    g_slots[slot].order = ++g_attachCount;
    XC_LOGI("usb pad %d: %s connected", slot, name.c_str());
}

void usbPadState(int slot, const ControllerState& in) {
    if (slot < 0 || slot >= kMaxUsbPads) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    Slot& s = g_slots[slot];
    if (!s.attached) return;
    if (in.btnNexus && !s.guideHeld) s.guideHeld = true, s.comboUsed = false;
    if (s.guideHeld) {
        const int step = in.btnR1 && !s.state.btnR1 ? 1 : in.btnL1 && !s.state.btnL1 ? -1 : 0;
        if (step && s.player >= 0) {
            movePlayer(s, slot, (s.player + step + kMaxPads) % kMaxPads);
            s.comboUsed = true;
        }
        if (!in.btnNexus) {
            s.guideHeld = false;
            if (!s.comboUsed && s.player >= 0) g_events.push_back({UsbPadEvent::Kind::MenuButton, s.player, s.name, slot});
        }
    }
    s.state = in;
    s.state.connected = true;
    ControllerState used = in;
    applyPadSettings(used);
    const bool pressed = inUse(used) || in.btnNexus;
    if (pressed) s.activeAt = platform::nowMs();
    else s.waitRelease = false;
}

void usbPadDetached(int slot) {
    if (slot < 0 || slot >= kMaxUsbPads) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    Slot& s = g_slots[slot];
    if (s.attached) {
        XC_LOGI("usb pad %d: %s disconnected", slot, s.name.c_str());
        if (s.player >= 0) g_events.push_back({UsbPadEvent::Kind::Disconnected, s.player, s.name, slot});
    }
    s = Slot{};
}

int usbPadPlayer(int slot) {
    if (slot < 0 || slot >= kMaxUsbPads) return -1;
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_slots[slot].attached ? g_slots[slot].player : -1;
}

std::vector<UsbPadEvent> takeUsbPadEvents() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<UsbPadEvent> events;
    events.swap(g_events);
    return events;
}

bool pollUsbPad(ControllerState& out) { return pollWhere(-1, -1, out); }

bool pollUsbPadsFor(int player, ControllerState& out, int except) {
    if (player < 0 || player >= kMaxPads) {
        out = ControllerState{};
        return false;
    }
    return pollWhere(player, except, out);
}

bool pollUsbPadSlot(int slot, ControllerState& out) {
    out = ControllerState{};
    if (slot < 0 || slot >= kMaxUsbPads) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    const Slot& s = g_slots[slot];
    if (!s.attached || !s.state.connected) return false;
    out = shown(s);
    return true;
}

void setUsbPadPlayer(int slot, int player) {
    if (slot < 0 || slot >= kMaxUsbPads || player < 0 || player >= kMaxPads) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    Slot& s = g_slots[slot];
    if (s.attached && s.player >= 0 && s.player != player) movePlayer(s, slot, player);
}

void usbPadsWaitRelease() {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (Slot& s : g_slots) {
        if (!s.attached) continue;
        ControllerState used = s.state;
        applyPadSettings(used);
        s.waitRelease = inUse(used) || s.state.btnNexus;
    }
}

std::string usbPadName(int slot) {
    if (slot < 0 || slot >= kMaxUsbPads) return {};
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_slots[slot].attached ? g_slots[slot].name : std::string();
}

int usbPadCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return static_cast<int>(std::count_if(g_slots.begin(), g_slots.end(), [](const Slot& s) { return s.attached; }));
}

std::string usbPadNameFor(int player) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const Slot& s : g_slots)
        if (s.attached && s.player == player && player >= 0) return s.name;
    return {};
}

uint64_t usbPadActiveAt(int player) {
    std::lock_guard<std::mutex> lock(g_mutex);
    uint64_t at = 0;
    for (const Slot& s : g_slots)
        if (s.attached && s.player == player && player >= 0) at = std::max(at, s.activeAt);
    return at;
}

}  // namespace xc::input
