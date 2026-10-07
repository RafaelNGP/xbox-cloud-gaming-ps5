// Plays a provisioned xCloud session on the console: WebRTC session, H.264
// decode thread drawing to the screen, Opus to the audio output, and the
// DualSense state forwarded as an Xbox controller.
#pragma once

#include "input/controller.h"
#include "xcloud/gssv.h"

#include <memory>
#include <string>

namespace xc::app {

class StreamPlayer {
public:
    explicit StreamPlayer(xcloud::GssvClient& gssv);
    ~StreamPlayer();

    // Blocking: connects the stream and starts playback.
    bool start(std::string& err);
    bool running() const;
    std::string endReason() const;
    // From the input thread, once per polled pad state.
    void sendInput(const input::ControllerState& pad);
    // Keepalives etc.; about once a second from the thread owning `gssv`.
    void tick();
    void stop();

    struct Stats {
        uint64_t videoFrames = 0, decodedFrames = 0, droppedFrames = 0, audioPackets = 0;
    };
    Stats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace xc::app
