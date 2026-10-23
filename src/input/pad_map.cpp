// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "input/pad_map.h"

#include "util/json.h"

#include <utility>

namespace xc::input {

namespace {

constexpr int kPadMapVersion = 1;
constexpr float kHalf = 0.5f;

// The button behind a PadFn in a state (const or not); null for the triggers.
template <class State>
auto buttonOf(State& s, int fn) -> decltype(&s.btnA) {
    switch (static_cast<PadFn>(fn)) {
    case PadFn::A: return &s.btnA;
    case PadFn::B: return &s.btnB;
    case PadFn::X: return &s.btnX;
    case PadFn::Y: return &s.btnY;
    case PadFn::LB: return &s.btnL1;
    case PadFn::RB: return &s.btnR1;
    case PadFn::LS: return &s.btnL3;
    case PadFn::RS: return &s.btnR3;
    case PadFn::View: return &s.btnTouchpad;
    case PadFn::Menu: return &s.btnOptions;
    case PadFn::Up: return &s.dpadUp;
    case PadFn::Down: return &s.dpadDown;
    case PadFn::Left: return &s.dpadLeft;
    case PadFn::Right: return &s.dpadRight;
    default: return nullptr;  // the triggers
    }
}

// How far a source is pressed, 0..1 (a button: 0 or 1).
float amountOf(const ControllerState& s, uint8_t src) {
    if (src == static_cast<uint8_t>(PadFn::LT)) return s.triggerL2;
    if (src == static_cast<uint8_t>(PadFn::RT)) return s.triggerR2;
    if (src >= kPadFns) return 0.0f;
    const bool* b = buttonOf(s, src);
    return b && *b ? 1.0f : 0.0f;
}

}  // namespace

PadMap defaultPadMap() {
    PadMap m{};
    for (int i = 0; i < kPadFns; ++i) m[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    return m;
}

ControllerState applyPadMap(const ControllerState& in, const PadMap& map) {
    ControllerState out = in;  // sticks, touch, the Xbox button
    for (int fn = 0; fn < kPadFns; ++fn) {
        float v = amountOf(in, map[static_cast<size_t>(fn)]);
        if (fn == static_cast<int>(PadFn::LT)) out.triggerL2 = v;
        else if (fn == static_cast<int>(PadFn::RT)) out.triggerR2 = v;
        else *buttonOf(out, fn) = v > kHalf;
    }
    return out;
}

uint32_t padButtons(const ControllerState& s) {
    uint32_t bits = 0;
    for (int i = 0; i < kPadFns; ++i)
        if (amountOf(s, static_cast<uint8_t>(i)) > kHalf) bits |= 1u << i;
    return bits;
}

void assignPadSource(PadMap& map, PadFn fn, uint8_t source) {
    const auto f = static_cast<size_t>(fn);
    if (f >= map.size()) return;
    if (source != kPadNone)
        for (auto& s : map)
            if (s == source) s = map[f];  // the other button takes fn's old source
    map[f] = source;
}

uint32_t padMapMissing(const PadMap& map) {
    uint32_t bits = 0;
    for (int i = 0; i < kPadFns; ++i)
        if (map[static_cast<size_t>(i)] >= kPadFns) bits |= 1u << i;
    return bits;
}

bool padMapValid(const PadMap& map) {
    uint32_t seen = 0;
    for (uint8_t s : map) {
        if (s >= kPadFns || (seen & (1u << s))) return false;
        seen |= 1u << s;
    }
    return true;
}

uint8_t physicalSource(uint8_t source, bool circleConfirms) {
    if (!circleConfirms) return source;
    if (source == static_cast<uint8_t>(PadFn::A)) return static_cast<uint8_t>(PadFn::B);
    if (source == static_cast<uint8_t>(PadFn::B)) return static_cast<uint8_t>(PadFn::A);
    return source;
}

const char* padFnName(PadFn fn) {
    static constexpr const char* kNames[kPadFns] = {"A",  "B",  "X",    "Y",    "LB",      "RB",        "LT",        "RT",
                                                    "LS", "RS", "View", "Menu", "D-pad up", "D-pad down", "D-pad left", "D-pad right"};
    auto i = static_cast<size_t>(fn);
    return i < static_cast<size_t>(kPadFns) ? kNames[i] : "?";
}

json::Value padMapJson(const PadMap& map) {
    json::Value v = json::Value::object();
    v.set("version", kPadMapVersion);
    json::Value items = json::Value::array();
    for (uint8_t s : map) items.push(static_cast<int>(s));
    v.set("map", std::move(items));
    return v;
}

bool readPadMap(const json::Value& v, PadMap& out) {
    out = defaultPadMap();
    if (!v.isObject() || v["version"].asInt(0) != kPadMapVersion) return false;
    const json::Value& items = v["map"];
    if (!items.isArray() || items.size() != static_cast<size_t>(kPadFns)) return false;
    PadMap m{};
    for (int i = 0; i < kPadFns; ++i) {
        const json::Value& s = items[i];
        int64_t n = s.asInt(-1);
        if (!s.isNumber() || n < 0 || n >= kPadFns) return false;
        m[static_cast<size_t>(i)] = static_cast<uint8_t>(n);
    }
    if (!padMapValid(m)) return false;
    out = m;
    return true;
}

TouchpadShortcuts defaultTouchpadShortcuts() {
    return TouchpadShortcuts{};
}

json::Value touchpadShortcutsJson(const TouchpadShortcuts& ts) {
    json::Value v = json::Value::object();
    v.set("version", 1);
    v.set("swipeUp", static_cast<int>(ts.swipeUp));
    v.set("swipeDown", static_cast<int>(ts.swipeDown));
    v.set("swipeLeft", static_cast<int>(ts.swipeLeft));
    v.set("swipeRight", static_cast<int>(ts.swipeRight));
    v.set("clickLeft", static_cast<int>(ts.clickLeft));
    v.set("clickRight", static_cast<int>(ts.clickRight));
    return v;
}

bool readTouchpadShortcuts(const json::Value& v, TouchpadShortcuts& out) {
    out = defaultTouchpadShortcuts();
    if (!v.isObject()) return false;
    if (v.has("version") && v["version"].asInt(1) != 1) return false;
    if (!v.has("swipeUp") && !v.has("swipeDown") && !v.has("swipeLeft") &&
        !v.has("swipeRight") && !v.has("clickLeft") && !v.has("clickRight")) {
        return false;
    }
    auto parseAct = [&](const char* key, TouchpadAction fallback) -> TouchpadAction {
        if (!v.has(key)) return fallback;
        int val = v[key].asInt(static_cast<int>(fallback));
        if (val < 0 || val >= kTouchpadActions) return fallback;
        return static_cast<TouchpadAction>(val);
    };
    out.swipeUp = parseAct("swipeUp", out.swipeUp);
    out.swipeDown = parseAct("swipeDown", out.swipeDown);
    out.swipeLeft = parseAct("swipeLeft", out.swipeLeft);
    out.swipeRight = parseAct("swipeRight", out.swipeRight);
    out.clickLeft = parseAct("clickLeft", out.clickLeft);
    out.clickRight = parseAct("clickRight", out.clickRight);
    return true;
}

bool isTouchpadButtonAction(TouchpadAction act) {
    return static_cast<uint8_t>(act) >= static_cast<uint8_t>(TouchpadAction::View) &&
           static_cast<uint8_t>(act) < static_cast<uint8_t>(TouchpadAction::Count);
}

bool applyTouchpadButtonAction(ControllerState& s, TouchpadAction act) {
    switch (act) {
    case TouchpadAction::View: s.btnTouchpad = true; return true;
    case TouchpadAction::Menu: s.btnOptions = true; return true;
    case TouchpadAction::L3: s.btnL3 = true; return true;
    case TouchpadAction::R3: s.btnR3 = true; return true;
    case TouchpadAction::A: s.btnA = true; return true;
    case TouchpadAction::B: s.btnB = true; return true;
    case TouchpadAction::X: s.btnX = true; return true;
    case TouchpadAction::Y: s.btnY = true; return true;
    case TouchpadAction::LB: s.btnL1 = true; return true;
    case TouchpadAction::RB: s.btnR1 = true; return true;
    case TouchpadAction::DpadUp: s.dpadUp = true; return true;
    case TouchpadAction::DpadDown: s.dpadDown = true; return true;
    case TouchpadAction::DpadLeft: s.dpadLeft = true; return true;
    case TouchpadAction::DpadRight: s.dpadRight = true; return true;
    default: return false;
    }
}

}  // namespace xc::input
