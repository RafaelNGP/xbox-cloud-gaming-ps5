// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "trophy/trophy_rules.h"

namespace xc::trophy {

const char* gradeToString(TrophyGrade grade) {
    switch (grade) {
        case TrophyGrade::Bronze:   return "Bronze";
        case TrophyGrade::Silver:   return "Silver";
        case TrophyGrade::Gold:     return "Gold";
        case TrophyGrade::Platinum: return "Platinum";
    }
    return "Bronze";
}

int gradeToPoints(TrophyGrade grade) {
    switch (grade) {
        case TrophyGrade::Bronze:   return 15;
        case TrophyGrade::Silver:   return 30;
        case TrophyGrade::Gold:     return 90;
        case TrophyGrade::Platinum: return 300;
    }
    return 15;
}

}  // namespace xc::trophy
