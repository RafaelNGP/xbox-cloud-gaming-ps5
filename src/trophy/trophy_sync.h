// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include "trophy/trophy_backend.h"
#include "trophy/trophy_types.h"
#include "ui/app_ui.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace xc::trophy {

class TrophySyncEngine {
public:
    TrophySyncEngine();
    explicit TrophySyncEngine(std::unique_ptr<ITrophyBackend> backend);
    ~TrophySyncEngine();

    // Initializes engine with user ID (default 0 detects active user)
    bool init(uint32_t userId = 0);

    // Sync achievements for a single title against Xbox Live and PlayStation backend
    bool syncTitleAchievements(const std::string& xblAuth, const std::string& xuid,
                               const std::string& xboxTitleId, const std::string& titleName,
                               int& newlyUnlocked, std::string& err,
                               const std::string& tileUrl = "",
                               bool fetchTrophyIcons = true,
                               int expectedEarned = -1,
                               int expectedGamerscore = -1);

    // Called every 20s during active stream (non-blocking async dispatch)
    bool pollActiveStream(const std::string& xblAuth, const std::string& xuid,
                          const std::string& xboxTitleId, const std::string& titleName,
                          const std::string& tileUrl = "");

    // Retro-synchronization (Back-Sync) for all titles in the user's account
    bool syncAllAccountTrophies(const std::string& xblAuth, const std::string& xuid,
                               const std::vector<ui::GameTile>& ownedGames,
                               int& totalTitles, int& totalTrophies, std::string& err,
                               const std::function<bool()>& cancelCheck = nullptr,
                               const std::function<void(int done, int total, const std::string& title)>& progressCb = nullptr,
                               bool forceFetchMissingIcons = true,
                               const std::function<bool(const std::string& xboxTitleId, const std::string& titleName)>& isTitleEnabled = nullptr);

    // Removes all local trophies and packages created by PSBox
    bool removeAllTrophies(int& removedCount, std::string& err);

    // Swap backend (useful for testing or platform initialization)
    void setBackend(std::unique_ptr<ITrophyBackend> backend);
    ITrophyBackend* backend() const { return backend_.get(); }

    bool isSyncing() const;

    std::string trophyCacheDir() const;
    std::string trophyCachePath(const std::string& xboxTitleId) const;

private:
    std::unique_ptr<ITrophyBackend> backend_;
    std::mutex mutex_;
    std::atomic<bool> pollInFlight_{false};
    std::atomic<int> syncingDepth_{0};
    bool initialized_ = false;
};

// Global singleton instance
TrophySyncEngine& syncEngine();

// --- Dedicated Background Trophy Sync Thread (g_trophyThread) ---

// Thread lifecycle
void startTrophyThread();
bool stopTrophyThread(uint32_t timeoutMs = 3000);
void resumeTrophySync();

// Asynchronous dispatch requests
void requestTrophySync(bool forceFetchIcons = true);
void requestTrophySync(const std::string& xblAuth, const std::string& xuid,
                       const std::vector<ui::GameTile>& ownedGames = {},
                       bool forceFetchIcons = true);
void cancelTrophySync();
void requestRemoveTrophies();

// Stream real-time achievement poll (bypasses batch queue, urgent priority)
bool dispatchStreamPoll(const std::string& xblAuth, const std::string& xuid,
                        const std::string& xboxTitleId, const std::string& titleName,
                        const std::string& tileUrl = "");

// Status queries
bool isSyncing();
int titlesDone();
int titlesTotal();

struct TrophySyncContext {
    std::string xblAuth;
    std::string xuid;
    std::vector<ui::GameTile> ownedGames;
    std::function<bool(const std::string& xboxTitleId, const std::string& titleName)> isTitleEnabled;
    std::function<bool()> isStreaming;
    std::function<void(bool loading, int done, int total, const std::string& title)> progressCb;
    std::function<void(int totalTitles, int totalTrophies, const std::string& err)> completeCb;
};

using TrophyContextProvider = std::function<bool(TrophySyncContext& ctx)>;
void setTrophyContextProvider(TrophyContextProvider provider);

}  // namespace xc::trophy
