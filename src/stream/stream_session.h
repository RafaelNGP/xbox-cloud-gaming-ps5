// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The WebRTC side of an xCloud session, once gssv reports it Provisioned:
// SDP/ICE exchange through GssvClient, then the four data channels (message,
// control, input, chat) and the audio/video tracks.
//
// Media leaves through callbacks on libdatachannel's threads: video as H.264
// Annex-B access units, audio as single Opus packets (48 kHz).
#pragma once

#include "stream/input_packet.h"
#include "stream/video_stats.h"
#include "xcloud/gssv.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace xc::stream {

struct StreamCallbacks {
    std::function<void(const uint8_t* data, size_t len, uint32_t rtpTimestamp)> video;
    std::function<void(const uint8_t* data, size_t len, uint32_t rtpTimestamp)> audio;
    std::function<void(const Vibration&)> vibration;
    // The stream ended (server disconnect, connection lost). Called once.
    std::function<void(const std::string& reason)> closed;
};

struct StreamOptions {
    int connectTimeoutMs = 30000;
    // The web client asks for a key frame this often to recover from loss
    // without NACK; 0 disables.
    int keyframeIntervalSec = 5;
    // Stream tier requested on the control channel after connecting, as the
    // xbox.com client does ("userRequestedResolutionUpdate"): "720", "720HQ",
    // "1080", "1080HQ" or "1440" (the HQ tiers and 1440 need Game Pass
    // Ultimate). Empty: don't send, the service picks.
    std::string resolutionAlias;
    // Test hook: drop this percentage of video RTP packets before the jitter
    // buffer, to exercise NACK / key frame recovery.
    int simulatedVideoLoss = 0;
    // Ceiling of the bandwidth estimate sent to the server (REMB), bits/s.
    uint32_t maxBitrate = 25000000;
};

// "a=candidate:..." with a Teredo (2001::/32) address -> the IPv4 address and
// port it embeds. Returns false for anything else.
bool decodeTeredo(const std::string& ipv6, std::string& ipv4, int& port);

class StreamSession {
public:
    StreamSession(xcloud::GssvClient& gssv, StreamCallbacks callbacks, StreamOptions options = {});
    ~StreamSession();
    StreamSession(const StreamSession&) = delete;
    StreamSession& operator=(const StreamSession&) = delete;

    // Blocking: negotiates, connects and completes the channel handshakes.
    bool start(std::string& err);
    bool isOpen() const;

    void sendGamepad(const GamepadFrame& frame);
    void requestKeyframe();
    // Sends keepalives and periodic key frame requests; call about once a
    // second from the thread that owns the GssvClient.
    void tick();
    void close();
    // RTP loss / retransmission counters of the video track.
    const VideoReceiveStats& videoStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace xc::stream
