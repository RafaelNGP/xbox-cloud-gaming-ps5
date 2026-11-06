// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Xbox Live Achievements service.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace xc::xcloud {

struct Achievement {
    std::string id;
    std::string name;
    std::string description;
    std::string lockedDescription;
    std::string iconUrl;
    int gamerscore = 0;
    bool unlocked = false;
    bool isSecret = false;
    std::string timeUnlocked;
};

struct TitleAchievements {
    int unlockedCount = 0;
    int totalCount = 0;
    int unlockedGamerscore = 0;
    int totalGamerscore = 0;
    std::vector<Achievement> items;
};

// Fetches achievements for a title from achievements.xboxlive.com.
// `xblAuth`: "XBL3.0 x=...;token" for http://xboxlive.com.
// `xuid`: user's XUID.
// `xboxTitleId`: decimal string Xbox Title ID.
// `language`: language tag (e.g. "pt-BR", "en-US").
bool fetchTitleAchievements(const std::string& xblAuth, const std::string& xuid,
                            const std::string& xboxTitleId, TitleAchievements& out,
                            std::string& err, const std::string& language = "en-US");

struct AchievementTitleHistoryItem {
    std::string titleId;
    std::string name;
    std::string titleType;
    std::string platform;
    int earnedAchievements = 0;
    int currentGamerscore = 0;
    int maxGamerscore = 0;
    std::string lastUnlock;
    std::string displayImage;
};

// Fetches user's achievement title history across all pages from achievements.xboxlive.com.
bool fetchAchievementHistoryTitles(const std::string& xblAuth, const std::string& xuid,
                                   std::vector<AchievementTitleHistoryItem>& outTitles,
                                   std::string& err, const std::string& language = "en-US");

// Fetches display images for a list of title IDs via TitleHub batch decoration.
bool fetchTitleHubBatchImages(const std::string& xblAuth,
                              const std::vector<std::string>& titleIds,
                              std::unordered_map<std::string, std::string>& outImages,
                              std::string& err, const std::string& language = "en-US");

}  // namespace xc::xcloud
