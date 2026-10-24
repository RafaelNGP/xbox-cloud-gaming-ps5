// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include "app/app_state.h"
#include "app/library.h"
#include "auth/auth_manager.h"
#include "platform/platform.h"
#include "xcloud/gssv.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace xc::app {

extern std::atomic<bool> g_stopHydration;
extern platform::Thread g_hydrationThread;
void stopHydration();

extern bool g_probing;
extern std::string g_afterProbeAlias;
extern bool g_gameClosedOnServer;
extern std::map<std::string, std::string> g_regionFallback;
extern std::shared_ptr<app::Library> s_library;
extern std::mutex s_libraryMutex;
extern int64_t s_playtimeRemainingSeconds;
extern std::string g_xblAuth;
extern std::string g_xuid;
extern std::string g_subscription;

bool keepSignedIn(auth::AuthManager& am, xcloud::GssvClient& gssv, std::string& err, bool force = false);
std::string play(auth::AuthManager& am, xcloud::GssvClient& gssv, const ui::GameTile& game, bool& failed,
                 const std::string& overrideRegion = {});
void loadLibrary(auth::AuthManager& am, xcloud::GssvClient& gssv, bool forceRefresh = false);
void checkForUpdate();
void runUpdate();
void skipOfferedUpdate();
void runSignInCheck(auth::AuthManager& am, xcloud::GssvClient& gssv);
void runNetworkTest(xcloud::GssvClient& gssv);

}  // namespace xc::app
