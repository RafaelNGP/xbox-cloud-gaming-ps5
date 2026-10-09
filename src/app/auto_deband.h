// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Block smoothing "auto": the level (0 off, 1 low, 2 high) follows the
// stream's bitrate, stronger as it falls.
#pragma once

namespace xc::app {

// The level for a bitrate (Mbps) coming from `now`: high below ~5 Mbps, off
// above ~10. A 1 Mbps margin each way keeps a bitrate near a limit from
// flipping it; -1 = no measure (below 0.5 Mbps: the stream is starting, or
// the picture is standing still).
int debandForMbps(double mbps, int now);

// Fed the bitrate once a second: ignores the first 5 s (it is still
// climbing), and moves to a new level after 3 s in a row asking for it.
class AutoDeband {
public:
    void reset() { *this = AutoDeband(); }  // a new stream
    // True when the level changed.
    bool update(double mbps);
    int level() const { return inUse_; }

private:
    int inUse_ = 1, next_ = -1, streak_ = 0, seconds_ = 0;
};

}  // namespace xc::app
