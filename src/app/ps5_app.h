// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// What the PS5 app's pieces share: ps5_main.cpp (the main thread: pad, UI,
// drawing), worker.cpp (the worker thread: all the network work),
// stream_screen.cpp (the streaming screen) and price_loop.cpp. Defined in
// ps5_main.cpp.
#pragma once

#include "app/settings.h"
#include "ui/app_ui.h"
#include "ui/stream_menu.h"

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

// --- Worker commands (worker.cpp) ----------------------------------------------
enum Command { kNone, kSignIn, kPlay, kSignOut, kReloadLibrary, kConsoles, kUpdate };
extern std::atomic<int> g_command;
extern std::atomic<bool> g_cancel;
extern std::mutex g_argMutex;
extern ui::GameTile g_playTile;  // what kPlay plays
void worker();
// The update pop-up's "Not now": that release isn't offered again on start.
void skipOfferedUpdate();

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

// --- Store prices (price_loop.cpp) -----------------------------------------------
void startPriceLoop();
void setPriceMarket(const std::string& market, const std::string& language);

}  // namespace xc::app
