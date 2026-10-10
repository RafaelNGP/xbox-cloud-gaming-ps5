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

// The game asks for text (a name, a chat line): the server's
// "ShowVirtualKeyboard". Answer with completeTextInput() or cancelTextInput().
struct TextInputRequest {
    std::string id;  // the transaction
    std::string title, description, defaultText;
    int inputScope = 0;  // 0 default, 1 URL, 5 e-mail, 29 number, 31 password, 32 phone, 50 search
    int maxLength = 0;   // 0: no limit
};

struct StreamCallbacks {
    std::function<void(const uint8_t* data, size_t len, uint32_t rtpTimestamp)> video;
    std::function<void(const uint8_t* data, size_t len, uint32_t rtpTimestamp)> audio;
    std::function<void(const Vibration&)> vibration;
    // The server will end the session for inactivity in `seconds` unless
    // input arrives.
    std::function<void(int seconds)> idleWarning;
    // The game gained or lost the focus on the cloud console (lost: another
    // window is up, such as a publisher's web page wanting touch).
    std::function<void(bool focused)> titleFocus;
    // Set: the game's keyboard requests come here (else the server draws its
    // own); `textInputCancelled` when the game withdraws one.
    std::function<void(const TextInputRequest&)> textInput;
    std::function<void(const std::string& id)> textInputCancelled;
    // The stream ended. Called once. `recoverable`: the connection was lost
    // (the cloud session may still be there to reconnect to); false when
    // the server ended it (idle kick, the game closed).
    std::function<void(const std::string& reason, bool recoverable)> closed;
};

struct StreamOptions {
    int connectTimeoutMs = 30000;
    // The web client asks for a key frame this often to recover from loss
    // without NACK; 0 disables.
    int keyframeIntervalSec = 5;
    // Stream tier requested on the control channel after connecting, as the
    // xbox.com client does ("userRequestedResolutionUpdate"): "720", "720HQ",
    // "1080", "1080HQ" or "1440" (the HQ tiers and 1440 need Game Pass
    // Ultimate; the plain tier is asked for first, so it holds where they
    // aren't granted). Empty: don't send, the service picks.
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

// Milliseconds on a clock shared by every session of the process (input and
// frame reports carry it; it must not restart when a session reconnects).
double clockMs();

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
    // Voice chat: sends an encoded Opus frame to the stream server (RTP @ 48 kHz).
    void sendAudio(const uint8_t* opus, size_t size, uint32_t timestamp);
    // Touch input on or off (off at the start).
    void setTouchEnabled(bool on);
    // A controller at `index` (1..3; 0 is attached at the start) came or went.
    void setGamepadConnected(int index, bool connected);
    void requestKeyframe();
    // A frame shown: its timings go back to the server (see FrameMetadata).
    void reportFrame(const FrameMetadata& frame);
    // The clock FrameMetadata times are on, in ms (stream::clockMs()).
    double clockMs() const;
    // Test hook: drops the connection as a network outage would.
    void simulateDrop();
    void completeTextInput(const std::string& id, const std::string& text);
    void cancelTextInput(const std::string& id);
    // Another stream tier mid-session (StreamOptions::resolutionAlias).
    void requestResolution(const std::string& alias);
    // Sends keepalives and periodic key frame requests; call about once a
    // second from the thread that owns the GssvClient.
    void tick();
    void close();
    // Round trip to the stream server (SCTP), ms; -1 while unknown.
    int rttMs() const;
    // RTP loss / retransmission counters of the video track.
    const VideoReceiveStats& videoStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace xc::stream
