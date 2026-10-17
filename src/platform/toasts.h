// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Short notices shown inside the app: platform::notify() in the desktop app,
// where there is no system notification to send. A thread-safe queue that
// the menus (ui::AppUi) and the streaming overlay (StreamScreen) draw from.
// Off until the desktop shell turns it on: the PS5 sends system
// notifications, the tools print to stdout.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace xc::platform {

class ToastQueue {
public:
    static constexpr uint64_t kShowMs = 5000;  // each one's time on screen
    static constexpr size_t kMaxShown = 3;     // at once; the next ones wait their turn
    static constexpr size_t kMaxQueued = 12;   // the oldest dropped beyond this

    // A notice; the same text again restarts it instead of stacking up.
    void push(const std::string& text, uint64_t nowMs);
    // The notices on screen at `nowMs`, oldest first: those whose time is up
    // are dropped and waiting ones take their place.
    std::vector<std::string> visible(uint64_t nowMs);
    // Changes whenever visible() would answer differently: a redraw is due.
    uint64_t version(uint64_t nowMs);
    void clear();

private:
    struct Toast {
        std::string text;
        bool shown = false;
        uint64_t shownAt = 0;
    };
    void update(uint64_t nowMs);  // mutex_ held

    std::mutex mutex_;
    std::vector<Toast> toasts_;
    uint64_t version_ = 0;
};

// The app's queue; platform::notify() pushes to it once it is enabled.
ToastQueue& toasts();
void enableToasts(bool on);
bool toastsEnabled();

}  // namespace xc::platform
