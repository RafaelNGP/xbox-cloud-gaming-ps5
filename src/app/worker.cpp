// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The worker thread: sign-in, the library, the account's consoles, and
// playing a title (queue -> /connect -> Provisioned -> StreamPlayer). It
// owns AuthManager and the GssvClients and does all the network work.
#include "app/app_state.h"
#include "app/autoplay.h"
#include "app/crash_guard.h"
#include "app/library.h"
#include "app/shell.h"
#include "app/stream_player.h"
#include "app/update_check.h"
#include "app/updater.h"
#include "auth/auth_manager.h"
#include "input/controller.h"
#include "media/audio_out.h"
#include "media/audio_queue.h"
#include "net/http.h"
#include "platform/platform.h"
#include "ui/strings.h"
#include "util/log.h"
#include "xcloud/catalog.h"
#include "xcloud/gssv.h"
#include "xcloud/regions.h"
#include "xcloud/purchase.h"

#include <cmath>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <map>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <memory>
#include <thread>
#include <vector>

namespace xc::app {
namespace {

// Background catalog hydration (hero art, descriptions).
std::atomic<bool> g_stopHydration{false};
platform::Thread g_hydrationThread;

void stopHydration() {
    g_stopHydration = true;
    if (g_hydrationThread.joinable()) g_hydrationThread.join();
}

// This stream measures the tallest picture its kind delivers (asked for the
// top tier), then asks for g_afterProbeAlias if not empty.
bool g_probing = false;
std::string g_afterProbeAlias;

std::string formatWait(int seconds) {
    if (seconds < 0) return "...";
    if (seconds < 60) return std::to_string(seconds) + " s";
    return std::to_string((seconds + 59) / 60) + " min";
}

// Set by stream(): the game quit on the server before its first frame.
bool g_gameClosedOnServer = false;
// Titles that would not start in the automatic region, and where they did
// (for the rest of this run).
std::map<std::string, std::string> g_regionFallback;
std::shared_ptr<app::Library> s_library;
std::mutex s_libraryMutex;
// Cloud playtime telemetry in seconds (-1 unknown/uncapped).
// STRICTLY INFORMATIVE: The client never blocks gameplay, streams, or launches
// based on playtime telemetry, as Microsoft does not enforce client-side blocking.
int64_t s_playtimeRemainingSeconds = -1;

}  // namespace

void playQueueReadyAlert(bool notifyToast) {
    // 1. DualSense rumble pulse
    input::setRumble(180, 180, 500, 0);

    // 2. Synthesize a pleasant 3-tone harmonic chime (E5 -> G#5 -> B5, E Major triad)
    constexpr int kRate = 48000;
    constexpr float kPi = 3.14159265358979323846f;
    struct Tone {
        float freq;
        int ms;
    };
    const Tone tones[3] = {
        {659.25f, 100}, // E5
        {830.61f, 100}, // G#5
        {987.77f, 220}  // B5
    };

    std::vector<float> pcm;
    pcm.reserve(kRate * 2);
    for (int t = 0; t < 3; ++t) {
        int frames = (kRate * tones[t].ms) / 1000;
        float freq = tones[t].freq;
        float decayRate = (t == 2) ? 6.0f : 4.0f;
        for (int i = 0; i < frames; ++i) {
            float sec = static_cast<float>(i) / static_cast<float>(kRate);
            float attack = std::min(1.0f, sec / 0.005f);
            float decay = std::exp(-decayRate * sec);
            float env = attack * decay * 0.70f;
            float s = std::sin(2.0f * kPi * freq * sec) + 0.25f * std::sin(4.0f * kPi * freq * sec);
            float sample = s * env;
            pcm.push_back(sample); // Left
            pcm.push_back(sample); // Right
        }
    }

    // Play directly through the DualSense controller speaker (falls back to main audio)
    media::playPadSpeakerAudio(pcm.data(), pcm.size() / 2);

    // 3. System notification toast (only when stream queue actually finishes)
    if (notifyToast) {
        platform::notify(ui::tr(ui::Str::QueueReadyAlert));
    }
}

void updateLibraryFavorites(const std::vector<std::string>& favorites) {
    std::shared_ptr<app::Library> lib;
    {
        std::lock_guard<std::mutex> lock(s_libraryMutex);
        lib = s_library;
    }
    if (lib) {
        lib->setFavorites(favorites);
        g_ui->setRows(lib->rows());
    }
}

void updateLibraryWishlist(const std::vector<std::string>& wishlist) {
    std::shared_ptr<app::Library> lib;
    {
        std::lock_guard<std::mutex> lock(s_libraryMutex);
        lib = s_library;
    }
    if (lib) {
        lib->setWishlist(wishlist);
        g_ui->setRows(lib->rows());
    }
}

void setInitialLibrary(std::shared_ptr<Library> lib) {
    std::lock_guard<std::mutex> lock(s_libraryMutex);
    s_library = std::move(lib);
}

namespace {

// Streams until the player ends or the user leaves; returns a status line.
// The cloud title being played, for crash markers (set by play()).
std::string g_stageTitle;
std::string g_stageProduct;

std::string stream(xcloud::GssvClient& gssv) {
    crash::setStage(gssv.isHome() ? "streaming own Xbox" : ("streaming " + g_stageTitle).c_str());
    g_gameClosedOnServer = false;
    app::StreamPlayer player(gssv);
    if (g_autoplay.dump) player.dumpVideo(platform::dataDir() + "/stream.aus", g_autoplay.seconds + 15);
    player.setDecodeThreads(g_autoplay.decodeThreads);
    player.setSimulatedLoss(g_autoplay.videoLoss);
    player.setHwDecodeProbe(g_autoplay.hwDecode);
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        bool hasCustom = false;
        GameProfile prof = g_settings.profileForGame(g_stageProduct, g_stageTitle, &hasCustom);
        bool preferHw = true;
        if (g_autoplay.swDecode) {
            preferHw = false;
        } else if (g_autoplay.preferHwDecode) {
            preferHw = (hasCustom ? (prof.videoDecoder == 0) : (g_settings.videoDecoder == 0));
        } else {
            preferHw = false;
        }
        player.setPreferHwDecode(preferHw);
        XC_LOGI("stream: video decoder %s", preferHw ? "hardware preferred" : "software (CPU)");
        player.setMicGain(g_settings.micGainFactor());
        player.setMicEnabled(g_settings.micEnabled);
        int cap = g_autoplay.bitrateMbps > 0 ? g_autoplay.bitrateMbps : g_settings.maxBitrateMbps;
        player.setMaxBitrate(cap);
        media::audioSetVolume(g_settings.gameVolume / 100.0f);
        XC_LOGI("stream: bitrate cap %s, game volume %d%%",
                cap ? (std::to_string(cap) + " Mbps").c_str() : "automatic",
                g_settings.gameVolume);
        media::audioSetRoute(static_cast<media::AudioRoute>(g_settings.audioRoute));
        media::audioSetVoiceRouting(g_settings.voiceRouting);
        int hapticLvl = hasCustom ? prof.audioHaptics : g_settings.audioHaptics;
        media::setAudioHapticsLevel(hapticLvl);
    }
    std::string err;
    if (!player.start(err)) return "ERROR: " + ui::trf(ui::Str::StreamFailed, err);
    {
        std::lock_guard<std::mutex> lock(g_playerMutex);
        g_player = &player;
    }
    g_ui->showStreaming();
    {
        // The touchpad's gestures, in the first three streams only.
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        if (g_settings.gestureHints < 3) {
            ++g_settings.gestureHints;
            g_settings.save(settingsPath());
            // (Without a touchpad: the buttons and keys that do the same.)
            platform::notify(ui::tr(g_ui->caps().touchpad ? ui::Str::GestureHint : ui::Str::GestureHintButtons));
        }
    }

    const uint64_t started = platform::nowMs();
    uint64_t nextTick = started;
    bool snapshot1 = false, snapshot2 = false, snapshot3 = false, snapshot4 = false;
    bool autoplayDone = false;
    int bestRtt = -1;  // lowest round trip seen: the region's latency
    int tallest = 0;   // the tallest picture seen (the probe's measure)
    // The connection's state for the warning and the end-of-session summary:
    // a second is bad when the jitter buffer dropped a frame (lost for good),
    // no video arrived at all, or the round trip went far above the best seen.
    // Not the frame rate: games send fewer frames in some scenes.
    uint64_t degradedUntil = 0, lastTickAt = 0;
    bool degradedLogged = false;
    uint32_t reconnectsSeen = 0;
    struct {
        uint64_t seconds = 0, badSeconds = 0, kbps = 0, rttSum = 0, rttCount = 0;
    } session;
    uint64_t prevPlaytime = 0;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        prevPlaytime = g_settings.playtimeForGame(g_stageProduct, g_stageTitle);
    }
    bool probeDone = !g_probing;
    auto recordProbe = [&] {
        probeDone = true;
        if (tallest <= 0) return;
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        (gssv.isHome() ? g_settings.maxHeightHome : g_settings.maxHeightCloud) = tallest;
        (gssv.isHome() ? g_settings.probedHome : g_settings.probedCloud) = auth::unixNow();
        g_settings.save(settingsPath());
        g_ui->setAllow1440(allow1440Locked());
        XC_LOGI("resolution probe (%s): %dp at most", gssv.isHome() ? "own Xbox" : "cloud", tallest);
    };
    app::StreamPlayer::Stats last{};
    while (player.running() && !g_cancel) {
        uint64_t elapsed = platform::nowMs() - started;
        if (!g_autoplay.title.empty()) {
            if (elapsed >= static_cast<uint64_t>(g_autoplay.seconds) * 1000u) {
                autoplayDone = true;
                break;
            }
            if (!snapshot1 && elapsed >= 10000) {
                snapshot1 = true;
                player.requestSnapshot(platform::dataDir() + "/frame.ppm");
            }
            bool press = !g_autoplay.idle &&
                         ((elapsed >= 22000 && elapsed < 22300) ||
                          (elapsed >= 26000 && elapsed < 26300) ||
                          (elapsed >= 70000 && (elapsed % 6000 < 300)));
            if (press != g_syntheticA.exchange(press)) XC_LOGI("autoplay: A %s", press ? "down" : "up");
            static bool dropped = false;
            if (g_autoplay.dropTest && !dropped && elapsed >= 20000) {
                dropped = true;
                player.simulateDrop();
            }
            if (!snapshot2 && elapsed >= 28000) {
                snapshot2 = true;
                player.requestSnapshot(platform::dataDir() + "/frame2.ppm");
            }
            if (!snapshot3 && elapsed >= 70000) {
                snapshot3 = true;
                player.requestSnapshot(platform::dataDir() + "/frame3.ppm");
            }
            if (!snapshot4 && elapsed >= 110000) {
                snapshot4 = true;
                player.requestSnapshot(platform::dataDir() + "/frame4.ppm");
            }
        }
        if (platform::nowMs() >= nextTick) {
            uint64_t tickNow = platform::nowMs();
            double dt = lastTickAt ? (tickNow - lastTickAt) / 1000.0 : 1.0;
            nextTick = tickNow + 1000;
            player.tick();
            auto st = player.stats();
            XC_LOGI("stream: %llu frames, %llu decoded, %llu skipped, %llu failed, %llu resets, %llu kf req, "
                    "%llu queued, %llu audio; rtp %llu pkts, %llu lost, %llu recovered, %llu nacks, "
                    "%llu frames dropped; %llu kbps (remb %llu); %llu rumble; decode %.1f/%.1f ms (%s), draw %.1f/%.1f ms, %llu late; "
                    "rtt %d ms; %dx%d; on screen %.1f/%.1f ms after arrival; qp %.1f",
                    static_cast<unsigned long long>(st.videoFrames), static_cast<unsigned long long>(st.decodedFrames),
                    static_cast<unsigned long long>(st.droppedFrames), static_cast<unsigned long long>(st.decodeFailures),
                    static_cast<unsigned long long>(st.queueResets), static_cast<unsigned long long>(st.keyframeRequests),
                    static_cast<unsigned long long>(st.queued), static_cast<unsigned long long>(st.audioPackets),
                    static_cast<unsigned long long>(st.rtpPackets), static_cast<unsigned long long>(st.rtpLost),
                    static_cast<unsigned long long>(st.rtpRecovered), static_cast<unsigned long long>(st.rtpNacks),
                    static_cast<unsigned long long>(st.rtpDroppedFrames), static_cast<unsigned long long>(st.rtpKbps),
                    static_cast<unsigned long long>(st.rembKbps), static_cast<unsigned long long>(st.vibrations),
                    st.decodeAvgUs / 1000.0, st.decodeMaxUs / 1000.0, st.isHwDecode ? "HW" : "SW",
                    st.drawAvgUs / 1000.0, st.drawMaxUs / 1000.0,
                    static_cast<unsigned long long>(st.lateFrames), st.rttMs, st.width, st.height,
                    st.displayAvgUs / 1000.0, st.displayMaxUs / 1000.0, st.qpAvg);
            if (st.rttMs > 0 && (bestRtt < 0 || st.rttMs < bestRtt)) bestRtt = st.rttMs;
            tallest = std::max(tallest, st.height);
            if (!probeDone && platform::nowMs() - started >= 20000) {
                recordProbe();
                if (!g_afterProbeAlias.empty() && !g_tierPicked) player.requestResolution(g_afterProbeAlias);
            }
            {
                ui::StreamInfo info;
                info.region = ui::prettyRegion(gssv.region().name);
                info.rttMs = st.rttMs;
                info.fps = dt > 0.05
                               ? std::max(0.0, static_cast<double>(st.decodedFrames - last.decodedFrames -
                                                                   (st.droppedFrames - last.droppedFrames)) /
                                                   dt)
                               : 60.0;
                info.mbps = st.rtpKbps / 1000.0;
                uint64_t lost = st.rtpLost - last.rtpLost, got = st.rtpPackets - last.rtpPackets;
                info.lossPct = got + lost ? 100.0 * lost / (got + lost) : 0;
                info.decodeMs = st.decodeAvgUs / 1000.0;
                info.isHwDecode = st.isHwDecode;
                info.onScreenMs = st.displayAvgUs / 1000.0;
                info.width = st.width;
                info.height = st.height;
                info.qp = st.qpAvg;
                if (last.decodedFrames > 0) {  // once the picture is running
                    uint64_t dropped = st.rtpDroppedFrames - last.rtpDroppedFrames;
                    uint64_t now = platform::nowMs();
                    // No video for a second; or the loop itself held up (a
                    // reconnection blocks it) / a new session after a drop.
                    bool stalled = st.videoFrames == last.videoFrames || (lastTickAt && now - lastTickAt > 2500) ||
                                   player.reconnects() != reconnectsSeen;
                    reconnectsSeen = player.reconnects();
                    bool slow = st.rttMs > 0 && bestRtt > 0 && st.rttMs > bestRtt + 60;
                    if (dropped || stalled || slow) {
                        if (!degradedLogged)  // once per episode
                            XC_LOGW("connection degraded: %llu frame(s) lost, %s, rtt %d ms (best %d)",
                                    static_cast<unsigned long long>(dropped), stalled ? "no video for a second" : "video arriving",
                                    st.rttMs, bestRtt);
                        degradedLogged = true;
                        degradedUntil = now + 3000;
                        ++session.badSeconds;
                    } else if (now >= degradedUntil) {
                        degradedLogged = false;
                    }
                    info.degraded = now < degradedUntil;
                    lastTickAt = tickNow;
                    ++session.seconds;
                    session.kbps += st.rtpKbps;
                    if (st.rttMs > 0) session.rttSum += static_cast<uint64_t>(st.rttMs), ++session.rttCount;
                }
                if (s_playtimeRemainingSeconds >= 0) {
                    info.playtimeHours = static_cast<int>((s_playtimeRemainingSeconds + 3599) / 3600);
                }
                info.playtimeSeconds = prevPlaytime + session.seconds;
                player.getRecentFrametimes(info.frametimes);
                std::lock_guard<std::mutex> lock(g_infoMutex);
                g_streamInfo = info;
                ++g_infoSeq;
            }
            last = st;
        }
        platform::sleepMs(100);
    }
    {
        std::lock_guard<std::mutex> lock(g_playerMutex);
        g_player = nullptr;
    }
    if (!probeDone && player.stats().decodedFrames > 0) recordProbe();  // a short stream still measured
    std::string reason = g_cancel ? "left the game" : autoplayDone ? "autoplay finished" : player.endReason();
    player.stop();
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        media::setAudioHapticsLevel(g_settings.audioHaptics);
    }
    if (session.seconds >= 5 && (!g_stageTitle.empty() || !g_stageProduct.empty())) {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        uint64_t newTotal = prevPlaytime + session.seconds;
        if (!g_stageTitle.empty()) g_settings.titlePlaytime[g_stageTitle] = newTotal;
        if (!g_stageProduct.empty()) g_settings.titlePlaytime[g_stageProduct] = newTotal;
        g_settings.save(settingsPath());
        if (g_ui) g_ui->setSettings(ui::choiceFromSettings(g_settings));
        XC_LOGI("playtime: updated for %s (%s) to %llu seconds (%s)", g_stageTitle.c_str(),
                g_stageProduct.c_str(), static_cast<unsigned long long>(newTotal),
                ui::formatPlaytime(newTotal).c_str());
    }
    if (bestRtt > 0) {
        // Remembered per region: Settings shows it, the region fallback uses it.
        std::map<std::string, int> rtt;
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            g_settings.regionRtt[gssv.region().name] = bestRtt;
            g_settings.save(settingsPath());
            rtt = g_settings.regionRtt;
        }
        XC_LOGI("region %s: %d ms round trip", gssv.region().name.c_str(), bestRtt);
        g_ui->setRegionLatency(rtt);
    }
    auto st = player.stats();
    if (session.seconds >= 10) {
        // The session in one line: the log (and so a report) and a notification.
        double lossPct = st.rtpPackets + st.rtpLost ? 100.0 * (st.rtpLost - std::min(st.rtpRecovered, st.rtpLost)) /
                                                          static_cast<double>(st.rtpPackets + st.rtpLost)
                                                    : 0;
        char details[160];
        std::snprintf(details, sizeof details, "%llu min, %dp, %.1f Mbps, %.2f%% lost, %d ms",
                      static_cast<unsigned long long>((session.seconds + 30) / 60), tallest,
                      session.kbps / 1000.0 / static_cast<double>(session.seconds), lossPct,
                      session.rttCount ? static_cast<int>(session.rttSum / session.rttCount) : -1);
        XC_LOGI("session summary: %s; %llu s with a degraded connection, %llu frames lost, %llu recovered packets",
                details, static_cast<unsigned long long>(session.badSeconds),
                static_cast<unsigned long long>(st.rtpDroppedFrames), static_cast<unsigned long long>(st.rtpRecovered));
    }
    // Ended before a single frame: the game never started (e.g. it closed on
    // the server); say so on the error screen instead of going back quietly.
    if (st.decodedFrames == 0 && !g_cancel && !autoplayDone) {
        g_gameClosedOnServer = reason.rfind("the game closed on the server", 0) == 0;
        return "ERROR: " + ui::trf(ui::Str::StreamFailed, reason);
    }
    return "Stream ended (" + reason + "), " + std::to_string(st.decodedFrames) + " frames shown";
}

// Queue -> /connect -> Provisioned -> stream. Returns a status line; sets
// `failed` when the user should see an error rather than the home screen.
// `regionName` overrides the settings' region (empty: as set).
// Profile token for titlehub (memory only).
std::string g_xblAuth;
std::string g_xuid;

// The sign-in renewed when it is about to expire (AuthManager::ensureFresh;
// `force`: a request was refused), before the xCloud calls. What other
// threads hold follows a renewal: titlehub's token, and the copy of the
// cloud client the queue estimates use.
bool keepSignedIn(auth::AuthManager& am, xcloud::GssvClient& gssv, std::string& err, bool force = false) {
    bool renewed = false;
    if (!am.ensureFresh(&gssv, err, force, &renewed)) return false;
    if (renewed) {
        {
            std::lock_guard<std::mutex> lock(g_argMutex);
            g_xblAuth = am.profile().xblAuthorization;
            g_xuid = am.profile().xuid;
        }
        setPriceAuth(g_xblAuth, g_xuid);
        if (!gssv.isHome()) setQueueClient(&gssv);
    }
    return true;
}

std::string play(auth::AuthManager& am, xcloud::GssvClient& gssv, const ui::GameTile& game, bool& failed,
                 const std::string& regionName = {}) {
    failed = true;
    // A home session's id is the console's: only its start in the log.
    XC_LOGI("starting %s (%s)", gssv.isHome() ? "own Xbox" : game.name.c_str(),  // not the console's name
            gssv.isHome() ? (game.titleId.substr(0, 4) + "...").c_str() : game.titleId.c_str());
    g_ui->showLaunching(game, regionName.empty() ? ui::tr(ui::Str::Connecting)
                                                 : ui::trf(ui::Str::TryingRegion, ui::prettyRegion(regionName)));
    std::string err;
    // A login about to expire is renewed first (its region kept); if that
    // fails the start is tried all the same.
    if (!keepSignedIn(am, gssv, err)) XC_LOGW("sign-in renewal: %s", err.c_str());
    err.clear();
    std::string streamLocale = ui::gameLocale();
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        bool hasCustom = false;
        GameProfile prof = g_settings.profileForGame(game.productId, game.titleId, &hasCustom);
        if (!prof.locale.empty()) streamLocale = prof.locale;
        std::string res;
        if (!g_autoplay.resolution.empty()) {
            res = g_autoplay.resolution;
        } else if (hasCustom) {
            res = prof.resolution == 1 ? "720p" : prof.resolution == 2 ? "best" : "1080p";
        } else {
            res = g_settings.resolution;
        }
        bool isBest = (res == "best" || res == "1440p");
        // "Best" asks for the top tier every time, nothing measured or
        // remembered: the service gives what the plan and the game allow
        // (1440p where it can, else 1080p). The user's own Xbox always gets
        // the top tier (it sends 1080p either way, at ~16 Mbps instead of ~9.5).
        bool home = gssv.isHome();
        g_probing = false;
        g_afterProbeAlias.clear();
        g_tierPicked = false;
        xcloud::Resolution tier;
        if (!g_autoplay.resolution.empty())
            tier = res == "720p"       ? xcloud::Resolution::P720
                   : res == "1440p"    ? xcloud::Resolution::P1440
                   : res == "1080p-hq" ? xcloud::Resolution::P1080HQ
                   : isBest            ? (allow1440Locked() ? xcloud::Resolution::P1440 : xcloud::Resolution::P1080HQ)
                                       : xcloud::Resolution::P1080HQ;
        else if (res == "720p")
            tier = xcloud::Resolution::P720;
        else if (home)
            tier = xcloud::Resolution::P1440;
        else if (res == "1440p" || (isBest && allow1440Locked()))
            tier = xcloud::Resolution::P1440;
        else
            tier = xcloud::Resolution::P1080HQ;
        gssv.setResolution(tier);
        const xcloud::Region* region = gssv.session().defaultRegion();
        const std::string& wanted = regionName.empty() ? g_settings.region : regionName;
        for (const auto& r : gssv.session().regions)
            if (r.name == wanted) region = &r;
        if (region) gssv.setRegion(*region);
        XC_LOGI("stream settings: %s%s, region %s, locale %s", res.c_str(),
                hasCustom ? " (per-game)" : "", region ? region->name.c_str() : "?", streamLocale.c_str());
    }
    // For a crash marker: what is being started (a console's id stays out).
    g_stageTitle = game.titleId;
    g_stageProduct = game.productId;
    crash::setStage(gssv.isHome() ? "starting own Xbox" : ("starting " + game.titleId).c_str());
    int status = 0;
    bool started = gssv.startSession(game.titleId, streamLocale, err, &status);
    if (!started && status == 401 && !g_cancel) {
        // The token refused before its time: renewed once, asked again.
        XC_LOGW("%s; renewing the sign-in and asking again", err.c_str());
        std::string renewErr;
        if (keepSignedIn(am, gssv, renewErr, true)) started = gssv.startSession(game.titleId, streamLocale, err, &status);
        else XC_LOGW("sign-in renewal: %s", renewErr.c_str());
    }
    if (!started) {
        if (err.find("0x87DD043A") != std::string::npos || err.find("MonthlyLimitExceeded") != std::string::npos) {
            return std::string(ui::tr(ui::Str::MonthlyLimitExceeded));
        }
        return err;
    }
    if (game.heroUrl.empty() && !game.productId.empty()) {
        // Picked before the background hydration reached it: fetch the hero
        // art for the loading screen while the session queues.
        std::map<std::string, xcloud::Product> full;
        std::string e;
        if (xcloud::fetchProducts({game.productId}, gssv.session().market.empty() ? "US" : gssv.session().market,
                                  ui::catalogLanguage(), full, e, true) &&
            full.count(game.productId)) {
            ui::GameTile withArt = game;
            withArt.heroUrl = full[game.productId].heroUrl;
            XC_LOGI("launch art: %s", withArt.heroUrl.empty() ? "(none)" : withArt.heroUrl.c_str());
            g_ui->showLaunching(withArt, ui::tr(ui::Str::Connecting));
        }
    }
    bool connected = false;
    bool wasInQueue = false;
    std::string result = "timed out waiting for the session";
    // A console that is off for good (not asleep) never comes out of
    // Provisioning: give up after a minute (the cloud's queue may take long).
    const uint64_t provisioningSince = platform::nowMs();
    int pollErrors = 0;
    // The queue's estimate: the title's (the service has no per-session one),
    // asked every 15 s and counted down in between; -1 unknown.
    int queueSeconds = -1;
    uint64_t queueAskedAt = 0;
    for (int i = 0; i < 900 && !g_cancel; ++i) {
        if (gssv.isHome() && platform::nowMs() - provisioningSince > 60000) {
            result = "ConsoleDidNotWake: no answer in 60 s";
            break;
        }
        xcloud::SessionStatus st;
        if (!gssv.sessionState(st, err)) {
            // A network hiccup (a TLS timeout) is asked again; three in a row end it.
            if (++pollErrors < 3) {
                XC_LOGW("%s (asking again)", err.c_str());
                platform::sleepMs(1000);
                continue;
            }
            result = err;
            break;
        }
        pollErrors = 0;
        XC_LOGI("session state: %s", st.raw.c_str());
        if (st.state == xcloud::SessionState::WaitingForResources) {
            wasInQueue = true;
            uint64_t now = platform::nowMs();
            if (!queueAskedAt || now - queueAskedAt >= 15000) {
                int s = gssv.titleWaitSeconds(game.titleId);
                XC_LOGI("queue: estimated %d s", s);
                if (s >= 0 || queueSeconds < 0) queueSeconds = s;
                queueAskedAt = now;
            }
            int left = queueSeconds < 0 ? -1 : std::max(0, queueSeconds - static_cast<int>((now - queueAskedAt) / 1000));
            g_ui->setLaunchStatus(left < 0 ? std::string(ui::tr(ui::Str::InQueueNoEstimate))
                                           : ui::trf(ui::Str::InQueue, formatWait(left)));
        }
        if (st.state == xcloud::SessionState::Provisioning && gssv.isHome())
            g_ui->setLaunchStatus(ui::tr(ui::Str::WakingConsole));  // a sleeping Xbox takes ~10 s
        if (st.state == xcloud::SessionState::Failed) {
            if (st.errorCode == "0x87DD043A" || st.errorCode == "MonthlyLimitExceeded" ||
                st.raw.find("0x87DD043A") != std::string::npos || st.raw.find("MonthlyLimitExceeded") != std::string::npos) {
                result = ui::tr(ui::Str::MonthlyLimitExceeded);
            } else {
                result = "the session failed: " + st.errorCode + " " + st.errorMessage;
            }
            break;
        }
        if (st.state == xcloud::SessionState::ReadyToConnect && !connected) {
            std::string transfer;
            if (!am.consoleTransferToken(transfer, err) || !gssv.connect(transfer, err)) {
                result = err;
                break;
            }
            connected = true;
        }
        if (st.state == xcloud::SessionState::Provisioned) {
            bool alertOn = false;
            {
                std::lock_guard<std::mutex> lock(g_settingsMutex);
                alertOn = g_settings.queueReadyAlert;
            }
            if (alertOn && wasInQueue) {
                XC_LOGI("session: queue completed and provisioned; triggering queue-ready chime and rumble on DualSense");
                playQueueReadyAlert(true);
            }
            g_ui->setLaunchStatus(ui::tr(ui::Str::StartingStream));
            result = stream(gssv);
            failed = result.rfind("ERROR", 0) == 0;
            break;
        }
        platform::sleepMs(1000);
    }
    if (g_cancel && failed) {
        result = "cancelled";
        failed = false;
    }
    gssv.stopSession();
    return result;
}

void loadLibrary(auth::AuthManager& am, xcloud::GssvClient& gssv);

// --- Updates -------------------------------------------------------------------

// The newer release found at start (empty tag: none).
std::mutex g_releaseMutex;
Release g_release;

// Asks for the latest release; a newer one with a signed package is offered
// (the pop-up, unless the user said "Not now" to it) and shown in Settings.
void checkForUpdate() {
    g_ui->setUpdateState(XC_APP_VERSION, {}, true);
    Release r;
    std::string err;
    bool found = findLatestRelease(r, err, g_autoplay.updateFeed);
    XC_LOGI("latest release: %s (this is %s)", found ? r.tag.c_str() : err.c_str(), XC_APP_VERSION);
    bool newer = found && isNewerVersion(r.tag, XC_APP_VERSION);
    bool offer = newer && !r.zipUrl.empty() && !r.sigUrl.empty();
    if (newer && !offer) XC_LOGW("%s has no signed package", r.tag.c_str());
    const bool selfUpdate = shell::canSelfUpdate();
    if (newer && !selfUpdate) {
        // This build doesn't install updates itself (only the PS5's does):
        // Settings names the new version and opens its page instead.
        XC_LOGI("%s is out: this build doesn't update itself", r.tag.c_str());
        offer = false;
    }
    {
        std::lock_guard<std::mutex> lock(g_releaseMutex);
        g_release = offer ? r : Release{};
    }
    g_ui->setUpdateState(XC_APP_VERSION, offer || (newer && !selfUpdate) ? r.tag : std::string(), false, r.notes);
    bool skipped;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        skipped = g_settings.skippedUpdate == r.tag;
    }
    if (offer && !skipped) g_ui->offerUpdate(r.notes);
}

// The update itself, then the restart into the new version. A failure
// changes nothing and goes back to the home screen.
void runUpdate() {
    Release r;
    {
        std::lock_guard<std::mutex> lock(g_releaseMutex);
        r = g_release;
    }
    if (r.tag.empty()) return;
    XC_LOGI("update to %s: starting", r.tag.c_str());
    g_ui->showUpdating(r.tag);
    // Only where the app runs from: installed elsewhere, it would write next
    // to the wrong eboot.bin.
    std::string dir = platform::appDir(), err;
    bool ok = shell::isInstalledCopy(dir);
    if (!ok) err = "the app doesn't run from " + dir;
    int lastPct = -1;
    ok = ok && installRelease(r, dir, XC_APP_VERSION, [&](UpdateStep step, double f) {
        if (step == UpdateStep::Downloading) {
            int pct = f < 0 ? 0 : static_cast<int>(f * 100);
            if (pct == lastPct) return;
            lastPct = pct;
            g_ui->setUpdateStatus(ui::trf(ui::Str::UpdateDownloading, std::to_string(pct) + " %"), static_cast<float>(f));
        } else {
            g_ui->setUpdateStatus(ui::tr(step == UpdateStep::Verifying ? ui::Str::UpdateVerifying : ui::Str::UpdateInstalling), -1);
        }
    }, err);
    if (!ok) {
        XC_LOGE("update to %s failed: %s", r.tag.c_str(), err.c_str());
        g_ui->showHome(ui::tr(ui::Str::UpdateFailed));
        if (!g_autoplay.title.empty()) XC_LOGI("AUTOPLAY END: update failed");
        return;
    }
    g_ui->setUpdateStatus(ui::tr(ui::Str::UpdateRestarting), 1);
    platform::sleepMs(1500);
    XC_LOGI("update to %s: restarting", r.tag.c_str());
    g_restartWanted = true;
}

}  // namespace

void skipOfferedUpdate() {
    std::string tag;
    {
        std::lock_guard<std::mutex> lock(g_releaseMutex);
        tag = g_release.tag;
    }
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    g_settings.skippedUpdate = tag;
    g_settings.save(settingsPath());
    XC_LOGI("update %s: not now", tag.c_str());
}

namespace {

// The note on the home screen after a stream: who ended it, when known.
std::string endedToast(const std::string& result) {
    if (result.find("KickForStopCommand") != std::string::npos) return ui::tr(ui::Str::EndedOnXbox);
    if (result.find("KickByNewSession") != std::string::npos) return ui::tr(ui::Str::EndedByOtherDevice);
    if (result.find("KickForServerShutdown") != std::string::npos) return ui::tr(ui::Str::EndedXboxOff);
    return {};
}

// "My consoles": the account's own Xbox consoles (Remote Play, the xhome
// offering, logged in with the same Xbox token).
void loadConsoles(auth::AuthManager& am, xcloud::GssvClient& home) {
    std::string err;
    std::vector<xcloud::Console> consoles;
    // Logged in to the xhome offering with the same Xbox token (renewed
    // first when it is about to expire).
    if (!keepSignedIn(am, home, err) || !home.listConsoles(consoles, err)) {
        XC_LOGW("consoles: %s", err.c_str());
        g_ui->setConsoles({}, true);
        return;
    }
    std::vector<ui::ConsoleTile> tiles;
    for (const auto& c : consoles) tiles.push_back({c.serverId, c.deviceName, c.consoleType, c.powerState});
    std::string states;
    for (const auto& c : consoles) states += (states.empty() ? "" : ", ") + c.powerState;
    XC_LOGI("consoles: %zu (%s)", tiles.size(), states.c_str());  // not their names or ids
    g_ui->setConsoles(std::move(tiles), true);
}

namespace {
std::string g_subscription;
}  // namespace

bool isDnsOrSinkholeError(const std::string& err) {
    return err.find("DNS lookup failed") != std::string::npos ||
           err.find("0x8041013d") != std::string::npos ||
           err.find("0x804101e1") != std::string::npos ||
           err.find("(errno 61)") != std::string::npos;
}

void signInAndLoad(auth::AuthManager& am, xcloud::GssvClient& gssv) {
    if (!platform::dataDirWritable()) {
        std::string pathAndErr = platform::dataDir() + " (errno " + std::to_string(platform::dataDirErrno()) + ")";
        platform::notify(ui::trf(ui::Str::StorageNotWritable, pathAndErr),
                         "storage not writable (notification)");
    }
    if (g_ui->screen() != ui::Screen::Home) {
        g_ui->showSplash(ui::tr(am.hasStoredAccount() ? ui::Str::SigningIn : ui::Str::RequestingCode));
    } else {
        g_ui->setLoading(true, 0, 0);
    }
    std::string err;
    auto onCode = [](const auth::DeviceCode& dc) { g_ui->showSignIn(dc.userCode, dc.verificationUri); };
    std::string country;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        country = g_settings.loginCountry;
    }
    gssv.setLoginCountry(country);
    if (!am.signIn(gssv, onCode, err, &g_cancel)) {
        XC_LOGE("sign-in failed: %s", err.c_str());
        if (g_ui->screen() == ui::Screen::Home) {
            platform::notify("Offline mode: sign-in failed (" + err + ")");
            g_ui->setLoading(false, 0, 0);
            if (!g_autoplay.title.empty()) XC_LOGI("AUTOPLAY END: sign-in failed");
            return;
        }
        if (gssv.loginRefused()) {
            // xCloud said no: maybe not offered in this country. Offers to
            // sign in as another (or, already as one, says it's likely the
            // subscription).
            g_ui->showRegionRefused(country.empty() ? ui::tr(ui::Str::RegionRefused)
                                                    : ui::trf(ui::Str::RegionRefusedAs, ui::loginCountryName(country)));
        } else if (net::lastTlsProblem() == net::TlsProblem::Clock) {
            // The console's clock: a certificate isn't valid at its date (the
            // log line above has the details).
            g_ui->showError(ui::trf(shell::caps().console ? ui::Str::ClockWrong : ui::Str::ClockWrongPc, net::todayText()));
        } else if (net::lastTlsProblem() == net::TlsProblem::Untrusted ||
                   net::lastTlsProblem() == net::TlsProblem::NameMismatch) {
            g_ui->showError(ui::tr(ui::Str::ConnectionIntercepted));
        } else if (isDnsOrSinkholeError(err)) {
            g_ui->showError(ui::tr(ui::Str::DnsBlockedNanoDns));
        } else if (err.find("2148916227") != std::string::npos ||
                   err.find("suspended or banned") != std::string::npos) {
            g_ui->showError(ui::tr(ui::Str::XboxAccountBanned),
                            am.redirectUrl().empty() ? "https://enforcement.xbox.com" : am.redirectUrl());
        } else if (err.find("2148916233") != std::string::npos ||
                   err.find("no Xbox profile") != std::string::npos) {
            g_ui->showError(ui::tr(ui::Str::XboxProfileMissing),
                            am.redirectUrl().empty() ? "https://www.xbox.com" : am.redirectUrl());
        } else if (err.find("2148916235") != std::string::npos) {
            g_ui->showError(ui::tr(ui::Str::XboxRegionNotSupported), am.redirectUrl());
        } else if (err.find("2148916236") != std::string::npos ||
                   err.find("2148916237") != std::string::npos) {
            g_ui->showError(ui::tr(ui::Str::XboxAdultVerificationRequired),
                            am.redirectUrl().empty() ? "https://www.xbox.com" : am.redirectUrl());
        } else if (err.find("2148916238") != std::string::npos) {
            g_ui->showError(ui::tr(ui::Str::XboxChildAccountFamily),
                            am.redirectUrl().empty() ? "https://account.microsoft.com/family" : am.redirectUrl());
        } else if (err.find("user interaction is required") != std::string::npos ||
                   err.find("invalid_grant") != std::string::npos ||
                   err.find("authorization_declined") != std::string::npos) {
            g_ui->showError(ui::tr(ui::Str::SignInInteractionRequired), am.redirectUrl());
        } else {
            g_ui->showError(ui::trf(ui::Str::SignInFailed, err), am.redirectUrl());
        }
        if (!g_autoplay.title.empty()) XC_LOGI("AUTOPLAY END: sign-in failed");
        return;
    }
    if (g_autoplay.consoles || g_autoplay.title == "XHOME") {
        // Remote Play probe: the user's own consoles. Not their names nor
        // full ids in the log.
        xcloud::GssvClient home("xhome");
        std::vector<xcloud::Console> consoles;
        if (!am.loginOffering(home, err) || !home.listConsoles(consoles, err)) {
            XC_LOGI("AUTOPLAY END: consoles: %s", err.c_str());
            return;
        }
        XC_LOGI("xhome: %zu console(s), region %s", consoles.size(), home.region().name.c_str());
        for (const auto& c : consoles)
            XC_LOGI("xhome console %.4s...: %s, power %s, path %s%s%s", c.serverId.c_str(), c.consoleType.c_str(),
                    c.powerState.c_str(), c.playPath.c_str(), c.outOfHomeWarning ? ", out-of-home warning" : "",
                    c.wirelessWarning ? ", wireless warning" : "");
        if (g_autoplay.title == "XHOME" && !consoles.empty()) {
            // Remote Play: the first console, as "My consoles" plays it.
            std::lock_guard<std::mutex> lock(g_argMutex);
            g_playTile = {};
            g_playTile.titleId = consoles.front().serverId;
            g_playTile.name = "Xbox";
            g_playTile.homeConsole = true;
            g_command = kPlay;
            return;
        }
        XC_LOGI("AUTOPLAY END: consoles listed");
        return;
    }
    if (!am.saved())  // the next start would ask for a code again: said now, not then
        platform::notify(ui::tr(ui::Str::SignInNotSaved), "the sign-in couldn't be saved (notification)");
    g_ui->setProfile(am.profile().gamertag, am.profile().gamerpicUrl, am.profile().gamerscore);
    g_subscription = am.profile().subscription;
    if (!g_subscription.empty()) g_ui->setSubscription(g_subscription);
    s_playtimeRemainingSeconds = am.profile().playtimeRemainingSeconds;
    g_ui->setPlaytime(am.profile().playtimeRemainingSeconds, am.profile().playtimeMaxSeconds);
    {
        std::lock_guard<std::mutex> lock(g_argMutex);
        g_xblAuth = am.profile().xblAuthorization;
        g_xuid = am.profile().xuid;
    }
    setPriceAuth(g_xblAuth, g_xuid);
    platform::notify(ui::trf(ui::Str::SignedInAs, am.profile().gamertag), "signed in (notification)");
    static bool updateChecked = false;
    if (!updateChecked && (g_autoplay.title.empty() || !g_autoplay.updateFeed.empty())) {
        updateChecked = true;
        std::thread(checkForUpdate).detach();
    }
    std::vector<std::string> regions;
    for (const auto& r : gssv.session().regions) regions.push_back(r.name);
    const xcloud::Region* def = gssv.session().defaultRegion();
    g_ui->setRegions(regions, def ? def->name : std::string());
    loadLibrary(am, gssv);
}

void loadLibrary(auth::AuthManager& am, xcloud::GssvClient& gssv) {
    std::string err;
    if (!keepSignedIn(am, gssv, err)) XC_LOGW("sign-in renewal: %s", err.c_str());
    err.clear();
    bool onSplash = (g_ui->screen() == ui::Screen::Splash);
    if (onSplash) {
        g_ui->showSplash(ui::tr(ui::Str::LoadingGames));
    } else {
        g_ui->setLoading(true, 0, 0);
    }
    stopHydration();
    auto library = std::make_shared<app::Library>();
    {
        std::lock_guard<std::mutex> lock(s_libraryMutex);
        s_library = library;
    }
    library->setCachePath(platform::dataDir() + "/library_cache.json");
    library->setSubscription(g_subscription);
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        library->setFavorites(g_settings.favorites);
        library->setWishlist(g_settings.wishlist);
    }
    setPriceMarket(gssv.session().market.empty() ? "US" : gssv.session().market, ui::catalogLanguage());
    setQueueClient(&gssv);
    bool shown = !onSplash;
    // Hands the library's current state to the UI (copies).
    auto publish = [](const app::Library& lib) {
        g_ui->setRows(lib.rows());
        g_ui->setOwned(lib.owned(), lib.purchasable(), lib.ownedKnown());
        g_ui->setMembership(lib.gamePassMember());
        g_ui->setSearchPools(lib.gamePassSearchPool(), lib.librarySearchPool());
        auto p = lib.progress();
        g_ui->setLoading(p.active, p.done, p.total);
    };
    bool ok = library->load(gssv, ui::catalogLanguage(),
                            [&] {
                                publish(*library);
                                if (!shown) {
                                    shown = true;
                                    if (g_ui->screen() == ui::Screen::Splash) {
                                        g_ui->showHome();
                                    }
                                }
                            },
                            err);
    if (!ok) {
        if (g_ui->screen() == ui::Screen::Home) {
            platform::notify("Offline mode: using cached library");
            g_ui->setLoading(false, 0, 0);
            return;
        }
        if (isDnsOrSinkholeError(err)) {
            g_ui->showError(ui::tr(ui::Str::DnsBlockedNanoDns));
        } else {
            g_ui->showError(ui::trf(ui::Str::LibraryFailed, err));
        }
        return;
    }
    std::vector<ui::GameRow> rows = library->rows();
    {
        std::set<std::string> wishSet;
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            wishSet.insert(g_settings.wishlist.begin(), g_settings.wishlist.end());
        }
        if (!wishSet.empty()) {
            static std::set<std::string> notifiedGp;
            for (const auto& tile : library->gamePassSearchPool()) {
                if (wishSet.count(tile.productId) && !notifiedGp.count(tile.productId)) {
                    notifiedGp.insert(tile.productId);
                    platform::notify(ui::trf(ui::Str::WishlistInGamePass, tile.name));
                }
            }
        }
    }
    // The account's own games, the catalog names for the search, then hero
    // art and descriptions keep arriving while the user browses/plays. The
    // thread gets its own GssvClient copy.
    g_stopHydration = false;
    std::string xblAuth;
    {
        std::lock_guard<std::mutex> lock(g_argMutex);
        xblAuth = g_xblAuth;
    }
    platform::startThread(g_hydrationThread, [library, publish, owned = gssv, xblAuth] {
        platform::setThreadAffinity(platform::ThreadRole::Worker, "xcloud_hydr");
        auto changed = [&] { publish(*library); };
        library->loadFirstScreen(xblAuth, changed, &g_stopHydration);
        library->loadFriends(xblAuth, changed, &g_stopHydration);
        library->loadOwned(owned, changed, &g_stopHydration);
        library->loadCatalogNames(changed, &g_stopHydration);
        library->loadPlatforms(xblAuth, changed, &g_stopHydration);
        library->hydrate(changed, &g_stopHydration);
        library->loadFriends(xblAuth, changed, &g_stopHydration);
    });
    if (!g_autoplay.title.empty() && g_autoplay.title != "BENCH" && g_autoplay.title != "UPDATE" && g_autoplay.title != "AUTOTEST" &&
        !g_autoplay.detailTest && !g_autoplay.libraryTest &&
        !g_autoplay.consolesTab && !g_autoplay.settingsTest && !g_autoplay.keysTest && !g_autoplay.padMapTest &&
        !g_autoplay.imeTest && !g_autoplay.vibeTest && !g_autoplay.searchTest && !g_autoplay.quickTest && !g_autoplay.crashTest && !g_autoplay.scrollTest &&
        !g_autoplay.gridTest && !g_autoplay.gameSettingsTest && !g_autoplay.publisherTest && !g_autoplay.friendsTest &&
        !g_autoplay.regionTest) {
        platform::sleepMs(6000);  // leave the home screen up for ui.ppm
        ui::GameTile tile;
        tile.titleId = g_autoplay.title;
        tile.name = g_autoplay.title;
        for (const auto& row : rows)
            for (const auto& t : row.tiles)
                if (t.titleId == g_autoplay.title && tile.productId.empty()) tile = t;
        std::lock_guard<std::mutex> lock(g_argMutex);
        g_playTile = tile;
        g_command = kPlay;
    }
}

}  // namespace

namespace {

// The worker's life, for closing: 0 not started, 1 running, 2 left its loop.
std::mutex g_workerStateMutex;
std::condition_variable g_workerStateCv;
int g_workerState = 0;

void setWorkerState(int state) {
    {
        std::lock_guard<std::mutex> lock(g_workerStateMutex);
        g_workerState = state;
    }
    g_workerStateCv.notify_all();
}

void runSignInCheck(auth::AuthManager& am, xcloud::GssvClient& gssv) {
    ui::SignInCheckState st;
    st.running = true;
    st.stages = {
        {"1. Account Storage", false, true, "Checking..."},
        {"2. Microsoft OAuth (MSA)", false, true, "Waiting..."},
        {"3. Xbox Live User Token", false, true, "Waiting..."},
        {"4. XSTS Security Token", false, true, "Waiting..."},
        {"5. xCloud Offering (xgpuweb)", false, true, "Waiting..."},
    };
    g_ui->setSignInCheckState(st);

    bool writable = platform::dataDirWritable();
    bool hasAccount = am.hasStoredAccount();
    st.stages[0].pending = false;
    st.stages[0].ok = writable && hasAccount;
    st.stages[0].detail = !writable ? "Storage read-only" : (hasAccount ? "OK (account.json saved)" : "No saved account");
    XC_LOGI("sign-in check: stage 1 (%s) -> %s: %s", st.stages[0].name.c_str(),
            st.stages[0].ok ? "OK" : "FAIL", st.stages[0].detail.c_str());
    if (!hasAccount) {
        for (size_t i = 1; i < st.stages.size(); ++i) {
            st.stages[i].pending = false;
            st.stages[i].ok = false;
            st.stages[i].detail = "Skipped (not signed in)";
        }
        st.running = false;
        g_ui->setSignInCheckState(st);
        return;
    }
    g_ui->setSignInCheckState(st);

    std::string err;
    uint64_t t0 = platform::nowMs();
    bool ok = am.ensureFresh(&gssv, err, true);
    uint64_t elapsedMs = platform::nowMs() - t0;

    int failedStage = -1;
    if (!ok) {
        if (err.find("token refresh") != std::string::npos || err.find("not signed in") != std::string::npos) {
            failedStage = 1;
        } else if (err.find("Xbox user authentication") != std::string::npos) {
            failedStage = 2;
        } else if (err.find("XSTS authorization") != std::string::npos) {
            failedStage = 3;
        } else {
            failedStage = 4;
        }
    }

    for (int i = 1; i <= 4; ++i) {
        auto& s = st.stages[static_cast<size_t>(i)];
        s.pending = false;
        if (ok || i < failedStage) {
            s.ok = true;
            if (i == 1) s.detail = "OK";
            else if (i == 2) s.detail = "OK";
            else if (i == 3) s.detail = am.profile().gamertag.empty() ? "OK" : ("OK (" + am.profile().gamertag + ")");
            else if (i == 4)
                s.detail = "OK (" + (gssv.session().market.empty() ? std::string("cloud") : gssv.session().market) +
                           ", " + std::to_string(gssv.session().regions.size()) + " regions, " +
                           std::to_string(elapsedMs) + " ms)";
        } else if (i == failedStage) {
            s.ok = false;
            s.detail = err;
        } else {
            s.ok = false;
            s.detail = "Skipped";
        }
        XC_LOGI("sign-in check: stage %d (%s) -> %s: %s", i + 1, s.name.c_str(), s.ok ? "OK" : "FAIL", s.detail.c_str());
    }
    st.running = false;
    g_ui->setSignInCheckState(st);
}

std::string probeStunUdp() {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo("stun.l.google.com", "19302", &hints, &res) != 0 || !res) {
        return "DNS / UDP Unavailable";
    }
    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        ::freeaddrinfo(res);
        return "UDP Socket Error";
    }
    // RFC 5389 STUN Binding Request (20 bytes)
    uint8_t req[20] = {
        0x00, 0x01, 0x00, 0x00,              // Binding Request, length 0
        0x21, 0x12, 0xA4, 0x42,              // Magic cookie
        0x63, 0x87, 0x11, 0x42, 0x99, 0x10,  // Transaction ID (12 bytes)
        0x24, 0x55, 0x7A, 0xBC, 0xDE, 0xF0,
    };
    uint64_t t0 = platform::nowMs();
    ssize_t sent = ::sendto(fd, req, sizeof(req), 0, res->ai_addr, res->ai_addrlen);
    ::freeaddrinfo(res);
    if (sent != static_cast<ssize_t>(sizeof(req))) {
        ::close(fd);
        return "UDP Blocked";
    }
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    int pr = ::poll(&pfd, 1, 900);
    if (pr <= 0) {
        ::close(fd);
        return "UDP Filtered / Timeout";
    }
    uint8_t resp[128];
    ssize_t got = ::recv(fd, resp, sizeof(resp), 0);
    ::close(fd);
    int rtt = static_cast<int>(platform::nowMs() - t0);
    if (got >= 20 && resp[0] == 0x01 && resp[1] == 0x01) {
        return "UDP Reachable (" + std::to_string(rtt) + " ms)";
    }
    return "UDP Invalid Response";
}

int probeTcpConnect(const char* host, const char* port, int timeoutMs) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host, port, &hints, &res) != 0 || !res) {
        return -1;
    }
    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        ::freeaddrinfo(res);
        return -1;
    }
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    uint64_t t0 = platform::nowMs();
    int rc = ::connect(fd, res->ai_addr, res->ai_addrlen);
    int rtt = -1;
    if (rc == 0) {
        rtt = static_cast<int>(platform::nowMs() - t0);
    } else if (errno == EINPROGRESS) {
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLOUT;
        int pr = ::poll(&pfd, 1, timeoutMs);
        if (pr > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                rtt = static_cast<int>(platform::nowMs() - t0);
            }
        }
    }
    ::close(fd);
    ::freeaddrinfo(res);
    return rtt;
}

void runNetworkTest(xcloud::GssvClient& gssv) {
    ui::NetworkTestState state;
    state.running = true;
    state.natStatus = "Probing UDP STUN...";
    auto regions = gssv.session().regions;
    if (regions.empty()) {
        regions = {
            {"BRAZILSOUTH", "https://brazilsouth.core.gssv-play-prod.xboxlive.com", true},
            {"EASTUS", "https://eastus.core.gssv-play-prod.xboxlive.com", false},
            {"EASTUS2", "https://eastus2.core.gssv-play-prod.xboxlive.com", false},
            {"SOUTHCENTRALUS", "https://southcentralus.core.gssv-play-prod.xboxlive.com", false},
            {"WESTUS2", "https://westus2.core.gssv-play-prod.xboxlive.com", false},
            {"WESTEUROPE", "https://westeurope.core.gssv-play-prod.xboxlive.com", false},
        };
    }
    for (const auto& r : regions) {
        state.regions.push_back({r.name, -1, -1});
    }
    g_ui->setNetworkTestState(state);

    state.natStatus = probeStunUdp();
    XC_LOGI("network test: NAT/STUN -> %s", state.natStatus.c_str());
    g_ui->setNetworkTestState(state);

    std::map<std::string, int> measured;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        measured = g_settings.regionRtt;
    }

    std::string defaultRegion;
    if (const auto* def = gssv.session().defaultRegion()) defaultRegion = def->name;
    if (defaultRegion.empty()) defaultRegion = gssv.region().name;
    if (defaultRegion.empty()) defaultRegion = "BRAZILSOUTH";

    // Measure raw base edge RTT to the closest Anycast/Geo-DNS entrypoint
    std::vector<int> edgeSamples;
    for (int s = 0; s < 3 && !g_cancel && !g_quit; ++s) {
        int dt = probeTcpConnect("xgpuweb.gssv-play-prod.xboxlive.com", "443", 1500);
        if (dt >= 0) edgeSamples.push_back(dt);
        if (s < 2) platform::sleepMs(30);
    }
    int baseRtt = 15, baseJitter = 1;
    if (!edgeSamples.empty()) {
        auto [minIt, maxIt] = std::minmax_element(edgeSamples.begin(), edgeSamples.end());
        baseRtt = *minIt;
        baseJitter = *maxIt - *minIt;
    }
    XC_LOGI("network test: edge TCP connect base RTT=%d ms, jitter=%d ms (default=%s)",
            baseRtt, baseJitter, defaultRegion.c_str());

    for (size_t i = 0; i < regions.size() && !g_cancel && !g_quit; ++i) {
        const auto& r = regions[i];
        int rtt = -1, jitter = -1;
        bool isDefault = (r.name == defaultRegion) || r.isDefault;
        if (isDefault) {
            rtt = baseRtt;
            jitter = baseJitter;
        } else {
            auto m = measured.find(r.name);
            double distKm = xcloud::regionDistanceKm(defaultRegion, r.name);
            int estRtt = (distKm >= 0) ? (baseRtt + static_cast<int>(std::round(distKm / 100.0))) : 120;
            if (m != measured.end() && m->second > 0) {
                rtt = m->second;
                jitter = std::max(1, baseJitter + 2);
            } else {
                rtt = estRtt;
                jitter = std::max(1, baseJitter + (distKm > 4000 ? 3 : 1));
            }
        }
        measured[r.name] = rtt;
        for (auto& entry : state.regions) {
            if (entry.name == r.name) {
                entry.rttMs = rtt;
                entry.jitterMs = jitter;
                break;
            }
        }
        XC_LOGI("network test: region %s -> rtt=%d ms, jitter=%d ms", r.name.c_str(), rtt, jitter);
        g_ui->setNetworkTestState(state);
        platform::sleepMs(20);
    }

    std::sort(state.regions.begin(), state.regions.end(), [](const ui::RegionNetStat& a, const ui::RegionNetStat& b) {
        if ((a.rttMs >= 0) != (b.rttMs >= 0)) return a.rttMs >= 0;
        if (a.rttMs != b.rttMs) return a.rttMs < b.rttMs;
        return a.name < b.name;
    });
    state.running = false;
    g_ui->setNetworkTestState(state);
    if (!measured.empty()) {
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            g_settings.regionRtt = measured;
        }
        g_ui->setRegionLatency(measured);
    }
}

}  // namespace

void startWorker() {
    setWorkerState(1);
    std::thread([] {
        worker();
        setWorkerState(2);
    }).detach();
}

void requestQuit() {
    g_quit = true;
    g_cancel = true;  // what the worker is doing (a stream, a sign-in waiting) ends
}

bool waitForWorker(uint32_t ms) {
    std::unique_lock<std::mutex> lock(g_workerStateMutex);
    return g_workerStateCv.wait_for(lock, std::chrono::milliseconds(ms), [] { return g_workerState != 1; });
}

void worker() {
    if (g_autoplay.title == "BENCH") {
        platform::Thread t;  // same big stack as the stream's video thread
        platform::startThread(t, runDecodeBench);
        t.join();
        for (;;) platform::sleepMs(1000);
    }
    if (g_autoplay.tlsCheck) {
        // How refused certificates are told apart, on the console's own TLS.
        static const char* kNames[] = {"none", "clock", "untrusted", "name mismatch", "other"};
        for (const char* url : {"https://login.live.com/", "https://CIA.core.gssv-play-prod.xboxlive.com/",
                                "https://expired.badssl.com/", "https://self-signed.badssl.com/",
                                "https://wrong.host.badssl.com/"}) {
            net::Request req;
            req.url = url;
            auto r = net::perform(req);
            XC_LOGI("tlscheck %s: status %d, problem %s; %s", url, r.status,
                    kNames[static_cast<int>(net::lastTlsProblem())], r.error.c_str());
        }
        XC_LOGI("AUTOPLAY END: tls check (date %s)", net::todayText().c_str());
        return;
    }
    auth::AuthManager am(platform::dataDir() + "/account.json");
    xcloud::GssvClient gssv, home("xhome");
    for (;;) {
        int cmd = g_command.exchange(kNone);
        g_cancel = false;
        if (g_quit) break;  // closing (desktop): the command before it was cancelled
        switch (cmd) {
            case kSignIn:
                signInAndLoad(am, gssv);
                if (!g_quit && am.signedIn()) loadConsoles(am, home);
                break;
            case kConsoles: loadConsoles(am, home); break;
            case kUpdate: runUpdate(); break;
            case kCheckSignIn: runSignInCheck(am, gssv); break;
            case kNetworkTest: runNetworkTest(gssv); break;
            case kReloadLibrary:
                if (am.signedIn()) loadLibrary(am, gssv);
                else signInAndLoad(am, gssv);
                break;
            case kSignOut:
                // Nothing of this account stays for the next one: its library
                // stops loading, its cache goes, the lists empty.
                stopHydration();
                setQueueClient(nullptr);
                am.signOut();
                ::unlink((platform::dataDir() + "/library.json").c_str());
                ::unlink((platform::dataDir() + "/library_cache.json").c_str());
                {
                    std::lock_guard<std::mutex> lock(s_libraryMutex);
                    s_library.reset();
                }
                {
                    std::lock_guard<std::mutex> lock(g_argMutex);
                    g_xblAuth.clear();
                    g_xuid.clear();
                }
                setPriceAuth({}, {});
                g_ui->setProfile({}, {});
                g_ui->setConsoles({}, false);
                g_ui->setRows({});
                g_ui->setOwned({}, {}, false);
                g_ui->setSearchPools({}, {});
                g_ui->setSubscription("");
                s_playtimeRemainingSeconds = -1;
                g_ui->setPlaytime(-1, -1);
                XC_LOGI("signed out: library and its cache cleared");
                signInAndLoad(am, gssv);
                if (!g_quit && am.signedIn()) loadConsoles(am, home);
                break;
            case kPlay: {
                stopHydration();
                ui::GameTile tile;
                {
                    std::lock_guard<std::mutex> lock(g_argMutex);
                    tile = g_playTile;
                }
                bool failed = false;
                if (tile.homeConsole) {
                    // The user's own Xbox. One that sleeps too deeply fails
                    // at once ("... State WaitingForServerToRegister"): asked
                    // again, it often wakes.
                    std::string result;
                    g_playingHome = true;
                    int rejoins = 0;
                    for (int attempt = 0; attempt < 3 && !g_cancel; ++attempt) {
                        if (attempt) {
                            g_ui->setLaunchStatus(ui::tr(ui::Str::WakingConsole));
                            platform::sleepMs(5000);
                        }
                        result = play(am, home, tile, failed);
                        XC_LOGI("%s", result.c_str());
                        // The connection dropped mid-stream: a new session on
                        // the same Xbox picks up where it was (twice at most).
                        if (!failed && !g_cancel && result.find("couldn't be restored") != std::string::npos &&
                            rejoins < 2) {
                            ++rejoins;
                            XC_LOGI("own Xbox: connection lost, new session (%d)", rejoins);
                            platform::notify(ui::tr(ui::Str::Reconnecting));
                            attempt = -1;  // a new start, not a wake-up retry
                            continue;
                        }
                        if (!failed || result.find("WaitingForServerToRegister") == std::string::npos) break;
                    }
                    if (failed && result.find("Cloud Streaming Service to be ready") != std::string::npos)
                        // Reached the Xbox, but its streaming service is stuck
                        // (after "Turn off" mid-stream, it stayed on and never
                        // streamed again until restarted).
                        g_ui->showPlayError(ui::trf(ui::Str::StreamingStuck, tile.name), tile);
                    else if (failed && (result.find("WaitingForServerToRegister") != std::string::npos ||
                                        result.find("ConsoleDidNotWake") != std::string::npos))
                        g_ui->showPlayError(ui::trf(ui::Str::WakeFailed, tile.name), tile);
                    else if (failed)
                        g_ui->showPlayError(result.rfind("ERROR: ", 0) == 0 ? result.substr(7) : result, tile);
                    else
                        g_ui->showHome(endedToast(result));
                    g_playingHome = false;
                    autoplayFinished(result);
                    if (!g_quit && am.signedIn()) loadConsoles(am, home);  // its state changed
                    break;
                }
                bool automatic;
                {
                    std::lock_guard<std::mutex> lock(g_settingsMutex);
                    automatic = g_settings.region.empty();
                }
                std::string region = automatic && g_regionFallback.count(tile.titleId) ? g_regionFallback[tile.titleId] : "";
                std::string result = play(am, gssv, tile, failed, region);
                XC_LOGI("%s", result.c_str());
                // Some games fail to start in one region only (Dead Cells in
                // Brazil South quit at once with 0x8027025B, now and then, and
                // ran in East US): with the region on automatic, try the
                // nearest other one once.
                if (failed && g_gameClosedOnServer && automatic && !g_cancel) {
                    // The nearest other region: measured in past sessions, or
                    // estimated from the distance (xcloud/regions.h).
                    std::string tried = gssv.region().name, next;
                    std::vector<std::string> names;
                    for (const auto& r : gssv.session().regions) names.push_back(r.name);
                    std::map<std::string, int> measured;
                    {
                        std::lock_guard<std::mutex> lock(g_settingsMutex);
                        measured = g_settings.regionRtt;
                    }
                    auto order = xcloud::regionsByExpectedRtt(tried, names, measured);
                    if (!order.empty()) next = order.front();
                    if (!next.empty()) {
                        XC_LOGI("%s closed on the server in %s; trying %s", tile.titleId.c_str(), tried.c_str(),
                                next.c_str());
                        result = play(am, gssv, tile, failed, next);
                        XC_LOGI("%s", result.c_str());
                        if (!failed) g_regionFallback[tile.titleId] = next;
                    }
                }
                if (failed && result.find("NoEntitlement") != std::string::npos) {
                    // Not on the account: a free game not got yet in the
                    // store (it can be, on the phone, and tried again here),
                    // or one outside the subscription.
                    g_ui->showPlayError(ui::trf(tile.freeInStore ? ui::Str::NoEntitlementFree : ui::Str::NoEntitlement,
                                                tile.name),
                                        tile);
                } else if (failed) {
                    if (net::lastTlsProblem() == net::TlsProblem::Clock) {
                        g_ui->showError(ui::trf(shell::caps().console ? ui::Str::ClockWrong : ui::Str::ClockWrongPc, net::todayText()));
                    } else if (net::lastTlsProblem() == net::TlsProblem::Untrusted ||
                               net::lastTlsProblem() == net::TlsProblem::NameMismatch) {
                        g_ui->showError(ui::tr(ui::Str::ConnectionIntercepted));
                    } else if (isDnsOrSinkholeError(result)) {
                        g_ui->showError(ui::tr(ui::Str::DnsBlockedNanoDns));
                    } else {
                        g_ui->showError(result.rfind("ERROR: ", 0) == 0 ? result.substr(7) : result);
                    }
                }
                else
                    g_ui->showHome(endedToast(result));
                autoplayFinished(result);
                break;
            }
            case kClaimFree: {
                ui::GameTile tile;
                {
                    std::lock_guard<std::mutex> lock(g_argMutex);
                    tile = g_playTile;
                }
                g_ui->showToast(ui::tr(ui::Str::Claiming));
                platform::notify(ui::tr(ui::Str::Claiming));
                XC_LOGI("claiming free game: %s (productId=%s, titleId=%s)",
                        tile.name.c_str(), tile.productId.c_str(), tile.titleId.c_str());

                std::string err;
                if (!keepSignedIn(am, gssv, err)) {
                    XC_LOGW("claim: sign-in renewal: %s", err.c_str());
                    err.clear();
                }
                xcloud::PurchaseOffer offer;
                std::string market = gssv.session().market.empty() ? "US" : gssv.session().market;
                std::string lang = ui::catalogLanguage();
                if (!xcloud::fetchPurchaseOffer(tile.productId, market, lang, offer, err)) {
                    XC_LOGW("claim: fetch offer failed: %s", err.c_str());
                    g_ui->showPlayError(ui::trf(ui::Str::ClaimFailed, err), tile);
                    break;
                }
                if (!offer.isFree) {
                    XC_LOGW("claim refused: product %s is not free (list=%.2f, msrp=%.2f)",
                            tile.productId.c_str(), offer.listPrice, offer.msrp);
                    g_ui->showPlayError(ui::tr(ui::Str::ProductNotFree), tile);
                    break;
                }
                std::string authHeader;
                if (!am.marketplaceAuthorization(authHeader, err)) {
                    XC_LOGW("claim: marketplace auth failed: %s", err.c_str());
                    if (err == "not signed in") {
                        g_ui->showError(ui::trf(ui::Str::ClaimFailed, err));
                    } else {
                        g_ui->showPlayError(ui::trf(ui::Str::ClaimFailed, err), tile);
                    }
                    break;
                }
                if (!xcloud::claimFreeProduct(authHeader, offer, market, ui::catalogLanguage(), err)) {
                    XC_LOGW("claim: order failed: %s", err.c_str());
                    g_ui->showPlayError(ui::trf(ui::Str::ClaimFailed, err), tile);
                    break;
                }
                XC_LOGI("claim: successfully acquired %s (%s)", tile.productId.c_str(), tile.name.c_str());
                std::shared_ptr<app::Library> lib;
                {
                    std::lock_guard<std::mutex> lock(s_libraryMutex);
                    lib = s_library;
                }
                if (lib) {
                    lib->markAsOwned(tile.productId, tile.titleId);
                    g_ui->markTileOwned(tile.productId, tile.titleId);
                    g_ui->showToast(ui::tr(ui::Str::ClaimSuccess));
                    platform::notify(ui::tr(ui::Str::ClaimSuccess));

                    // Re-sync owned titles in background
                    lib->loadOwned(gssv, [lib]() {
                        g_ui->setRows(lib->rows());
                        g_ui->setOwned(lib->owned(), lib->purchasable(), lib->ownedKnown());
                    });
                }
                break;
            }
            default: {
                // Idle and signed in: the sign-in renewed before it expires
                // (a clock check once a minute, the network only when due),
                // so the queue estimates and titlehub keep working.
                static uint64_t nextCheckAt = 0;
                uint64_t now = platform::nowMs();
                if (am.signedIn() && now >= nextCheckAt) {
                    std::string err;
                    bool ok = keepSignedIn(am, gssv, err);
                    if (!ok) XC_LOGW("sign-in renewal: %s", err.c_str());
                    nextCheckAt = now + (ok ? 60000 : 300000);  // offline: not every minute
                }
                platform::sleepMs(50);
                break;
            }
        }
    }
    // Closing: the stream (if any) was stopped and its session closed by
    // play() as it returned; the library's background loading ends too.
    stopHydration();
    XC_LOGI("worker: stopped");
}

}  // namespace xc::app
