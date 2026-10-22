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
    std::atomic<uint64_t> decodeUs{0}, decodeCalls{0}, decodeMaxUs{0}, drawUs{0}, drawCalls{0}, drawMaxUs{0};
    std::atomic<uint64_t> qpSumX100{0}, qpCount{0};  // pictures' quantiser since the last stats()
    int decodeThreads = 1;
    int simulatedLoss = 0;
    int maxBitrateMbps = 0;
    bool hwProbe = false;

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
    std::atomic<bool> micEnabled{true};
    std::atomic<bool> micMuted{false};
    std::atomic<float> micGain{1.0f};
    std::atomic<uint32_t> audioSendTimestamp{0};
    bool encoderInitialized = false;
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
                encoderInitialized = audioEncoder.init(24000);
                if (!encoderInitialized) {
                    XC_LOGW("audio encoder: Opus init failed, mic input & meter will still operate");
                }
            }
            audioIn.setMuted(micMuted.load());
            audioIn.setGain(micGain.load());
            audioIn.open([this](const int16_t* pcmFrames, size_t samples) {
                if (!running) return;
                if (!encoderInitialized) return;
                auto s = current();
                if (!s || !s->isOpen()) return;
                std::vector<uint8_t> opus;
                if (audioEncoder.encode(pcmFrames, samples, opus)) {
                    uint32_t ts = audioSendTimestamp.fetch_add(static_cast<uint32_t>(samples));
                    s->sendAudio(opus.data(), opus.size(), ts);
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
            std::lock_guard<std::mutex> lock(audioMutex_);
        }
        audioCv.notify_all();
    }

    void audioLoop() {
        platform::setThreadAffinity(platform::ThreadRole::Audio, "xcloud_adec");
        platform::setThreadRealtimePriority();
        audioDecoder.resetPlc();
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
                // When audio buffer builds up beyond healthy headroom (>= 100 ms) due to network bursts,
                // decode stale packets in Opus to preserve continuous LPC/CELT filter history,
                // but skip pushing into AudioQueue until queue drains to healthy target (~60 ms).
                // This keeps audio latency at ~40-60 ms, perfectly in sync with 22-25 ms video,
                // and completely eliminates 240-700 ms bufferbloat cuts.
                size_t queued = media::audioQueue().queuedFrames();
                if (queued >= kOpusFrameSamples * 5) {
                    continue;
                }
                media::audioPush(pcm.data(), pcm.size() / 2);
            }
        }
    }

    void videoLoop() {
        platform::setThreadAffinity(platform::ThreadRole::VideoDecode, "xcloud_vdec");
        media::VideoDecoder decoder;
        if (!decoder.init(decodeThreads)) {
            end("could not start the H.264 decoder");
            return;
        }
        VideoFrame frame;
        std::vector<uint8_t>& au = frame.data;
        // Timings of the frames handed to the decoder, by RTP timestamp:
        // pictures can come out later than their input.
        std::deque<stream::FrameMetadata> inFlight;
        media::HwDecoder hw;
        bool hwOk = hwProbe && hw.init(1920, 1088);
        if (hwProbe) XC_LOGI("hwdec: probe %s", hwOk ? "running" : "failed to start");
        uint64_t hwUs = 0, hwMaxUs = 0, hwPictures = 0, hwCalls = 0, hwLogAt = platform::nowMs() + 1000;
        constexpr uint64_t kShownRing = 8;
        std::pair<uint64_t, uint64_t> inScreenQueue[kShownRing] = {};  // present id, arrival us
        uint64_t lastShownId = 0;
        uint64_t lastFramePresentUs = 0;
        while (running) {
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&] { return !running || !frames.empty(); });
                if (!running) break;
                frame = std::move(frames.front());
                frames.pop_front();
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
            uint64_t t0 = platform::nowUs();
            bool ok = decoder.decode(au.data(), au.size(), pic, frame.rtpTimestamp);
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
            if (decoder.needsKeyframe()) keyframeWanted = true;
            requestKeyframeIfWanted();
            if (!ok) {
                if (!decoder.pending()) ++decodeFailures;
                continue;
            }
            ++decodedFrames;
            if (decoder.lastQp() >= 0) {
                qpSumX100 += static_cast<uint64_t>(decoder.lastQp() * 100);
                ++qpCount;
            }
            pictureSize = (static_cast<uint32_t>(pic.width) << 16) | static_cast<uint32_t>(pic.height);
            // When a burst of frames arrives after network wait/jitter, fast-forward decode
            // stale frames in FFmpeg without waiting for vblank present, keeping reference
            // picture state intact while draining backlog in ~3.4ms per frame.
            // Present only once caught up to the live edge (backlog <= 1).
            size_t pendingFrames = 0;
            {
                std::lock_guard<std::mutex> lock(mutex);
                pendingFrames = frames.size();
            }
            if (pendingFrames > 1) {
                ++droppedFrames;
                continue;
            }
            bool screenshot = saveSnapshotIfAsked(pic);
            std::lock_guard<std::mutex> lock(display::frameMutex());
            if (decodedFrames == 1) XC_LOGI("first draw %dx%d", pic.width, pic.height);
            uint64_t t2 = platform::nowUs();
            // Try acquire with zero wait first; if not ready, wait for swapchain image.
            bool drawn = display::drawYuv420(pic.y, pic.u, pic.v, pic.strideY, pic.strideU, pic.strideV,
                                             pic.width, pic.height, false);
            if (!drawn) {
                drawn = display::drawYuv420(pic.y, pic.u, pic.v, pic.strideY, pic.strideU, pic.strideV,
                                            pic.width, pic.height, true);
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
            // Display latency: network arrival to the TV, once the GPU says
            // the frame is on the screen (a frame or so later).
            if (uint64_t id = display::lastPresentId()) {
                inScreenQueue[id % kShownRing] = {id, frame.arrivalUs};
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
        }
    }

    bool start(std::string& err) {
        if (!audioDecoder.init()) XC_LOGW("no audio decoder: playing without sound");
        if (!media::audioStart()) XC_LOGW("no audio output: playing without sound");

        stream::StreamCallbacks cb;
        cb.video = [this](const uint8_t* d, size_t n, uint32_t rtpTimestamp) {
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
            frames.push_back({std::vector<uint8_t>(d, d + n), rtpTimestamp, stream::clockMs(), platform::nowUs()});
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
        if (maxBitrateMbps > 0)  // the user's cap (Wi-Fi) or test override: REMB offers this rate
            opts.maxBitrate = static_cast<uint32_t>(std::clamp(maxBitrateMbps, 5, 50)) * 1000000u;
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
            !platform::startThread(audioThread, [this] { audioLoop(); }, 1u << 20)) {
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
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int c = (pic.y[(2 * y) * pic.strideY + 2 * x] - 16) * 1192;
                int d = pic.u[y * pic.strideU + x] - 128;
                int e = pic.v[y * pic.strideV + x] - 128;
                auto clamp = [](int v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); };
                uint8_t* o = &rgb[(static_cast<size_t>(y) * w + x) * 3];
                o[0] = clamp((c + 1836 * e + 512) >> 10);
                o[1] = clamp((c - 218 * d - 546 * e + 512) >> 10);
                o[2] = clamp((c + 2163 * d + 512) >> 10);
            }
        std::thread([path, rgb = std::move(rgb), w, h]() mutable {
            std::string data = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
            data.append(reinterpret_cast<const char*>(rgb.data()), rgb.size());
            bool ok = platform::writeFileAtomic(path, data);
            XC_LOGI("snapshot %dx%d -> %s: %s", w, h, path.c_str(), ok ? "ok" : "failed");
        }).detach();
        return true;
    }

    // The scan-out buffer just presented, at full resolution.
    void saveScreen() {
        std::vector<uint8_t> rgb;
        int w = 0, h = 0;
        if (!display::readBackRgb(rgb, w, h)) return;
        std::string path = screenPath;
        std::thread([path, rgb = std::move(rgb), w, h]() mutable {
            std::string data = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
            data.append(reinterpret_cast<const char*>(rgb.data()), rgb.size());
            bool ok = platform::writeFileAtomic(path, data);
            XC_LOGI("screen -> %s: %s", path.c_str(), ok ? "ok" : "failed");
        }).detach();
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
        if (videoThread.joinable()) videoThread.join();
        if (audioThread.joinable()) audioThread.join();
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
    auto s = impl_->current();
    if (!s || !impl_->running) return;
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
    s->sendGamepad(f);
}

void StreamPlayer::sendKbm(const input::KbmInput& in) {
    auto s = impl_->current();
    if (!s || !impl_->running) return;
    if (!in.keys.empty()) {
        std::vector<stream::KeyFrame> keys;
        for (const auto& k : in.keys) keys.push_back({k.vk, k.pressed});
        s->sendKeys(keys);
    }
    if (in.moved) {
        stream::MouseFrame m;
        m.dx = in.dx * 2;  // as the web client: twice the pointer's movement
        m.dy = in.dy * 2;
        m.wheelY = in.wheel * 120;  // one notch = WHEEL_DELTA (120), as Windows (and the game) count it
        m.buttons = in.buttons;
        s->sendMouse(m);
    }
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

void StreamPlayer::requestSnapshot(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->snapshotMutex);
    impl_->snapshotPath = path;
}
void StreamPlayer::stop() { impl_->stop(); }

void StreamPlayer::setHwDecodeProbe(bool on) { impl_->hwProbe = on; }

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
    if (auto s = impl_->current()) {
        const auto& v = s->videoStats();
        st.rtpPackets = v.packets;
        st.rtpLost = v.lost;
        st.rtpRecovered = v.recovered;
        st.rtpNacks = v.nacks;
        st.rtpDroppedFrames = v.framesDropped;
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
