// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/worker_internal.h"

#include "app/autoplay.h"
#include "app/crash_guard.h"
#include "app/shell.h"
#include "app/stream_player.h"
#include "input/controller.h"
#include "media/audio_out.h"
#include "media/audio_queue.h"
#include "platform/platform.h"
#include "ui/strings.h"
#include "util/log.h"
#include "xcloud/catalog.h"
#include "xcloud/regions.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace xc::app {

namespace {

std::string formatWait(int seconds) {
    if (seconds < 0) return "...";
    if (seconds < 60) return std::to_string(seconds) + " s";
    return std::to_string((seconds + 59) / 60) + " min";
}

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

        std::shared_ptr<app::Library> lib;
        {
            std::lock_guard<std::mutex> lockLib(s_libraryMutex);
            lib = s_library;
        }
        if (lib) {
            if (!g_stageProduct.empty()) lib->recordRecent(g_stageProduct);
            else if (!g_stageTitle.empty()) lib->recordRecent(g_stageTitle);
            lib->setPlaytime(g_settings.titlePlaytime);
            g_ui->setRows(lib->rows());
            g_ui->setOwned(lib->owned(), lib->purchasable(), lib->ownedKnown());
        }
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

}  // namespace

std::string play(auth::AuthManager& am, xcloud::GssvClient& gssv, const ui::GameTile& game, bool& failed,
                 const std::string& regionName) {
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
        gssv.setPreviewFeatures(g_settings.previewFeatures);
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
    uint64_t queueStartedAt = 0;
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
            if (!queueStartedAt) queueStartedAt = now;
            int elapsedSec = static_cast<int>((now - queueStartedAt) / 1000);
            if (!queueAskedAt || now - queueAskedAt >= 15000) {
                int s = gssv.titleWaitSeconds(game.titleId);
                XC_LOGI("queue: estimated %d s (elapsed %d s)", s, elapsedSec);
                if (s >= 0 || queueSeconds < 0) queueSeconds = s;
                queueAskedAt = now;
            }
            bool indeterminate = (queueSeconds < 0) || (queueSeconds <= 15 && elapsedSec >= 15);
            if (indeterminate) {
                g_ui->setLaunchStatus(ui::trf(ui::Str::InQueueIndeterminate, formatWait(elapsedSec)));
            } else {
                int left = std::max(0, queueSeconds - static_cast<int>((now - queueAskedAt) / 1000));
                g_ui->setLaunchStatus(ui::trf(ui::Str::InQueueEstimated, formatWait(left), formatWait(elapsedSec)));
            }
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
            if (alertOn && (wasInQueue || (platform::nowMs() - provisioningSince >= 2000))) {
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

}  // namespace xc::app
