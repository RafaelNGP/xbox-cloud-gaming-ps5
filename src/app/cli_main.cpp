// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
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
//   xcloud-cli ui-preview <dir>  render the menus to PNGs with live data
//   xcloud-cli render-icon <out.png> [size]
//                               the PS5 home-screen icon (icon0.png: 512)
//   xcloud-cli logout
#include "app/library.h"
#include "auth/auth_manager.h"
#include "media/decoder.h"
#include "net/http.h"
#include "stream/stream_session.h"
#include "platform/platform.h"
#include "util/log.h"
#include "ui/app_ui.h"
#include "ui/brand.h"
#include "ui/strings.h"
#include "xcloud/gssv.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
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
                 "  ui-preview <dir>      render every menu screen to <dir>/*.png\n"
                 "  render-icon <out.png> [size]  draw the app icon (512 for icon0.png)\n"
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

int cmdStream(auth::AuthManager& am, const std::string& titleId, int seconds, const char* outPath,
              xcloud::Resolution res, const stream::StreamOptions& opts, const std::string& region = {}) {
    xcloud::GssvClient gssv;
    gssv.setResolution(res);
    if (!signIn(am, gssv)) return 1;
    for (const auto& r : gssv.session().regions) {
        XC_LOGD("region %s%s", r.name.c_str(), r.isDefault ? " (default)" : "");
        if (!region.empty() && strcasecmp(r.name.c_str(), region.c_str()) == 0) {
            gssv.setRegion(r);
            XC_LOGI("using region %s (%s)", r.name.c_str(), r.baseUri.c_str());
        }
    }
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
        XC_LOGI("rumble %u/%u for %ums", v.leftMotor, v.rightMotor, v.durationMs);
    };
    cb.closed = [&](const std::string&) { closed = true; };

    int rc = 1;
    {
        stream::StreamSession session(gssv, cb, opts);
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
                    const auto& rtp = session.videoStats();
                    std::printf("video %llu frames (%llu key) %.1f MB, audio %llu packets (%.1f s decoded); "
                                "rtp %llu pkts, %llu lost, %llu recovered, %llu nacks, %llu dropped, %llu kf req, %u kbps (remb %u)\n",
                                static_cast<unsigned long long>(videoFrames.load()),
                                static_cast<unsigned long long>(keyFrames.load()), videoBytes.load() / 1048576.0,
                                static_cast<unsigned long long>(audioPackets.load()),
                                audioSamples.load() / double(media::AudioDecoder::kSampleRate),
                                static_cast<unsigned long long>(rtp.packets.load()),
                                static_cast<unsigned long long>(rtp.lost.load()),
                                static_cast<unsigned long long>(rtp.recovered.load()),
                                static_cast<unsigned long long>(rtp.nacks.load()),
                                static_cast<unsigned long long>(rtp.framesDropped.load()),
                                static_cast<unsigned long long>(rtp.keyframeRequests.load()),
                                rtp.receiveRate.load() / 1000, rtp.estimate.load() / 1000);
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
    std::vector<std::pair<size_t, size_t>> aus;
    if (std::string(path).size() > 4 && std::string(path).substr(std::string(path).size() - 4) == ".aus") {
        // Length-prefixed access units as received on the console.
        for (size_t p = 0; p + 4 <= data.size();) {
            uint32_t n;
            std::memcpy(&n, data.data() + p, 4);
            if (p + 4 + n > data.size()) break;
            aus.emplace_back(p + 4, n);
            p += 4 + n;
        }
    } else {
        aus = media::splitAccessUnits(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }

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

// Renders each screen of the console UI with live account data, so the
// layout can be checked on a PC.
int cmdUiPreview(auth::AuthManager& am, const std::string& dir) {
    ui::Fonts fonts;
    if (!fonts.load("assets/fonts")) return 1;
    ui::ImageCache images([] {});
    ui::AppUi app(fonts, images);
    ui::Canvas canvas(1920, 1080);
    uint64_t t = 1000;
    auto save = [&](const char* name, int settleMs = 0) {
        // Let images arrive and animations settle.
        for (int waited = 0; waited <= settleMs; waited += 250) {
            app.render(canvas, t += 250);
            if (settleMs) platform::sleepMs(250);
        }
        app.render(canvas, t += 400);
        std::string path = dir + "/" + name + ".png";
        stbi_write_png(path.c_str(), canvas.width(), canvas.height(), 4, canvas.data(), canvas.width() * 4);
        std::printf("wrote %s\n", path.c_str());
    };

    app.showSplash(ui::tr(ui::Str::SigningIn));
    save("splash");
    app.showSignIn("A1B2C3D4", "https://www.microsoft.com/link");
    save("signin");

    xcloud::GssvClient gssv;
    if (!signIn(am, gssv)) return 1;
    app.setProfile(am.profile().gamertag, am.profile().gamerpicUrl);
    std::string err;
    app::Library library;
    auto onRows = [&](const std::vector<ui::GameRow>& rows) { app.setRows(rows); };
    if (!library.load(gssv, ui::catalogLanguage(), onRows, err)) {
        XC_LOGE("%s", err.c_str());
        return 1;
    }
    library.hydrate(onRows);
    app.showHome();
    save("home", 4000);
    ui::NavInput right;
    right.right = true;
    for (int i = 0; i < 3; ++i) app.handle(right);
    ui::NavInput down;
    down.down = true;
    app.handle(down);
    save("home_nav", 3000);
    ui::NavInput accept;
    accept.accept = true;
    app.handle(accept);
    save("details", 2000);
    ui::GameTile game;
    game.name = "Balatro";
    app.showLaunching(game, ui::trf(ui::Str::InQueue, "1 min"));
    save("launching");
    app.showError("Could not connect to the stream: the server did not answer in time.");
    save("error");

    // Settings, then the same screens in Portuguese.
    std::vector<std::string> regions;
    for (const auto& r : gssv.session().regions) regions.push_back(r.name);
    app.setRegions(regions, gssv.region().name);
    app.showHome();
    ui::NavInput tri;
    tri.options = true;
    app.handle(tri);
    save("settings");
    ui::NavInput down2;
    down2.down = true;
    app.handle(down2);
    app.handle(down2);
    ui::NavInput rightOne;
    rightOne.right = true;
    app.handle(rightOne);  // pick the first non-automatic region
    ui::NavInput up2;
    up2.up = true;
    app.handle(up2);
    app.handle(up2);
    app.handle(rightOne);  // English -> Portugues (Brasil)
    save("settings_pt");
    app.handle(tri);  // back to home
    if (!library.load(gssv, ui::catalogLanguage(), onRows, err)) XC_LOGE("%s", err.c_str());
    library.hydrate(onRows);
    app.showHome();
    save("home_pt", 3000);
    // Sign-out hold feedback, 2 s into the 5 s hold.
    ui::NavInput hold;
    hold.touchpad = true;
    hold.nowMs = 100000;
    app.handle(hold);
    hold.nowMs = 102000;
    app.handle(hold);
    app.render(canvas, 102000);
    std::string holdPath = dir + "/signout_hold.png";
    stbi_write_png(holdPath.c_str(), canvas.width(), canvas.height(), 4, canvas.data(), canvas.width() * 4);
    std::printf("wrote %s\n", holdPath.c_str());
    ui::setLanguage(ui::Language::English);
    return 0;
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
    if (cmd == "render-icon" && argi < argc) {
        int size = argi + 1 < argc ? std::atoi(argv[argi + 1]) : 512;
        ui::Fonts fonts;
        if (!fonts.load("assets/fonts")) return 1;
        ui::Canvas icon(size, size);
        ui::drawAppIcon(icon, fonts);
        // icon0.png is RGB: drop the alpha channel.
        std::vector<uint8_t> rgb(static_cast<size_t>(size) * size * 3);
        for (size_t i = 0; i < static_cast<size_t>(size) * size; ++i) {
            uint32_t p = icon.data()[i];
            rgb[i * 3] = p & 0xFF;
            rgb[i * 3 + 1] = (p >> 8) & 0xFF;
            rgb[i * 3 + 2] = (p >> 16) & 0xFF;
        }
        return stbi_write_png(argv[argi], size, size, 3, rgb.data(), size * 3) ? 0 : 1;
    }
    if ((cmd == "render-pic0" || cmd == "render-pic1") && argi < argc) {
        // PS5 home-screen backgrounds (PNG; tools/ps5/home-art.sh makes DDS).
        int w = argi + 1 < argc ? std::atoi(argv[argi + 1]) : 3840;
        int h = w * 9 / 16;
        ui::Fonts fonts;
        if (!fonts.load("assets/fonts")) return 1;
        ui::Canvas art(w, h);
        ui::drawHomeArt(art, fonts, cmd == "render-pic1");
        std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
            uint32_t p = art.data()[i];
            rgb[i * 3] = p & 0xFF;
            rgb[i * 3 + 1] = (p >> 8) & 0xFF;
            rgb[i * 3 + 2] = (p >> 16) & 0xFF;
        }
        return stbi_write_png(argv[argi], w, h, 3, rgb.data(), w * 3) ? 0 : 1;
    }
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
        // stream <id> [seconds] [out.h264] [--720p] [--region=NAME]
        std::vector<const char*> pos;
        xcloud::Resolution res = xcloud::Resolution::P1080;
        stream::StreamOptions opts;
        if (const char* loss = std::getenv("XC_SIM_LOSS")) opts.simulatedVideoLoss = std::atoi(loss);
        std::string region;
        for (; argi < argc; ++argi) {
            std::string a = argv[argi];
            if (a.rfind("--region=", 0) == 0) region = a.substr(9);
            else if (a == "--720p") res = xcloud::Resolution::P720;
            else if (a == "--qhd-display") res = xcloud::Resolution::P1440;
            else if (a.rfind("--tier=", 0) == 0) opts.resolutionAlias = a.substr(7);
            else pos.push_back(argv[argi]);
        }
        int seconds = pos.size() > 1 ? std::atoi(pos[1]) : 30;
        rc = cmdStream(am, pos[0], seconds > 0 ? seconds : 30, pos.size() > 2 ? pos[2] : nullptr, res, opts, region);
    } else if (cmd == "ui-preview" && argi < argc) {
        rc = cmdUiPreview(am, argv[argi]);
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
