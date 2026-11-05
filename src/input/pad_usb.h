// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Controllers the PS5 itself does not take (Xbox One / Series pads on a
// cable, 8BitDo and other XInput pads and their 2.4G dongles), read by the
// app straight from USB. Every one drives the app's menus; in a game each
// plays as a player of its own (usbPadPlayer).
//
// The reports are parsed here, on every platform (the tests feed them);
// reading them off USB is the platform's (pad_usb_ps5.cpp: /dev/ugen,
// after OmniPad CE, github.com/jpemendonca/OmniPad-CE, GPL-3.0). Elsewhere
// there is no transport and no USB pad.
#pragma once

#include "input/controller.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xc::input {

// What a pad speaks, from its USB interface (class 0xFF):
//   XInput: subclass 0x5D, protocol 0x01 (Xbox 360 and its clones: 8BitDo,
//           GameSir, Machenike... in their PC mode);
//   GIP:    subclass 0x47, protocol 0xD0 (Xbox One and Series pads).
enum class UsbPadProtocol {
    None,
    XInput,
    Gip,
    DualShock3,
    DualShock4,
    DualSense,
    SwitchPro,
    SwitchInputOnly,
    Stadia,
    GenericHid
};
const char* usbPadProtocolName(UsbPadProtocol protocol);
UsbPadProtocol usbPadProtocolFor(uint8_t ifaceClass, uint8_t ifaceSubclass, uint8_t ifaceProtocol);

// The pad's interface in a configuration descriptor (as USB_GET_FULL_DESC
// gives it): the first XInput / GIP one (alternate setting 0) and its
// interrupt endpoints. protocol None when the device has none.
struct UsbPadInterface {
    UsbPadProtocol protocol = UsbPadProtocol::None;
    uint8_t number = 0;
    uint8_t epIn = 0, epOut = 0;  // endpoint addresses (epIn has bit 7); 0: none
};
UsbPadInterface findUsbPadInterface(const uint8_t* configDesc, size_t len);

// GIP: the acknowledgements (13 bytes each) a report asks for, one per
// message in it with the ACK option (0x10); the pad repeats them otherwise.
std::vector<std::array<uint8_t, 13>> gipAcksFor(const uint8_t* data, size_t len);

// One report into `state`, as read: sticks -1..1 (+y down) with no dead
// zone, triggers 0..1. `state` comes in with the pad's last state: a
// report that carries only some of the buttons (GIP's Xbox button) leaves
// the rest as they were. False for a report that is not input (LED,
// rumble and status reports, GIP's announce / keep-alive...).
bool parseXInputReport(const uint8_t* data, size_t len, ControllerState& state);
bool parseGipReport(const uint8_t* data, size_t len, ControllerState& state);
bool parseDualShock3Report(const uint8_t* data, size_t len, ControllerState& state);
bool parseDualShock4Report(const uint8_t* data, size_t len, ControllerState& state);
bool parseDualSenseReport(const uint8_t* data, size_t len, ControllerState& state);
bool parseSwitchProReport(const uint8_t* data, size_t len, ControllerState& state);
bool parseSwitchInputOnlyReport(const uint8_t* data, size_t len, ControllerState& state);
bool parseStadiaReport(const uint8_t* data, size_t len, ControllerState& state);
bool parseUsbPadReport(UsbPadProtocol protocol, const uint8_t* data, size_t len, ControllerState& state);

// The transport tells the shared code about each pad (slot 0..kMaxUsbPads-1).
constexpr int kMaxUsbPads = 4;
void usbPadAttached(int slot, const std::string& name);
void usbPadState(int slot, const ControllerState& state);
void usbPadDetached(int slot);

// Players (0..kMaxPads-1). A pad takes the first free one as it connects,
// in the order they come (free: no DualSense connected there and no other
// USB pad on it), from the first pollUsbPad() after it came. Holding its
// Xbox / Home button, LB and RB move it to the player before / after; the
// button alone (let go without them) asks for the in-game menu, for that
// pad (UsbPadEvent::MenuButton). It never reaches the game itself, and
// LB / RB do not while it is held.
int usbPadPlayer(int slot);  // -1: no pad, or not given a player yet
void setUsbPadPlayer(int slot, int player);  // the in-game menu's choice
std::string usbPadName(int slot);

// What the app acts on, oldest first: notifications, and the in-game menu.
struct UsbPadEvent {
    enum class Kind { Connected, Moved, Disconnected, MenuButton } kind = Kind::Connected;
    int player = 0;
    std::string name;
    int slot = -1;
};
std::vector<UsbPadEvent> takeUsbPadEvents();

// The USB pads connected, added up, with the dead zones and the Circle /
// Cross swap applied as on the DualSense: every one (the menus), those
// playing as `player` but `except` (a slot whose pad has the in-game
// menu), or the one in `slot`. False (and `out` cleared) when there is none.
bool pollUsbPad(ControllerState& out);
bool pollUsbPadsFor(int player, ControllerState& out, int except = -1);
bool pollUsbPadSlot(int slot, ControllerState& out);
int usbPadCount();
// After the in-game menu closes: each pad stays out of the game until its
// buttons are let go (the one that closed the menu would press it there).
void usbPadsWaitRelease();
// The name of the first USB pad playing as `player` (empty: none), and
// when one of them was last used (ms; 0: never).
std::string usbPadNameFor(int player);
uint64_t usbPadActiveAt(int player);

// PS5: starts and stops looking for USB pads (pad_usb_ps5.cpp; called by
// controller_ps5.cpp). Not defined on the other platforms.
void usbPadsStart();
void usbPadsStop();

}  // namespace xc::input
