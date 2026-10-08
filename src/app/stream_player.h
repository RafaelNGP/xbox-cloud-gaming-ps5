// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Plays a provisioned xCloud session on the console: WebRTC session, H.264
// decode thread drawing to the screen, Opus to the audio output, and the
// DualSense state forwarded as an Xbox controller.
#pragma once

#include "input/controller.h"
#include "stream/stream_session.h"
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
    // From the input thread, once per polled pad state; `index` 0..3 is the
    // controller's slot (input::pollPad).
    void sendInput(const input::ControllerState& pad, int index = 0);
    // Controllers 1..3 coming and going (0 is there from the start).
    void setPadConnected(int index, bool connected);
    // The game's requests for text, oldest first (main thread): false when
    // none. Each one is answered once with answerTextInput() (accepted false
    // = cancelled); textInputWithdrawn() turns true when the game gave up
    // waiting, and the answer is then dropped.
    bool takeTextInput(stream::TextInputRequest& out);
    bool textInputWithdrawn(const std::string& id);
    void answerTextInput(const std::string& id, bool accepted, const std::string& text);
    // A new key frame (a clean picture), as after packet loss.
    void requestKeyframe();
    // Another stream tier mid-session: "720", "720HQ", "1080", "1080HQ", "1440".
    void requestResolution(const std::string& alias);
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
        uint64_t rtpKbps = 0, rembKbps = 0, vibrations = 0;
        uint64_t lateFrames = 0;  // decoded, not drawn: both flips still queued
        int rttMs = -1;  // to the stream server
        int width = 0, height = 0;  // of the last decoded picture
        // Since the previous stats() call (so call it from one place only).
        uint64_t decodeAvgUs = 0, decodeMaxUs = 0, drawAvgUs = 0, drawMaxUs = 0;
    };
    Stats stats() const;
    // Before start(): H.264 decoder threads (more than 1 = frame threading).
    void setDecodeThreads(int threads);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace xc::app
