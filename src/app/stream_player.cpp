#include "app/stream_player.h"

#include "display/display.h"
#include "media/audio_out.h"
#include "media/decoder.h"
#include "platform/platform.h"
#include "stream/stream_session.h"
#include "util/log.h"

#include <atomic>
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
    std::thread videoThread;

    media::AudioDecoder audioDecoder;
    std::vector<float> pcm;
    std::atomic<uint64_t> videoFrames{0}, decodedFrames{0}, droppedFrames{0}, audioPackets{0};
    std::atomic<bool> keyframeWanted{false};
    uint64_t lastKeyframeRequestMs = 0;

    explicit Impl(xcloud::GssvClient& g) : gssv(g) {}

    void end(const std::string& reason) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (endReason.empty()) endReason = reason;
        }
        running = false;
        cv.notify_all();
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
            media::Picture pic;
            bool ok = decoder.decode(au.data(), au.size(), pic);
            if (decoder.needsKeyframe()) keyframeWanted = true;
            requestKeyframeIfWanted();
            if (!ok) continue;
            ++decodedFrames;
            if (backlog > 2) {  // behind: skip drawing to catch up
                ++droppedFrames;
                continue;
            }
            std::lock_guard<std::mutex> lock(display::frameMutex());
            display::drawYuv420(pic.y, pic.u, pic.v, pic.strideY, pic.strideU, pic.strideV, pic.width, pic.height);
            display::present();
        }
    }

    bool start(std::string& err) {
        if (!audioDecoder.init()) XC_LOGW("no audio decoder: playing without sound");
        if (!media::audioStart()) XC_LOGW("no audio output: playing without sound");

        stream::StreamCallbacks cb;
        cb.video = [this](const uint8_t* d, size_t n, uint32_t) {
            ++videoFrames;
            std::lock_guard<std::mutex> lock(mutex);
            if (frames.size() >= kMaxQueuedFrames) {
                // Hopelessly behind: start over from the next key frame.
                frames.clear();
                keyframeWanted = true;
            }
            frames.emplace_back(d, d + n);
            cv.notify_one();
        };
        cb.audio = [this](const uint8_t* d, size_t n, uint32_t) {
            ++audioPackets;
            pcm.clear();  // audio callbacks come from a single thread
            if (audioDecoder.decode(d, n, pcm)) media::audioPush(pcm.data(), pcm.size() / 2);
        };
        cb.closed = [this](const std::string& reason) { end(reason); };

        session = std::make_unique<stream::StreamSession>(gssv, cb);
        running = true;
        videoThread = std::thread([this] { videoLoop(); });
        if (!session->start(err)) {
            end(err);
            return false;
        }
        return true;
    }

    // Video thread only (data channel sends are thread-safe).
    void requestKeyframeIfWanted() {
        uint64_t now = platform::nowMs();
        if (keyframeWanted && now - lastKeyframeRequestMs > 500 && session) {
            keyframeWanted = false;
            lastKeyframeRequestMs = now;
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
void StreamPlayer::stop() { impl_->stop(); }

StreamPlayer::Stats StreamPlayer::stats() const {
    return {impl_->videoFrames, impl_->decodedFrames, impl_->droppedFrames, impl_->audioPackets};
}

}  // namespace xc::app
