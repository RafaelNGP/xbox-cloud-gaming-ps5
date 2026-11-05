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
                               int& newlyUnlocked, std::string& err);

    // Called every 20s during active stream (non-blocking async dispatch)
    bool pollActiveStream(const std::string& xblAuth, const std::string& xuid,
                          const std::string& xboxTitleId, const std::string& titleName);

    // Retro-synchronization (Back-Sync) for all titles in the user's account
    bool syncAllAccountTrophies(const std::string& xblAuth, const std::string& xuid,
                               const std::vector<ui::GameTile>& ownedGames,
                               int& totalTitles, int& totalTrophies, std::string& err,
                               const std::function<bool()>& cancelCheck = nullptr,
                               const std::function<void(int done, int total)>& progressCb = nullptr);

    // Removes all local trophies and packages created by PSBox
    bool removeAllTrophies(int& removedCount, std::string& err);

    // Swap backend (useful for testing or platform initialization)
    void setBackend(std::unique_ptr<ITrophyBackend> backend);
    ITrophyBackend* backend() const { return backend_.get(); }

    bool isSyncing() const { return syncing_.load(); }

private:
    std::string trophyCacheDir() const;
    std::string trophyCachePath(const std::string& xboxTitleId) const;

    std::unique_ptr<ITrophyBackend> backend_;
    std::mutex mutex_;
    std::atomic<bool> pollInFlight_{false};
    std::atomic<bool> syncing_{false};
    bool initialized_ = false;
};

// Global singleton instance
TrophySyncEngine& syncEngine();

}  // namespace xc::trophy
