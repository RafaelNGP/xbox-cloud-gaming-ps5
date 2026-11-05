// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include "trophy/trophy_types.h"

namespace xc::trophy {

// Gamerscore to PlayStation TrophyGrade conversion:
//   <= 50G    -> Bronze
//   55G..99G  -> Silver
//   >= 100G   -> Gold
inline TrophyGrade scoreToGrade(int gamerscore) {
    if (gamerscore >= 100) return TrophyGrade::Gold;
    if (gamerscore >= 55)  return TrophyGrade::Silver;
    return TrophyGrade::Bronze;
}

// 100% achievements unlocked awards Platinum automatically
inline bool hasEarnedPlatinum(int unlockedCount, int totalCount) {
    return (totalCount > 0 && unlockedCount >= totalCount);
}

const char* gradeToString(TrophyGrade grade);

int gradeToPoints(TrophyGrade grade);

}  // namespace xc::trophy
