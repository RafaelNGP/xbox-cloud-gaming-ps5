// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "stream/rtcp_reporter.h"

#include <algorithm>
#include <chrono>
#include <vector>

namespace xc::stream {

namespace {

constexpr uint64_t kReportIntervalUs = 500000;
constexpr uint32_t kMinBitrate = 1500000;

uint64_t nowUs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t get32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
void put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s));
}

}  // namespace

RtcpReporter::RtcpReporter(uint32_t clockRate, uint32_t maxBitrate)
    : clockRate_(clockRate), maxBitrate_(maxBitrate) {
    estimate_ = maxBitrate;
}

void RtcpReporter::onRtp(const uint8_t* d, size_t n, uint64_t now) {
    uint32_t ssrc = get32(d + 8);
    uint16_t seq = get16(d + 2);
    if (!started_ || ssrc != mediaSsrc_) {  // RFC 3550 A.1, without probation
        started_ = true;
        mediaSsrc_ = ssrc;
        baseSeq_ = seq;
        maxSeq_ = seq;
        cycles_ = 0;
        received_ = expectedPrior_ = receivedPrior_ = 0;
        haveTransit_ = false;
        jitter_ = 0;
    } else {
        uint16_t delta = static_cast<uint16_t>(seq - maxSeq_);
        if (delta < 0x8000) {  // in order, maybe with a gap
            if (seq < maxSeq_) cycles_ += 0x10000;
            maxSeq_ = seq;
        }
        // Older packets (reordered, retransmitted) only count as received.
    }
    ++received_;
    bytesSinceReport_ += n;

    // Interarrival jitter (A.8), in RTP clock units. Like libwebrtc, only
    // the first packet of each frame counts: the packets of one frame share a
    // timestamp but are sent over several milliseconds.
    uint32_t ts = get32(d + 4);
    if (haveTransit_ && ts == lastTimestamp_) return;
    int64_t arrival = static_cast<int64_t>(now * clockRate_ / 1000000u);
    int64_t transit = arrival - static_cast<int64_t>(ts);
    if (haveTransit_) {
        int64_t dt = transit - lastTransit_;
        if (dt < 0) dt = -dt;
        if (dt < static_cast<int64_t>(clockRate_)) jitter_ += (static_cast<double>(dt) - jitter_) / 16.0;
    }
    lastTransit_ = transit;
    lastTimestamp_ = ts;
    haveTransit_ = true;
}

void RtcpReporter::onRtcp(const uint8_t* d, size_t n, uint64_t now) {
    // A compound packet: walk it for the sender report.
    for (size_t off = 0; off + 4 <= n;) {
        uint8_t pt = d[off + 1];
        size_t len = (static_cast<size_t>(get16(d + off + 2)) + 1) * 4;
        if (off + len > n) break;
        if (pt == 200 && len >= 28) {
            lastSr_ = (get32(d + off + 8) << 16) | (get32(d + off + 12) >> 16);
            lastSrArrivalUs_ = now;
        }
        off += len;
    }
}

void RtcpReporter::report(uint64_t now, const rtc::message_callback& send) {
    uint64_t intervalUs = lastReportUs_ ? now - lastReportUs_ : kReportIntervalUs;
    lastReportUs_ = now;

    uint32_t extMax = cycles_ + maxSeq_;
    uint32_t expected = extMax - baseSeq_ + 1;
    int64_t lost = static_cast<int64_t>(expected) - received_;
    lost = std::clamp<int64_t>(lost, -0x800000, 0x7FFFFF);
    uint32_t expectedInterval = expected - expectedPrior_;
    uint32_t receivedInterval = received_ - receivedPrior_;
    expectedPrior_ = expected;
    receivedPrior_ = received_;
    int64_t lostInterval = static_cast<int64_t>(expectedInterval) - receivedInterval;
    uint8_t fraction = 0;
    if (expectedInterval > 0 && lostInterval > 0)
        fraction = static_cast<uint8_t>(std::min<int64_t>(255, (lostInterval << 8) / expectedInterval));
    uint32_t dlsr = 0;
    if (lastSrArrivalUs_) dlsr = static_cast<uint32_t>((now - lastSrArrivalUs_) * 65536 / 1000000);

    std::vector<uint8_t> b;
    b.reserve(64);
    b.push_back(0x81);  // V=2, one report block
    b.push_back(201);   // RR
    put16(b, 7);
    put32(b, kReceiverSsrc);
    put32(b, mediaSsrc_);
    put32(b, (uint32_t(fraction) << 24) | (static_cast<uint32_t>(lost) & 0xFFFFFF));
    put32(b, extMax);
    put32(b, static_cast<uint32_t>(jitter_));
    put32(b, lastSr_);
    put32(b, lastSr_ ? dlsr : 0);

    if (maxBitrate_) {
        uint32_t rate = intervalUs ? static_cast<uint32_t>(bytesSinceReport_ * 8 * 1000000 / intervalUs) : 0;
        receiveRate_ = rate;
        // Like a receive-side estimator, much simplified: back off below
        // what arrives when the network drops packets, otherwise climb back
        // to the ceiling.
        double loss = fraction / 256.0;
        uint32_t est = estimate_;
        if (loss > 0.10)
            est = std::max<uint32_t>(kMinBitrate, static_cast<uint32_t>(rate * (1.0 - 0.5 * loss)));
        else if (loss < 0.02)
            est = std::min<uint32_t>(maxBitrate_, std::max<uint32_t>(est, kMinBitrate) + maxBitrate_ / 20);
        estimate_ = est;

        uint8_t exp = 0;
        uint32_t mantissa = est;
        while (mantissa >= (1u << 18)) {
            mantissa >>= 1;
            ++exp;
        }
        b.push_back(0x8F);  // V=2, FMT=15 (application layer feedback)
        b.push_back(206);   // PSFB
        put16(b, 5);
        put32(b, kReceiverSsrc);
        put32(b, 0);
        b.insert(b.end(), {'R', 'E', 'M', 'B'});
        put32(b, (1u << 24) | (uint32_t(exp) << 18) | mantissa);
        put32(b, mediaSsrc_);
    }
    bytesSinceReport_ = 0;

    auto* p = reinterpret_cast<const std::byte*>(b.data());
    send(rtc::make_message(p, p + b.size(), rtc::Message::Control));
}

void RtcpReporter::incoming(rtc::message_vector& messages, const rtc::message_callback& send) {
    uint64_t now = nowUs();
    rtc::message_vector rtp;
    for (auto& message : messages) {
        const auto* d = reinterpret_cast<const uint8_t*>(message->data());
        size_t n = message->size();
        if (message->type == rtc::Message::Control) {
            onRtcp(d, n, now);
        } else if (message->type == rtc::Message::Binary && n >= 12 && (d[0] >> 6) == 2) {
            onRtp(d, n, now);
            rtp.push_back(std::move(message));
        }
    }
    messages.swap(rtp);
    if (started_ && now - lastReportUs_ >= kReportIntervalUs) report(now, send);
}

}  // namespace xc::stream
