// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The account's Xbox cloud time (limited each month since November 2026):
// how it is written on screen, and how a stream counts it down.
#pragma once

#include "ui/canvas.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace xc::ui {

// "4 h 10 min", "47 min", "< 1 min": whole minutes, rounded down, so never
// more than is left.
inline std::string formatCloudTime(int64_t seconds) {
    if (seconds <= 0) return "0 min";
    if (seconds < 60) return "< 1 min";
    const int64_t hours = seconds / 3600, minutes = seconds % 3600 / 60;
    if (!hours) return std::to_string(minutes) + " min";
    if (!minutes) return std::to_string(hours) + " h";
    return std::to_string(hours) + " h " + std::to_string(minutes) + " min";
}

// A clock face of radius `r`: its ring and two hands.
inline void drawClockIcon(Canvas& c, float cx, float cy, float r, Color color) {
    const float t = r * 0.26f;
    c.strokeArc(cx, cy, r - t / 2, t, 0.0f, 6.2832f, color);
    c.line(cx, cy, cx, cy - r * 0.52f, t, color);
    c.line(cx, cy, cx + r * 0.42f, cy, t, color);
}

// The time left during a stream. The service answers once at the start; the
// clock counts down from there and takes a later answer as reconcile() says.
// The notices: one when 15 minutes are left, one at 5.
class CloudTimeClock {
public:
    static constexpr int64_t kNotices[2] = {15 * 60, 5 * 60};

    // The time left after some play: `counted` by the clock, `answer` the
    // service's latest, `before` its answer from before that play (-1: none
    // known). An answer over `before` brought new hours (a new month, hours
    // bought); otherwise the lower of the two, as the service may count a
    // session only once it ends: never more than is left.
    static int64_t reconcile(int64_t counted, int64_t answer, int64_t before) {
        if (answer < 0) return counted;
        if (counted < 0 || answer > before) return answer;
        return std::min(counted, answer);
    }

    // `remaining` < 0: the account has no limit, or it is not known.
    void start(int64_t remaining, uint64_t nowMs) {
        base_ = start_ = remaining;
        baseMs_ = nowMs;
        next_ = 0;
    }
    bool limited() const { return base_ >= 0; }
    int64_t left(uint64_t nowMs) const {
        if (base_ < 0) return -1;
        const int64_t gone = static_cast<int64_t>((nowMs - baseMs_) / 1000);
        return std::max<int64_t>(0, base_ - gone);
    }
    void answer(int64_t remaining, uint64_t nowMs) {
        const int64_t counted = left(nowMs), now = reconcile(counted, remaining, start_);
        if (remaining > start_) start_ = remaining;
        if (now == counted) return;
        base_ = now;
        baseMs_ = nowMs;
    }
    // The time to tell the player about now, once per notice, when the time
    // shown (whole minutes) reaches it; a stream that starts under 15
    // minutes gets one right away. -1 when none is due.
    int64_t noticeDue(uint64_t nowMs) {
        const int64_t l = left(nowMs);
        if (l <= 0) return -1;
        bool due = false;
        while (next_ < 2 && l < kNotices[next_] + 60) ++next_, due = true;
        return due ? l : -1;
    }

private:
    int64_t base_ = -1, start_ = -1;  // the count's base; the service's answer it started from
    uint64_t baseMs_ = 0;
    int next_ = 0;  // the next of kNotices still to come
};

}  // namespace xc::ui
