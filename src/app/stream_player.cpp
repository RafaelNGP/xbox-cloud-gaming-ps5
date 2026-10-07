// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PSBox Cloud Gaming contributors
#include "app/stream_player.h"

#include "display/display.h"
#include "media/audio_out.h"
#include "media/decoder.h"
#include "platform/platform.h"
#include "stream/stream_session.h"
#include "util/log.h"

#include <atomic>
#include <cstdio>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace xc::app {

namespace {
constexpr size_t kMaxQueuedFrames = 8;  // beyond this, decode without drawing
}

struct StreamPlayer::Impl {
    xcloud::GssvClient& gssv;
    std::unique_ptr<stream::StreamSession> session;

    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::vector<uint8_t>> frames;
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
    bool loggedFirstDecode = false;
    FILE* dumpFile = nullptr;
    uint64_t dumpUntilMs = 0;
    std::mutex snapshotMutex;
    std::string snapshotPath;
    uint64_t lastKeyframeRequestMs = 0;

    explicit Impl(xcloud::GssvClient& g) : gssv(g) {}

    void end(const std::string& reason) {
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
        if (!decoder.init(1)) {
            end("could not start the H.264 decoder");
            return;
        }
        std::vector<uint8_t> au;
        while (running) {
            size_t backlog;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&] { return !running || !frames.empty(); });
                if (!running) break;
                au = std::move(frames.front());
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
            bool ok = decoder.decode(au.data(), au.size(), pic);
            if (first) {
                loggedFirstDecode = true;
                XC_LOGI("first video decode done: ok=%d", ok);
            }
            if (decoder.needsKeyframe()) keyframeWanted = true;
            requestKeyframeIfWanted();
            if (!ok) {
                ++decodeFailures;
                continue;
            }
            ++decodedFrames;
            saveSnapshotIfAsked(pic);
            if (backlog > 2) {  // behind: skip drawing to catch up
                ++droppedFrames;
                continue;
            }
            std::lock_guard<std::mutex> lock(display::frameMutex());
            if (decodedFrames == 1) XC_LOGI("first draw %dx%d", pic.width, pic.height);
            display::drawYuv420(pic.y, pic.u, pic.v, pic.strideY, pic.strideU, pic.strideV, pic.width, pic.height);
            display::present();
            if (decodedFrames == 1) XC_LOGI("first frame presented");
        }
    }

    bool start(std::string& err) {
        if (!audioDecoder.init()) XC_LOGW("no audio decoder: playing without sound");
        if (!media::audioStart()) XC_LOGW("no audio output: playing without sound");

        stream::StreamCallbacks cb;
        cb.video = [this](const uint8_t* d, size_t n, uint32_t) {
            if (n == 0) {  // the depacketizer emits these after loss
                keyframeWanted = true;
                return;
            }
            if (++videoFrames == 1) XC_LOGI("first video frame received: %zu bytes", n);
            std::lock_guard<std::mutex> lock(mutex);
            if (frames.size() >= kMaxQueuedFrames) {
                // Hopelessly behind: start over from the next key frame.
                ++queueResets;
                frames.clear();
                keyframeWanted = true;
            }
            frames.emplace_back(d, d + n);
            cv.notify_one();
        };
        cb.audio = [this](const uint8_t* d, size_t n, uint32_t) {
            if (++audioPackets == 1) XC_LOGI("first audio packet: %zu bytes", n);
            std::lock_guard<std::mutex> lock(mutex);
            if (audioPackets_.size() < 50) audioPackets_.emplace_back(d, d + n);
            audioCv.notify_one();
        };
        cb.closed = [this](const std::string& reason) { end(reason); };

        session = std::make_unique<stream::StreamSession>(gssv, cb);
        running = true;
        if (!platform::startThread(videoThread, [this] { videoLoop(); }) ||
            !platform::startThread(audioThread, [this] { audioLoop(); }, 1u << 20)) {
            err = "could not start the media threads";
            running = false;
            return false;
        }
        if (!session->start(err)) {
            end(err);
            return false;
        }
        return true;
    }

    void saveSnapshotIfAsked(const media::Picture& pic) {
        std::string path;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex);
            path.swap(snapshotPath);
        }
        if (path.empty()) return;
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
    }

    // Video thread only (data channel sends are thread-safe).
    void requestKeyframeIfWanted() {
        uint64_t now = platform::nowMs();
        if (keyframeWanted && now - lastKeyframeRequestMs > 500 && session) {
            keyframeWanted = false;
            lastKeyframeRequestMs = now;
            ++keyframeRequests;
            session->requestKeyframe();
        }
    }

    void tick() {
        if (session) session->tick();
    }

    void stop() {
        end("stopped");
        if (session) session->close();
        if (videoThread.joinable()) videoThread.join();
        if (audioThread.joinable()) audioThread.join();
        session.reset();
        media::audioStop();
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

void StreamPlayer::sendInput(const input::ControllerState& p) {
    if (!impl_->session || !impl_->running) return;
    stream::GamepadFrame f;
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
    f.leftX = p.leftStickX;
    f.leftY = -p.leftStickY;  // pad: +down; wire: +up
    f.rightX = p.rightStickX;
    f.rightY = -p.rightStickY;
    f.leftTrigger = p.triggerL2;
    f.rightTrigger = p.triggerR2;
    impl_->session->sendGamepad(f);
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

StreamPlayer::Stats StreamPlayer::stats() const {
    Stats st;
    st.videoFrames = impl_->videoFrames;
    st.decodedFrames = impl_->decodedFrames;
    st.droppedFrames = impl_->droppedFrames;
    st.audioPackets = impl_->audioPackets;
    st.decodeFailures = impl_->decodeFailures;
    st.queueResets = impl_->queueResets;
    st.keyframeRequests = impl_->keyframeRequests;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        st.queued = impl_->frames.size();
    }
    return st;
}

}  // namespace xc::app
