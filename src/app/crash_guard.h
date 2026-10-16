// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Notices when the app went down. On the PS5 the app never ends normally:
// closed from the system's menu, its process is killed like a crash would.
// So a crash leaves a marker itself: a fatal signal (or an uncaught C++
// exception) writes what it was and the app's stage at the time, and a main
// loop stuck for long writes one too. The next start reads it and offers a
// report; being closed by the user leaves none.
#pragma once

#include <cstdint>
#include <string>

namespace xc::app::crash {

// Handlers for the fatal signals and std::terminate, writing to `markerPath`.
void install(const std::string& markerPath);
// What the app is doing (copied; up to 95 bytes): "home", "streaming ...".
void setStage(const char* stage);
// The main loop is alive. startWatchdog(): a thread that writes a "stalled"
// marker after `stallMs` without a heartbeat, and removes it if the loop
// comes back (a long hang, not a crash).
void heartbeat();
void startWatchdog(uint32_t stallMs);

struct Previous {
    bool found = false;
    std::string kind;   // "signal 11 (SIGSEGV)", "uncaught exception", "stalled"
    std::string stage;  // setStage()'s at the time
    std::string crash;  // "crash: SIGSEGV pc=eboot+0x1a2b3c addr=0x0 ..."
};
// The last session's marker, if it left one (then removed).
Previous takePrevious(const std::string& markerPath);

}  // namespace xc::app::crash
