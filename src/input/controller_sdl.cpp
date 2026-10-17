// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// SDL3 gamepads on the desktop (controller.h): each one plugged in takes the
// first free slot, is read, and is sent the effects the shared code
// (controller_common.cpp) works out: rumble, the triggers (an Xbox pad's
// trigger motors, or the DualSense's effects through SDL_SendGamepadEffect),
// the light bar. Gyro aiming and the touchpad where the pad has them.
// The gamepads come and go with the shell's events (sdl_input.cpp).
//
// A DualSense or DS4 plugged in while the app runs (Linux): SDL can open it
// through its evdev driver ("PS5 Controller", /dev/input/eventN), which has
// no light bar, trigger effects, gyro or touchpad, instead of its HIDAPI
// driver (/dev/hidrawN), which has them all and is what SDL picks for a pad
// already there at start. SDL's evdev driver only defers to HIDAPI when
// HIDAPI already knows the pad as its node appears; the hidraw node can come
// a moment later, and a user's log showed SDL never adding the HIDAPI
// instance afterwards (18 minutes) although the user could open the node.
// So a PlayStation pad opened without its light bar (padLacksHidapi):
// - the log names its path and the hidraw node of the same HID device
//   (sysfs) and whether this user can open it (else: a udev rule is missing);
// - SDL's gamepads are looked at again over the next ~3 s (refreshPads(),
//   each second from the main loop): an instance of the same HID device on
//   its hidraw node takes the slot over;
// - if none came and the node can be opened, SDL's gamepad subsystem is
//   started again once for that pad (as at start, when HIDAPI gets it) and
//   the pads are opened again in their slots.
// No SDL hint helps: SDL_HINT_JOYSTICK_HIDAPI is on by default (and an
// environment variable would win over it anyway), and turning the evdev
// driver off would lose every pad without hidraw access.
// One pad seen twice (two instance ids for one HID device) never takes two
// slots, and a pad SDL removes and adds again takes its slot back.
#include "input/controller.h"
#include "input/controller_backend.h"
#include "input/input_ext.h"
#include "input/sdl_backend.h"
#include "input/sdl_mapping.h"
#include "util/log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <vector>
#if defined(__linux__)
#include <unistd.h>
#endif

namespace xc::input {

namespace {

// SDL's longest rumble (SDL_MAX_RUMBLE_DURATION_MS): what "until changed"
// becomes. A duration keeps SDL re-sending the rumble to the pads that let
// it fade on their own; the shared code stops it when its time is up.
constexpr uint32_t kRumbleForever = 0xFFFF;

struct Slot {
    SDL_Gamepad* pad = nullptr;
    SDL_JoystickID id = 0;
    PadCaps caps;
    bool triggerMotors = false;  // SDL can drive its triggers' motors (an Xbox pad)
    bool accel = false;          // its accelerometer is on (gyro aiming's "up")
    bool lacksHidapi = false;    // a PlayStation pad without its effects' driver (padLacksHidapi)
    std::string name;
    std::string path;            // SDL_GetGamepadPath
    std::string hidDev;          // the HID device behind it (Linux sysfs), "" unknown
};
// The pads are opened, closed and read on the main thread (the shell's
// events and coreFrame); g_mutex keeps the caps and names whole for the
// UI's questions.
Slot g_slots[kMaxPads];
std::mutex g_mutex;
bool g_initedGamepads = false;  // SDL's gamepad subsystem started here (not by the shell)
// Main thread. Until when SDL's gamepads are looked at again for a pad's
// HIDAPI instance (SDL ms; 0 none), and the HID devices SDL was restarted
// for (once each).
uint64_t g_lookUntilMs = 0;
std::set<std::string> g_restartedFor;
constexpr uint64_t kLookForHidapiMs = 3000;

// The HID device behind a pad's node (Linux sysfs), "" where unknown.
std::string hidDeviceOf(const char* path) {
    std::string link = padSysfsLink(path);
    if (link.empty()) return {};
    std::error_code ec;
    auto real = std::filesystem::canonical(link, ec);
    return ec ? std::string() : real.string();
}

// That device's hidraw node ("/dev/hidrawN"), "" if none.
std::string hidrawOf(const std::string& hidDev) {
    if (hidDev.empty()) return {};
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(hidDev + "/hidraw", ec))
        return "/dev/" + e.path().filename().string();
    return {};
}

bool canOpen(const std::string& node) {
#if defined(__linux__)
    return !node.empty() && access(node.c_str(), R_OK | W_OK) == 0;
#else
    (void)node;
    return false;
#endif
}

uint32_t sdlDuration(uint32_t ms) { return ms ? std::min(ms, kRumbleForever) : kRumbleForever; }

PadKind kindOf(SDL_GamepadType type) {
    switch (type) {
    case SDL_GAMEPAD_TYPE_PS5: return PadKind::DualSense;
    case SDL_GAMEPAD_TYPE_PS4: return PadKind::DualShock4;
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE: return PadKind::Xbox;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return PadKind::Switch;
    default: return PadKind::Generic;
    }
}

const char* kindName(PadKind kind) {
    switch (kind) {
    case PadKind::DualSense: return "DualSense";
    case PadKind::DualShock4: return "DualShock 4";
    case PadKind::Xbox: return "Xbox";
    case PadKind::Switch: return "Switch";
    case PadKind::Generic: return "generic";
    default: return "none";
    }
}

void closePad(int slot);

// Opens gamepad `id` in the first free slot (`preferred` first when free).
void openPad(SDL_JoystickID id, int preferred = -1) {
    for (const Slot& s : g_slots)
        if (s.pad && s.id == id) return;  // the first scan and its ADDED event
    // The same pad through another driver (another instance id, one HID
    // device): its HIDAPI instance takes over the slot of one without its
    // effects; otherwise the slot keeps the one it has.
    const char* newPath = SDL_GetGamepadPathForID(id);
    std::string hidDev = hidDeviceOf(newPath);
    if (!hidDev.empty()) {
        for (int i = 0; i < kMaxPads; ++i) {
            const Slot& s = g_slots[i];
            if (!s.pad || s.hidDev != hidDev) continue;
            if (!(s.lacksHidapi && padPathIsHidraw(newPath))) return;  // seen already: one slot per pad
            XC_LOGI("pad %d: the same pad on SDL's HIDAPI driver (%s): it takes the slot over", i, newPath);
            closePad(i);
            preferred = i;
            break;
        }
    }
    int slot = preferred >= 0 && preferred < kMaxPads && !g_slots[preferred].pad ? preferred : -1;
    for (int i = 0; i < kMaxPads && slot < 0; ++i)
        if (!g_slots[i].pad) slot = i;
    if (slot < 0) {
        const char* name = SDL_GetGamepadNameForID(id);
        XC_LOGI("gamepad %s: no free slot (%d in use)", name ? name : "?", kMaxPads);
        return;
    }
    SDL_Gamepad* gp = SDL_OpenGamepad(id);
    if (!gp) {
        XC_LOGW("SDL_OpenGamepad(%u): %s", static_cast<unsigned>(id), SDL_GetError());
        return;
    }
    SDL_SetGamepadPlayerIndex(gp, slot);  // the pad's own player lights
    SDL_PropertiesID props = SDL_GetGamepadProperties(gp);
    Slot s;
    s.pad = gp;
    s.id = id;
    s.caps.kind = kindOf(SDL_GetGamepadType(gp));
    s.caps.rumble = SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
    s.caps.lightBar = SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN, false);
    // The DualSense's trigger effects go through SDL's own driver for it
    // (HIDAPI), the one that also drives its light bar; Linux's evdev
    // fallback has neither. A refused effect turns them off too.
    s.caps.adaptiveTriggers = s.caps.kind == PadKind::DualSense && s.caps.lightBar;
    s.triggerMotors = SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false);
    s.caps.triggerRumble = s.caps.adaptiveTriggers || s.triggerMotors;
    s.caps.gyro = SDL_GamepadHasSensor(gp, SDL_SENSOR_GYRO) && SDL_SetGamepadSensorEnabled(gp, SDL_SENSOR_GYRO, true);
    s.accel = SDL_GamepadHasSensor(gp, SDL_SENSOR_ACCEL) && SDL_SetGamepadSensorEnabled(gp, SDL_SENSOR_ACCEL, true);
    s.caps.touchpad = SDL_GetNumGamepadTouchpads(gp) > 0;
    const char* name = SDL_GetGamepadName(gp);
    s.name = name ? name : "";
    const char* path = SDL_GetGamepadPath(gp);
    s.path = path ? path : "";
    s.hidDev = hidDeviceOf(path);
    s.lacksHidapi = padLacksHidapi(static_cast<int>(s.caps.kind), s.caps.lightBar, path);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_slots[slot] = s;
    }
    slotOpened(slot);
    if (s.caps.lightBar) {  // the player's colour, as the PS5 shows it
        uint8_t c[3];
        playerLightColour(slot, c);
        SDL_SetGamepadLED(gp, c[0], c[1], c[2]);
    }
    const char* type = SDL_GetGamepadStringForType(SDL_GetGamepadType(gp));
    XC_LOGI("pad %d opened: %s (%s, SDL type %s, path %s; rumble %d, trigger rumble %d, adaptive triggers %d, light bar %d, "
            "gyro %d, accel %d, touchpad %d)",
            slot, s.name.c_str(), kindName(s.caps.kind), type ? type : "?", s.path.empty() ? "?" : s.path.c_str(),
            s.caps.rumble, s.caps.triggerRumble, s.caps.adaptiveTriggers, s.caps.lightBar, s.caps.gyro, s.accel,
            s.caps.touchpad);
    if (s.lacksHidapi) {
        std::string node = hidrawOf(s.hidDev);
        XC_LOGW("pad %d: not on SDL's HIDAPI driver (no light bar, trigger effects, gyro or touchpad); its hidraw node %s%s",
                slot, node.empty() ? "not found" : node.c_str(),
                node.empty()        ? ""
                : canOpen(node)     ? " can be opened: looking for it again"
                                    : " can't be opened by this user (a udev rule for the controller gives access)");
        g_lookUntilMs = SDL_GetTicks() + kLookForHidapiMs;
    }
}

// The gamepads SDL has that no slot holds: opened (a pad's other instance
// only where it is the better one).
void openUnopened(int preferred = -1) {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count; ++i) openPad(ids[i], preferred);
    SDL_free(ids);
}

// SDL's gamepads started again (as at the app's start, when its HIDAPI
// driver gets a PlayStation pad), each pad back in its slot.
void restartGamepads() {
    std::string had[kMaxPads];
    for (int i = 0; i < kMaxPads; ++i) {
        had[i] = g_slots[i].pad ? g_slots[i].hidDev : std::string();
        closePad(i);
    }
    kbmSdlForgetTouches();
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    bool joystickUp = SDL_WasInit(SDL_INIT_JOYSTICK) != 0;
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        XC_LOGE("SDL gamepads (again): %s", SDL_GetError());
        return;
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    XC_LOGI("controllers: SDL's gamepads started again (%d found%s)", count, joystickUp ? "; joysticks were still up" : "");
    for (int pass = 0; pass < 2; ++pass)  // those back in their slots first
        for (int i = 0; ids && i < count; ++i) {
            std::string dev = hidDeviceOf(SDL_GetGamepadPathForID(ids[i]));
            int was = -1;
            for (int k = 0; k < kMaxPads && !dev.empty(); ++k)
                if (had[k] == dev) was = k;
            if ((pass == 0) == (was >= 0)) openPad(ids[i], was);
        }
    SDL_free(ids);
}

void closePad(int slot) {
    Slot& s = g_slots[slot];
    if (!s.pad) return;
    if (SDL_GamepadConnected(s.pad)) {  // still there (the app closing): its effects stopped
        if (s.caps.rumble) SDL_RumbleGamepad(s.pad, 0, 0, 0);
        if (s.caps.adaptiveTriggers) {
            uint8_t off[kDs5EffectsSize];
            ds5TriggerEffects(TriggerCommand{}, TriggerCommand{}, off);
            SDL_SendGamepadEffect(s.pad, off, sizeof off);
        } else if (s.triggerMotors) {
            SDL_RumbleGamepadTriggers(s.pad, 0, 0, 0);
        }
    }
    slotClosed(slot);
    SDL_CloseGamepad(s.pad);
    XC_LOGI("pad %d closed (%s)", slot, s.name.c_str());
    std::lock_guard<std::mutex> lock(g_mutex);
    s = {};
}

}  // namespace

namespace backend {

bool padOpen(int slot) { return g_slots[slot].pad != nullptr; }

bool readPad(int slot, RawPad& out) {
    const Slot& s = g_slots[slot];
    SDL_Gamepad* gp = s.pad;
    if (!SDL_GamepadConnected(gp)) return false;
    auto button = [gp](SDL_GamepadButton b) { return SDL_GetGamepadButton(gp, b); };
    out.dpadUp = button(SDL_GAMEPAD_BUTTON_DPAD_UP);
    out.dpadDown = button(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    out.dpadLeft = button(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    out.dpadRight = button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    // By position, whatever is printed on them (Cross / A at the bottom).
    out.btnA = button(SDL_GAMEPAD_BUTTON_SOUTH);
    out.btnB = button(SDL_GAMEPAD_BUTTON_EAST);
    out.btnX = button(SDL_GAMEPAD_BUTTON_WEST);
    out.btnY = button(SDL_GAMEPAD_BUTTON_NORTH);
    out.btnL1 = button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    out.btnR1 = button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    out.btnL3 = button(SDL_GAMEPAD_BUTTON_LEFT_STICK);
    out.btnR3 = button(SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    out.btnOptions = button(SDL_GAMEPAD_BUTTON_START);
    // View: an Xbox pad's View (Back), or the DualSense's touchpad click as on the PS5.
    out.btnTouchpad = button(SDL_GAMEPAD_BUTTON_BACK) || button(SDL_GAMEPAD_BUTTON_TOUCHPAD);
    // The desktop leaves the guide / PS button to the app: the Xbox button.
    out.btnNexus = button(SDL_GAMEPAD_BUTTON_GUIDE);

    out.leftX = sdlStickAxis(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX));
    out.leftY = sdlStickAxis(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY));
    out.rightX = sdlStickAxis(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTX));
    out.rightY = sdlStickAxis(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTY));
    out.l2 = sdlTriggerAxis(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    out.r2 = sdlTriggerAxis(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));

    // rad/s, x pitch, y yaw, z roll (scePad's convention); the "gyro:" log
    // lines show the signs on a real pad.
    if (s.caps.gyro) SDL_GetGamepadSensorData(gp, SDL_SENSOR_GYRO, out.gyro, 3);
    if (s.accel) {
        float ms2[3] = {0, 0, 0};
        if (SDL_GetGamepadSensorData(gp, SDL_SENSOR_ACCEL, ms2, 3)) {
            sdlAccelToG(ms2, out.accel);
            out.hasAccel = true;
        }
    }

    if (s.caps.touchpad) {
        bool down = false;
        float x = 0, y = 0, pressure = 0;
        if (SDL_GetGamepadTouchpadFinger(gp, 0, 0, &down, &x, &y, &pressure) && down) {
            out.touching = true;  // SDL gives 0..1 from the top left already
            out.touchX = std::clamp(x, 0.0f, 1.0f);
            out.touchY = std::clamp(y, 0.0f, 1.0f);
        }
    }
    return true;
}

void setMotors(int slot, uint8_t large, uint8_t small, uint32_t durationMs) {
    const Slot& s = g_slots[slot];
    if (!s.caps.rumble) return;
    bool ok = SDL_RumbleGamepad(s.pad, static_cast<uint16_t>(large * 257), static_cast<uint16_t>(small * 257),
                                sdlDuration(durationMs));
    static int logged = 0;  // the first, and a few failures
    if (logged == 0 || (!ok && logged < 6))
        XC_LOGI("SDL_RumbleGamepad(%u, %u, %u ms): %s", large, small, durationMs, ok ? "ok" : SDL_GetError()), ++logged;
}

TriggerStyle triggerStyle(int slot) {
    const Slot& s = g_slots[slot];
    if (s.caps.adaptiveTriggers) return TriggerStyle::Adaptive;
    return s.triggerMotors ? TriggerStyle::Motors : TriggerStyle::None;
}

int setTriggerEffects(int slot, const TriggerCommand command[2]) {
    Slot& s = g_slots[slot];
    uint8_t packet[kDs5EffectsSize];
    ds5TriggerEffects(command[0], command[1], packet);
    if (SDL_SendGamepadEffect(s.pad, packet, sizeof packet)) return 0;
    // No effects through this pad's driver (Linux's evdev, without access to
    // its hidraw node): plain triggers from now on, and Settings hides them.
    static bool logged = false;
    if (!logged) XC_LOGW("pad %d: trigger effects refused (%s): no adaptive triggers", slot, SDL_GetError());
    logged = true;
    std::lock_guard<std::mutex> lock(g_mutex);
    s.caps.adaptiveTriggers = false;
    s.caps.triggerRumble = s.triggerMotors;
    return -1;
}

void setTriggerMotors(int slot, uint8_t left, uint8_t right, uint32_t durationMs) {
    const Slot& s = g_slots[slot];
    bool ok = SDL_RumbleGamepadTriggers(s.pad, static_cast<uint16_t>(left * 257), static_cast<uint16_t>(right * 257),
                                        sdlDuration(durationMs));
    static int logged = 0;
    if (logged == 0 || (!ok && logged < 6))
        XC_LOGI("SDL_RumbleGamepadTriggers(%u, %u, %u ms): %s", left, right, durationMs, ok ? "ok" : SDL_GetError()), ++logged;
}

void setLight(int slot, uint8_t r, uint8_t g, uint8_t b) {
    const Slot& s = g_slots[slot];
    if (!s.caps.lightBar) return;
    bool ok = SDL_SetGamepadLED(s.pad, r, g, b);
    static int logged = 0;
    if (logged == 0 || (!ok && logged < 6)) XC_LOGI("SDL_SetGamepadLED(%u, %u, %u): %s", r, g, b, ok ? "ok" : SDL_GetError()), ++logged;
}

void resetLight(int slot) {
    const Slot& s = g_slots[slot];
    if (!s.caps.lightBar) return;
    uint8_t c[3];
    playerLightColour(slot, c);
    SDL_SetGamepadLED(s.pad, c[0], c[1], c[2]);
}

}  // namespace backend

void padSdlInit() {
    if (!SDL_WasInit(SDL_INIT_GAMEPAD)) {
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
            XC_LOGE("SDL gamepads: %s", SDL_GetError());
            return;
        }
        g_initedGamepads = true;
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count; ++i) openPad(ids[i]);
    SDL_free(ids);
    XC_LOGI("controllers: %d gamepad(s) found", count);
}

void padSdlEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        openPad(event.gdevice.which);
    } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (int i = 0; i < kMaxPads; ++i)
            if (g_slots[i].pad && g_slots[i].id == event.gdevice.which) {
                closePad(i);
                // SDL replacing a device: its other instance (added first,
                // and left alone while this one held the slot) takes it.
                openUnopened(i);
            }
    }
}

void padSdlShutdown() {
    shutdown();
    if (g_initedGamepads) SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    g_initedGamepads = false;
}

bool init() { return true; }  // the pads come with sdlInputInit() and the window's events

void shutdown() {
    for (int i = 0; i < kMaxPads; ++i) closePad(i);
}

bool poll(ControllerState& out) {
    for (int i = 0; i < kMaxPads; ++i)
        if (g_slots[i].pad) return pollPad(i, out);
    out = {};
    return false;
}

void refreshPads() {
    // Pads plugged in or out arrive as events; this (each second, main
    // thread) only looks for a PlayStation pad's HIDAPI instance.
    if (!g_lookUntilMs) return;
    openUnopened();
    if (SDL_GetTicks() < g_lookUntilMs) return;
    g_lookUntilMs = 0;
    for (int i = 0; i < kMaxPads; ++i) {
        const Slot& s = g_slots[i];
        if (!s.pad || !s.lacksHidapi || s.hidDev.empty() || g_restartedFor.count(s.hidDev)) continue;
        if (!canOpen(hidrawOf(s.hidDev))) continue;
        XC_LOGI("pad %d: no HIDAPI instance came; SDL's gamepads are started again", i);
        g_restartedFor.insert(s.hidDev);
        restartGamepads();
        return;
    }
}

std::string padUserName(int) { return {}; }

std::string padDeviceName(int index) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return index >= 0 && index < kMaxPads && g_slots[index].pad ? g_slots[index].name : std::string();
}

int32_t padUserId(int) { return -1; }

void setEmbeddedMicActive(bool, int) {}  // no such weakening outside the console

PadCaps padCaps(int index) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return index >= 0 && index < kMaxPads && g_slots[index].pad ? g_slots[index].caps : PadCaps{};
}

PadCaps anyPadCaps() {
    std::lock_guard<std::mutex> lock(g_mutex);
    PadCaps any;
    for (const Slot& s : g_slots) {
        if (!s.pad) continue;
        if (any.kind == PadKind::None) any.kind = s.caps.kind;  // the first pad's kind
        any.rumble |= s.caps.rumble;
        any.triggerRumble |= s.caps.triggerRumble;
        any.adaptiveTriggers |= s.caps.adaptiveTriggers;
        any.lightBar |= s.caps.lightBar;
        any.gyro |= s.caps.gyro;
        any.touchpad |= s.caps.touchpad;
    }
    return any;
}

}  // namespace xc::input
