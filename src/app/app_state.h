// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// What the app's pieces share: app_core.cpp (the main thread: pad, UI,
// drawing), worker.cpp (the worker thread: all the network work),
// stream_screen.cpp (the streaming screen) and price_loop.cpp. Defined in
// app_state.cpp.
#pragma once

#include "app/settings.h"
#include "ui/app_ui.h"
#include "ui/stream_menu.h"
#include "xcloud/gssv.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace xc::app {

class StreamPlayer;

extern std::unique_ptr<ui::AppUi> g_ui;

// User settings (settings.json); read by the worker, changed by the UI thread.
extern std::mutex g_settingsMutex;
extern Settings g_settings;
std::string settingsPath();
// Caller holds g_settingsMutex. 1440p is offered once a stream delivered it.
bool allow1440Locked();
// Settings' screen takes g_settings again (ui::choiceFromSettings) after
// something outside it changed them (the game menu, a test putting them
// back): else leaving Settings would save its older copy over them. Caller
// holds g_settingsMutex (then AppUi's lock is taken: the usual order). Not
// while Settings is open with changes of its own (it would drop them).
void refreshSettingsUiLocked();

// --- Worker commands (worker.cpp) ----------------------------------------------
enum Command { kNone, kSignIn, kPlay, kSignOut, kReloadLibrary, kConsoles, kUpdate, kClaimFree, kCheckSignIn, kNetworkTest };
extern std::atomic<int> g_command;
extern std::atomic<bool> g_cancel;
extern std::mutex g_argMutex;
extern ui::GameTile g_playTile;  // what kPlay plays
void worker();
// The worker thread started (detached), as coreStartup() does.
void startWorker();
// The update pop-up's "Not now": that release isn't offered again on start.
void skipOfferedUpdate();
// Updates the running library's favorites list and refreshes UI rows.
void updateLibraryFavorites(const std::vector<std::string>& favorites);
// Updates the running library's wishlist and refreshes UI rows.
void updateLibraryWishlist(const std::vector<std::string>& wishlist);

// The running stream, for the input thread.
extern std::mutex g_playerMutex;
extern StreamPlayer* g_player;
// The stream's numbers for the in-game menu, refreshed every second by the
// worker; the sequence number tells the main thread something changed.
extern std::mutex g_infoMutex;
extern ui::StreamInfo g_streamInfo;
extern std::atomic<uint32_t> g_infoSeq;
extern std::atomic<bool> g_playingHome;          // the stream is the user's own Xbox
extern std::atomic<bool> g_tierPicked;           // the game menu chose a tier: the probe leaves it
extern std::atomic<uint64_t> g_xboxButtonUntil;  // the Xbox button, held until then (ms)
// An update is in place: the main thread starts the app again (the system
// call that does it takes the app down when made from another thread).
extern std::atomic<bool> g_restartWanted;

// --- Closing (desktop: the PS5 is closed by the system, never by the app) ------
// The app is closing: the worker leaves its loop after what it is doing is
// cancelled (g_cancel): a stream is stopped and its xCloud session closed,
// so the account's streaming slot is free at once.
extern std::atomic<bool> g_quit;
void requestQuit();
// After requestQuit(): true once the worker has left its loop (with the
// threads it started), false if `ms` went by first (a request hanging on
// the network). True at once when it never started.
bool waitForWorker(uint32_t ms);

// --- Store prices (price_loop.cpp) -----------------------------------------------
void startPriceLoop();
// Closing: the price loop told to end; true once it has (joined) within
// `ms`, else it is left to the process's exit.
bool stopPriceLoop(uint32_t ms);
void setPriceMarket(const std::string& market, const std::string& language);
// Auth tokens for title achievements and store features.
void setPriceAuth(const std::string& xblAuth, const std::string& xuid);
// The signed-in xCloud client (a copy), for the open game page's queue
// estimate; nullptr after signing out.
void setQueueClient(const xcloud::GssvClient* gssv);

}  // namespace xc::app
