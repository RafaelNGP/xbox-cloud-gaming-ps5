// Host (Linux) command-line front end, used to validate sign-in, the title
// list and session provisioning on a PC before running on the console.
//
//   xcloud-cli login            sign in (device code) and show the profile
//   xcloud-cli titles [--recent] list streamable titles with names
//   xcloud-cli provision <id>   start a session, wait until it can be
//                               connected, run /connect, then stop it
//   xcloud-cli stream <id> [seconds] [out.h264]
//                               provision, connect WebRTC, receive video
//                               (optionally dumped as Annex-B H.264)
//   xcloud-cli bench-decode <file.h264> [threads]
//                               decode speed of a dumped stream
//   xcloud-cli logout
#include "auth/auth_manager.h"
#include "media/decoder.h"
#include "net/http.h"
#include "stream/stream_session.h"
#include "platform/platform.h"
#include "util/log.h"
#include "xcloud/gssv.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace xc;

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage: xcloud-cli [-v] <command>\n"
                 "  login                 sign in with a Microsoft account\n"
                 "  titles [--recent]     list streamable titles\n"
                 "  provision <titleId>   provision a cloud session, then stop it\n"
                 "  stream <titleId> [seconds] [out.h264]\n"
                 "                        provision and stream; dump video to a file\n"
                 "  bench-decode <file.h264> [threads]\n"
                 "                        measure H.264 decode speed\n"
                 "  logout                forget the stored account\n");
    return 2;
}

void showCode(const auth::DeviceCode& dc) {
    std::printf("\n==============================================\n");
    std::printf("  On your phone or PC, open:  %s\n", dc.verificationUri.c_str());
    std::printf("  and enter the code:         %s\n", dc.userCode.c_str());
    std::printf("==============================================\n\n");
    std::fflush(stdout);
}

bool signIn(auth::AuthManager& am, xcloud::GssvClient& gssv) {
    std::string err;
    if (!am.signIn(gssv, showCode, err)) {
        XC_LOGE("%s", err.c_str());
        return false;
    }
    std::printf("Signed in as %s (xuid %s), region %s\n", am.profile().gamertag.c_str(),
                am.profile().xuid.c_str(), gssv.region().name.c_str());
    return true;
}

int cmdTitles(auth::AuthManager& am, bool recent) {
    xcloud::GssvClient gssv;
    if (!signIn(am, gssv)) return 1;
    std::vector<xcloud::Title> titles;
    std::string err;
    if (!gssv.listTitles(titles, err, recent)) {
        XC_LOGE("%s", err.c_str());
        return 1;
    }
    std::string market = gssv.session().market.empty() ? "US" : gssv.session().market;
    if (!gssv.hydrateTitles(titles, market, "en-us", err)) XC_LOGW("%s", err.c_str());
    for (const auto& t : titles)
        std::printf("%-28s %-14s %s\n", t.titleId.c_str(), t.productId.c_str(),
                    t.name.empty() ? "?" : t.name.c_str());
    std::printf("%zu titles\n", titles.size());
    return 0;
}

// Starts a session for `titleId` and waits until it is Provisioned (queue,
// then /connect with the console-transfer token).
bool provision(auth::AuthManager& am, xcloud::GssvClient& gssv, const std::string& titleId) {
    std::string err;
    if (!gssv.startSession(titleId, "en-US", err)) {
        XC_LOGE("%s", err.c_str());
        return false;
    }
    bool connected = false;
    for (int i = 0; i < 600; ++i) {  // up to ~10 minutes of queue
        xcloud::SessionStatus st;
        if (!gssv.sessionState(st, err)) {
            XC_LOGE("%s", err.c_str());
            return false;
        }
        if (st.state == xcloud::SessionState::WaitingForResources) {
            XC_LOGI("in queue, estimated wait %ds", gssv.waitTimeSeconds());
        } else {
            XC_LOGI("session state: %s", st.raw.c_str());
        }
        if (st.state == xcloud::SessionState::Failed) {
            XC_LOGE("session failed: %s %s", st.errorCode.c_str(), st.errorMessage.c_str());
            return false;
        }
        if (st.state == xcloud::SessionState::ReadyToConnect && !connected) {
            std::string transfer;
            if (!am.consoleTransferToken(transfer, err) || !gssv.connect(transfer, err)) {
                XC_LOGE("%s", err.c_str());
                return false;
            }
            connected = true;
        }
        if (st.state == xcloud::SessionState::Provisioned) return true;
        platform::sleepMs(1000);
    }
    XC_LOGE("timed out waiting for the session");
    return false;
}

int cmdProvision(auth::AuthManager& am, const std::string& titleId) {
    xcloud::GssvClient gssv;
    if (!signIn(am, gssv)) return 1;
    bool ok = provision(am, gssv, titleId);
    if (ok) std::printf("Session provisioned.\n");
    gssv.stopSession();
    return ok ? 0 : 1;
}

int cmdStream(auth::AuthManager& am, const std::string& titleId, int seconds, const char* outPath) {
    xcloud::GssvClient gssv;
    if (!signIn(am, gssv)) return 1;
    if (!provision(am, gssv, titleId)) {
        gssv.stopSession();
        return 1;
    }

    FILE* out = outPath ? std::fopen(outPath, "wb") : nullptr;
    std::atomic<uint64_t> videoFrames{0}, videoBytes{0}, audioPackets{0}, keyFrames{0};
    std::atomic<bool> closed{false};
    stream::StreamCallbacks cb;
    cb.video = [&](const uint8_t* d, size_t n, uint32_t) {
        ++videoFrames;
        videoBytes += n;
        // An IDR slice (NAL type 5) after a 4-byte start code anywhere in the AU.
        for (size_t i = 0; i + 4 < n; ++i)
            if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1 && (d[i + 3] & 0x1f) == 5) {
                ++keyFrames;
                break;
            }
        if (out) std::fwrite(d, 1, n, out);
    };
    media::AudioDecoder audioDecoder;
    if (!audioDecoder.init()) return 1;
    std::atomic<uint64_t> audioSamples{0};
    std::vector<float> pcm;
    cb.audio = [&](const uint8_t* d, size_t n, uint32_t) {
        ++audioPackets;
        pcm.clear();
        if (audioDecoder.decode(d, n, pcm)) audioSamples += pcm.size() / 2;
    };
    cb.vibration = [](const stream::Vibration& v) {
        XC_LOGD("rumble %u/%u for %ums", v.leftMotor, v.rightMotor, v.durationMs);
    };
    cb.closed = [&](const std::string&) { closed = true; };

    int rc = 1;
    {
        stream::StreamSession session(gssv, cb);
        std::string err;
        if (!session.start(err)) {
            XC_LOGE("stream: %s", err.c_str());
        } else {
            std::printf("Streaming %s for %ds...\n", titleId.c_str(), seconds);
            uint64_t end = platform::nowMs() + static_cast<uint64_t>(seconds) * 1000u;
            uint64_t nextReport = platform::nowMs() + 1000;
            stream::GamepadFrame idle;
            while (platform::nowMs() < end && !closed) {
                session.sendGamepad(idle);
                if (platform::nowMs() >= nextReport) {
                    nextReport += 1000;
                    session.tick();
                    std::printf("video %llu frames (%llu key) %.1f MB, audio %llu packets (%.1f s decoded)\n",
                                static_cast<unsigned long long>(videoFrames.load()),
                                static_cast<unsigned long long>(keyFrames.load()), videoBytes.load() / 1048576.0,
                                static_cast<unsigned long long>(audioPackets.load()),
                                audioSamples.load() / double(media::AudioDecoder::kSampleRate));
                    std::fflush(stdout);
                }
                platform::sleepMs(16);
            }
            rc = videoFrames > 0 ? 0 : 1;
        }
        session.close();
    }
    if (out) std::fclose(out);
    gssv.stopSession();
    return rc;
}

// Splits an Annex-B dump at access unit delimiters / first slices with the
// FFmpeg parser and times the decoder on it.
int cmdBenchDecode(const char* path, int threads) {
    std::string data;
    if (!platform::readFile(path, data)) {
        XC_LOGE("cannot read %s", path);
        return 1;
    }
    // Access units start at each SPS or non-IDR/IDR slice with first_mb == 0;
    // the stream from xCloud starts every AU with an AUD or SPS, so splitting
    // at AUD (type 9) or SPS (type 7) not preceded by an AUD is enough.
    std::vector<std::pair<size_t, size_t>> aus;
    size_t start = std::string::npos;
    int prevType = -1;
    for (size_t i = 0; i + 4 < data.size(); ++i) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            int type = data[i + 3] & 0x1f;
            size_t sc = (i > 0 && data[i - 1] == 0) ? i - 1 : i;
            bool boundary = type == 9 || (type == 7 && prevType != 9) ||
                            ((type == 1 || type == 5) && prevType != 7 && prevType != 8 && prevType != 9 &&
                             prevType != 6 && (static_cast<uint8_t>(data[i + 4]) & 0x80));
            if (boundary) {
                if (start != std::string::npos) aus.emplace_back(start, sc - start);
                start = sc;
            }
            prevType = type;
            i += 3;
        }
    }
    if (start != std::string::npos) aus.emplace_back(start, data.size() - start);

    media::VideoDecoder dec;
    if (!dec.init(threads)) return 1;
    media::Picture pic;
    int pictures = 0;
    uint64_t t0 = platform::nowMs();
    for (const auto& [off, len] : aus)
        if (dec.decode(reinterpret_cast<const uint8_t*>(data.data()) + off, len, pic)) ++pictures;
    uint64_t ms = platform::nowMs() - t0;
    std::printf("%zu access units, %d pictures (%dx%d) in %llu ms: %.1f fps, %.2f ms/picture\n", aus.size(),
                pictures, pic.width, pic.height, static_cast<unsigned long long>(ms),
                ms ? pictures * 1000.0 / ms : 0.0, pictures ? double(ms) / pictures : 0.0);
    return pictures > 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    int argi = 1;
    if (argi < argc && std::strcmp(argv[argi], "-v") == 0) {
        log::setLevel(log::Level::Debug);
        ++argi;
    }
    if (argi >= argc) return usage();
    std::string cmd = argv[argi++];
    if (cmd == "bench-decode" && argi < argc)
        return cmdBenchDecode(argv[argi], argi + 1 < argc ? std::atoi(argv[argi + 1]) : 1);

    platform::init();
    if (!net::initTls(platform::caBundlePath())) return 1;
    auth::AuthManager am(platform::dataDir() + "/account.json");

    int rc;
    if (cmd == "login") {
        xcloud::GssvClient gssv;
        rc = signIn(am, gssv) ? 0 : 1;
    } else if (cmd == "titles") {
        rc = cmdTitles(am, argi < argc && std::strcmp(argv[argi], "--recent") == 0);
    } else if (cmd == "provision" && argi < argc) {
        rc = cmdProvision(am, argv[argi]);
    } else if (cmd == "stream" && argi < argc) {
        const char* id = argv[argi++];
        int seconds = argi < argc ? std::atoi(argv[argi++]) : 30;
        rc = cmdStream(am, id, seconds > 0 ? seconds : 30, argi < argc ? argv[argi] : nullptr);
    } else if (cmd == "logout") {
        am.signOut();
        std::printf("Signed out.\n");
        rc = 0;
    } else {
        rc = usage();
    }
    net::shutdownTls();
    platform::shutdown();
    return rc;
}
