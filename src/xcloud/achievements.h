// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Xbox Live Achievements service.
#pragma once

#include <string>
#include <vector>

namespace xc::xcloud {

struct Achievement {
    std::string id;
    std::string name;
    std::string description;
    std::string lockedDescription;
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

}  // namespace xc::xcloud
