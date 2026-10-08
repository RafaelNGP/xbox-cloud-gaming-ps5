// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "stream/stream_session.h"

#include "stream/h264_receiver.h"
#include "stream/rtcp_reporter.h"

#include "platform/platform.h"
#include "util/json.h"
#include "util/log.h"

#include <rtc/rtc.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <vector>

namespace xc::stream {

namespace {

// Fixed values the xbox.com web client sends.
constexpr const char* kControlAccessKey = "4BDB3609-C1F1-4195-9B37-FEFF45DA8B8E";
constexpr const char* kHandshakeId = "be0bfc6d-1e83-4c8a-90ed-fa8601c5a179";
constexpr const char* kClientAppInstallId = "c97d7ee0-73b2-4239-bf1d-9d805a338429";
// SystemUiType of the system UIs this client draws itself.
constexpr int kShowVirtualKeyboard = 10;

std::string uuid4() {
    uint8_t b[16];
    platform::randomBytes(b, sizeof b);
    b[6] = static_cast<uint8_t>((b[6] & 0x0f) | 0x40);
    b[8] = static_cast<uint8_t>((b[8] & 0x3f) | 0x80);
    char s[37];
    std::snprintf(s, sizeof s, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1],
                  b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return s;
}

std::vector<std::string> midsInOrder(const std::string& sdp) {
    std::vector<std::string> mids;
    size_t pos = 0;
    while ((pos = sdp.find("a=mid:", pos)) != std::string::npos) {
        pos += 6;
        size_t end = sdp.find_first_of("\r\n", pos);
        mids.push_back(sdp.substr(pos, end - pos));
    }
    return mids;
}

// a=imageattr after every a=fmtp line: the largest picture we accept. The
// xbox.com client adds "recv [x=[0:1920],y=[0:1080],fps=[0:60]]" to limit
// devices that can't do 1440p; announcing 2560x1440 is what allows 1440p.
std::string advertiseReceiveSize(const std::string& sdp, int w, int h) {
    if (sdp.find("a=imageattr") != std::string::npos) return sdp;
    std::string out;
    size_t pos = 0;
    while (pos < sdp.size()) {
        size_t eol = sdp.find('\n', pos);
        if (eol == std::string::npos) eol = sdp.size() - 1;
        std::string line = sdp.substr(pos, eol - pos + 1);
        out += line;
        if (line.rfind("a=fmtp:", 0) == 0 && line.find("profile-level-id") != std::string::npos) {
            std::string pt = line.substr(7, line.find(' ') - 7);
            out += "a=imageattr:" + pt + " recv [x=[0:" + std::to_string(w) + "],y=[0:" + std::to_string(h) +
                   "],fps=[0:60]]\r\n";
        }
        pos = eol + 1;
    }
    return out;
}

void initRtcLogging() {
    static std::once_flag once;
    std::call_once(once, [] {
        rtc::InitLogger(rtc::LogLevel::Warning, [](rtc::LogLevel level, std::string message) {
            if (level <= rtc::LogLevel::Error)
                XC_LOGE("rtc: %s", message.c_str());
            else
                XC_LOGW("rtc: %s", message.c_str());
        });
    });
}

}  // namespace

bool decodeTeredo(const std::string& ipv6, std::string& ipv4, int& port) {
    in6_addr a{};
    if (::inet_pton(AF_INET6, ipv6.c_str(), &a) != 1) return false;
    const uint8_t* b = a.s6_addr;
    if (!(b[0] == 0x20 && b[1] == 0x01 && b[2] == 0x00 && b[3] == 0x00)) return false;
    // Bytes 10-11: obfuscated (inverted) port; 12-15: inverted client IPv4.
    port = static_cast<uint16_t>(~((b[10] << 8) | b[11]));
    char s[16];
    std::snprintf(s, sizeof s, "%u.%u.%u.%u", b[12] ^ 0xff, b[13] ^ 0xff, b[14] ^ 0xff, b[15] ^ 0xff);
    ipv4 = s;
    return true;
}

struct StreamSession::Impl {
    xcloud::GssvClient& gssv;
    StreamCallbacks cb;
    StreamOptions opt;

    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> chat, control, input, message;
    std::shared_ptr<rtc::Track> audio, video;

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<rtc::Candidate> localCandidates;
    bool gatheringDone = false;
    bool handshakeDone = false;
    bool failed = false;
    std::string failReason;

    std::atomic<bool> open{false};
    std::atomic<bool> closedNotified{false};
    std::atomic<uint32_t> inputSequence{1};
    uint64_t lastKeepaliveMs = 0;
    uint64_t lastKeyframeMs = 0;
    VideoReceiveStats videoStats;
    std::shared_ptr<RtcpReporter> videoReporter;
    std::atomic<int> otherInputReports{0};

    Impl(xcloud::GssvClient& g, StreamCallbacks c, StreamOptions o) : gssv(g), cb(std::move(c)), opt(o) {}

    double nowMs() const { return stream::clockMs(); }

    void fail(const std::string& reason, bool recoverable = true) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!failed) {
                failed = true;
                failReason = reason;
            }
        }
        cv.notify_all();
        open = false;
        if (!closedNotified.exchange(true)) {
            XC_LOGW("stream closed: %s", reason.c_str());
            if (cb.closed) cb.closed(reason, recoverable);
        }
    }

    static void sendText(const std::shared_ptr<rtc::DataChannel>& ch, const std::string& text) {
        // The web client sends its JSON as binary (TextEncoder) messages.
        if (!ch || !ch->isOpen()) return;
        try {
            ch->send(reinterpret_cast<const rtc::byte*>(text.data()), text.size());
        } catch (const std::exception& e) {
            XC_LOGW("send on %s: %s", ch->label().c_str(), e.what());
        }
    }

    static void sendBinary(const std::shared_ptr<rtc::DataChannel>& ch, const std::vector<uint8_t>& data) {
        if (!ch || !ch->isOpen()) return;
        try {
            ch->send(reinterpret_cast<const rtc::byte*>(data.data()), data.size());
        } catch (const std::exception& e) {
            XC_LOGW("send on %s: %s", ch->label().c_str(), e.what());
        }
    }

    static std::string asText(const rtc::message_variant& m) {
        if (auto s = std::get_if<std::string>(&m)) return *s;
        const auto& b = std::get<rtc::binary>(m);
        return std::string(reinterpret_cast<const char*>(b.data()), b.size());
    }

    // --- message channel -----------------------------------------------------

    std::string messageEnvelope(const std::string& target, const json::Value& content) {
        json::Value v = json::Value::object();
        v.set("type", "Message");
        v.set("content", content.dump());
        v.set("id", uuid4());
        v.set("target", target);
        v.set("cv", "");
        return v.dump();
    }

    void completeTransaction(const std::string& id, const json::Value& content) {
        json::Value v = json::Value::object();
        v.set("type", "TransactionComplete");
        v.set("content", content.dump());
        v.set("id", id);
        v.set("cv", "");
        sendText(message, v.dump());
    }

    void cancelTransaction(const std::string& id) {
        json::Value v = json::Value::object();
        v.set("type", "ReceiverCancel");
        v.set("id", id);
        v.set("cv", "");
        sendText(message, v.dump());
    }

    void sendResolution(const std::string& alias) {
        // The HQ tiers and 1440 need Game Pass Ultimate in a market where
        // they're enabled; elsewhere the service ignores them and keeps what
        // it had (1080 at the start). The plain tier first, then: measured,
        // "720" switches in a few seconds, "720HQ" is ignored in Brazil.
        std::string base = alias == "1440" ? "1080" : alias;
        if (base.size() > 2 && base.compare(base.size() - 2, 2, "HQ") == 0) base.resize(base.size() - 2);
        // The user's own Xbox offers every tier on any plan: just the one asked for.
        if (gssv.isHome()) base = alias;
        auto send = [this](const std::string& a) {
            json::Value res = json::Value::object();
            res.set("message", "userRequestedResolutionUpdate");
            res.set("resolutionAlias", a);
            sendText(control, res.dump());
            XC_LOGI("requested stream resolution %s", a.c_str());
        };
        send(base);
        if (alias != base) send(alias);
    }

    void sendClientConfig() {
        json::Value ver = json::Value::array();
        ver.push(0);
        ver.push(2);
        ver.push(0);
        json::Value ui = json::Value::object();
        ui.set("version", ver);
        json::Value systemUis = json::Value::array();
        if (cb.textInput) systemUis.push(kShowVirtualKeyboard);
        ui.set("systemUis", systemUis);
        sendText(message, messageEnvelope("/streaming/systemUi/configuration", ui));

        json::Value install = json::Value::object();
        install.set("clientAppInstallId", kClientAppInstallId);
        sendText(message, messageEnvelope("/streaming/properties/clientappinstallidchanged", install));

        json::Value orientation = json::Value::object();
        orientation.set("orientation", 0);
        sendText(message, messageEnvelope("/streaming/characteristics/orientationchanged", orientation));

        json::Value touch = json::Value::object();
        touch.set("touchInputEnabled", false);
        sendText(message, messageEnvelope("/streaming/characteristics/touchinputenabledchanged", touch));

        sendText(message, messageEnvelope("/streaming/characteristics/clientdevicecapabilities", json::Value::object()));

        // The picture's size, as the web client sends its video element's:
        // a 1440p tier needs a 1440p one (the display itself is 4K).
        int w = opt.resolutionAlias == "1440" ? 2560 : 1920, h = opt.resolutionAlias == "1440" ? 1440 : 1080;
        json::Value dims = json::Value::object();
        dims.set("horizontal", w);
        dims.set("vertical", h);
        dims.set("preferredWidth", w);
        dims.set("preferredHeight", h);
        dims.set("safeAreaLeft", 0);
        dims.set("safeAreaTop", 0);
        dims.set("safeAreaRight", w);
        dims.set("safeAreaBottom", h);
        dims.set("supportsCustomResolution", true);
        sendText(message, messageEnvelope("/streaming/characteristics/dimensionschanged", dims));
    }

    void sendGamepadChanged(int index, bool added) {
        json::Value v = json::Value::object();
        v.set("message", "gamepadChanged");
        v.set("gamepadIndex", index);
        v.set("wasAdded", added);
        sendText(control, v.dump());
    }

    void onHandshakeAck() {
        XC_LOGI("message channel handshake complete");
        json::Value auth = json::Value::object();
        auth.set("message", "authorizationRequest");
        auth.set("accessKey", kControlAccessKey);
        sendText(control, auth.dump());
        // Same sequence as the web client: reset slot 0, then attach the pad.
        sendGamepadChanged(0, true);
        sendGamepadChanged(0, false);
        sendGamepadChanged(0, true);
        if (!opt.resolutionAlias.empty()) sendResolution(opt.resolutionAlias);


        // One touch point, as the web client announces (touch is then turned
        // on only while a touch screen is up: setTouchEnabled()).
        sendBinary(input, clientMetadataReport(0, nowMs(), static_cast<uint8_t>(std::getenv("XC_TOUCH_POINTS") ? std::atoi(std::getenv("XC_TOUCH_POINTS")) : 1)));
        sendClientConfig();
        {
            std::lock_guard<std::mutex> lock(mutex);
            handshakeDone = true;
        }
        open = true;
        cv.notify_all();
    }

    void onMessageChannel(const rtc::message_variant& m) {
        std::string text = asText(m);
        auto j = json::parse(text);
        if (!j) {
            XC_LOGW("message channel: unparsable %s", text.substr(0, 200).c_str());
            return;
        }
        std::string type = (*j)["type"].str();
        if (type == "HandshakeAck") {
            onHandshakeAck();
            return;
        }
        if (type == "SenderCancel") {
            XC_LOGI("message channel: the server withdrew %s", (*j)["id"].str().c_str());
            if (cb.textInputCancelled) cb.textInputCancelled((*j)["id"].str());
            return;
        }
        if (type != "Message" && type != "TransactionStart") {
            XC_LOGD("message channel: %s", text.substr(0, 300).c_str());
            return;
        }
        std::string target = (*j)["target"].str();
        std::string id = (*j)["id"].str();
        XC_LOGI("message %s %s", type.c_str(), target.c_str());
        if (target == "/streaming/properties/titleinfo") {
            auto info = json::parse((*j)["content"].str());
            if (info && cb.titleFocus) cb.titleFocus((*info)["focused"].asBool());
        }
        if (target == "/streaming/sessionLifetimeManagement/serverInitiatedDisconnect") {
            std::string body = (*j)["content"].str();
            XC_LOGW("server disconnect: %s", body.substr(0, 300).c_str());
            completeTransaction(id, json::Value(""));
            // A warning shares this message with the real kicks: the session
            // goes on (the xbox.com client only shows a notice).
            auto content = json::parse(body);
            std::string reason = content ? (*content)["reason"].str() : std::string();
            if (reason == "WarningForBeingIdle") {
                if (cb.idleWarning) cb.idleWarning(static_cast<int>((*content)["secondsUntilKick"].asInt(120)));
            } else if (reason == "KickForClosedGame") {
                // The game itself quit (or failed to start) on the server.
                char hr[16];
                std::snprintf(hr, sizeof hr, "0x%08X", static_cast<unsigned>((*content)["hr"].asInt()));
                fail(std::string("the game closed on the server (") + hr + ")", false);
            } else {
                fail(reason.empty() ? "the server ended the session" : "the server ended the session (" + reason + ")",
                     false);
            }
        } else if (target == "/streaming/systemUi/messages/ShowVirtualKeyboard" && cb.textInput) {
            auto content = json::parse((*j)["content"].str());
            TextInputRequest req;
            req.id = id;
            if (content) {
                req.title = (*content)["TitleText"].str();
                req.description = (*content)["DescriptionText"].str();
                req.defaultText = (*content)["DefaultText"].str();
                req.inputScope = static_cast<int>((*content)["InputScope"].asInt());
                req.maxLength = static_cast<int>((*content)["MaxLength"].asInt());
            }
            XC_LOGI("the game asks for text (scope %d, up to %d characters)", req.inputScope, req.maxLength);
            cb.textInput(req);
        } else if (target == "/streaming/systemUi/messages/ShowMessageDialog") {
            // No dialog UI yet: log it and pick the first (default) button.
            auto content = json::parse((*j)["content"].str());
            if (content)
                XC_LOGW("server dialog: %s / %s", (*content)["TitleText"].str().c_str(),
                        (*content)["ContentText"].str().c_str());
            json::Value result = json::Value::object();
            result.set("Result", 0);
            completeTransaction(id, result);
        }
    }

    // --- set-up ---------------------------------------------------------------

    std::shared_ptr<rtc::DataChannel> makeChannel(const char* label, const char* protocol) {
        rtc::DataChannelInit init;
        init.protocol = protocol;
        auto ch = pc->createDataChannel(label, init);
        ch->onOpen([label] { XC_LOGI("data channel %s open", label); });
        ch->onClosed([this, label] {
            XC_LOGI("data channel %s closed", label);
            if (open) fail(std::string("data channel ") + label + " closed");
        });
        return ch;
    }

    void createPeer() {
        initRtcLogging();
        rtc::Configuration config;
        config.disableAutoNegotiation = true;
        pc = std::make_shared<rtc::PeerConnection>(config);

        pc->onLocalCandidate([this](rtc::Candidate c) {
            std::lock_guard<std::mutex> lock(mutex);
            localCandidates.push_back(std::move(c));
        });
        pc->onGatheringStateChange([this](rtc::PeerConnection::GatheringState s) {
            if (s == rtc::PeerConnection::GatheringState::Complete) {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    gatheringDone = true;
                }
                cv.notify_all();
            }
        });
        pc->onStateChange([this](rtc::PeerConnection::State s) {
            XC_LOGI("peer connection state: %d", static_cast<int>(s));
            if (s == rtc::PeerConnection::State::Failed) fail("WebRTC connection failed");
            if (s == rtc::PeerConnection::State::Disconnected) fail("WebRTC connection lost");
        });

        // Same channels, in the same order, as the web client.
        chat = makeChannel("chat", "chatV1");
        control = makeChannel("control", "controlV1");
        input = makeChannel("input", "1.0");
        message = makeChannel("message", "messageV1");

        message->onOpen([this] {
            XC_LOGI("data channel message open: sending handshake");
            json::Value hs = json::Value::object();
            hs.set("type", "Handshake");
            hs.set("version", "messageV1");
            hs.set("id", kHandshakeId);
            hs.set("cv", "0");
            sendText(message, hs.dump());
        });
        message->onMessage([this](rtc::message_variant m) { onMessageChannel(m); });
        control->onMessage([](rtc::message_variant m) { XC_LOGD("control: %s", asText(m).substr(0, 300).c_str()); });
        input->onMessage([this](rtc::message_variant m) {
            auto b = std::get_if<rtc::binary>(&m);
            if (!b) return;
            auto* d = reinterpret_cast<const uint8_t*>(b->data());
            Vibration v;
            uint32_t w, h;
            if (parseVibration(d, b->size(), v)) {
                if (cb.vibration) cb.vibration(v);
            } else if (parseServerMetadata(d, b->size(), w, h)) {
                XC_LOGI("server video size %ux%u", w, h);
            } else if (b->size() >= 2 && ++otherInputReports <= 5) {
                XC_LOGI("input channel: report type %u, %zu bytes", d[0], b->size());
            }
        });

        // Audio: sendrecv like the browser (the send side carries chat later).
        rtc::Description::Audio a("0", rtc::Description::Direction::SendRecv);
        a.addOpusCodec(111, "minptime=10;useinbandfec=1;stereo=1");
        audio = pc->addTrack(a);
        auto audioDepacketizer = std::make_shared<rtc::OpusRtpDepacketizer>();
        audioDepacketizer->addToChain(std::make_shared<RtcpReporter>(48000, 0));
        audio->setMediaHandler(audioDepacketizer);
        audio->onFrame([this](rtc::binary data, rtc::FrameInfo info) {
            if (cb.audio) cb.audio(reinterpret_cast<const uint8_t*>(data.data()), data.size(), info.timestamp);
        });

        // Video: H.264 only, Main before Constrained Baseline / Baseline.
        rtc::Description::Video v("1", rtc::Description::Direction::RecvOnly);
        // Main profile at level 5.1 (0x33): the level that allows 1440p60; with
        // 3.1 the service tops out at 1080p (it answers level 4.2).
        v.addH264Codec(102, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=4d0033");
        v.addH264Codec(104, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");
        v.addH264Codec(106, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f");
        video = pc->addTrack(v);
        // Our jitter buffer (NACK, whole frames only) in place of
        // H264RtpDepacketizer, which hands over frames with holes in them.
        auto videoReceiver = std::make_shared<H264Receiver>(
            [this] {
                if (!open) return;
                try {
                    requestKeyframe();
                } catch (const std::exception& e) {
                    XC_LOGW("keyframe request: %s", e.what());
                }
            },
            videoStats);
        videoReceiver->setSimulatedLoss(opt.simulatedVideoLoss);
        videoReporter = std::make_shared<RtcpReporter>(90000, opt.maxBitrate);
        videoReceiver->addToChain(videoReporter);
        video->setMediaHandler(videoReceiver);
        video->onFrame([this](rtc::binary data, rtc::FrameInfo info) {
            if (cb.video) cb.video(reinterpret_cast<const uint8_t*>(data.data()), data.size(), info.timestamp);
        });
    }

    bool waitFor(const std::function<bool()>& pred, int timeoutMs) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return failed || pred(); }) && !failed;
    }

    void addRemoteCandidate(const std::string& candidate, const std::string& mid) {
        std::string c = candidate;
        while (!c.empty() && (c.back() == ' ' || c.back() == '\r' || c.back() == '\n')) c.pop_back();
        try {
            pc->addRemoteCandidate(rtc::Candidate(c, mid));
            XC_LOGD("remote candidate added");  // not its address
        } catch (const std::exception& e) {
            XC_LOGW("ignoring a remote candidate: %s", e.what());
        }
    }

    bool start(std::string& err) {
        createPeer();
        pc->setLocalDescription(rtc::Description::Type::Offer);
        if (!waitFor([this] { return gatheringDone; }, 10000))
            XC_LOGW("ICE gathering did not complete; continuing with %zu candidates", localCandidates.size());

        auto local = pc->localDescription();
        if (!local) {
            err = "no local SDP offer";
            return false;
        }
        std::string offer = std::string(*local);
        if (opt.resolutionAlias == "1440") offer = advertiseReceiveSize(offer, 2560, 1440);
        XC_LOGD("local offer:\n%s", offer.c_str());
        if (!gssv.sendSdpOffer(offer, err)) return false;

        std::string answer;
        for (int i = 0; i < 60 && answer.empty(); ++i) {
            if (!gssv.pollSdpAnswer(answer, err)) return false;
            if (answer.empty()) platform::sleepMs(500);
        }
        if (answer.empty()) {
            err = "timed out waiting for the SDP answer";
            return false;
        }
        XC_LOGD("remote answer:\n%s", answer.c_str());
        for (size_t p = answer.find("a=rtpmap:"); p != std::string::npos; p = answer.find("a=rtpmap:", p + 1))
            XC_LOGI("answer %s", answer.substr(p, answer.find_first_of("\r\n", p) - p).c_str());
        try {
            pc->setRemoteDescription(rtc::Description(answer, rtc::Description::Type::Answer));
        } catch (const std::exception& e) {
            err = std::string("remote SDP rejected: ") + e.what();
            return false;
        }

        // Local candidates -> gssv, with their m-line index in our offer.
        std::vector<std::string> mids = midsInOrder(offer);
        std::vector<xcloud::IceCandidate> mine;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (const auto& c : localCandidates) {
                xcloud::IceCandidate ic;
                ic.candidate = std::string(c);  // "candidate:...", as a browser sends
                ic.sdpMid = c.mid();
                for (size_t i = 0; i < mids.size(); ++i)
                    if (mids[i] == ic.sdpMid) ic.sdpMLineIndex = static_cast<int>(i);
                mine.push_back(std::move(ic));
            }
        }
        XC_LOGI("sending %zu local ICE candidates", mine.size());
        if (!gssv.sendIceCandidates(mine, err)) return false;

        std::vector<xcloud::IceCandidate> theirs;
        for (int i = 0; i < 30 && theirs.empty(); ++i) {
            if (!gssv.pollIceCandidates(theirs, err)) return false;
            if (theirs.empty()) platform::sleepMs(1000);
        }
        if (theirs.empty()) {
            err = "the server sent no ICE candidates";
            return false;
        }
        std::vector<std::string> remoteMids = midsInOrder(answer);
        std::string bundleMid = remoteMids.empty() ? "0" : remoteMids.front();
        for (const auto& c : theirs) {
            if (c.candidate.find("end-of-candidates") != std::string::npos) continue;
            std::string mid = c.sdpMid.empty() ? bundleMid : c.sdpMid;
            // The address field of "candidate:<f> <c> <proto> <prio> <addr> <port> ...".
            std::vector<std::string> parts;
            size_t p = 0;
            while (p < c.candidate.size()) {
                size_t e = c.candidate.find(' ', p);
                if (e == std::string::npos) e = c.candidate.size();
                parts.push_back(c.candidate.substr(p, e - p));
                p = e + 1;
            }
            std::string ipv4;
            int port = 0;
            if (parts.size() > 4 && decodeTeredo(parts[4], ipv4, port)) {
                // The console's public address: not in the log.
                XC_LOGI("teredo candidate -> port %d (and 9002)", port);
                addRemoteCandidate("a=candidate:10 1 UDP 1 " + ipv4 + " 9002 typ host", mid);
                addRemoteCandidate("a=candidate:11 1 UDP 1 " + ipv4 + " " + std::to_string(port) + " typ host", mid);
            }
            addRemoteCandidate(c.candidate, mid);
        }

        if (!waitFor([this] { return handshakeDone; }, opt.connectTimeoutMs)) {
            std::lock_guard<std::mutex> lock(mutex);
            err = failed ? failReason : "timed out connecting the stream";
            return false;
        }
        lastKeepaliveMs = lastKeyframeMs = platform::nowMs();
        return true;
    }

    void requestKeyframe() {
        json::Value v = json::Value::object();
        v.set("message", "videoKeyframeRequested");
        v.set("ifrRequested", true);
        sendText(control, v.dump());
    }

    void tick() {
        if (!open) return;
        uint64_t now = platform::nowMs();
        if (now - lastKeepaliveMs >= 30000) {
            lastKeepaliveMs = now;
            std::string err;
            if (!gssv.keepalive(err)) XC_LOGW("%s", err.c_str());
        }
        if (opt.keyframeIntervalSec > 0 && now - lastKeyframeMs >= static_cast<uint64_t>(opt.keyframeIntervalSec) * 1000u) {
            lastKeyframeMs = now;
            requestKeyframe();
        }
    }

    void close() {
        open = false;
        if (pc) {
            try {
                pc->close();
            } catch (const std::exception& e) {
                XC_LOGW("closing peer connection: %s", e.what());
            }
        }
    }
};

StreamSession::StreamSession(xcloud::GssvClient& gssv, StreamCallbacks callbacks, StreamOptions options)
    : impl_(std::make_unique<Impl>(gssv, std::move(callbacks), options)) {}

StreamSession::~StreamSession() {
    impl_->closedNotified = true;  // no callback while tearing down
    impl_->close();
}

bool StreamSession::start(std::string& err) {
    // libdatachannel reports set-up failures (e.g. no usable socket) by throwing.
    try {
        return impl_->start(err);
    } catch (const std::exception& e) {
        err = std::string("WebRTC: ") + e.what();
    } catch (...) {
        err = "WebRTC: unknown error";
    }
    impl_->close();
    return false;
}
bool StreamSession::isOpen() const { return impl_->open; }

void StreamSession::sendGamepad(const GamepadFrame& frame) {
    if (!impl_->open) return;
    Impl::sendBinary(impl_->input, gamepadReport(impl_->inputSequence++, impl_->nowMs(), frame));
}

void StreamSession::setTouchEnabled(bool on) {
    if (!impl_->open) return;
    json::Value touch = json::Value::object();
    touch.set("touchInputEnabled", on);
    Impl::sendText(impl_->message, impl_->messageEnvelope("/streaming/characteristics/touchinputenabledchanged", touch));
}

void StreamSession::setGamepadConnected(int index, bool connected) {
    if (!impl_->open) return;
    XC_LOGI("gamepad %d %s", index, connected ? "attached" : "detached");
    impl_->sendGamepadChanged(index, connected);
}

void StreamSession::requestKeyframe() {
    try {
        impl_->requestKeyframe();
    } catch (const std::exception& e) {
        XC_LOGW("keyframe request: %s", e.what());
    }
}
void StreamSession::reportFrame(const FrameMetadata& frame) {
    if (!impl_->open) return;
    Impl::sendBinary(impl_->input, metadataReport(impl_->inputSequence++, impl_->nowMs(), {frame}));
}

double StreamSession::clockMs() const { return stream::clockMs(); }

void StreamSession::simulateDrop() {
    XC_LOGW("test: dropping the connection");
    if (impl_->pc) impl_->pc->close();
}

double clockMs() {
    static const auto epoch = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - epoch).count();
}

void StreamSession::completeTextInput(const std::string& id, const std::string& text) {
    json::Value v = json::Value::object();
    v.set("Text", text);
    impl_->completeTransaction(id, v);
}

void StreamSession::cancelTextInput(const std::string& id) { impl_->cancelTransaction(id); }

void StreamSession::requestResolution(const std::string& alias) { impl_->sendResolution(alias); }

void StreamSession::tick() { impl_->tick(); }
void StreamSession::close() { impl_->close(); }
int StreamSession::rttMs() const {
    try {
        if (impl_->pc)
            if (auto rtt = impl_->pc->rtt()) return static_cast<int>(rtt->count());
    } catch (const std::exception&) {
    }
    return -1;
}

const VideoReceiveStats& StreamSession::videoStats() const {
    if (impl_->videoReporter) {
        impl_->videoStats.receiveRate = impl_->videoReporter->receiveRate();
        impl_->videoStats.estimate = impl_->videoReporter->estimate();
    }
    return impl_->videoStats;
}

}  // namespace xc::stream
