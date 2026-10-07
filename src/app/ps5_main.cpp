// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// PS5 front end: the menus (ui::AppUi) and playing a title
// (queue -> /connect -> Provisioned -> StreamPlayer).
//
// One worker thread owns AuthManager/GssvClient and does all network work;
// the main thread reads the pad, drives the UI and draws it.
#include "app/library.h"
#include "app/settings.h"
#include "app/stream_player.h"
#include "auth/auth_manager.h"
#include "display/display.h"
#include "input/controller.h"
#include "media/decoder.h"
#include "net/http.h"
#include "platform/platform.h"
#include "ui/app_ui.h"
#include "ui/strings.h"
#include "util/json.h"
#include "util/log.h"
#include "xcloud/catalog.h"
#include "xcloud/gssv.h"
#include "xcloud/prices.h"
#include "xcloud/regions.h"

#include <atomic>
#include <ctime>
#include <map>
#include <strings.h>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace xc;

extern "C" int sceSystemServiceHideSplashScreen(void);

namespace {

std::unique_ptr<ui::ImageCache> g_images;
std::unique_ptr<ui::AppUi> g_ui;

// --- Worker commands ---------------------------------------------------------

enum Command { kNone, kSignIn, kPlay, kSignOut, kReloadLibrary };

// User settings (settings.json); read by the worker, changed by the UI thread.
std::mutex g_settingsMutex;
app::Settings g_settings;
std::string settingsPath() { return platform::dataDir() + "/settings.json"; }

std::atomic<int> g_command{kSignIn};
std::atomic<bool> g_cancel{false};
std::mutex g_argMutex;
ui::GameTile g_playTile;

// Background catalog hydration (hero art, descriptions).
std::atomic<bool> g_stopHydration{false};
platform::Thread g_hydrationThread;

void stopHydration() {
    g_stopHydration = true;
    if (g_hydrationThread.joinable()) g_hydrationThread.join();
}

// The running stream, for the input thread.
std::mutex g_playerMutex;
app::StreamPlayer* g_player = nullptr;

// --- Unattended test mode (tools/ps5/autotest.sh) ----------------------------
// <dataDir>/autoplay.txt holds "<titleId> <seconds> [option]". The app signs
// in, plays that title for that long (pressing A at 15 s and 20 s), saves
// decoded frames and logs "AUTOPLAY END". Options, comma-separated: nosimd,
// dump, repeat, idle (no A presses), threads=N (H.264 decoder threads),
// rumbletest (rumbles the pad for 1.5 s at start), detailtest (opens a game to
// buy far down the list instead of playing, saves detail.ppm).
// The title "BENCH" decodes <dataDir>/sample.h264 instead.

std::string g_autoplayTitle;
int g_autoplaySeconds = 0;
bool g_autoplayDump = false;
int g_autoplayRuns = 1;
bool g_autoplayIdle = false;
bool g_autoplayDetailTest = false;  // open a game to buy far down the list, save its page
int g_decodeThreads = 1;
std::atomic<bool> g_syntheticA{false};

void loadAutoplay() {
    std::string text;
    if (!platform::readFile(platform::dataDir() + "/autoplay.txt", text)) return;
    char title[128] = {};
    char option[64] = {};
    int seconds = 0;
    if (std::sscanf(text.c_str(), "%127s %d %63s", title, &seconds, option) < 1) return;
    std::string options = option;  // comma-separated
    for (size_t start = 0; start <= options.size();) {
        size_t comma = options.find(',', start);
        if (comma == std::string::npos) comma = options.size();
        std::string opt = options.substr(start, comma - start);
        start = comma + 1;
        if (opt == "nosimd") media::disableSimd();
        if (opt == "dump") g_autoplayDump = true;
        if (opt == "repeat") g_autoplayRuns = 2;
        if (opt == "idle") g_autoplayIdle = true;
        if (opt == "rumbletest") input::setRumble(200, 200, 1500);
        if (opt == "detailtest") g_autoplayDetailTest = true;
        if (opt.rfind("threads=", 0) == 0) g_decodeThreads = std::atoi(opt.c_str() + 8);
    }
    g_autoplayTitle = title;
    g_autoplaySeconds = seconds > 0 ? seconds : 60;
    XC_LOGI("AUTOPLAY %s for %ds %s", title, g_autoplaySeconds, option);
    platform::probeNetworking();
}

void autoplayFinished(const std::string& result) {
    if (g_autoplayTitle.empty()) return;
    if (--g_autoplayRuns > 0) {
        XC_LOGI("AUTOPLAY next run: %s", result.c_str());
        g_command = kPlay;
    } else {
        XC_LOGI("AUTOPLAY END: %s", result.c_str());
    }
}

void runDecodeBench() {
    std::string data;
    if (!platform::readFile(platform::dataDir() + "/sample.h264", data)) {
        XC_LOGE("AUTOPLAY END: no sample.h264");
        return;
    }
    auto aus = media::splitAccessUnits(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    XC_LOGI("bench: %zu bytes, %zu access units", data.size(), aus.size());
    media::VideoDecoder dec;
    if (!dec.init(1)) {
        XC_LOGE("AUTOPLAY END: decoder init failed");
        return;
    }
    media::Picture pic;
    int pictures = 0;
    uint64_t t0 = platform::nowMs();
    for (const auto& [off, len] : aus)
        if (dec.decode(reinterpret_cast<const uint8_t*>(data.data()) + off, len, pic)) ++pictures;
    uint64_t ms = platform::nowMs() - t0;
    XC_LOGI("AUTOPLAY END: bench %d pictures (%dx%d) in %llu ms = %.2f ms/picture", pictures, pic.width,
            pic.height, static_cast<unsigned long long>(ms), pictures ? double(ms) / pictures : 0.0);
}

// --- Worker ----------------------------------------------------------------------

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
    if (g_autoplayDump) player.dumpVideo(platform::dataDir() + "/stream.aus", 20);
    player.setDecodeThreads(g_decodeThreads);
    std::string err;
    if (!player.start(err)) return "ERROR: " + ui::trf(ui::Str::StreamFailed, err);
    {
        std::lock_guard<std::mutex> lock(g_playerMutex);
        g_player = &player;
    }
    g_ui->showStreaming();
    platform::notify(ui::tr(ui::Str::LeaveHint));

    const uint64_t started = platform::nowMs();
    uint64_t nextTick = started;
    bool snapshot1 = false, snapshot2 = false;
    bool autoplayDone = false;
    int bestRtt = -1;  // lowest round trip seen: the region's latency
    while (player.running() && !g_cancel) {
        uint64_t elapsed = platform::nowMs() - started;
        if (!g_autoplayTitle.empty()) {
            if (elapsed >= static_cast<uint64_t>(g_autoplaySeconds) * 1000u) {
                autoplayDone = true;
                break;
            }
            if (!snapshot1 && elapsed >= 10000) {
                snapshot1 = true;
                player.requestSnapshot(platform::dataDir() + "/frame.ppm");
            }
            bool press = !g_autoplayIdle && ((elapsed >= 15000 && elapsed < 15300) || (elapsed >= 20000 && elapsed < 20300));
            if (press != g_syntheticA.exchange(press)) XC_LOGI("autoplay: A %s", press ? "down" : "up");
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
                    "rtt %d ms",
                    static_cast<unsigned long long>(st.videoFrames), static_cast<unsigned long long>(st.decodedFrames),
                    static_cast<unsigned long long>(st.droppedFrames), static_cast<unsigned long long>(st.decodeFailures),
                    static_cast<unsigned long long>(st.queueResets), static_cast<unsigned long long>(st.keyframeRequests),
                    static_cast<unsigned long long>(st.queued), static_cast<unsigned long long>(st.audioPackets),
                    static_cast<unsigned long long>(st.rtpPackets), static_cast<unsigned long long>(st.rtpLost),
                    static_cast<unsigned long long>(st.rtpRecovered), static_cast<unsigned long long>(st.rtpNacks),
                    static_cast<unsigned long long>(st.rtpDroppedFrames), static_cast<unsigned long long>(st.rtpKbps),
                    static_cast<unsigned long long>(st.rembKbps), static_cast<unsigned long long>(st.vibrations),
                    st.decodeAvgUs / 1000.0, st.decodeMaxUs / 1000.0, st.drawAvgUs / 1000.0, st.drawMaxUs / 1000.0,
                    static_cast<unsigned long long>(st.lateFrames), st.rttMs);
            if (st.rttMs > 0 && (bestRtt < 0 || st.rttMs < bestRtt)) bestRtt = st.rttMs;
        }
        platform::sleepMs(100);
    }
    {
        std::lock_guard<std::mutex> lock(g_playerMutex);
        g_player = nullptr;
    }
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
    XC_LOGI("starting %s (%s)", game.name.c_str(), game.titleId.c_str());
    g_ui->showLaunching(game, regionName.empty() ? ui::tr(ui::Str::Connecting)
                                                 : ui::trf(ui::Str::TryingRegion, ui::prettyRegion(regionName)));
    std::string err;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        gssv.setResolution(g_settings.resolution == "720p"    ? xcloud::Resolution::P720
                           : g_settings.resolution == "1440p" ? xcloud::Resolution::P1440
                                                              : xcloud::Resolution::P1080);
        const xcloud::Region* region = gssv.session().defaultRegion();
        const std::string& wanted = regionName.empty() ? g_settings.region : regionName;
        for (const auto& r : gssv.session().regions)
            if (r.name == wanted) region = &r;
        if (region) gssv.setRegion(*region);
        XC_LOGI("stream settings: %s, region %s, locale %s", g_settings.resolution.c_str(),
                region ? region->name.c_str() : "?", ui::gameLocale());
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
    for (int i = 0; i < 900 && !g_cancel; ++i) {
        xcloud::SessionStatus st;
        if (!gssv.sessionState(st, err)) {
            result = err;
            break;
        }
        XC_LOGI("session state: %s", st.raw.c_str());
        if (st.state == xcloud::SessionState::WaitingForResources)
            g_ui->setLaunchStatus(ui::trf(ui::Str::InQueue, formatWait(gssv.waitTimeSeconds())));
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

// --- Store prices ---------------------------------------------------------------
// Asked for the games to buy as they come on screen; kept a day in
// <dataDir>/prices.json. The same thread fetches the description of an
// opened game that has none yet.
std::mutex g_priceMutex;
std::string g_priceMarket, g_priceLanguage;  // set by loadLibrary()
platform::Thread g_priceThread;
std::atomic<bool> g_stopPrices{false};
constexpr int64_t kPriceTtlSeconds = 24 * 3600;

std::string pricesPath() { return platform::dataDir() + "/prices.json"; }

ui::PriceInfo priceTexts(const xcloud::Price& p) {
    ui::PriceInfo info;
    info.now = p.list < 0.005 ? ui::tr(ui::Str::Free) : xcloud::formatPrice(p.list, p.currency);
    info.was = p.msrp > p.list + 0.005 ? xcloud::formatPrice(p.msrp, p.currency) : std::string();
    info.list = p.list;
    info.msrp = p.msrp;
    return info;
}

void priceLoop() {
    json::Value cache = json::Value::object();
    {
        std::string text;
        if (platform::readFile(pricesPath(), text))
            if (auto j = json::parse(text)) cache = *j;
    }
    // Fresh cached prices to the UI right away.
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    std::map<std::string, ui::PriceInfo> shown;
    json::Value kept = json::Value::object();
    for (const auto& [id, v] : cache.members()) {
        if (now - v["t"].asInt() > kPriceTtlSeconds) continue;
        kept.set(id, v);
        xcloud::Price p{v["list"].asNumber(), v["msrp"].asNumber(), v["cur"].str()};
        shown[id] = priceTexts(p);
    }
    cache = kept;
    if (!shown.empty()) g_ui->setPrices(shown);
    while (!g_stopPrices) {
        std::string market, language;
        {
            std::lock_guard<std::mutex> lock(g_priceMutex);
            market = g_priceMarket;
            language = g_priceLanguage;
        }
        // The open page's description, for games that only have the light
        // catalog data (games to buy, search results).
        if (std::string id = market.empty() ? std::string() : g_ui->detailWanted(platform::nowMs()); !id.empty()) {
            std::map<std::string, xcloud::Product> full;
            std::string err;
            bool ok = xcloud::fetchProducts({id}, market, language, full, err, true) && full.count(id);
            if (ok) {
                const auto& p = full[id];
                g_ui->setDetailInfo(id, p.description, p.publisher, p.categories, p.heroUrl);
            }
            XC_LOGI("details of %s: %s", id.c_str(), ok ? "ok" : err.empty() ? "not in the catalog" : err.c_str());
        }
        std::vector<std::string> ids = market.empty() ? std::vector<std::string>() : g_ui->pricesWanted(20, true);
        if (ids.empty()) {
            platform::sleepMs(300);
            continue;
        }
        std::map<std::string, xcloud::Price> got;
        std::string err;
        if (!xcloud::fetchPrices(ids, market, language, got, err)) XC_LOGW("%s", err.c_str());
        std::map<std::string, ui::PriceInfo> texts;
        now = static_cast<int64_t>(std::time(nullptr));
        for (const auto& [id, p] : got) {
            texts[id] = priceTexts(p);
            json::Value v = json::Value::object();
            v.set("list", p.list);
            v.set("msrp", p.msrp);
            v.set("cur", p.currency);
            v.set("t", now);
            cache.set(id, v);
        }
        if (!texts.empty()) {
            g_ui->setPrices(texts);
            platform::writeFileAtomic(pricesPath(), cache.dump());
        }
    }
}
void signInAndLoad(auth::AuthManager& am, xcloud::GssvClient& gssv) {
    g_ui->showSplash(ui::tr(am.hasStoredAccount() ? ui::Str::SigningIn : ui::Str::RequestingCode));
    std::string err;
    auto onCode = [](const auth::DeviceCode& dc) { g_ui->showSignIn(dc.userCode, dc.verificationUri); };
    if (!am.signIn(gssv, onCode, err, &g_cancel)) {
        XC_LOGE("sign-in failed: %s", err.c_str());
        g_ui->showError(ui::trf(ui::Str::SignInFailed, err));
        if (!g_autoplayTitle.empty()) XC_LOGI("AUTOPLAY END: sign-in failed");
        return;
    }
    g_ui->setProfile(am.profile().gamertag, am.profile().gamerpicUrl);
    {
        std::lock_guard<std::mutex> lock(g_argMutex);
        g_xblAuth = am.profile().xblAuthorization;
    }
    platform::notify(ui::trf(ui::Str::SignedInAs, am.profile().gamertag));
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
    {
        std::lock_guard<std::mutex> lock(g_priceMutex);
        g_priceMarket = gssv.session().market.empty() ? "US" : gssv.session().market;
        g_priceLanguage = ui::catalogLanguage();
    }
    bool shown = false;
    // Hands the library's current state to the UI (copies).
    auto publish = [](const app::Library& lib) {
        g_ui->setRows(lib.rows());
        g_ui->setOwned(lib.owned(), lib.purchasable(), lib.ownedKnown());
        g_ui->setSearchPools(lib.gamePassSearchPool(), lib.librarySearchPool());
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
        library->loadOwned(owned, changed, &g_stopHydration);
        library->loadCatalogNames(changed, &g_stopHydration);
        library->loadPlatforms(xblAuth, changed, &g_stopHydration);
        library->hydrate(changed, &g_stopHydration);
    });
    if (!g_autoplayTitle.empty() && g_autoplayTitle != "BENCH" && !g_autoplayDetailTest) {
        platform::sleepMs(6000);  // leave the home screen up for ui.ppm
        ui::GameTile tile;
        tile.titleId = g_autoplayTitle;
        tile.name = g_autoplayTitle;
        for (const auto& row : rows)
            for (const auto& t : row.tiles)
                if (t.titleId == g_autoplayTitle && tile.productId.empty()) tile = t;
        std::lock_guard<std::mutex> lock(g_argMutex);
        g_playTile = tile;
        g_command = kPlay;
    }
}

void worker() {
    if (g_autoplayTitle == "BENCH") {
        platform::Thread t;  // same big stack as the stream's video thread
        platform::startThread(t, runDecodeBench);
        t.join();
        for (;;) platform::sleepMs(1000);
    }
    auth::AuthManager am(platform::dataDir() + "/account.json");
    xcloud::GssvClient gssv;
    for (;;) {
        int cmd = g_command.exchange(kNone);
        g_cancel = false;
        switch (cmd) {
            case kSignIn: signInAndLoad(am, gssv); break;
            case kReloadLibrary: loadLibrary(gssv); break;  // e.g. after a language change
            case kSignOut:
                am.signOut();
                g_ui->setProfile({}, {});
                signInAndLoad(am, gssv);
                break;
            case kPlay: {
                ui::GameTile tile;
                {
                    std::lock_guard<std::mutex> lock(g_argMutex);
                    tile = g_playTile;
                }
                bool failed = false;
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
                if (failed)
                    g_ui->showError(result.rfind("ERROR: ", 0) == 0 ? result.substr(7) : result);
                else
                    g_ui->showHome(ui::tr(ui::Str::StreamEnded));
                autoplayFinished(result);
                break;
            }
            default: platform::sleepMs(50); break;
        }
    }
}

// --- Input ---------------------------------------------------------------------

// D-pad / left stick with key repeat: first press, then every 110 ms after
// 350 ms held.
class Repeater {
public:
    bool update(bool held, uint64_t now) {
        if (!held) {
            since_ = 0;
            return false;
        }
        if (!since_) {
            since_ = next_ = now;
            next_ += 350;
            return true;
        }
        if (now >= next_) {
            next_ = now + 110;
            return true;
        }
        return false;
    }

private:
    uint64_t since_ = 0, next_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    platform::init();
    log::setFile((platform::dataDir() + "/xcloud.log").c_str());
    XC_LOGI("=== PSBox Cloud Gaming starting ===");
    loadAutoplay();
    g_settings.load(settingsPath());
    ui::setLanguage(ui::languageFromCode(g_settings.language));
    XC_LOGI("settings: language %s, %s, region %s", g_settings.language.c_str(), g_settings.resolution.c_str(),
            g_settings.region.empty() ? "auto" : g_settings.region.c_str());
    sceSystemServiceHideSplashScreen();

    bool haveDisplay = display::init();
    if (!input::init()) XC_LOGE("controller init failed");

    ui::Fonts fonts;
    if (!fonts.load(platform::assetDir() + "/fonts")) XC_LOGE("fonts missing in %s", platform::assetDir().c_str());
    g_images = std::make_unique<ui::ImageCache>(
        [] {
            if (g_ui) g_ui->invalidate();
        },
        160u << 20, platform::dataDir() + "/imgcache");
    g_ui = std::make_unique<ui::AppUi>(fonts, *g_images);
    platform::startThread(g_priceThread, priceLoop);
    {
        ui::SettingsChoice choice;
        choice.language = static_cast<int>(ui::language());
        choice.resolution = g_settings.resolution == "720p" ? 1 : g_settings.resolution == "1440p" ? 2 : 0;
        choice.region = g_settings.region;
        g_ui->setSettings(choice);
        g_ui->setRegionLatency(g_settings.regionRtt);
        g_ui->setPrefs(g_settings.hidden, g_settings.librarySort == "az"        ? ui::LibrarySort::AZ
                                          : g_settings.librarySort == "console" ? ui::LibrarySort::Console
                                                                                : ui::LibrarySort::Recent);
    }
    ui::Canvas canvas(display::kWidth, display::kHeight);

    if (!net::initTls(platform::caBundlePath())) {
        platform::notify("xCloud: could not load the TLS certificates");
        g_ui->showError("Could not load " + platform::caBundlePath());
    } else {
        std::thread(worker).detach();
    }

    // Never return from main: the app is closed from the home screen.
    input::ControllerState prev{}, pad{};
    Repeater up, down, left, right;
    uint64_t exitHeldSince = 0;
    uint64_t homeSince = 0, launchSince = 0;
    bool uiSaved = false, launchSaved = false;
    for (;;) {
        input::poll(pad);
        // Autoplay runs unattended: the physical pad must not interfere.
        if (!g_autoplayTitle.empty()) pad = input::ControllerState{};
        uint64_t now = platform::nowMs();

        if (g_ui->screen() == ui::Screen::Streaming) {
            {
                std::lock_guard<std::mutex> lock(g_playerMutex);
                if (g_player) {
                    input::ControllerState sent = pad;
                    if (g_syntheticA) sent.btnA = true;
                    g_player->sendInput(sent);
                }
            }
            // OPTIONS + TOUCHPAD held for a second leaves the game.
            if (pad.btnOptions && pad.btnTouchpad) {
                if (!exitHeldSince) exitHeldSince = now;
                if (now - exitHeldSince > 1000) g_cancel = true;
            } else {
                exitHeldSince = 0;
            }
            prev = pad;
            platform::sleepMs(8);  // ~120 Hz input
            continue;
        }

        ui::NavInput nav;
        nav.up = up.update(pad.dpadUp || pad.leftStickY < -0.6f, now);
        nav.down = down.update(pad.dpadDown || pad.leftStickY > 0.6f, now);
        nav.left = left.update(pad.dpadLeft || pad.leftStickX < -0.6f, now);
        nav.right = right.update(pad.dpadRight || pad.leftStickX > 0.6f, now);
        nav.accept = pad.btnA && !prev.btnA;
        nav.back = pad.btnB && !prev.btnB;
        nav.options = pad.btnOptions && !prev.btnOptions;
        nav.l1 = pad.btnL1 && !prev.btnL1;
        nav.r1 = pad.btnR1 && !prev.btnR1;
        nav.square = pad.btnX && !prev.btnX;    // Square (Xbox X)
        nav.triangle = pad.btnY && !prev.btnY;  // Triangle (Xbox Y)
        nav.r3 = pad.btnR3 && !prev.btnR3;
        nav.l2 = pad.triggerL2 > 0.5f && prev.triggerL2 <= 0.5f;
        nav.r2 = pad.triggerR2 > 0.5f && prev.triggerR2 <= 0.5f;
        nav.touchpad = pad.btnTouchpad;
        nav.nowMs = now;
        prev = pad;

        ui::UiEvent ev = g_ui->handle(nav);
        switch (ev.action) {
            case ui::Action::Play: {
                std::lock_guard<std::mutex> lock(g_argMutex);
                g_playTile = ev.game;
                g_command = kPlay;
                break;
            }
            case ui::Action::SignOut:
                g_cancel = true;
                g_command = kSignOut;
                break;
            case ui::Action::Retry: g_command = kSignIn; break;
            case ui::Action::CancelLaunch: g_cancel = true; break;
            case ui::Action::PrefsChanged: {
                std::lock_guard<std::mutex> lock(g_settingsMutex);
                g_settings.hidden = ev.hidden;
                g_settings.librarySort = ev.librarySort == ui::LibrarySort::AZ        ? "az"
                                         : ev.librarySort == ui::LibrarySort::Console ? "console"
                                                                                       : "recent";
                if (!g_settings.save(settingsPath())) XC_LOGW("could not save settings");
                XC_LOGI("prefs saved: %zu hidden, sort %s", g_settings.hidden.size(), g_settings.librarySort.c_str());
                break;
            }
            case ui::Action::SettingsChanged: {
                bool languageChanged;
                {
                    std::lock_guard<std::mutex> lock(g_settingsMutex);
                    std::string code = ui::languageCode(static_cast<ui::Language>(ev.settings.language));
                    languageChanged = code != g_settings.language;
                    g_settings.language = code;
                    g_settings.resolution = ev.settings.resolution == 1   ? "720p"
                                            : ev.settings.resolution == 2 ? "1440p"
                                                                          : "1080p";
                    g_settings.region = ev.settings.region;
                    if (!g_settings.save(settingsPath())) XC_LOGW("could not save settings");
                    XC_LOGI("settings saved: language %s, %s, region %s", code.c_str(), g_settings.resolution.c_str(),
                            g_settings.region.empty() ? "auto" : g_settings.region.c_str());
                }
                // Row titles and game details come from the catalog in the
                // chosen language.
                if (languageChanged) g_command = kReloadLibrary;
                break;
            }
            case ui::Action::None: break;
        }

        // Autoplay: save the rendered home and loading screens.
        auto saveCanvas = [&](const char* name) {
            std::string ppm = "P6\n1920 1080\n255\n";
            ppm.reserve(ppm.size() + 1920u * 1080u * 3u);
            for (size_t i = 0; i < 1920u * 1080u; ++i) {
                uint32_t p = canvas.data()[i];
                ppm += static_cast<char>(p & 0xFF);
                ppm += static_cast<char>((p >> 8) & 0xFF);
                ppm += static_cast<char>((p >> 16) & 0xFF);
            }
            XC_LOGI("ui snapshot %s: %s", name,
                    platform::writeFileAtomic(platform::dataDir() + "/" + name, ppm) ? "ok" : "failed");
        };
        if (!g_autoplayTitle.empty() && !launchSaved && g_ui->screen() == ui::Screen::Launching) {
            if (!launchSince) launchSince = now;
            if (now - launchSince > 8000) {
                launchSaved = true;
                saveCanvas("launch.ppm");
            }
        }
        if (g_autoplayDetailTest && uiSaved) {
            // A game to buy far down the list (no prefetched details): its
            // page must fill in on its own.
            static uint64_t openedAt = 0;
            ui::GameTile tile;
            if (!openedAt && g_ui->purchasableAt(40, tile)) {
                XC_LOGI("autoplay: opening %s (%s)", tile.name.c_str(), tile.productId.c_str());
                g_ui->showDetails(tile);
                openedAt = now;
            } else if (openedAt && now - openedAt > 8000) {
                saveCanvas("detail.ppm");
                g_autoplayDetailTest = false;
                XC_LOGI("AUTOPLAY END: detail test");
            }
        }
        if (!g_autoplayTitle.empty() && !uiSaved && g_ui->screen() == ui::Screen::Home) {
            if (!homeSince) homeSince = now;
            if (now - homeSince > 5000) {
                uiSaved = true;
                saveCanvas("ui.ppm");
            }
        }

        if (haveDisplay && g_ui->needsRedraw(now)) {
            uint64_t t0 = platform::nowMs();
            g_ui->render(canvas, now);
            uint64_t t1 = platform::nowMs();
            static int logged = 0;
            if (logged < 5 && g_ui->screen() == ui::Screen::Home) {
                ++logged;
                XC_LOGI("ui render %llu ms", static_cast<unsigned long long>(t1 - t0));
            }
            std::lock_guard<std::mutex> lock(display::frameMutex());
            if (g_ui->screen() != ui::Screen::Streaming) {
                display::drawRgba(canvas.data());
                display::present();  // paced by the next draw waiting for a free buffer
            }
        } else {
            platform::sleepMs(8);
        }
    }
}
