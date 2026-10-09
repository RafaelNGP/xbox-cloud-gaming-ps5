// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/auto_deband.h"

namespace xc::app {

int debandForMbps(double mbps, int now) {
    if (mbps < 0.5) return -1;
    if (now == 2) return mbps >= 6 ? (mbps >= 11 ? 0 : 1) : 2;
    if (now == 1) return mbps < 4 ? 2 : mbps >= 11 ? 0 : 1;
    return mbps < 4 ? 2 : mbps < 9 ? 1 : 0;
}

bool AutoDeband::update(double mbps) {
    int want = ++seconds_ <= 5 ? -1 : debandForMbps(mbps, inUse_);
    if (want < 0 || want == inUse_) {
        streak_ = 0;
    } else if (want == next_ && ++streak_ >= 3) {
        inUse_ = want;
        streak_ = 0;
        return true;
    } else if (want != next_) {
        next_ = want;
        streak_ = 1;
    }
    return false;
}

}  // namespace xc::app
