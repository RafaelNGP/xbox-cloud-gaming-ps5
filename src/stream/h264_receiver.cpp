// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "stream/h264_receiver.h"

#include "stream/rtcp_reporter.h"

#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <cstring>

namespace xc::stream {

namespace {

// How long a hole may wait for its retransmission (a couple of RTTs), how
// often a NACK is repeated, and how many times.
constexpr uint64_t kRenackMs = 50;
constexpr int kMaxNacks = 5;
constexpr size_t kMaxBuffered = 1800;      // packets (within 2048-slot ring)

constexpr uint8_t kStartCode[] = {0, 0, 0, 1};

// NAL types that begin a decodable access unit after loss.
bool isKeyNal(uint8_t type) { return type == 5 || type == 7; }

void put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s));
}

const std::byte* asBytes(const std::vector<uint8_t>& v) { return reinterpret_cast<const std::byte*>(v.data()); }

}  // namespace

H264Receiver::H264Receiver(std::function<void()> onKeyframeNeeded, VideoReceiveStats& stats, uint64_t waitMs)
    : onKeyframeNeeded_(std::move(onKeyframeNeeded)),
      stats_(stats),
      waitMs_(waitMs),
      ring_(std::make_unique<PacketSlot[]>(kRingSize)) {
    clearRing();
}

void H264Receiver::putPacket(int64_t s, uint32_t ts, bool marker, const uint8_t* data, size_t len) {
    size_t idx = static_cast<size_t>(s) & kRingMask;
    if (ring_[idx].seq == -1) {
        ++packetCount_;
    }
    ring_[idx].seq = s;
    ring_[idx].timestamp = ts;
    ring_[idx].marker = marker;
    ring_[idx].length = static_cast<uint16_t>(std::min<size_t>(len, 1536));
    if (ring_[idx].length > 0 && data) {
        std::memcpy(ring_[idx].payload, data, ring_[idx].length);
    }
}

void H264Receiver::erasePacket(int64_t s) {
    size_t idx = static_cast<size_t>(s) & kRingMask;
    if (ring_[idx].seq == s) {
        ring_[idx].seq = -1;
        ring_[idx].length = 0;
        if (packetCount_ > 0) --packetCount_;
    }
}

void H264Receiver::clearRing() {
    for (size_t i = 0; i < kRingSize; ++i) {
        ring_[i].seq = -1;
        ring_[i].length = 0;
    }
    packetCount_ = 0;
}

int64_t H264Receiver::unwrap(uint16_t seq) {
    if (highest_ < 0) return seq;
    int64_t base = highest_ & ~int64_t(0xFFFF);
    int64_t candidate = base | seq;
    // Pick the value closest to the highest seen (handles both wrap directions).
    if (candidate - highest_ > 0x8000) candidate -= 0x10000;
    else if (highest_ - candidate > 0x8000) candidate += 0x10000;
    return candidate;
}

void H264Receiver::sendNack(const std::vector<int64_t>& seqs, const rtc::message_callback& send) {
    if (seqs.empty() || !mediaSsrc_) return;
    // Generic NACK (RFC 4585 6.2.1): FCI = PID + bitmask of the next 16.
    std::vector<std::pair<uint16_t, uint16_t>> fci;
    for (size_t i = 0; i < seqs.size();) {
        int64_t pid = seqs[i];
        uint16_t blp = 0;
        size_t j = i + 1;
        while (j < seqs.size() && seqs[j] - pid <= 16) {
            blp |= static_cast<uint16_t>(1u << (seqs[j] - pid - 1));
            ++j;
        }
        fci.emplace_back(static_cast<uint16_t>(pid), blp);
        i = j;
    }
    std::vector<uint8_t> b;
    b.push_back(0x81);  // V=2, FMT=1
    b.push_back(205);   // RTPFB
    put16(b, static_cast<uint16_t>(2 + fci.size()));
    put32(b, kReceiverSsrc);
    put32(b, mediaSsrc_);
    for (auto [pid, blp] : fci) {
        put16(b, pid);
        put16(b, blp);
    }
    send(rtc::make_message(asBytes(b), asBytes(b) + b.size(), rtc::Message::Control));
    ++stats_.nacks;
    if (simulatedLoss_ > 0)
        for (int64_t s : seqs) {
            auto h = held_.find(static_cast<uint16_t>(s));
            if (h == held_.end()) continue;
            redeliver_.emplace_back(platform::nowMs() + 30, std::move(h->second));
            held_.erase(h);
        }
}

void H264Receiver::sendPli(const rtc::message_callback& send) {
    if (!mediaSsrc_) return;
    std::vector<uint8_t> b;
    b.push_back(0x81);  // V=2, FMT=1
    b.push_back(206);   // PSFB
    put16(b, 2);
    put32(b, kReceiverSsrc);
    put32(b, mediaSsrc_);
    send(rtc::make_message(asBytes(b), asBytes(b) + b.size(), rtc::Message::Control));
}

void H264Receiver::askKeyframe(const rtc::message_callback& send) {
    waitingKeyframe_ = true;
    uint64_t now = platform::nowMs();
    if (now - lastKeyframeRequestMs_ < 300) return;  // one request per burst
    lastKeyframeRequestMs_ = now;
    ++stats_.keyframeRequests;
    sendPli(send);
    if (onKeyframeNeeded_) onKeyframeNeeded_();
}

void H264Receiver::noteGap(int64_t from, int64_t to, uint64_t now, const rtc::message_callback& send) {
    if (to - from > 512) {  // a huge jump: not worth NACKing, resynchronise
        stats_.lost += static_cast<uint64_t>(to - from);
        return;
    }
    std::vector<int64_t> seqs;
    for (int64_t s = from; s < to; ++s) {
        missing_[s] = Missing{now, now, 1};
        seqs.push_back(s);
    }
    stats_.lost += seqs.size();
    sendNack(seqs, send);
}

void H264Receiver::resendNacks(uint64_t now, const rtc::message_callback& send) {
    std::vector<int64_t> seqs;
    for (auto& [s, m] : missing_)
        if (m.nacks < kMaxNacks && now - m.lastNackMs >= kRenackMs) {
            m.lastNackMs = now;
            ++m.nacks;
            seqs.push_back(s);
        }
    sendNack(seqs, send);
}

bool H264Receiver::assemble(int64_t first, int64_t last, std::vector<uint8_t>& out, bool& keyframe) const {
    out.clear();
    keyframe = false;
    for (int64_t s = first; s <= last; ++s) {
        const auto* slot = getPacket(s);
        if (!slot || slot->length == 0) return false;
        const uint8_t* p = slot->payload;
        size_t pLen = slot->length;
        uint8_t type = p[0] & 0x1F;
        if (type >= 1 && type <= 23) {  // single NAL unit
            out.insert(out.end(), kStartCode, kStartCode + 4);
            out.insert(out.end(), p, p + pLen);
            keyframe |= isKeyNal(type);
        } else if (type == 24) {  // STAP-A
            size_t i = 1;
            while (i + 2 <= pLen) {
                size_t n = (static_cast<size_t>(p[i]) << 8) | p[i + 1];
                i += 2;
                if (n == 0 || i + n > pLen) break;
                out.insert(out.end(), kStartCode, kStartCode + 4);
                out.insert(out.end(), p + i, p + i + n);
                keyframe |= isKeyNal(p[i] & 0x1F);
                i += n;
            }
        } else if (type == 28 && pLen > 2) {  // FU-A
            bool start = (p[1] & 0x80) != 0;
            if (start) {
                out.insert(out.end(), kStartCode, kStartCode + 4);
                out.push_back(static_cast<uint8_t>((p[0] & 0xE0) | (p[1] & 0x1F)));
                keyframe |= isKeyNal(p[1] & 0x1F);
            }
            out.insert(out.end(), p + 2, p + pLen);
        }
    }
    return !out.empty();
}

bool H264Receiver::resync(rtc::message_vector& out) {
    // Find a frame boundary (the packet before is present and is a marker, or
    // carries another timestamp) followed by a complete key frame.
    int64_t startSeq = std::max<int64_t>(0, highest_ - static_cast<int64_t>(kRingSize - 1));
    for (int64_t s = startSeq; s <= highest_; ++s) {
        const auto* cur = getPacket(s);
        if (!cur) continue;
        const auto* prev = getPacket(s - 1);
        if (!prev) continue;
        if (!prev->marker && prev->timestamp == cur->timestamp) continue;

        // Walk to the marker of this frame.
        int64_t end = s;
        bool complete = false;
        while (end <= highest_) {
            const auto* p = getPacket(end);
            if (!p) break;
            if (p->marker) {
                complete = true;
                break;
            }
            ++end;
        }
        if (!complete) continue;

        std::vector<uint8_t> frame;
        bool key = false;
        if (assemble(s, end, frame, key) && key) {
            nextSeq_ = s;
            for (int64_t e = startSeq; e < nextSeq_; ++e) erasePacket(e);
            for (auto e = missing_.begin(); e != missing_.end() && e->first < nextSeq_;) {
                e = missing_.erase(e);
            }
            (void)out;
            return true;
        }
    }
    return false;
}

void H264Receiver::drain(uint64_t now, rtc::message_vector& out, const rtc::message_callback& send) {
    for (;;) {
        if (nextSeq_ < 0) {
            if (!resync(out)) return;
        }
        // Collect the frame starting at nextSeq_.
        int64_t s = nextSeq_;
        const auto* it = getPacket(s);
        int64_t hole = -1;
        if (!it) {
            hole = s;
        } else {
            while (true) {
                const auto* p = getPacket(s);
                if (!p) {
                    hole = s;
                    break;
                }
                if (p->marker) break;
                ++s;
            }
        }
        if (hole >= 0) {
            if (hole > highest_) return;  // just not here yet
            auto m = missing_.find(hole);
            uint64_t since = m != missing_.end() ? m->second.sinceMs : now;
            if (m == missing_.end()) missing_[hole] = Missing{now, now, 0};
            if (now - since < waitMs_) return;  // give the retransmission time
            // Lost for good: drop what we have of this frame and resync on
            // the next key frame.
            ++stats_.framesDropped;
            for (int64_t e = nextSeq_; e <= hole; ++e) erasePacket(e);
            for (auto e = missing_.begin(); e != missing_.end() && e->first <= hole;) e = missing_.erase(e);
            nextSeq_ = -1;
            askKeyframe(send);
            continue;
        }
        std::vector<uint8_t> frame;
        bool key = false;
        bool ok = assemble(nextSeq_, s, frame, key);
        const auto* markerPkt = getPacket(s);
        uint32_t ts = markerPkt ? markerPkt->timestamp : 0;
        for (int64_t e = nextSeq_; e <= s; ++e) erasePacket(e);
        for (auto e = missing_.begin(); e != missing_.end() && e->first <= s;) e = missing_.erase(e);
        nextSeq_ = s + 1;
        if (!ok) continue;
        if (waitingKeyframe_ && !key) {
            ++stats_.framesDropped;  // would decode against lost references
            continue;
        }
        if (key) waitingKeyframe_ = false;
        ++stats_.framesOut;
        out.push_back(rtc::make_message(asBytes(frame), asBytes(frame) + frame.size(), std::make_shared<rtc::FrameInfo>(ts)));
    }
}

void H264Receiver::accept(const uint8_t* d, size_t n, uint64_t now, const rtc::message_callback& send) {
    if (n < 12 || (d[0] >> 6) != 2) return;
    uint16_t rawSeq = static_cast<uint16_t>((d[2] << 8) | d[3]);
    if (simulatedLoss_ > 0) {
        lossState_ = lossState_ * 1103515245u + 12345u;
        if ((lossState_ >> 16) % 100 < static_cast<uint32_t>(simulatedLoss_) && !held_.count(rawSeq)) {
            held_[rawSeq].assign(d, d + n);
            if (held_.size() > 2000) held_.erase(held_.begin());
            return;
        }
    }
    size_t off = 12 + 4u * (d[0] & 0x0F);
    if (d[0] & 0x10) {  // header extension
        if (off + 4 > n) return;
        off += 4 + 4u * ((static_cast<size_t>(d[off + 2]) << 8) | d[off + 3]);
    }
    size_t end = n;
    if (d[0] & 0x20) {  // padding
        if (d[n - 1] > n) return;
        end -= d[n - 1];
    }
    if (off > end) return;
    mediaSsrc_ = (uint32_t(d[8]) << 24) | (uint32_t(d[9]) << 16) | (uint32_t(d[10]) << 8) | d[11];
    ++stats_.packets;

    int64_t seq = unwrap(rawSeq);
    if (nextSeq_ >= 0 && seq < nextSeq_) return;  // too late or duplicate
    if (hasPacket(seq)) return;
    if (missing_.erase(seq)) ++stats_.recovered;

    uint32_t ts = (uint32_t(d[4]) << 24) | (uint32_t(d[5]) << 16) | (uint32_t(d[6]) << 8) | d[7];
    bool marker = (d[1] & 0x80) != 0;
    putPacket(seq, ts, marker, d + off, end - off);

    if (highest_ >= 0 && seq > highest_ + 1) noteGap(highest_ + 1, seq, now, send);
    if (seq > highest_) highest_ = seq;
}

void H264Receiver::incoming(rtc::message_vector& messages, const rtc::message_callback& send) {
    rtc::message_vector out;
    uint64_t now = platform::nowMs();
    for (auto it = redeliver_.begin(); it != redeliver_.end();) {
        if (it->first > now) {
            ++it;
            continue;
        }
        std::vector<uint8_t> packet = std::move(it->second);
        it = redeliver_.erase(it);
        accept(packet.data(), packet.size(), now, send);
    }
    for (auto& message : messages) {
        if (message->type == rtc::Message::Control) continue;  // RTCP: handled down the chain
        accept(reinterpret_cast<const uint8_t*>(message->data()), message->size(), now, send);
    }
    resendNacks(now, send);
    drain(now, out, send);
    // Still without a key frame: ask again (askKeyframe allows one every
    // 300 ms), as the request or the key frame itself may have been lost;
    // otherwise the picture stays frozen until the buffer overflows (seconds).
    if (nextSeq_ < 0 && packetCount_ > 0) askKeyframe(send);
    if (packetCount_ > kMaxBuffered || (nextSeq_ >= 0 && highest_ - nextSeq_ > static_cast<int64_t>(kMaxBuffered))) {  // stuck: start over
        clearRing();
        missing_.clear();
        nextSeq_ = -1;
        askKeyframe(send);
    }
    messages.swap(out);
}

}  // namespace xc::stream
