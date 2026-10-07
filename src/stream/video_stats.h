// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Counters of the video RTP receiver (see h264_receiver.h).
#pragma once

#include <atomic>
#include <cstdint>

namespace xc::stream {

struct VideoReceiveStats {
    std::atomic<uint64_t> packets{0};
    std::atomic<uint64_t> lost{0};        // sequence numbers that went missing
    std::atomic<uint64_t> recovered{0};   // ...and later arrived (retransmitted)
    std::atomic<uint64_t> nacks{0};       // NACK packets sent
    std::atomic<uint64_t> framesOut{0};
    std::atomic<uint64_t> framesDropped{0};  // incomplete, or waiting for a key frame
    std::atomic<uint64_t> keyframeRequests{0};
    std::atomic<uint32_t> receiveRate{0};  // bits/s, last report interval
    std::atomic<uint32_t> estimate{0};     // bits/s offered to the server (REMB)
};

}  // namespace xc::stream
