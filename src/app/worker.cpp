// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The worker thread: sign-in, the library, the account's consoles, and
// playing a title (queue -> /connect -> Provisioned -> StreamPlayer). It
// owns AuthManager and the GssvClients and does all the network work.
#include "app/autoplay.h"
#include "app/library.h"
#include "app/ps5_app.h"
#include "app/stream_player.h"
#include "app/update_check.h"
#include "app/updater.h"
#include "auth/auth_manager.h"
#include "platform/platform.h"
#include "ui/strings.h"
#include "util/log.h"
#include "xcloud/catalog.h"
#include "xcloud/gssv.h"
#include "xcloud/regions.h"

#include <cstdlib>
#include <map>
#include <sys/stat.h>
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
constexpr int64_t kReprobeSeconds = 7 * 24 * 3600;

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

// Streams until the player ends or the user leaves; returns a status line.
std::string stream(xcloud::GssvClient& gssv) {
    g_gameClosedOnServer = false;
    app::StreamPlayer player(gssv);
    if (g_autoplay.dump) player.dumpVideo(platform::dataDir() + "/stream.aus", 20);
    player.setDecodeThreads(g_autoplay.decodeThreads);
    player.setHwDecodeProbe(g_autoplay.hwDecode);
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
            platform::notify(ui::tr(ui::Str::GestureHint));
        }
    }

    const uint64_t started = platform::nowMs();
    uint64_t nextTick = started;
    bool snapshot1 = false, snapshot2 = false;
    bool autoplayDone = false;
    int bestRtt = -1;  // lowest round trip seen: the region's latency
    int tallest = 0;   // the tallest picture seen (the probe's measure)
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
            bool press = !g_autoplay.idle && ((elapsed >= 15000 && elapsed < 15300) || (elapsed >= 20000 && elapsed < 20300));
            if (press != g_syntheticA.exchange(press)) XC_LOGI("autoplay: A %s", press ? "down" : "up");
            static bool dropped = false;
            if (g_autoplay.dropTest && !dropped && elapsed >= 20000) {
                dropped = true;
                player.simulateDrop();
            }
            if (!snapshot2 && elapsed >= 26000) {
                snapshot2 = true;
                player.requestSnapshot(platform::dataDir() + "/frame2.ppm");
            }
        }
        if (platform::nowMs() >= nextTick) {
            nextTick += 1000;
            player.tick();
            auto st = player.stats();
            XC_LOGI("stream: %llu frames, %llu decoded, %llu skipped, %llu failed, %llu resets, %llu kf req, "
                    "%llu queued, %llu audio; rtp %llu pkts, %llu lost, %llu recovered, %llu nacks, "
                    "%llu frames dropped; %llu kbps (remb %llu); %llu rumble; decode %.1f/%.1f ms, draw %.1f/%.1f ms, %llu late; "
                    "rtt %d ms; %dx%d; on screen %.1f/%.1f ms after arrival",
                    static_cast<unsigned long long>(st.videoFrames), static_cast<unsigned long long>(st.decodedFrames),
                    static_cast<unsigned long long>(st.droppedFrames), static_cast<unsigned long long>(st.decodeFailures),
                    static_cast<unsigned long long>(st.queueResets), static_cast<unsigned long long>(st.keyframeRequests),
                    static_cast<unsigned long long>(st.queued), static_cast<unsigned long long>(st.audioPackets),
                    static_cast<unsigned long long>(st.rtpPackets), static_cast<unsigned long long>(st.rtpLost),
                    static_cast<unsigned long long>(st.rtpRecovered), static_cast<unsigned long long>(st.rtpNacks),
                    static_cast<unsigned long long>(st.rtpDroppedFrames), static_cast<unsigned long long>(st.rtpKbps),
                    static_cast<unsigned long long>(st.rembKbps), static_cast<unsigned long long>(st.vibrations),
                    st.decodeAvgUs / 1000.0, st.decodeMaxUs / 1000.0, st.drawAvgUs / 1000.0, st.drawMaxUs / 1000.0,
                    static_cast<unsigned long long>(st.lateFrames), st.rttMs, st.width, st.height,
                    st.displayAvgUs / 1000.0, st.displayMaxUs / 1000.0);
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
                info.fps = static_cast<double>(st.decodedFrames - last.decodedFrames - (st.droppedFrames - last.droppedFrames));
                info.mbps = st.rtpKbps / 1000.0;
                uint64_t lost = st.rtpLost - last.rtpLost, got = st.rtpPackets - last.rtpPackets;
                info.lossPct = got + lost ? 100.0 * lost / (got + lost) : 0;
                info.decodeMs = st.decodeAvgUs / 1000.0;
                info.onScreenMs = st.displayAvgUs / 1000.0;
                info.width = st.width;
                info.height = st.height;
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
std::string play(auth::AuthManager& am, xcloud::GssvClient& gssv, const ui::GameTile& game, bool& failed,
                 const std::string& regionName = {}) {
    failed = true;
    // A home session's id is the console's: only its start in the log.
    XC_LOGI("starting %s (%s)", game.name.c_str(),
            gssv.isHome() ? (game.titleId.substr(0, 4) + "...").c_str() : game.titleId.c_str());
    g_ui->showLaunching(game, regionName.empty() ? ui::tr(ui::Str::Connecting)
                                                 : ui::trf(ui::Str::TryingRegion, ui::prettyRegion(regionName)));
    std::string err;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        bool hasCustom = false;
        GameProfile prof = g_settings.profileForGame(game.productId, game.titleId, &hasCustom);
        std::string res;
        if (!g_autoplay.resolution.empty()) {
            res = g_autoplay.resolution;
        } else if (hasCustom) {
            res = prof.resolution == 1 ? "720p" : prof.resolution == 2 ? "best" : "1080p";
        } else {
            res = g_settings.resolution;
        }
        bool isBest = (res == "best" || res == "1440p");
        // What each kind of stream can deliver is measured, not assumed: the
        // first stream (and one a week) asks for the top tier and records
        // the picture's height. The user's own Xbox always gets the top tier
        // (it sends 1080p either way, at ~16 Mbps instead of ~9.5).
        bool home = gssv.isHome();
        int64_t now = auth::unixNow();
        int known = home ? g_settings.maxHeightHome : g_settings.maxHeightCloud;
        int64_t probed = home ? g_settings.probedHome : g_settings.probedCloud;
        g_probing = g_autoplay.resolution.empty() && res != "720p" && (known == 0 || now - probed > kReprobeSeconds);
        g_afterProbeAlias.clear();
        g_tierPicked = false;
        xcloud::Resolution tier;
        if (!g_autoplay.resolution.empty())
            tier = res == "720p"       ? xcloud::Resolution::P720
                   : isBest            ? xcloud::Resolution::P1440
                   : res == "1080p-hq" ? xcloud::Resolution::P1080HQ
                                       : xcloud::Resolution::P1080;
        else if (res == "720p")
            tier = xcloud::Resolution::P720;
        else if (home || g_probing || (isBest && known >= 1440))
            tier = xcloud::Resolution::P1440;
        else if (isBest)
            tier = xcloud::Resolution::P1080HQ;
        else
            tier = xcloud::Resolution::P1080;
        // A cloud probe goes back to what the user chose once measured.
        if (g_probing && !home && !isBest) g_afterProbeAlias = "1080HQ";
        gssv.setResolution(tier);
        const xcloud::Region* region = gssv.session().defaultRegion();
        const std::string& wanted = regionName.empty() ? g_settings.region : regionName;
        for (const auto& r : gssv.session().regions)
            if (r.name == wanted) region = &r;
        if (region) gssv.setRegion(*region);
        XC_LOGI("stream settings: %s%s, region %s, locale %s", res.c_str(),
                hasCustom ? " (per-game)" : "", region ? region->name.c_str() : "?", ui::gameLocale());
    }
    if (!gssv.startSession(game.titleId, ui::gameLocale(), err)) return err;
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
    std::string result = "timed out waiting for the session";
    // A console that is off for good (not asleep) never comes out of
    // Provisioning: give up after a minute (the cloud's queue may take long).
    const uint64_t provisioningSince = platform::nowMs();
    int pollErrors = 0;
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
        if (st.state == xcloud::SessionState::WaitingForResources)
            g_ui->setLaunchStatus(ui::trf(ui::Str::InQueue, formatWait(gssv.waitTimeSeconds())));
        if (st.state == xcloud::SessionState::Provisioning && gssv.isHome())
            g_ui->setLaunchStatus(ui::tr(ui::Str::WakingConsole));  // a sleeping Xbox takes ~10 s
        if (st.state == xcloud::SessionState::Failed) {
            result = "the session failed: " + st.errorCode + " " + st.errorMessage;
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

void loadLibrary(xcloud::GssvClient& gssv);

// Profile token for titlehub (memory only).
std::string g_xblAuth;

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
    bool offer = found && isNewerVersion(r.tag, XC_APP_VERSION) && !r.zipUrl.empty() && !r.sigUrl.empty();
    if (found && isNewerVersion(r.tag, XC_APP_VERSION) && !offer) XC_LOGW("%s has no signed package", r.tag.c_str());
    {
        std::lock_guard<std::mutex> lock(g_releaseMutex);
        g_release = offer ? r : Release{};
    }
    g_ui->setUpdateState(XC_APP_VERSION, offer ? r.tag : std::string(), false);
    bool skipped;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        skipped = g_settings.skippedUpdate == r.tag;
    }
    if (offer && !skipped) g_ui->offerUpdate();
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
    // Only where the app runs from (/app0 is the data folder): installed
    // elsewhere, it would write next to the wrong eboot.bin.
    std::string dir = platform::dataDir(), err;
    struct stat here {}, running {};
    bool ok = ::stat((dir + "/eboot.bin").c_str(), &here) == 0 && ::stat("/app0/eboot.bin", &running) == 0 &&
              here.st_ino == running.st_ino;
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
    return ui::tr(ui::Str::StreamEnded);
}

// "My consoles": the account's own Xbox consoles (Remote Play, the xhome
// offering, logged in with the same Xbox token).
void loadConsoles(auth::AuthManager& am, xcloud::GssvClient& home) {
    std::string err;
    std::vector<xcloud::Console> consoles;
    bool fresh = home.session().expiresAt > auth::unixNow() + 120;
    if ((!fresh && !am.loginOffering(home, err)) || !home.listConsoles(consoles, err)) {
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

void signInAndLoad(auth::AuthManager& am, xcloud::GssvClient& gssv) {
    g_ui->showSplash(ui::tr(am.hasStoredAccount() ? ui::Str::SigningIn : ui::Str::RequestingCode));
    std::string err;
    auto onCode = [](const auth::DeviceCode& dc) { g_ui->showSignIn(dc.userCode, dc.verificationUri); };
    if (!am.signIn(gssv, onCode, err, &g_cancel)) {
        XC_LOGE("sign-in failed: %s", err.c_str());
        g_ui->showError(ui::trf(ui::Str::SignInFailed, err));
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
    g_ui->setProfile(am.profile().gamertag, am.profile().gamerpicUrl, am.profile().gamerscore);
    {
        std::lock_guard<std::mutex> lock(g_argMutex);
        g_xblAuth = am.profile().xblAuthorization;
    }
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
    loadLibrary(gssv);
}

void loadLibrary(xcloud::GssvClient& gssv) {
    std::string err;
    g_ui->showSplash(ui::tr(ui::Str::LoadingGames));
    stopHydration();
    auto library = std::make_shared<app::Library>();
    library->setCachePath(platform::dataDir() + "/library.json");
    setPriceMarket(gssv.session().market.empty() ? "US" : gssv.session().market, ui::catalogLanguage());
    bool shown = false;
    // Hands the library's current state to the UI (copies).
    auto publish = [](const app::Library& lib) {
        g_ui->setRows(lib.rows());
        g_ui->setOwned(lib.owned(), lib.purchasable(), lib.ownedKnown());
        g_ui->setSearchPools(lib.gamePassSearchPool(), lib.librarySearchPool());
        auto p = lib.progress();
        g_ui->setLoading(p.active, p.done, p.total);
    };
    bool ok = library->load(gssv, ui::catalogLanguage(),
                            [&] {
                                publish(*library);
                                if (!shown) {
                                    shown = true;
                                    g_ui->showHome();
                                }
                            },
                            err);
    if (!ok) {
        g_ui->showError(ui::trf(ui::Str::LibraryFailed, err));
        return;
    }
    std::vector<ui::GameRow> rows = library->rows();
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
        !g_autoplay.consolesTab && !g_autoplay.settingsTest &&
        !g_autoplay.imeTest && !g_autoplay.vibeTest && !g_autoplay.searchTest && !g_autoplay.quickTest && !g_autoplay.scrollTest &&
        !g_autoplay.gridTest && !g_autoplay.gameSettingsTest && !g_autoplay.publisherTest && !g_autoplay.friendsTest) {
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

void worker() {
    if (g_autoplay.title == "BENCH") {
        platform::Thread t;  // same big stack as the stream's video thread
        platform::startThread(t, runDecodeBench);
        t.join();
        for (;;) platform::sleepMs(1000);
    }
    auth::AuthManager am(platform::dataDir() + "/account.json");
    xcloud::GssvClient gssv, home("xhome");
    for (;;) {
        int cmd = g_command.exchange(kNone);
        g_cancel = false;
        switch (cmd) {
            case kSignIn:
                signInAndLoad(am, gssv);
                loadConsoles(am, home);  // empty when the sign-in failed
                break;
            case kConsoles: loadConsoles(am, home); break;
            case kUpdate: runUpdate(); break;
            case kReloadLibrary: loadLibrary(gssv); break;  // e.g. after a language change
            case kSignOut:
                am.signOut();
                g_ui->setProfile({}, {});
                g_ui->setConsoles({}, false);
                signInAndLoad(am, gssv);
                loadConsoles(am, home);
                break;
            case kPlay: {
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
                    loadConsoles(am, home);  // its state changed
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
                    g_ui->showError(result.rfind("ERROR: ", 0) == 0 ? result.substr(7) : result);
                }
                else
                    g_ui->showHome(endedToast(result));
                autoplayFinished(result);
                break;
            }
            default: platform::sleepMs(50); break;
        }
    }
}

}  // namespace xc::app
