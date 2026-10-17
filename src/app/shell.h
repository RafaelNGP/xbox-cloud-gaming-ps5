// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The parts of start-up and of the app's life that only some platforms
// have, as free functions each front end's shell defines, picked at link
// time like the backends: shell_ps5.cpp on the console, shell_desktop.cpp in
// the desktop app (SDL3: the window's events, the browser), shell_null.cpp
// for a desktop build without a window (psbox-headless). See
// docs/multiplatform.md.
#pragma once

#include "app/settings.h"

#include <string>
#include <vector>

namespace xc::app::shell {

// Before settings.json is read: an update cut short is rolled back (and the
// app started again), a store update's leftovers brought back.
void beforeSettings();
// Once the settings are read, before the display comes up: the PS5 hides
// its splash screen and settles the video mode (120 Hz).
void beforeDisplay(const Settings& settings);
// Right after display::init(): the desktop app hands its window to the
// input backend. False when the app can't run without the display that
// failed (the desktop app has nothing to show then): it closes.
bool afterDisplay(bool haveDisplay);
// Where the system's icon font may be (keyboard and mouse glyphs), first
// choice first; empty when there is none (the UI draws its own icons).
std::vector<std::string> iconFontPaths();
// Each pass of the main loop: the system's events. False once the user
// closed the app (never on the PS5, where the system ends it).
bool pumpEvents();
// Whether the app's window has the keyboard's focus (always on the PS5):
// the mouse is only held for a game while it has.
bool hasFocus();
// Whether this build installs an update itself; elsewhere a newer release
// is only noted in the log, never offered.
bool canSelfUpdate();
// Whether the running app is the copy in `dir` (where an update goes): one
// started from elsewhere wouldn't be the one replaced.
bool isInstalledCopy(const std::string& dir);
// After an update: the new version started in place of this one, else the
// app closed (its next start is the new one).
[[noreturn]] void restartOrExit();
// coreShutdown(), once the worker has stopped and before the backends
// close (never on the PS5): the desktop app lets go of its input hooks.
void beforeShutdown();

// What kind of system this is, for what the screens say and offer.
struct Caps {
    bool console = false;     // the PS5: "this console", the paths of its menus
    bool openUrl = false;     // openUrl() works: a link on screen opens in the browser
    bool selfUpdate = false;  // canSelfUpdate()
    bool window = false;      // the app is a window: it can go fullscreen (setFullscreen())
};
Caps caps();
// A desktop's window fullscreen or not (Settings > Display mode); nothing
// where the app isn't a window.
void setFullscreen(bool on);
// True once after the user switched fullscreen another way (F11 /
// Alt+Enter), with the new state in `on`: the setting follows. Never on the
// PS5.
bool fullscreenSwitched(bool& on);
// Opens `url` in the system's browser; false where there is none (PS5).
bool openUrl(const std::string& url);
// The keyboard typing text into a field (the home search): on while the UI
// wants it (main thread, each pass). True where typed text comes through
// takeTextInput() (the desktop); false where there is none (PS5, null),
// whose keys keep moving through the screens as before.
bool setTextInput(bool on);
// The text typed since the last call, UTF-8, with "\b" for each Backspace
// and "\n" for Enter pressed while text input is on. Empty elsewhere.
std::string takeTextInput();

}  // namespace xc::app::shell
