// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The worker thread: sign-in, the library, the account's consoles, and
// playing a title (queue -> /connect -> Provisioned -> StreamPlayer). It
// owns AuthManager and the GssvClients and does all the network work.
#include "app/worker_internal.h"

#include "app/autoplay.h"
#include "app/crash_guard.h"
#include "app/shell.h"
#include "app/stream_player.h"
#include "net/http.h"
#include "platform/platform.h"
#include "ui/strings.h"
#include "util/log.h"
#include "xcloud/catalog.h"
#include "xcloud/purchase.h"
#include "xcloud/regions.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <map>
#include <memory>
#include <thread>
#include <unistd.h>
#include <vector>

namespace xc::app {

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

std::shared_ptr<Library> currentLibrary() {
    std::lock_guard<std::mutex> lock(s_libraryMutex);
    return s_library;
}

std::string g_xblAuth;
std::string g_xuid;

// The sign-in renewed when it is about to expire (AuthManager::ensureFresh;
// `force`: a request was refused), before the xCloud calls. What other
// threads hold follows a renewal: titlehub's token, and the copy of the
// cloud client the queue estimates use.
bool keepSignedIn(auth::AuthManager& am, xcloud::GssvClient& gssv, std::string& err, bool force) {
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

}  // namespace

std::string g_subscription;

namespace {

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
    bool preview = false;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        country = g_settings.loginCountry;
        preview = g_settings.previewFeatures;
    }
    gssv.setLoginCountry(country);
    gssv.setPreviewFeatures(preview);
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
    if (g_ui->screen() == ui::Screen::SignIn) {
        g_ui->showSplash(ui::tr(ui::Str::LoadingGames));
    }
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

}  // namespace

void loadLibrary(auth::AuthManager& am, xcloud::GssvClient& gssv, bool forceRefresh) {
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        gssv.setPreviewFeatures(g_settings.previewFeatures);
    }
    std::string err;
    if (!keepSignedIn(am, gssv, err)) XC_LOGW("sign-in renewal: %s", err.c_str());
    err.clear();

    std::shared_ptr<app::Library> existingLib;
    {
        std::lock_guard<std::mutex> lock(s_libraryMutex);
        existingLib = s_library;
    }

    if (!forceRefresh && existingLib && (existingLib->ownedKnown() || !existingLib->rows().empty())) {
        stopHydration();
        existingLib->setSubscription(g_subscription);
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            existingLib->setPlaytime(g_settings.titlePlaytime);
        }
        setPriceMarket(gssv.session().market.empty() ? "US" : gssv.session().market, ui::catalogLanguage());
        setQueueClient(&gssv);
        XC_LOGI("library: delta-sync against active library (forceRefresh=%d)", forceRefresh);
        if (g_ui->screen() == ui::Screen::Splash || g_ui->screen() == ui::Screen::SignIn) {
            g_ui->showHome();
        }
        g_ui->setLoading(true, 0, 0);
        auto publish = [](const app::Library& lib) {
            auto p = lib.progress();
            g_ui->setCatalogState(lib.rows(), lib.owned(), lib.purchasable(), lib.ownedKnown(),
                                  lib.gamePassMember(), lib.gamePassSearchPool(), lib.librarySearchPool(),
                                  p.active, p.done, p.total);
        };
        size_t newCount = existingLib->syncDelta(gssv, [&] { publish(*existingLib); }, err, ui::catalogLanguage());
        g_ui->setLoading(false, 0, 0);
        publish(*existingLib);

        if (newCount > 0) {
            std::string msg = ui::trf(ui::Str::LibraryUpdatedCount, std::to_string(newCount));
            if (!shell::caps().console) g_ui->showToast(msg);
            platform::notify(msg);
        } else if (forceRefresh) {
            std::string msg = ui::tr(ui::Str::LibraryUpToDate);
            if (!shell::caps().console) g_ui->showToast(msg);
            platform::notify(msg);
        }

        std::vector<ui::GameRow> rows = existingLib->rows();
        g_stopHydration = false;
        std::string xblAuth;
        {
            std::lock_guard<std::mutex> lock(g_argMutex);
            xblAuth = g_xblAuth;
        }
        platform::startThread(g_hydrationThread, [existingLib, publish, xblAuth] {
            platform::setThreadAffinity(platform::ThreadRole::Worker, "xcloud_hydr");
            auto changed = [&] { publish(*existingLib); };
            existingLib->loadFirstScreen(xblAuth, changed, &g_stopHydration);
            existingLib->loadFriends(xblAuth, changed, &g_stopHydration);
            existingLib->loadPlatforms(xblAuth, changed, &g_stopHydration);
            existingLib->hydrate(changed, &g_stopHydration);
            existingLib->loadFriends(xblAuth, changed, &g_stopHydration);
            g_ui->setLoading(false, 0, 0);
        });
        if (!g_autoplay.title.empty() && g_autoplay.title != "BENCH" && g_autoplay.title != "UPDATE" && g_autoplay.title != "AUTOTEST" &&
            !g_autoplay.detailTest && !g_autoplay.libraryTest &&
            !g_autoplay.consolesTab && !g_autoplay.settingsTest && !g_autoplay.keysTest && !g_autoplay.padMapTest &&
            !g_autoplay.imeTest && !g_autoplay.vibeTest && !g_autoplay.searchTest && !g_autoplay.quickTest && !g_autoplay.crashTest && !g_autoplay.scrollTest &&
            !g_autoplay.gridTest && !g_autoplay.gameSettingsTest && !g_autoplay.publisherTest && !g_autoplay.friendsTest &&
            !g_autoplay.regionTest && !g_autoplay.mw4Test && !g_autoplay.v150Test && !g_autoplay.claimTest) {
            platform::sleepMs(6000);
            auto matchTile = [](const ui::GameTile& t, const std::string& target) {
                auto eq = [](std::string_view a, std::string_view b) {
                    if (a.size() != b.size()) return false;
                    for (size_t i = 0; i < a.size(); ++i)
                        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                            return false;
                    return true;
                };
                return eq(t.titleId, target) || eq(t.productId, target) || eq(t.name, target);
            };
            ui::GameTile tile;
            tile.titleId = g_autoplay.title;
            tile.name = g_autoplay.title;
            for (const auto& row : rows)
                for (const auto& t : row.tiles)
                    if (matchTile(t, g_autoplay.title) && tile.productId.empty()) tile = t;
            std::lock_guard<std::mutex> lock(g_argMutex);
            g_playTile = tile;
            g_command = kPlay;
        }
        return;
    }

    bool onSplash = (g_ui->screen() == ui::Screen::Splash || g_ui->screen() == ui::Screen::SignIn);
    if (onSplash) {
        g_ui->showSplash(ui::tr(ui::Str::LoadingGames));
    } else {
        g_ui->setLoading(true, 0, 0);
    }
    stopHydration();
    auto library = std::make_shared<app::Library>();
    {
        std::lock_guard<std::mutex> lock(s_libraryMutex);
        if (existingLib) {
            library->setAcquired(existingLib->acquiredProducts(), existingLib->acquiredTitles());
        }
        s_library = library;
    }
    library->setCachePath(platform::dataDir() + "/library_cache.json");
    library->setSubscription(g_subscription);
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        library->setFavorites(g_settings.favorites);
        library->setWishlist(g_settings.wishlist);
        library->setPlaytime(g_settings.titlePlaytime);
    }
    setPriceMarket(gssv.session().market.empty() ? "US" : gssv.session().market, ui::catalogLanguage());
    setQueueClient(&gssv);
    bool shown = !onSplash;
    // Hands the library's current state to the UI (coalesced into a single rebuild).
    auto publish = [](const app::Library& lib) {
        auto p = lib.progress();
        g_ui->setCatalogState(lib.rows(), lib.owned(), lib.purchasable(), lib.ownedKnown(),
                              lib.gamePassMember(), lib.gamePassSearchPool(), lib.librarySearchPool(),
                              p.active, p.done, p.total);
    };
    bool ok = library->load(gssv, ui::catalogLanguage(),
                            [&] {
                                publish(*library);
                                if (!shown) {
                                    shown = true;
                                    if (g_ui->screen() == ui::Screen::Splash || g_ui->screen() == ui::Screen::SignIn) {
                                        g_ui->showHome();
                                    }
                                }
                            },
                            err,
                            forceRefresh);
    if (!ok) {
        ui::GameTile dummy;
        if (g_ui->screen() == ui::Screen::Home || g_ui->firstTile(dummy)) {
            if (g_ui->screen() == ui::Screen::Splash || g_ui->screen() == ui::Screen::SignIn) {
                g_ui->showHome();
            }
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
    if (forceRefresh) {
        std::string msg = ui::tr(ui::Str::LibraryUpToDate);
        if (!shell::caps().console) g_ui->showToast(msg);
        platform::notify(msg);
    }
    if (g_ui->screen() == ui::Screen::Splash || g_ui->screen() == ui::Screen::SignIn) {
        g_ui->showHome();
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
        g_ui->setLoading(false, 0, 0);
    });
    if (!g_autoplay.title.empty() && g_autoplay.title != "BENCH" && g_autoplay.title != "UPDATE" && g_autoplay.title != "AUTOTEST" &&
        !g_autoplay.detailTest && !g_autoplay.libraryTest &&
        !g_autoplay.consolesTab && !g_autoplay.settingsTest && !g_autoplay.keysTest && !g_autoplay.padMapTest &&
        !g_autoplay.imeTest && !g_autoplay.vibeTest && !g_autoplay.searchTest && !g_autoplay.quickTest && !g_autoplay.crashTest && !g_autoplay.scrollTest &&
        !g_autoplay.gridTest && !g_autoplay.gameSettingsTest && !g_autoplay.publisherTest && !g_autoplay.friendsTest &&
        !g_autoplay.regionTest && !g_autoplay.mw4Test && !g_autoplay.v150Test && !g_autoplay.claimTest) {
        platform::sleepMs(6000);  // leave the home screen up for ui.ppm
        ui::GameTile tile;
        tile.titleId = g_autoplay.title;
        tile.name = g_autoplay.title;
        auto matchTile = [](const ui::GameTile& t, const std::string& target) {
            auto eq = [](std::string_view a, std::string_view b) {
                if (a.size() != b.size()) return false;
                for (size_t i = 0; i < a.size(); ++i)
                    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                        return false;
                return true;
            };
            return eq(t.titleId, target) || eq(t.productId, target) || eq(t.name, target);
        };
        for (const auto& row : rows)
            for (const auto& t : row.tiles)
                if (matchTile(t, g_autoplay.title) && tile.productId.empty()) tile = t;
        std::lock_guard<std::mutex> lock(g_argMutex);
        g_playTile = tile;
        g_command = kPlay;
    }
}

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
                if (am.signedIn()) loadLibrary(am, gssv, true);
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
                bool isEntitlementErr = result.find("NoEntitlement") != std::string::npos ||
                                        result.find("NotEntitled") != std::string::npos ||
                                        result.find("PurchasableProductRequired") != std::string::npos ||
                                        result.find("LicenseRequired") != std::string::npos;
                if (failed && isEntitlementErr) {
                    bool isFreeUser = g_ui->isFreeAccount();
                    bool isF2P = g_ui->isFreeGame(tile) || tile.freeInStore;
                    bool previewActive = false;
                    {
                        std::lock_guard<std::mutex> lock(g_settingsMutex);
                        previewActive = g_settings.previewFeatures;
                    }
                    if (isFreeUser && isF2P) {
                        if (!previewActive) {
                            g_ui->showPlayError(ui::tr(ui::Str::F2pPreviewRequiredDesc), tile, "https://www.xbox.com/play");
                        } else {
                            g_ui->showPlayError(ui::tr(ui::Str::F2pPreviewAccountNeededDesc), tile, "https://www.xbox.com/play");
                        }
                    } else {
                        g_ui->showPlayError(ui::trf(tile.freeInStore ? ui::Str::NoEntitlementFree : ui::Str::NoEntitlement,
                                                    tile.name),
                                            tile);
                    }
                } else if (failed) {
                    if (net::lastTlsProblem() == net::TlsProblem::Clock) {
                        g_ui->showError(ui::trf(shell::caps().console ? ui::Str::ClockWrong : ui::Str::ClockWrongPc, net::todayText()));
                    } else if (net::lastTlsProblem() == net::TlsProblem::Untrusted ||
                               net::lastTlsProblem() == net::TlsProblem::NameMismatch) {
                        g_ui->showError(ui::tr(ui::Str::ConnectionIntercepted));
                    } else if (isDnsOrSinkholeError(result)) {
                        g_ui->showError(ui::tr(ui::Str::DnsBlockedNanoDns));
                    } else {
                        g_ui->showPlayError(result.rfind("ERROR: ", 0) == 0 ? result.substr(7) : result, tile);
                    }
                }
                else
                    g_ui->showHome(endedToast(result));
                autoplayFinished(result);
                break;
            }
            case kClaimFree: {
                stopHydration();
                ui::GameTile tile;
                {
                    std::lock_guard<std::mutex> lock(g_argMutex);
                    tile = g_playTile;
                }
                if (!shell::caps().console) {
                    g_ui->showToast(ui::tr(ui::Str::Claiming));
                }
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
                    std::shared_ptr<app::Library> lib;
                    {
                        std::lock_guard<std::mutex> lock(s_libraryMutex);
                        lib = s_library;
                    }
                    if (lib) {
                        lib->markUnclaimable(tile.productId);
                    }
                    g_ui->markUnclaimable(tile.productId);
                    tile.freeInStore = false;
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
                    g_ui->setLoading(true, 0, 0);
                    lib->markAsOwned(tile.productId, tile.titleId, tile.name);
                    g_ui->markTileOwned(tile.productId, tile.titleId);
                    if (!shell::caps().console) {
                        g_ui->showToast(ui::tr(ui::Str::ClaimSuccess));
                    }
                    platform::notify(ui::tr(ui::Str::ClaimSuccess));

                    // Re-sync owned titles in background
                    lib->loadOwned(gssv, [lib]() {
                        auto p = lib->progress();
                        g_ui->setCatalogState(lib->rows(), lib->owned(), lib->purchasable(), lib->ownedKnown(),
                                              lib->gamePassMember(), lib->gamePassSearchPool(), lib->librarySearchPool(),
                                              p.active, p.done, p.total);
                    });
                    g_ui->setLoading(false, 0, 0);
                }
                tile.playable = true;
                tile.purchasable = false;
                if (g_ui->screen() == ui::Screen::Details || g_ui->screen() == ui::Screen::Error) {
                    g_ui->showDetails(tile);
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
