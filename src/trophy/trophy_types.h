// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xc::trophy {

enum class TrophyGrade {
    Bronze = 1,
    Silver = 2,
    Gold = 3,
    Platinum = 4
};

constexpr int kPlatinumTrophyId = 0;

struct TrophyDef {
    int trophyId = 0;  // 0 for Platinum, 1-indexed for regular achievements (1, 2, ...)
    std::string name;
    std::string description;
    std::string iconUrl;
    TrophyGrade grade = TrophyGrade::Bronze;
    int gamerscore = 0;
    bool unlocked = false;
    std::string timeUnlocked;
};

struct TitleTrophySet {
    std::string xboxTitleId;
    std::string productId;
    std::string titleName;
    std::string npCommId;
    std::string sonyTitleId;
    int totalCount = 0;
    int unlockedCount = 0;
    std::vector<TrophyDef> items;
};

}  // namespace xc::trophy
