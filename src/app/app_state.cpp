// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The state the app's threads share (app_state.h).
#include "app/app_state.h"

#include "platform/platform.h"

#include <algorithm>

namespace xc::app {

std::unique_ptr<ui::AppUi> g_ui;
std::mutex g_settingsMutex;
Settings g_settings;
std::atomic<uint64_t> g_settingsVersion{1};
std::string settingsPath() { return platform::dataDir() + "/settings.json"; }
bool allow1440Locked() { return std::max(g_settings.maxHeightCloud, g_settings.maxHeightHome) >= 1440; }
void refreshSettingsUiLocked() {
    ++g_settingsVersion;
    if (g_ui) g_ui->setSettings(ui::choiceFromSettings(g_settings));
}
std::atomic<int> g_command{kSignIn};
std::atomic<bool> g_cancel{false};
std::mutex g_argMutex;
ui::GameTile g_playTile;
std::mutex g_playerMutex;
StreamPlayer* g_player = nullptr;
std::mutex g_infoMutex;
ui::StreamInfo g_streamInfo;
std::atomic<uint32_t> g_infoSeq{0};
std::atomic<bool> g_playingHome{false};
std::atomic<bool> g_tierPicked{false};
std::atomic<uint64_t> g_xboxButtonUntil{0};
std::atomic<bool> g_restartWanted{false};
std::atomic<bool> g_quit{false};

}  // namespace xc::app
