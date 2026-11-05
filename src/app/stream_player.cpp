// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/stream_player.h"

#include "display/display.h"
#include "input/controller.h"
#include "media/audio_out.h"
#include "media/audio_queue.h"
#include "media/audio_in.h"
#include "media/audio_encoder.h"
#include "media/decoder.h"
#include "media/hw_decoder.h"
#include "platform/platform.h"
#include "stream/stream_session.h"
#include "ui/strings.h"
#include "util/log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace xc::app {

namespace {
// Beyond this, start over from a key frame. Generous: the jitter buffer
// hands over several frames at once after waiting for a retransmission, and
// decoding without drawing catches up with those in a few milliseconds.
constexpr size_t kMaxQueuedFrames = 90;

// True when an Annex B access unit holds an IDR slice (NAL type 5).
bool hasIdrSlice(const std::vector<uint8_t>& au) {
    for (size_t i = 0; i + 3 < au.size(); ++i)
        if (au[i] == 0 && au[i + 1] == 0 && au[i + 2] == 1) {
            if ((au[i + 3] & 0x1F) == 5) return true;
            i += 2;
        }
    return false;
}
}

struct StreamPlayer::Impl {
    xcloud::GssvClient& gssv;
    // The WebRTC session: replaced when the connection drops and the stream
    // reconnects (worker thread), read by the video, input and main threads.
    mutable std::mutex sessionMutex;
    std::shared_ptr<stream::StreamSession> session;
    std::shared_ptr<stream::StreamSession> current() const {
        std::lock_guard<std::mutex> lock(sessionMutex);
        return session;
    }
    stream::StreamCallbacks callbacks;
    stream::StreamOptions options;
    // Reconnecting after a lost connection, as the xbox.com client does: up
    // to 20 attempts a second apart while the cloud session is still there.
    std::atomic<bool> reconnecting{false};
    std::atomic<bool> titleFocused{true};
    int reconnectAttempts = 0;
    std::atomic<uint32_t> reconnects{0};
    uint64_t lastRtpPackets = 0, lastAudioPackets = 0;
    int silentTicks = 0;

    std::mutex mutex;
    std::condition_variable cv;
    // An access unit, with what the server wants to hear about it.
    struct VideoFrame {
        std::vector<uint8_t> data;
        uint32_t rtpTimestamp = 0;
        double arrivalMs = 0;  // StreamSession::clockMs()
        uint64_t arrivalUs = 0;  // platform::nowUs(), for the display latency
        uint64_t firstPacketUs = 0;  // its first packet came (before the retransmission wait), 0 unknown
    };
    std::deque<VideoFrame> frames;
    std::atomic<bool> running{false};
    std::string endReason;
    platform::Thread videoThread, audioThread;
    struct QueuedAudioPacket {
        std::vector<uint8_t> data;
        uint32_t rtpTimestamp = 0;
    };
    std::mutex audioMutex_;
    std::deque<QueuedAudioPacket> audioPackets_;
    std::condition_variable audioCv;

    media::AudioDecoder audioDecoder;
    std::vector<float> pcm;
    std::atomic<uint64_t> videoFrames{0}, decodedFrames{0}, droppedFrames{0}, audioPackets{0};
    std::atomic<uint64_t> decodeFailures{0}, queueResets{0}, keyframeRequests{0};
    std::atomic<bool> keyframeWanted{false};
    std::atomic<uint32_t> pictureSize{0};  // width << 16 | height
    std::atomic<uint64_t> vibrations{0}, lateFrames{0};
    std::atomic<uint64_t> triggerVibrations{0};
    std::atomic<uint64_t> displayUs{0}, displayCount{0}, displayMaxUs{0};
    std::atomic<uint64_t> glassUs{0}, glassCount{0}, glassMaxUs{0};  // first packet to the screen
    std::atomic<uint64_t> decodeUs{0}, decodeCalls{0}, decodeMaxUs{0}, drawUs{0}, drawCalls{0}, drawMaxUs{0};
    std::atomic<uint64_t> qpSumX100{0}, qpCount{0};  // pictures' quantiser since the last stats()
    int decodeThreads = 1;
    int simulatedLoss = 0;
    int maxBitrateMbps = 0;
    bool hwProbe = false;
    bool preferHwDecode = true;
    std::atomic<bool> isHwDecode{false};

    static void atomicMax(std::atomic<uint64_t>& a, uint64_t v) {
        uint64_t cur = a;
        while (v > cur && !a.compare_exchange_weak(cur, v)) {
        }
    }
    bool loggedFirstDecode = false;
    FILE* dumpFile = nullptr;
    uint64_t dumpUntilMs = 0;
    std::mutex snapshotMutex;
    std::string snapshotPath;
    std::string screenPath;  // video thread only
    uint64_t lastKeyframeRequestMs = 0;
    // The game's text requests not yet taken, and those it withdrew (mutex).
    std::deque<stream::TextInputRequest> textRequests;
    std::set<std::string> textWithdrawn;

    mutable std::mutex frametimesMutex;
    static constexpr size_t kFrametimeRing = 60;
    std::array<float, kFrametimeRing> frametimesRing{};
    size_t frametimesCount = 0;
    size_t frametimesHead = 0;

    media::AudioIn audioIn;
    media::AudioEncoder audioEncoder;
    std::atomic<bool> highRefreshOutput{false};  // setHighRefreshOutput()
    std::atomic<bool> legacy{false};             // setLegacyTuning()
    std::atomic<int> decoderSwitch{-1};          // switchDecoder(): 0 software, 1 hardware
    std::atomic<bool> micEnabled{true};
    std::atomic<bool> micMuted{false};
    std::atomic<float> micGain{1.0f};
    std::atomic<uint32_t> audioSendTimestamp{0};
    // The microphone's packets, every 10 s (issue #187, a mic heard late):
    // how many went out, the longest encode + send, and how far the RTP
    // clock (48 kHz, from the samples sent) is from the real one since the
    // first packet: a receiver's jitter buffer grows with that gap.
    struct MicStats {
        uint64_t firstUs = 0, windowUs = 0, maxWorkUs = 0;
        uint32_t firstTs = 0, packets = 0, skips = 0;
        void skipped() { ++skips; }
        void sent(uint64_t startUs, uint64_t endUs, uint32_t tsAfter, uint32_t frame) {
            if (!firstUs) firstUs = windowUs = startUs, firstTs = tsAfter - frame;
            ++packets;
            maxWorkUs = std::max(maxWorkUs, endUs - startUs);
            if (endUs - windowUs < 10000000) return;
            const double realMs = static_cast<double>(endUs - firstUs) / 1000.0;
            const double rtpMs = static_cast<double>(tsAfter - firstTs) / 48.0;
            XC_LOGI("mic: %u packets in %.1f s (%u without one), encode + send up to %.2f ms, RTP clock %+.0f ms from real time",
                    packets, static_cast<double>(endUs - windowUs) / 1e6, skips, static_cast<double>(maxWorkUs) / 1000.0,
                    rtpMs - realMs);
            windowUs = endUs, packets = skips = 0, maxWorkUs = 0;
        }
    } micStats;
    bool encoderInitialized = false;
    platform::Thread inputThread;
    platform::Thread snapshotThread;
    platform::Thread screenThread;
    struct LatestPicture {
        media::Picture pic;
        std::vector<uint8_t> yPlane;
        std::vector<uint8_t> uPlane;
        std::vector<uint8_t> vPlane;
        stream::FrameMetadata meta;
        bool known = false;
        bool isHw = false;
        uint64_t arrivalUs = 0;
        uint64_t seq = 0;
    };
    std::mutex presentMutex;
    std::condition_variable presentCv;
    LatestPicture latestPic;
    uint64_t consumedPicSeq = 0;
    bool presentStop = false;
    std::mutex inputMutex;
    std::condition_variable inputCv;
    std::array<stream::GamepadFrame, input::kMaxPads> pendingPads{};
    std::array<bool, input::kMaxPads> pendingPadDirty{};
    std::vector<stream::KeyFrame> pendingKeys;
    std::vector<stream::MouseFrame> pendingMouse;

    // Opening and closing the microphone happen on several threads (start,
    // reconnect, the end of the stream): one at a time.
    std::mutex micMutex;

    void closeMic() {
        std::lock_guard<std::mutex> lock(micMutex);
        audioIn.close();
        input::setEmbeddedMicActive(false);
    }

    void updateMicState() {
        std::lock_guard<std::mutex> lock(micMutex);
        if (!running) return;
        if (!micEnabled) {
            if (audioIn.isRecording()) {
                audioIn.close();
                input::setEmbeddedMicActive(false);
            }
            return;
        }
        if (!audioIn.isRecording()) {
            if (!encoderInitialized) {
                // 10 ms packets: the voice waits half as long to go out
                // (20 ms before 2026-11-04, the legacy A/B baseline).
                encoderInitialized = audioEncoder.init(24000, legacy ? 20 : 10);
                if (!encoderInitialized) {
                    XC_LOGW("audio encoder: Opus init failed, mic input & meter will still operate");
                }
            }
            audioIn.setMuted(micMuted.load());
            audioIn.setGain(micGain.load());
            audioIn.setFrameSize(audioEncoder.frameSamples());
            audioIn.open([this](const int16_t* pcmFrames, size_t samples) {
                if (!running) return;
                if (!encoderInitialized) return;
                auto s = current();
                if (!s || !s->isOpen()) return;
                const uint64_t t0 = platform::nowUs();
                std::vector<uint8_t> opus;
                if (audioEncoder.encode(pcmFrames, samples, opus)) {
                    uint32_t ts = audioSendTimestamp.fetch_add(static_cast<uint32_t>(samples));
                    s->sendAudio(opus.data(), opus.size(), ts);
                    micStats.sent(t0, platform::nowUs(), ts + static_cast<uint32_t>(samples), static_cast<uint32_t>(samples));
                } else {
                    micStats.skipped();
                }
            });
            input::setEmbeddedMicActive(true);
        } else {
            audioIn.setMuted(micMuted.load());
            audioIn.setGain(micGain.load());
        }
    }

    explicit Impl(xcloud::GssvClient& g) : gssv(g) {}

    void end(const std::string& reason) {
        closeMic();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (endReason.empty()) endReason = reason;
        }
        running = false;
        cv.notify_all();
        {
            std::lock_guard<std::mutex> lock(presentMutex);
            presentStop = true;
        }
        presentCv.notify_all();
        {
            std::lock_guard<std::mutex> lock(audioMutex_);
        }
        audioCv.notify_all();
        {
            std::lock_guard<std::mutex> lock(inputMutex);
        }
        inputCv.notify_all();
    }

    void inputLoop() {
        platform::setThreadAffinity(platform::ThreadRole::Network, "xcloud_in");
        while (running) {
            std::array<stream::GamepadFrame, input::kMaxPads> pads;
            std::array<bool, input::kMaxPads> dirty{};
            std::vector<stream::KeyFrame> keys;
            std::vector<stream::MouseFrame> mouse;
            {
                std::unique_lock<std::mutex> lock(inputMutex);
                inputCv.wait(lock, [&] {
                    if (!running) return true;
                    if (!pendingKeys.empty() || !pendingMouse.empty()) return true;
                    for (bool d : pendingPadDirty) if (d) return true;
                    return false;
                });
                if (!running) break;
                pads = pendingPads;
                dirty = pendingPadDirty;
                pendingPadDirty.fill(false);
                keys = std::move(pendingKeys);
                pendingKeys.clear();
                mouse = std::move(pendingMouse);
                pendingMouse.clear();
            }
            auto s = current();
            if (s && running) {
                for (int i = 0; i < input::kMaxPads; ++i) {
                    if (dirty[i]) s->sendGamepad(pads[i]);
                }
                if (!keys.empty()) s->sendKeys(keys);
                for (const auto& m : mouse) s->sendMouse(m);
            }
        }
    }

    // The sound's delay: what waits in the audio queue. Once a second, if
    // even its lowest point over the last 5 s stayed above the target, up to
    // 20 ms is cut out (crossfaded); a pull that found it dry raises the
    // target 10 ms (up to 100), a minute without one lowers it 5 (down to
    // 40). A network burst would otherwise leave the sound that much later
    // for good (it sat at ~100 ms).
    struct AudioLatency {
        uint64_t nextMs = 0, calmSinceMs = 0;
        int targetMs = 40;
        bool warm = false;  // past the first 3 s, while the queue fills
        size_t lows[5] = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};  // each second's lowest
        size_t lowNext = 0;
        void reset() {
            nextMs = 0, targetMs = 40, warm = false, lowNext = 0;
            for (size_t& l : lows) l = SIZE_MAX;
            media::audioQueue().setRefill(0);
        }
        void update(uint64_t now) {
            auto& q = media::audioQueue();
            if (!nextMs) nextMs = now + 3000;
            q.setRefill(static_cast<size_t>(targetMs) * 48);  // what a dry queue fills back to
            if (now < nextMs) return;
            nextMs = now + 1000;
            if (!warm) {
                warm = true, calmSinceMs = now;
                q.takeUnderruns(), q.takeLowWater();  // the start's own
                return;
            }
            const uint64_t dry = q.takeUnderruns();
            lows[lowNext++ % 5] = q.takeLowWater();
            if (dry) {
                if (targetMs < 100) {
                    targetMs = std::min(targetMs + 10, 100);
                    XC_LOGI("audio: ran dry (%llu grains), target %d ms", static_cast<unsigned long long>(dry), targetMs);
                }
                calmSinceMs = now;
                return;
            }
            if (now - calmSinceMs >= 60000 && targetMs > 40) {
                targetMs -= 5, calmSinceMs = now;
                XC_LOGI("audio: steady for a minute, target %d ms", targetMs);
            }
            if (lowNext < 5) return;  // 5 s seen first
            size_t low = SIZE_MAX;
            for (size_t l : lows) low = std::min(low, l);
            const size_t target = static_cast<size_t>(targetMs) * 48;
            if (low != SIZE_MAX && low > target + 480) {
                const size_t cut = std::min<size_t>(low - target, 960);
                if (q.cutAhead(cut))
                    for (size_t& l : lows) l = l == SIZE_MAX ? l : l - std::min(l, cut);  // the queue is that much shorter
            }
        }
    } audioLatency;

    void audioLoop() {
        platform::setThreadAffinity(platform::ThreadRole::Audio, "xcloud_adec");
        platform::setThreadRealtimePriority();
        audioDecoder.resetPlc();
        audioLatency.reset();
        QueuedAudioPacket packet;
        bool first = true;
        uint32_t lastRtpTimestamp = 0;
        uint64_t concealedPackets = 0;
        constexpr uint32_t kOpusFrameSamples = 960;  // 20 ms at 48 kHz
        while (running) {
            size_t remainingPackets = 0;
            {
                std::unique_lock<std::mutex> lock(audioMutex_);
                audioCv.wait(lock, [&] { return !running || !audioPackets_.empty(); });
                if (!running) break;

                // When an RTP timestamp gap appears, if the audio queue has comfortable headroom
                // (>= 40 ms), wait briefly (up to 12 ms) to allow network jitter or reordering to
                // deliver the missing packet before assuming it lost and synthesizing PLC.
                if (lastRtpTimestamp != 0 && !audioPackets_.empty()) {
                    int32_t frontDiff = static_cast<int32_t>(audioPackets_.front().rtpTimestamp - lastRtpTimestamp);
                    if (frontDiff > static_cast<int32_t>(kOpusFrameSamples) &&
                        frontDiff <= static_cast<int32_t>(kOpusFrameSamples * 5)) {
                        size_t queued = media::audioQueue().queuedFrames();
                        if (queued >= kOpusFrameSamples * 2) {
                            audioCv.wait_for(lock, std::chrono::milliseconds(12), [&] {
                                if (!running || audioPackets_.empty()) return true;
                                int32_t d = static_cast<int32_t>(audioPackets_.front().rtpTimestamp - lastRtpTimestamp);
                                return d <= static_cast<int32_t>(kOpusFrameSamples);
                            });
                            if (!running) break;
                        }
                    }
                }

                packet = std::move(audioPackets_.front());
                audioPackets_.pop_front();
                remainingPackets = audioPackets_.size();
            }
            if (lastRtpTimestamp != 0) {
                int32_t diff = static_cast<int32_t>(packet.rtpTimestamp - lastRtpTimestamp);
                if (diff <= 0) {
                    // Late or duplicate packet: skip to preserve timeline and prevent timestamp drift
                    continue;
                }
                // Detect packet loss from RTP timestamp gap.
                // Only synthesize PLC if we do not already have pending real packets in the backlog
                // and the hardware output queue is running low (< 60 ms).
                if (remainingPackets == 0 && diff > static_cast<int32_t>(kOpusFrameSamples) &&
                    diff <= static_cast<int32_t>(kOpusFrameSamples * 10)) {
                    uint32_t lostCount = (static_cast<uint32_t>(diff) + kOpusFrameSamples / 2) / kOpusFrameSamples - 1;
                    size_t queued = media::audioQueue().queuedFrames();
                    constexpr size_t kTargetHeadroom = kOpusFrameSamples * 3;  // 60 ms
                    size_t maxPlcCanFit = (kTargetHeadroom > queued + kOpusFrameSamples)
                                              ? (kTargetHeadroom - queued - kOpusFrameSamples) / kOpusFrameSamples
                                              : 0;
                    uint32_t plcToPush = std::min<uint32_t>({lostCount, static_cast<uint32_t>(maxPlcCanFit), 2u});
                    for (uint32_t i = 0; i < plcToPush; ++i) {
                        pcm.clear();
                        audioDecoder.conceal(pcm, kOpusFrameSamples);
                        if (!pcm.empty()) media::audioPush(pcm.data(), pcm.size() / 2);
                        ++concealedPackets;
                    }
                    if (plcToPush > 0 && (concealedPackets <= 5 || concealedPackets % 50 == 0)) {
                        XC_LOGI("audio plc: concealed %u/%u lost packet(s) (%llu total concealed)",
                                plcToPush, lostCount, static_cast<unsigned long long>(concealedPackets));
                    }
                }
            }
            lastRtpTimestamp = packet.rtpTimestamp;

            pcm.clear();
            bool decoded = audioDecoder.decode(packet.data.data(), packet.data.size(), pcm);
            if (first && decoded) {
                first = false;
                XC_LOGI("first audio packet decoded: %zu samples", pcm.size() / 2);
            }
            if (decoded) {
                if (legacy) {
                    // Before 2026-11-04 (A/B baseline): a packet dropped whole
                    // past 100 ms queued, which left the queue at ~100 ms.
                    if (media::audioQueue().queuedFrames() >= kOpusFrameSamples * 5) continue;
                    media::audioPush(pcm.data(), pcm.size() / 2);
                } else {
                    media::audioPush(pcm.data(), pcm.size() / 2);
                    audioLatency.update(platform::nowMs());
                }
            }
        }
    }

    void asyncPresentLoop() {
        platform::setThreadAffinity(platform::ThreadRole::Worker, "ps4_present");
        constexpr uint64_t kShownRing = 8;
        std::pair<uint64_t, uint64_t> inScreenQueue[kShownRing] = {};  // present id, arrival us
        uint64_t lastShownId = 0;
        uint64_t lastFramePresentUs = 0;
        bool loggedFirstPresent = false;
        LatestPicture cur;

        while (running) {
            {
                std::unique_lock<std::mutex> lock(presentMutex);
                presentCv.wait(lock, [&] { return !running || presentStop || latestPic.seq > consumedPicSeq; });
                if (!running || presentStop) break;
                consumedPicSeq = latestPic.seq;
                cur.pic = latestPic.pic;
                cur.meta = latestPic.meta;
                cur.known = latestPic.known;
                cur.isHw = latestPic.isHw;
                cur.arrivalUs = latestPic.arrivalUs;
                cur.seq = latestPic.seq;
                if (!cur.isHw) {
                    cur.yPlane.swap(latestPic.yPlane);
                    cur.uPlane.swap(latestPic.uPlane);
                    cur.vPlane.swap(latestPic.vPlane);
                    cur.pic.y = cur.yPlane.data();
                    cur.pic.u = cur.uPlane.data();
                    cur.pic.v = cur.pic.format == media::Picture::Format::Nv12 ? nullptr : cur.vPlane.data();
                }
            }

            bool screenshot = saveSnapshotIfAsked(cur.pic);
            std::unique_lock<std::recursive_mutex> lock(display::frameMutex());
            if (!loggedFirstPresent) {
                XC_LOGI("first draw %dx%d (%s, %s)", cur.pic.width, cur.pic.height,
                        cur.pic.format == media::Picture::Format::Nv12 ? "NV12" : "YUV420P",
                        cur.isHw ? "hardware" : "software");
            }
            uint64_t t2 = platform::nowUs();
            auto drawPic = [](const media::Picture& p, bool wait) {
                if (p.format == media::Picture::Format::Nv12) {
                    return display::drawNv12(p.y, p.u, p.strideY, p.strideU, p.width, p.height, wait);
                }
                return display::drawYuv420(p.y, p.u, p.v, p.strideY, p.strideU, p.strideV, p.width, p.height, wait);
            };
            bool drawn = drawPic(cur.pic, false);
            if (!drawn) {
                drawn = drawPic(cur.pic, true);
            }
            if (!drawn) {
                ++lateFrames;
                continue;
            }
            uint64_t tDrawDone = platform::nowUs();
            display::present();
            uint64_t t3 = platform::nowUs();
            if (lastFramePresentUs > 0) {
                float dt = static_cast<float>(t3 - lastFramePresentUs) / 1000.0f;
                if (dt > 0.0f && dt < 250.0f) {
                    std::lock_guard<std::mutex> lk(frametimesMutex);
                    frametimesRing[frametimesHead % kFrametimeRing] = dt;
                    ++frametimesHead;
                    if (frametimesCount < kFrametimeRing) ++frametimesCount;
                }
            }
            lastFramePresentUs = t3;
            if (uint64_t id = display::lastPresentId()) {
                inScreenQueue[id % kShownRing] = {id, cur.arrivalUs};
                uint64_t shownId = 0, shownAt = 0;
                if (display::lastShown(shownId, shownAt) && shownId != lastShownId) {
                    lastShownId = shownId;
                    const auto& q = inScreenQueue[shownId % kShownRing];
                    if (q.first == shownId && shownAt > q.second) {
                        uint64_t lat = shownAt - q.second;
                        displayUs += lat;
                        ++displayCount;
                        atomicMax(displayMaxUs, lat);
                    }
                }
            }
            // The display is done with: the report to the server (a data-channel send) and
            // the logging run without holding it (a screenshot still needs it).
            if (!screenshot) lock.unlock();
            cur.meta.renderedMs = stream::clockMs();
            if (auto s = current(); s && cur.known) s->reportFrame(cur.meta);
            if (!loggedFirstPresent || (drawCalls % 600 == 0))
                XC_LOGI("frame %u: queued %.1f, decoded %.1f, shown %.1f ms after arrival", cur.meta.serverDataKey,
                        cur.meta.submittedMs - cur.meta.firstPacketArrivalMs,
                        cur.meta.decodedMs - cur.meta.firstPacketArrivalMs,
                        cur.meta.renderedMs - cur.meta.firstPacketArrivalMs);
            if (screenshot) saveScreen();
            drawUs += tDrawDone - t2;
            ++drawCalls;
            atomicMax(drawMaxUs, tDrawDone - t2);
            if (!loggedFirstPresent) {
                loggedFirstPresent = true;
                XC_LOGI("first frame presented");
            }
        }
    }

    void videoLoop() {
        platform::setThreadAffinity(platform::ThreadRole::VideoDecode, "xcloud_vdec");
        auto decoder = std::make_unique<media::VideoDecoder>();
        if (!decoder->init(decodeThreads, preferHwDecode)) {
            end("could not start the H.264 decoder");
            return;
        }
        isHwDecode = decoder->isHardware();
        const bool useAsyncPresent = display::caps().asyncPresent;
        platform::Thread presentThread;
        if (useAsyncPresent) {
            {
                std::lock_guard<std::mutex> lock(presentMutex);
                latestPic.seq = 0;
                consumedPicSeq = 0;
                presentStop = false;
            }
            platform::startThread(presentThread, [this] { asyncPresentLoop(); }, 512u << 10);
        }
        struct PresentJoinGuard {
            Impl* self;
            platform::Thread& t;
            bool active;
            ~PresentJoinGuard() {
                if (!active) return;
                {
                    std::lock_guard<std::mutex> lock(self->presentMutex);
                    self->presentStop = true;
                }
                self->presentCv.notify_all();
                if (t.joinable()) t.join();
            }
        } presentGuard{this, presentThread, useAsyncPresent};
        VideoFrame frame;
        std::vector<uint8_t>& au = frame.data;
        // Timings of the frames handed to the decoder, by RTP timestamp:
        // pictures can come out later than their input.
        std::deque<stream::FrameMetadata> inFlight;
        media::HwDecoder hw;
        bool hwOk = hwProbe && !decoder->isHardware() && hw.init(1920, 1088);
        if (hwProbe && !decoder->isHardware()) XC_LOGI("hwdec: probe %s", hwOk ? "running" : "failed to start");
        uint64_t hwUs = 0, hwMaxUs = 0, hwPictures = 0, hwCalls = 0, hwLogAt = platform::nowMs() + 1000;
        constexpr uint64_t kShownRing = 8;
        struct OnItsWay {
            uint64_t id = 0, arrivalUs = 0, firstPacketUs = 0;
        };
        OnItsWay inScreenQueue[kShownRing] = {};  // presented, not yet known on the screen
        uint64_t lastShownId = 0, lastPresentedId = 0;
        uint64_t lastFramePresentUs = 0;
        const uint64_t maxLagUs = uint64_t{display::caps().maxVideoLagMs} * 1000;
        bool lagAwaitingIdr = false;
        // Display latency: from the frame's arrival (and from its first
        // packet) to the TV, once the GPU says it is on the screen.
        auto noteShown = [&] {
            uint64_t shownId = 0, shownAt = 0;
            if (!display::lastShown(shownId, shownAt) || shownId == lastShownId) return;
            lastShownId = shownId;
            const auto& q = inScreenQueue[shownId % kShownRing];
            if (q.id != shownId) return;
            if (shownAt > q.arrivalUs) {
                uint64_t lat = shownAt - q.arrivalUs;
                displayUs += lat;
                ++displayCount;
                atomicMax(displayMaxUs, lat);
            }
            if (q.firstPacketUs && shownAt > q.firstPacketUs) {
                uint64_t lat = shownAt - q.firstPacketUs;
                glassUs += lat;
                ++glassCount;
                atomicMax(glassMaxUs, lat);
            }
        };
        while (running) {
            {
                std::unique_lock<std::mutex> lock(mutex);
                // While a frame is on its way to the screen, look every 2 ms
                // (the GPU is asked without waiting) so its time is known
                // within 2 ms instead of at the next frame.
                while (running && frames.empty() && lastPresentedId > lastShownId) {
                    if (cv.wait_for(lock, std::chrono::milliseconds(2)) == std::cv_status::no_timeout) continue;
                    lock.unlock();
                    {
                        std::lock_guard<std::recursive_mutex> displayLock(display::frameMutex());
                        noteShown();
                    }
                    lock.lock();
                }
                cv.wait(lock, [&] { return !running || !frames.empty(); });
                if (!running) break;
                frame = std::move(frames.front());
                frames.pop_front();
                if (maxLagUs && !lagAwaitingIdr && platform::nowUs() - frame.arrivalUs > maxLagUs) {
                    // Too far behind for this device: the backlog would only add delay.
                    ++queueResets;
                    frames.clear();
                    keyframeWanted = true;
                    lagAwaitingIdr = true;
                }
            }
            if (lagAwaitingIdr) {
                // Frames before the next IDR reference pictures that were dropped.
                if (!hasIdrSlice(au)) {
                    requestKeyframeIfWanted();
                    continue;
                }
                lagAwaitingIdr = false;
            }
            if (dumpFile) {
                // 4-byte little-endian length, then the access unit as received.
                uint32_t n = static_cast<uint32_t>(au.size());
                std::fwrite(&n, 4, 1, dumpFile);
                std::fwrite(au.data(), 1, au.size(), dumpFile);
                if (platform::nowMs() >= dumpUntilMs) {
                    std::fclose(dumpFile);
                    dumpFile = nullptr;
                    XC_LOGI("video dump complete");
                }
            }
            media::Picture pic;
            const bool first = decodedFrames == 0 && !loggedFirstDecode;
            if (first) XC_LOGI("first video decode: %zu bytes", au.size());
            if (hwOk) {
                media::HwDecoder::Picture hp;
                uint64_t h0 = platform::nowUs();
                if (hw.decode(au.data(), au.size(), frame.rtpTimestamp, hp)) ++hwPictures;
                uint64_t took = platform::nowUs() - h0;
                hwUs += took;
                hwMaxUs = std::max(hwMaxUs, took);
                ++hwCalls;
                if (platform::nowMs() >= hwLogAt) {
                    hwLogAt += 1000;
                    XC_LOGI("hwdec: %llu calls, %llu pictures, %.2f/%.2f ms", static_cast<unsigned long long>(hwCalls),
                            static_cast<unsigned long long>(hwPictures), hwCalls ? hwUs / 1000.0 / hwCalls : 0.0,
                            hwMaxUs / 1000.0);
                    hwUs = hwMaxUs = hwCalls = hwPictures = 0;
                }
            }
            stream::FrameMetadata meta;
            meta.serverDataKey = frame.rtpTimestamp;
            meta.firstPacketArrivalMs = frame.arrivalMs;
            meta.submittedMs = stream::clockMs();
            inFlight.push_back(meta);
            if (inFlight.size() > 16) inFlight.pop_front();
            // A/B tests: another decoder while streaming, from the next key
            // frame (autoplay "abcycle").
            if (int want = decoderSwitch.exchange(-1); want >= 0) {
                auto next = std::make_unique<media::VideoDecoder>();
                if (next->init(decodeThreads, want == 1)) {
                    decoder = std::move(next);
                    keyframeWanted = true;
                    requestKeyframeIfWanted();
                    XC_LOGI("stream: decoder switched to %s", decoder->isHardware() ? "hardware" : "software");
                }
            }
            uint64_t t0 = platform::nowUs();
            bool ok = decoder->decode(au.data(), au.size(), pic, frame.rtpTimestamp);
            isHwDecode = decoder->isHardware();
            uint64_t t1 = platform::nowUs();
            bool known = false;
            if (ok) {
                for (auto it = inFlight.begin(); it != inFlight.end(); ++it)
                    if (it->serverDataKey == pic.tag) {
                        meta = *it;
                        inFlight.erase(inFlight.begin(), it + 1);  // and anything older: never coming
                        known = true;
                        break;
                    }
                meta.decodedMs = stream::clockMs();
            }
            decodeUs += t1 - t0;
            ++decodeCalls;
            atomicMax(decodeMaxUs, t1 - t0);
            if (first) {
                loggedFirstDecode = true;
                XC_LOGI("first video decode done: ok=%d", ok);
            }
            if (decoder->needsKeyframe()) keyframeWanted = true;
            requestKeyframeIfWanted();
            if (!ok) {
                if (!decoder->pending()) ++decodeFailures;
                continue;
            }
            ++decodedFrames;
            if (decoder->lastQp() >= 0) {
                qpSumX100 += static_cast<uint64_t>(decoder->lastQp() * 100);
                ++qpCount;
            }
            pictureSize = (static_cast<uint32_t>(pic.width) << 16) | static_cast<uint32_t>(pic.height);
            if (useAsyncPresent) {
                {
                    std::lock_guard<std::mutex> lock(presentMutex);
                    if (latestPic.seq > consumedPicSeq) {
                        ++droppedFrames;
                    }
                    latestPic.pic = pic;
                    latestPic.meta = meta;
                    latestPic.known = known;
                    latestPic.isHw = decoder->isHardware();
                    latestPic.arrivalUs = frame.arrivalUs;
                    ++latestPic.seq;
                    if (!latestPic.isHw) {
                        size_t yBytes = static_cast<size_t>(pic.strideY) * pic.height;
                        size_t uvH = (pic.height + 1) / 2;
                        size_t uBytes = static_cast<size_t>(pic.strideU) * uvH;
                        latestPic.yPlane.assign(pic.y, pic.y + yBytes);
                        latestPic.uPlane.assign(pic.u, pic.u + uBytes);
                        if (pic.format != media::Picture::Format::Nv12 && pic.v) {
                            size_t vBytes = static_cast<size_t>(pic.strideV) * uvH;
                            latestPic.vPlane.assign(pic.v, pic.v + vBytes);
                        }
                    }
                }
                presentCv.notify_one();
                continue;
            }
            // Behind the live edge (frames that came at once after a pause in
            // delivery or a retransmission): the frame is decoded, keeping the
            // references, but not shown. At 120 Hz a couple of frames are shown
            // instead (a refresh or two drains them); a longer backlog, or a
            // frame already over 35 ms old with others waiting, is skipped. At
            // 60 Hz a second frame waiting is skipped: showing it would hold
            // the next ones back a refresh.
            size_t pendingFrames = 0;
            {
                std::lock_guard<std::mutex> lock(mutex);
                pendingFrames = frames.size();
            }
            const bool behind = highRefreshOutput && !legacy
                                    ? pendingFrames > 2 || (pendingFrames > 0 && platform::nowUs() - frame.arrivalUs > 35000)
                                    : pendingFrames > 1;
            if (behind) {
                ++droppedFrames;
                continue;
            }
            bool screenshot = saveSnapshotIfAsked(pic);
            std::lock_guard<std::recursive_mutex> lock(display::frameMutex());
            if (decodedFrames == 1) {
                XC_LOGI("first draw %dx%d (%s, %s)", pic.width, pic.height,
                        pic.format == media::Picture::Format::Nv12 ? "NV12" : "YUV420P",
                        decoder->isHardware() ? "hardware" : "software");
            }
            uint64_t t2 = platform::nowUs();
            // Try acquire with zero wait first; if not ready, wait for swapchain image.
            auto drawPic = [](const media::Picture& p, bool wait) {
                if (p.format == media::Picture::Format::Nv12) {
                    return display::drawNv12(p.y, p.u, p.strideY, p.strideU, p.width, p.height, wait);
                }
                return display::drawYuv420(p.y, p.u, p.v, p.strideY, p.strideU, p.strideV, p.width, p.height, wait);
            };
            bool drawn = drawPic(pic, false);
            if (!drawn) {
                drawn = drawPic(pic, true);
            }
            if (!drawn) {
                ++lateFrames;
                continue;
            }
            display::present();
            uint64_t t3 = platform::nowUs();
            if (lastFramePresentUs > 0) {
                float dt = static_cast<float>(t3 - lastFramePresentUs) / 1000.0f;
                if (dt > 0.0f && dt < 250.0f) {
                    std::lock_guard<std::mutex> lk(frametimesMutex);
                    frametimesRing[frametimesHead % kFrametimeRing] = dt;
                    ++frametimesHead;
                    if (frametimesCount < kFrametimeRing) ++frametimesCount;
                }
            }
            lastFramePresentUs = t3;
            if (uint64_t id = display::lastPresentId()) {
                inScreenQueue[id % kShownRing] = {id, frame.arrivalUs, frame.firstPacketUs};
                lastPresentedId = id;
                noteShown();
            }
            meta.renderedMs = stream::clockMs();
            if (auto s = current(); s && known) s->reportFrame(meta);
            if (decodedFrames % 600 == 1)
                XC_LOGI("frame %u: queued %.1f, decoded %.1f, shown %.1f ms after arrival", meta.serverDataKey,
                        meta.submittedMs - meta.firstPacketArrivalMs, meta.decodedMs - meta.firstPacketArrivalMs,
                        meta.renderedMs - meta.firstPacketArrivalMs);
            if (screenshot) saveScreen();
            drawUs += t3 - t2;
            ++drawCalls;
            atomicMax(drawMaxUs, t3 - t2);
            if (decodedFrames == 1) XC_LOGI("first frame presented");
            if (imageTestWanted.exchange(false)) imageTest(pic, drawPic);
        }
    }

    bool start(std::string& err) {
        if (!audioDecoder.init()) XC_LOGW("no audio decoder: playing without sound");
        if (!media::audioStart()) XC_LOGW("no audio output: playing without sound");

        stream::StreamCallbacks cb;
        cb.video = [this](const uint8_t* d, size_t n, uint32_t rtpTimestamp, uint64_t firstPacketUs) {
            if (n == 0) {  // the depacketizer emits these after loss
                keyframeWanted = true;
                return;
            }
            if (++videoFrames == 1) XC_LOGI("first video frame received: %zu bytes", n);
            static int lastProfile = -1, lastLevel = -1;
            if (lastProfile == -1) {
                // The H.264 profile the server encodes with (from the first SPS).
                for (size_t i = 0; i + 6 < n; ++i)
                    if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1 && (d[i + 3] & 0x1F) == 7) {
                        int profile = d[i + 4], level = d[i + 6];
                        if (profile != lastProfile || level != lastLevel) {
                            lastProfile = profile;
                            lastLevel = level;
                            XC_LOGI("video: H.264 profile %d (%s), level %d.%d", profile,
                                    profile == 66 ? "Baseline" : profile == 77 ? "Main" : profile == 100 ? "High" : "?",
                                    level / 10, level % 10);
                        }
                        break;
                    }
            }
            std::lock_guard<std::mutex> lock(mutex);
            if (frames.size() >= kMaxQueuedFrames) {
                // Hopelessly behind: start over from the next key frame.
                ++queueResets;
                frames.clear();
                keyframeWanted = true;
            }
            frames.push_back(
                {std::vector<uint8_t>(d, d + n), rtpTimestamp, stream::clockMs(), platform::nowUs(), firstPacketUs});
            cv.notify_one();
        };
        cb.audio = [this](const uint8_t* d, size_t n, uint32_t rtpTimestamp) {
            if (++audioPackets == 1) XC_LOGI("first audio packet: %zu bytes (timestamp %u)", n, rtpTimestamp);
            std::lock_guard<std::mutex> lock(audioMutex_);
            if (audioPackets_.size() < 50) {
                auto it = audioPackets_.end();
                while (it != audioPackets_.begin()) {
                    auto prev = it - 1;
                    int32_t diff = static_cast<int32_t>(rtpTimestamp - prev->rtpTimestamp);
                    if (diff >= 0) break;
                    it = prev;
                }
                audioPackets_.insert(it, {std::vector<uint8_t>(d, d + n), rtpTimestamp});
            }
            audioCv.notify_one();
        };
        // xCloud's motors are 0..100 percent, like the web client's
        // dual-rumble effect (left = strong, right = weak). The trigger
        // motors (impulse triggers) become the adaptive triggers vibrating.
        cb.vibration = [this](const stream::Vibration& v) {
            // The first few, and the first few with the triggers (rarer: does
            // the service send them at all for a game?).
            bool triggers = v.leftTrigger || v.rightTrigger;
            if (++vibrations <= 5 || (triggers && ++triggerVibrations <= 5))
                XC_LOGI("vibration %u/%u/%u/%u for %ums (pad %u)", v.leftMotor, v.rightMotor, v.leftTrigger,
                        v.rightTrigger, v.durationMs, v.gamepadIndex);
            auto scale = [](uint8_t pct) { return static_cast<uint8_t>(std::min<int>(pct, 100) * 255 / 100); };
            input::setRumble(scale(v.leftMotor), scale(v.rightMotor), v.durationMs, v.gamepadIndex);
            input::setTriggerRumble(scale(v.leftTrigger), scale(v.rightTrigger), v.durationMs, v.gamepadIndex);
        };
        cb.titleFocus = [this](bool focused) {
            if (titleFocused.exchange(focused) != focused) XC_LOGI("title %s the focus", focused ? "has" : "lost");
        };
        cb.idleWarning = [](int seconds) {
            platform::notify(ui::trf(ui::Str::IdleWarning, std::to_string(seconds)));
        };
        cb.closed = [this](const std::string& reason, bool recoverable) {
            if (std::string res = platform::diagnostics(); !res.empty())
                XC_LOGI("resources at stream close: %s", res.c_str());
            // A lost connection: the worker's tick() reconnects. The server
            // ending the session (or no game left) ends the stream.
            if (recoverable && running) {
                if (!reconnecting.exchange(true)) XC_LOGW("connection lost (%s): reconnecting", reason.c_str());
            } else {
                end(reason);
            }
        };
        // With the console's keyboard, the game's text fields use it;
        // otherwise the server draws the Xbox keyboard into the picture.
        if (platform::systemKeyboardAvailable()) {
            cb.textInput = [this](const stream::TextInputRequest& req) {
                std::lock_guard<std::mutex> lock(mutex);
                textRequests.push_back(req);
            };
            cb.textInputCancelled = [this](const std::string& id) {
                std::lock_guard<std::mutex> lock(mutex);
                textWithdrawn.insert(id);
            };
        }

        stream::StreamOptions opts;
        // Each setting asks for the best tier of its resolution, like the
        // xbox.com client: the HQ tiers (higher bitrate) and 1440p need Game
        // Pass Ultimate and a market where Microsoft enabled them; elsewhere
        // the plain tier the session asks for first holds.
        switch (gssv.resolution()) {
        case xcloud::Resolution::P1440: opts.resolutionAlias = "1440"; break;
        case xcloud::Resolution::P1080:
        case xcloud::Resolution::P1080HQ: opts.resolutionAlias = "1080HQ"; break;
        case xcloud::Resolution::P720: opts.resolutionAlias = "720HQ"; break;
        }
        opts.maxBitrate = gssv.resolution() == xcloud::Resolution::P720    ? 12000000
                          : gssv.resolution() == xcloud::Resolution::P1440 ? 40000000
                          : gssv.resolution() == xcloud::Resolution::P1080HQ ? 30000000
                                                                           : 25000000;
        if (maxBitrateMbps > 0) {  // the user's cap (Wi-Fi) or test override: REMB offers this rate
            opts.maxBitrate = static_cast<uint32_t>(std::clamp(maxBitrateMbps, 5, 50)) * 1000000u;
        } else if (uint32_t deviceCap = display::caps().defaultMaxBitrate; deviceCap > 0) {
            // A device that can't keep up with the HQ tiers (PS4): the plain tier, at most
            // its decoder's rate.
            opts.maxBitrate = std::min(opts.maxBitrate, deviceCap);
            if (opts.resolutionAlias == "1080HQ") opts.resolutionAlias = "1080";
        }
        if (legacy) opts.keyframeIntervalSec = 5;
        opts.simulatedVideoLoss = simulatedLoss;
        if (const char* loss = std::getenv("XC_SIM_LOSS")) opts.simulatedVideoLoss = std::atoi(loss);
        callbacks = cb;
        options = opts;
        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            session = std::make_shared<stream::StreamSession>(gssv, cb, opts);
        }
        running = true;
        if (!platform::startThread(videoThread, [this] { videoLoop(); }) ||
            !platform::startThread(audioThread, [this] { audioLoop(); }, 1u << 20) ||
            !platform::startThread(inputThread, [this] { inputLoop(); }, 256u << 10)) {
            err = "could not start the media threads";
            running = false;
            return false;
        }
        if (!current()->start(err)) {
            end(err);
            return false;
        }
        updateMicState();
        return true;
    }

    // One reconnection attempt (worker thread, from tick()).
    void reconnect() {
        if (reconnectAttempts == 0) platform::notify(ui::tr(ui::Str::Reconnecting));
        if (++reconnectAttempts > 20) {
            end("the connection was lost and couldn't be restored");
            return;
        }
        // Only while the cloud session is still there to connect to.
        xcloud::SessionStatus status;
        std::string err;
        if (!gssv.sessionState(status, err)) {
            XC_LOGW("reconnect %d: no session state (%s)", reconnectAttempts, err.c_str());
            return;  // the network may not be back yet
        }
        if (status.state == xcloud::SessionState::Provisioning ||
            status.state == xcloud::SessionState::WaitingForResources) {
            // Being set up again: wait. A home Xbox's session that dropped
            // stays there for good: a new session is started instead (the
            // caller does it; the game goes on on the console meanwhile).
            XC_LOGI("reconnect %d: session %s, waiting", reconnectAttempts, status.raw.c_str());
            if (gssv.isHome() && reconnectAttempts >= 4) end("the connection was lost and couldn't be restored");
            return;
        }
        if (status.state != xcloud::SessionState::Provisioned && status.state != xcloud::SessionState::ReadyToConnect) {
            end("the session ended on the server while reconnecting (" + status.raw + ")");
            return;
        }
        std::shared_ptr<stream::StreamSession> old;
        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            old.swap(session);
        }
        if (old) old->close();
        old.reset();
        stream::StreamOptions opts = options;
        opts.connectTimeoutMs = 8000;
        auto next = std::make_shared<stream::StreamSession>(gssv, callbacks, opts);
        // Cleared first: the new session's own failure must count again.
        reconnecting = false;
        if (!next->start(err)) {
            XC_LOGW("reconnect %d failed: %s", reconnectAttempts, err.c_str());
            reconnecting = true;
            return;
        }
        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            session = next;
        }
        updateMicState();
        XC_LOGI("reconnected after %d attempt(s)", reconnectAttempts);
        reconnectAttempts = 0;
        silentTicks = 0;
        lastRtpPackets = 0;
        lastAudioPackets = 0;
        ++reconnects;
        keyframeWanted = true;
        platform::notify(ui::tr(ui::Str::Reconnected));
    }

    // Returns true when a snapshot was taken: the caller then saves what the
    // screen shows too (saveScreen()).
    bool saveSnapshotIfAsked(const media::Picture& pic) {
        std::string path;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex);
            path.swap(snapshotPath);
        }
        if (path.empty()) return false;
        screenPath = path.substr(0, path.rfind('/') + 1) + "screen.ppm";
        int w = pic.width / 2, h = pic.height / 2;
        std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
        const bool isNv12 = (pic.format == media::Picture::Format::Nv12);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int c = (pic.y[(2 * y) * pic.strideY + 2 * x] - 16) * 1192;
                int d = isNv12 ? (pic.u[y * pic.strideU + 2 * x] - 128)
                               : (pic.u[y * pic.strideU + x] - 128);
                int e = isNv12 ? (pic.u[y * pic.strideU + 2 * x + 1] - 128)
                               : (pic.v[y * pic.strideV + x] - 128);
                auto clamp = [](int v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); };
                uint8_t* o = &rgb[(static_cast<size_t>(y) * w + x) * 3];
                o[0] = clamp((c + 1836 * e + 512) >> 10);
                o[1] = clamp((c - 218 * d - 546 * e + 512) >> 10);
                o[2] = clamp((c + 2163 * d + 512) >> 10);
            }
        if (snapshotThread.joinable()) snapshotThread.join();
        platform::startThread(snapshotThread, [path, rgb = std::move(rgb), w, h]() mutable {
            std::string data = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
            data.append(reinterpret_cast<const char*>(rgb.data()), rgb.size());
            bool ok = platform::writeFileAtomic(path, data);
            XC_LOGI("snapshot %dx%d -> %s: %s", w, h, path.c_str(), ok ? "ok" : "failed");
        }, 512u << 10);
        return true;
    }

    // The scan-out buffer just presented, at full resolution.
    // Measurements (autoplay "imgtest"): this picture drawn again with each
    // look (upscaler, sharpening), a 1280x720 crop of the 4K output
    // saved for each (img_<name>.ppm). The stream is left with the defaults.
    std::atomic<bool> imageTestWanted{false};
    template <class Draw>
    void imageTest(const media::Picture& pic, const Draw& drawPic) {
        struct Look {
            const char* name;
            int upscaler, sharpness;  // stream_screen.cpp's applyUpscaler mode, kSharpAmount
        };
        static constexpr Look kLooks[] = {
            {"1_clean_med", 2, 176}, {"2_fsr_med", 0, 176},  {"3_clean_high", 2, 256}, {"4_clean_low", 2, 96},
            {"5_clean_off", 2, 0},   {"6_fsr_off", 0, 0},    {"7_a4k_med", 1, 176},    {"8_a4k_clean_med", 3, 176},
            {"9_default", 2, 176},
        };
        for (const Look& look : kLooks) {
            display::setUpscaler(look.upscaler & 1);
            display::setRestore(look.upscaler >= 2);
            display::setSharpness(look.sharpness);
            std::vector<uint8_t> rgb;
            int w = 0, h = 0;
            {
                std::lock_guard<std::recursive_mutex> lock(display::frameMutex());
                if (!drawPic(pic, true)) continue;
                display::present();
                if (!display::readBackRgb(rgb, w, h)) continue;
            }
            if (std::string(look.name) == "9_default") break;
            const int cw = std::min(1280, w), ch = std::min(720, h), x0 = std::min(1100, w - cw), y0 = std::min(300, h - ch);
            std::string data = "P6\n" + std::to_string(cw) + " " + std::to_string(ch) + "\n255\n";
            for (int y = 0; y < ch; ++y)
                data.append(reinterpret_cast<const char*>(rgb.data() + (static_cast<size_t>(y0 + y) * w + x0) * 3),
                            static_cast<size_t>(cw) * 3);
            const std::string path = platform::dataDir() + "/img_" + look.name + ".ppm";
            XC_LOGI("image test: %s (%dx%d of %dx%d) %s", look.name, cw, ch, w, h,
                    platform::writeFileAtomic(path, data) ? "saved" : "not saved");
        }
    }

    void saveScreen() {
        try {
            std::vector<uint8_t> rgb;
            int w = 0, h = 0;
            if (!display::readBackRgb(rgb, w, h)) return;
            std::string path = screenPath;
            if (screenThread.joinable()) screenThread.join();
            platform::startThread(screenThread, [path, rgb = std::move(rgb), w, h]() mutable {
                try {
                    std::string data = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
                    data.append(reinterpret_cast<const char*>(rgb.data()), rgb.size());
                    std::vector<uint8_t>().swap(rgb); // release the 6.2 MB vector immediately
                    bool ok = platform::writeFileAtomic(path, data);
                    XC_LOGI("screen -> %s: %s", path.c_str(), ok ? "ok" : "failed");
                } catch (const std::exception& e) {
                    XC_LOGW("screen thread error: %s", e.what());
                }
            }, 512u << 10);
        } catch (const std::exception& e) {
            XC_LOGW("saveScreen failed: %s", e.what());
        }
    }

    // Video thread only (data channel sends are thread-safe).
    void requestKeyframeIfWanted() {
        uint64_t now = platform::nowMs();
        if (keyframeWanted && now - lastKeyframeRequestMs > 500) {
            auto s = current();
            if (!s) return;
            keyframeWanted = false;
            lastKeyframeRequestMs = now;
            ++keyframeRequests;
            s->requestKeyframe();
        }
    }

    // About once a second (worker thread).
    void tick() {
        if (!running) return;
        if (reconnecting) return reconnect();
        auto s = current();
        if (!s) return;
        s->tick();
        // xCloud sends video all the time, even for a still picture: three
        // seconds without a packet is a dead connection, long before WebRTC
        // notices. With the sound still coming, the connection is alive and
        // the picture only paused (a home Xbox does it while a game starts
        // and switches display modes): up to 15 s then.
        uint64_t packets = s->videoStats().packets;
        uint64_t audio = audioPackets;
        bool audioAlive = audio != lastAudioPackets;
        lastAudioPackets = audio;
        silentTicks = packets == lastRtpPackets ? silentTicks + 1 : 0;
        lastRtpPackets = packets;
        int limit = audioAlive ? 15 : 3;
        if (silentTicks >= limit && !reconnecting.exchange(true))
            XC_LOGW("no video for %d s%s: reconnecting", silentTicks, audioAlive ? " (sound still coming)" : "");
    }

    void stop() {
        end("stopped");
        if (auto s = current()) s->close();
        if (inputThread.joinable()) inputThread.join();
        if (videoThread.joinable()) videoThread.join();
        if (audioThread.joinable()) audioThread.join();
        if (snapshotThread.joinable()) snapshotThread.join();
        if (screenThread.joinable()) screenThread.join();
        std::shared_ptr<stream::StreamSession> last;
        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            last = std::move(session);
        }
        last.reset();  // its teardown waits for libdatachannel's callbacks: not under the lock
        closeMic();
        media::audioStop();
        for (int i = 0; i < input::kMaxPads; ++i) {
            input::setRumble(0, 0, 0, i);
            input::setTriggerRumble(0, 0, 0, i);
        }
    }
};

StreamPlayer::StreamPlayer(xcloud::GssvClient& gssv) : impl_(std::make_unique<Impl>(gssv)) {}
StreamPlayer::~StreamPlayer() { impl_->stop(); }

bool StreamPlayer::start(std::string& err) { return impl_->start(err); }
bool StreamPlayer::running() const { return impl_->running; }

std::string StreamPlayer::endReason() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->endReason;
}

void StreamPlayer::sendInput(const input::ControllerState& p, int index) {
    if (!impl_->running || index < 0 || index >= input::kMaxPads) return;
    stream::GamepadFrame f;
    f.index = static_cast<uint8_t>(index);
    auto set = [&](bool on, uint16_t bit) {
        if (on) f.buttons |= bit;
    };
    set(p.btnA, stream::kA);
    set(p.btnB, stream::kB);
    set(p.btnX, stream::kX);
    set(p.btnY, stream::kY);
    set(p.dpadUp, stream::kDPadUp);
    set(p.dpadDown, stream::kDPadDown);
    set(p.dpadLeft, stream::kDPadLeft);
    set(p.dpadRight, stream::kDPadRight);
    set(p.btnL1, stream::kLeftShoulder);
    set(p.btnR1, stream::kRightShoulder);
    set(p.btnL3, stream::kLeftThumb);
    set(p.btnR3, stream::kRightThumb);
    set(p.btnOptions, stream::kMenu);
    set(p.btnTouchpad, stream::kView);
    set(p.btnNexus, stream::kNexus);
    f.leftX = p.leftStickX;
    f.leftY = -p.leftStickY;  // pad: +down; wire: +up
    f.rightX = p.rightStickX;
    f.rightY = -p.rightStickY;
    f.leftTrigger = p.triggerL2;
    f.rightTrigger = p.triggerR2;

    {
        std::lock_guard<std::mutex> lock(impl_->inputMutex);
        impl_->pendingPads[index] = f;
        impl_->pendingPadDirty[index] = true;
    }
    impl_->inputCv.notify_one();
}

void StreamPlayer::sendKbm(const input::KbmInput& in) {
    if (!impl_->running) return;
    {
        std::lock_guard<std::mutex> lock(impl_->inputMutex);
        if (!in.keys.empty()) {
            for (const auto& k : in.keys) {
                if (impl_->pendingKeys.size() < 128) impl_->pendingKeys.push_back({k.vk, k.pressed});
            }
        }
        if (in.moved) {
            stream::MouseFrame m;
            m.dx = in.dx * 2;  // as the web client: twice the pointer's movement
            m.dy = in.dy * 2;
            m.wheelY = in.wheel * 120;  // one notch = WHEEL_DELTA (120), as Windows (and the game) count it
            m.buttons = in.buttons;
            if (impl_->pendingMouse.size() < 128) impl_->pendingMouse.push_back(m);
        }
    }
    impl_->inputCv.notify_one();
}

bool StreamPlayer::titleFocused() const { return impl_->titleFocused; }

void StreamPlayer::setTouchEnabled(bool on) {
    if (auto s = impl_->current(); s && impl_->running) s->setTouchEnabled(on);
}

void StreamPlayer::setPadConnected(int index, bool connected) {
    if (auto s = impl_->current(); s && impl_->running) s->setGamepadConnected(index, connected);
}

bool StreamPlayer::takeTextInput(stream::TextInputRequest& out) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    while (!impl_->textRequests.empty()) {
        out = std::move(impl_->textRequests.front());
        impl_->textRequests.pop_front();
        if (!impl_->textWithdrawn.count(out.id)) return true;
    }
    return false;
}

bool StreamPlayer::textInputWithdrawn(const std::string& id) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->textWithdrawn.count(id) != 0;
}

void StreamPlayer::answerTextInput(const std::string& id, bool accepted, const std::string& text) {
    auto s = impl_->current();
    if (textInputWithdrawn(id) || !s) return;
    XC_LOGI("text input %s", accepted ? "sent" : "cancelled");
    if (accepted)
        s->completeTextInput(id, text);
    else
        s->cancelTextInput(id);
}

void StreamPlayer::requestKeyframe() { impl_->keyframeWanted = true; }

uint32_t StreamPlayer::reconnects() const { return impl_->reconnects; }

void StreamPlayer::simulateDrop() {
    if (auto s = impl_->current()) s->simulateDrop();
}

void StreamPlayer::requestResolution(const std::string& alias) {
    if (auto s = impl_->current()) s->requestResolution(alias);
}

void StreamPlayer::tick() { impl_->tick(); }

void StreamPlayer::dumpVideo(const std::string& path, int seconds) {
    // Before start(): only the video thread touches the file afterwards.
    impl_->dumpFile = std::fopen(path.c_str(), "wb");
    impl_->dumpUntilMs = platform::nowMs() + static_cast<uint64_t>(seconds) * 1000u;
    XC_LOGI("dumping video to %s: %s", path.c_str(), impl_->dumpFile ? "ok" : "failed");
}

void StreamPlayer::requestImageTest() { impl_->imageTestWanted = true; }

void StreamPlayer::requestSnapshot(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->snapshotMutex);
    impl_->snapshotPath = path;
}
void StreamPlayer::stop() { impl_->stop(); }

void StreamPlayer::setHwDecodeProbe(bool on) { impl_->hwProbe = on; }
void StreamPlayer::setPreferHwDecode(bool on) { impl_->preferHwDecode = on; }
bool StreamPlayer::preferHwDecode() const { return impl_->preferHwDecode; }

void StreamPlayer::setDecodeThreads(int threads) { impl_->decodeThreads = std::max(1, threads); }
void StreamPlayer::setMaxBitrate(int mbps) { impl_->maxBitrateMbps = std::clamp(mbps, 0, 50); }
void StreamPlayer::setSimulatedLoss(int percent) { impl_->simulatedLoss = std::clamp(percent, 0, 50); }

StreamPlayer::Stats StreamPlayer::stats() const {
    Stats st;
    st.videoFrames = impl_->videoFrames;
    st.decodedFrames = impl_->decodedFrames;
    st.droppedFrames = impl_->droppedFrames;
    st.audioPackets = impl_->audioPackets;
    st.decodeFailures = impl_->decodeFailures;
    st.queueResets = impl_->queueResets;
    st.keyframeRequests = impl_->keyframeRequests;
    st.vibrations = impl_->vibrations;
    st.lateFrames = impl_->lateFrames;
    st.width = static_cast<int>(impl_->pictureSize >> 16);
    st.height = static_cast<int>(impl_->pictureSize & 0xFFFF);
    st.isHwDecode = impl_->isHwDecode.load();
    // Timings since the previous call.
    uint64_t dc = impl_->decodeCalls.exchange(0), du = impl_->decodeUs.exchange(0);
    uint64_t rc = impl_->drawCalls.exchange(0), ru = impl_->drawUs.exchange(0);
    st.decodeAvgUs = dc ? du / dc : 0;
    st.decodeMaxUs = impl_->decodeMaxUs.exchange(0);
    uint64_t qn = impl_->qpCount.exchange(0), qs = impl_->qpSumX100.exchange(0);
    st.qpAvg = qn ? qs / 100.0 / static_cast<double>(qn) : -1;
    st.drawAvgUs = rc ? ru / rc : 0;
    st.drawMaxUs = impl_->drawMaxUs.exchange(0);
    uint64_t dn = impl_->displayCount.exchange(0), du2 = impl_->displayUs.exchange(0);
    st.displayAvgUs = dn ? du2 / dn : 0;
    st.displayMaxUs = impl_->displayMaxUs.exchange(0);
    uint64_t gn = impl_->glassCount.exchange(0), gu = impl_->glassUs.exchange(0);
    st.glassAvgUs = gn ? gu / gn : 0;
    st.glassMaxUs = impl_->glassMaxUs.exchange(0);
    if (auto s = impl_->current()) {
        const auto& v = s->videoStats();
        st.rtpPackets = v.packets;
        st.rtpLost = v.lost;
        st.rtpRecovered = v.recovered;
        st.rtpNacks = v.nacks;
        st.rtpDroppedFrames = v.framesDropped;
        st.rtpGapMs = v.maxGapMs.exchange(0), st.rtpBurst = v.maxBurst.exchange(0), st.rtpLateMs = v.maxLateMs.exchange(0);
        st.keyframeRequests += v.keyframeRequests;
        st.rtpKbps = v.receiveRate / 1000;
        st.rttMs = s->rttMs();
        st.rembKbps = v.estimate / 1000;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        st.queued = impl_->frames.size();
    }
    return st;
}

void StreamPlayer::switchDecoder(bool hardware) { impl_->decoderSwitch = hardware ? 1 : 0; }

void StreamPlayer::setLegacyTuning(bool on) {
    impl_->legacy = on;
    if (on) XC_LOGI("stream: legacy tuning (measurement baseline)");
}

void StreamPlayer::setHighRefreshOutput(bool on) {
    impl_->highRefreshOutput = on;
    XC_LOGI("video: %s output, frames that come at once %s", on ? "120 Hz" : "60 Hz",
            on ? "shown up to 2 behind" : "skipped from the second one waiting");
}

void StreamPlayer::setMicEnabled(bool enabled) {
    impl_->micEnabled = enabled;
    impl_->updateMicState();
}

bool StreamPlayer::micEnabled() const {
    return impl_->micEnabled;
}

void StreamPlayer::setMicMuted(bool muted) {
    impl_->micMuted = muted;
    impl_->audioIn.setMuted(muted);
}

bool StreamPlayer::micMuted() const {
    return impl_->micMuted;
}

bool StreamPlayer::isMicHardwareMuted() const {
    return impl_ ? impl_->audioIn.isHardwareMuted() : false;
}

void StreamPlayer::setMicGain(float gain) {
    impl_->micGain = gain;
    impl_->audioIn.setGain(gain);
}

float StreamPlayer::micGain() const {
    return impl_->micGain;
}

float StreamPlayer::micLevel() const {
    return impl_ ? impl_->audioIn.level() : 0.0f;
}

void StreamPlayer::getRecentFrametimes(std::vector<float>& out) const {
    if (!impl_) {
        out.clear();
        return;
    }
    std::lock_guard<std::mutex> lk(impl_->frametimesMutex);
    out.clear();
    out.reserve(impl_->frametimesCount);
    size_t count = impl_->frametimesCount;
    size_t head = impl_->frametimesHead;
    size_t start = head >= count ? head - count : 0;
    for (size_t i = 0; i < count; ++i) {
        out.push_back(impl_->frametimesRing[(start + i) % Impl::kFrametimeRing]);
    }
}

}  // namespace xc::app
