// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// H.264 RTP receiver: a small jitter buffer that hands the decoder only
// complete access units, in order.
//
// libdatachannel's H264RtpDepacketizer emits a frame as soon as its marker
// packet arrives, holes and all; the decoder then smears the damage over every
// following frame until the next key frame (macroblocks, pink/green blocks).
// Here a hole is answered with an RTCP NACK (the xCloud server retransmits on
// the same SSRC), the frame waits briefly for the retransmission, and if it
// never comes the frame is dropped, a key frame is requested (PLI plus the
// caller's callback) and nothing is shown until that key frame: a short
// freeze instead of corruption.
#pragma once

#include "stream/video_stats.h"

#include <rtc/rtc.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace xc::stream {

class H264Receiver final : public rtc::MediaHandler {
public:
    // `onKeyframeNeeded` runs on libdatachannel's thread.
    H264Receiver(std::function<void()> onKeyframeNeeded, VideoReceiveStats& stats, uint64_t waitMs = 90);

    void incoming(rtc::message_vector& messages, const rtc::message_callback& send) override;

    // Test hook: hold back this percentage of incoming RTP packets and
    // deliver each one 30 ms after it is NACKed, like a retransmission (a real
    // retransmission cannot be tested this way: SRTP drops it as a replay).
    void setSimulatedLoss(int percent) { simulatedLoss_ = percent; }

private:
    static constexpr size_t kMaxPayload = 1536;  // a larger payload is treated as lost (accept())
    struct PacketSlot {
        int64_t seq = -1;
        uint32_t timestamp = 0;
        bool marker = false;
        uint16_t length = 0;  // 0: padding only (kept: it fills its sequence number)
        uint8_t payload[kMaxPayload];
    };
    static constexpr size_t kRingSize = 2048;
    static constexpr size_t kRingMask = kRingSize - 1;

    struct Missing {
        uint64_t sinceMs = 0, lastNackMs = 0;
        int nacks = 0;
    };

    bool hasPacket(int64_t s) const { return s >= 0 && ring_[static_cast<size_t>(s) & kRingMask].seq == s; }
    const PacketSlot* getPacket(int64_t s) const {
        if (s >= 0 && ring_[static_cast<size_t>(s) & kRingMask].seq == s)
            return &ring_[static_cast<size_t>(s) & kRingMask];
        return nullptr;
    }
    void putPacket(int64_t s, uint32_t ts, bool marker, const uint8_t* data, size_t len);
    void erasePacket(int64_t s);
    void clearRing();

    void accept(const uint8_t* d, size_t n, uint64_t now, const rtc::message_callback& send);
    int64_t unwrap(uint16_t seq);
    void noteGap(int64_t from, int64_t to, uint64_t now, const rtc::message_callback& send);
    void resendNacks(uint64_t now, const rtc::message_callback& send);
    void sendNack(const std::vector<int64_t>& seqs, const rtc::message_callback& send);
    void sendPli(const rtc::message_callback& send);
    void askKeyframe(const rtc::message_callback& send);
    // Emits every complete frame from nextSeq_ on; gives up on holes older
    // than the retransmission wait.
    void drain(uint64_t now, rtc::message_vector& out, const rtc::message_callback& send);
    bool resync(rtc::message_vector& out);
    bool assemble(int64_t first, int64_t last, std::vector<uint8_t>& annexB, bool& keyframe) const;

    std::function<void()> onKeyframeNeeded_;
    VideoReceiveStats& stats_;
    uint64_t waitMs_ = 300;
    std::unique_ptr<PacketSlot[]> ring_;
    size_t packetCount_ = 0;
    std::map<int64_t, Missing> missing_;
    int64_t highest_ = -1;    // highest extended sequence number seen
    int64_t nextSeq_ = -1;    // first packet of the next frame to emit; -1 = resync
    bool waitingKeyframe_ = true;
    uint32_t mediaSsrc_ = 0;
    uint64_t lastKeyframeRequestMs_ = 0;
    uint64_t oversized_ = 0;  // payloads too large for a slot, dropped
    int simulatedLoss_ = 0;
    uint32_t lossState_ = 12345;
    std::map<uint16_t, std::vector<uint8_t>> held_;                     // simulated loss
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> redeliver_;  // (due, packet)
};

}  // namespace xc::stream
