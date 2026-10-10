// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/stream_player.h"

#include "display/display.h"
#include "input/controller.h"
#include "media/audio_out.h"
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
constexpr size_t kMaxQueuedFrames = 30;
constexpr int kMaxFullStreak = 30;  // frames shown one refresh late before dropping one
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
    std::deque<std::vector<uint8_t>> audioPackets_;
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
    int decodeThreads = 1;
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

    media::AudioIn audioIn;
    media::AudioEncoder audioEncoder;
    std::atomic<bool> micEnabled{true};
    std::atomic<bool> micMuted{false};
    std::atomic<float> micGain{1.0f};
    std::atomic<uint32_t> audioSendTimestamp{0};
    bool encoderInitialized = false;

    void updateMicState() {
        if (!running) return;
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
        audioIn.close();
        input::setEmbeddedMicActive(false);
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (endReason.empty()) endReason = reason;
        }
        running = false;
        cv.notify_all();
        audioCv.notify_all();
    }

    void audioLoop() {
        std::vector<uint8_t> packet;
        bool first = true;
        while (running) {
            {
                std::unique_lock<std::mutex> lock(mutex);
                audioCv.wait(lock, [&] { return !running || !audioPackets_.empty(); });
                if (!running) break;
                packet = std::move(audioPackets_.front());
                audioPackets_.pop_front();
            }
            pcm.clear();
            if (audioDecoder.decode(packet.data(), packet.size(), pcm)) media::audioPush(pcm.data(), pcm.size() / 2);
            if (first) {
                first = false;
                XC_LOGI("first audio packet decoded: %zu samples", pcm.size() / 2);
            }
        }
    }

    void videoLoop() {
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
        int fullStreak = 0;
        int skippedInRow = 0;
        constexpr uint64_t kShownRing = 8;
        std::pair<uint64_t, uint64_t> inScreenQueue[kShownRing] = {};  // present id, arrival us
        uint64_t lastShownId = 0;
        while (running) {
            size_t backlog;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&] { return !running || !frames.empty(); });
                if (!running) break;
                frame = std::move(frames.front());
                frames.pop_front();
                backlog = frames.size();
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
            pictureSize = (static_cast<uint32_t>(pic.width) << 16) | static_cast<uint32_t>(pic.height);
            bool screenshot = saveSnapshotIfAsked(pic);
            // A newer frame already waits: decode this one (the next needs it)
            // but show the newer one, a frame sooner. At most two in a row, so
            // the picture keeps moving when decoding runs behind.
            if (backlog > 0 && skippedInRow < 2) {
                ++skippedInRow;
                ++droppedFrames;
                continue;
            }
            skippedInRow = 0;
            std::lock_guard<std::mutex> lock(display::frameMutex());
            if (decodedFrames == 1) XC_LOGI("first draw %dx%d", pic.width, pic.height);
            uint64_t t2 = platform::nowUs();
            // Both flips queued means the picture shows a frame later than
            // it could. Arrival jitter does that now and then; it is waited
            // out (the queue then stays full: stream and display both run at
            // 60 Hz). After half a second of that, a frame is dropped to get
            // the latency back.
            bool drawn = display::drawYuv420(pic.y, pic.u, pic.v, pic.strideY, pic.strideU, pic.strideV,
                                             pic.width, pic.height, false);
            if (drawn) {
                fullStreak = 0;
            } else if (++fullStreak < kMaxFullStreak) {
                drawn = display::drawYuv420(pic.y, pic.u, pic.v, pic.strideY, pic.strideU, pic.strideV,
                                            pic.width, pic.height, true);
            } else {
                fullStreak = 0;
            }
            if (!drawn) {
                ++lateFrames;
                continue;
            }
            display::present();
            uint64_t t3 = platform::nowUs();
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
            {
                // The H.264 profile the server encodes with (from each new SPS).
                static int lastProfile = -1, lastLevel = -1;
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
        cb.audio = [this](const uint8_t* d, size_t n, uint32_t) {
            if (++audioPackets == 1) XC_LOGI("first audio packet: %zu bytes", n);
            std::lock_guard<std::mutex> lock(mutex);
            if (audioPackets_.size() < 50) audioPackets_.emplace_back(d, d + n);
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
        std::string data = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
        data.append(reinterpret_cast<const char*>(rgb.data()), rgb.size());
        bool ok = platform::writeFileAtomic(path, data);
        XC_LOGI("snapshot %dx%d -> %s: %s", w, h, path.c_str(), ok ? "ok" : "failed");
        return true;
    }

    // The scan-out buffer just presented, at full resolution.
    void saveScreen() {
        std::vector<uint8_t> rgb;
        int w = 0, h = 0;
        if (!display::readBackRgb(rgb, w, h)) return;
        std::string data = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
        data.append(reinterpret_cast<const char*>(rgb.data()), rgb.size());
        bool ok = platform::writeFileAtomic(screenPath, data);
        XC_LOGI("screen -> %s: %s", screenPath.c_str(), ok ? "ok" : "failed");
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
        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            session.reset();
        }
        audioIn.close();
        input::setEmbeddedMicActive(false);
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

}  // namespace xc::app
