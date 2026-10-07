// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Plays a provisioned xCloud session on the console: WebRTC session, H.264
// decode thread drawing to the screen, Opus to the audio output, and the
// DualSense state forwarded as an Xbox controller.
#pragma once

#include "input/controller.h"
#include "xcloud/gssv.h"

#include <memory>
#include <string>

namespace xc::app {

class StreamPlayer {
public:
    explicit StreamPlayer(xcloud::GssvClient& gssv);
    ~StreamPlayer();

    // Blocking: connects the stream and starts playback.
    bool start(std::string& err);
    bool running() const;
    std::string endReason() const;
    // From the input thread, once per polled pad state.
    void sendInput(const input::ControllerState& pad);
    // Keepalives etc.; about once a second from the thread owning `gssv`.
    void tick();
    void stop();
    // Appends every received access unit to `path` for `seconds` (diagnostics).
    void dumpVideo(const std::string& path, int seconds);
    // Writes the next decoded picture, half size, as a binary PPM.
    void requestSnapshot(const std::string& path);

    struct Stats {
        uint64_t videoFrames = 0, decodedFrames = 0, droppedFrames = 0, audioPackets = 0;
        uint64_t decodeFailures = 0, queueResets = 0, keyframeRequests = 0, queued = 0;
        // RTP: packets lost, recovered by NACK, frames the jitter buffer dropped.
        uint64_t rtpPackets = 0, rtpLost = 0, rtpRecovered = 0, rtpNacks = 0, rtpDroppedFrames = 0;
    };
    Stats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace xc::app
