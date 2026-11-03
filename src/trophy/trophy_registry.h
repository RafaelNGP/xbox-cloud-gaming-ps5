// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include "trophy/trophy_types.h"
#include "xcloud/achievements.h"

#include <string>
#include <vector>

namespace xc::trophy {

struct KnownTitleMapping {
    std::string xboxTitleId;
    std::string productId;
    std::string titleName;
    std::string sonyTitleId;
    std::string npCommId;
};

// Looks up a known cross-platform title (Scenario A) by Xbox Title ID or Store Product ID.
const KnownTitleMapping* findKnownTitle(const std::string& xboxTitleId, const std::string& productId = {});

// Generates a deterministic virtual NP Communication ID for Xbox exclusives (Scenario B), e.g. NPWR9XXXX_00.
std::string generateNpCommId(const std::string& xboxTitleId);

// Generates a deterministic virtual PlayStation Title ID for Xbox exclusives, e.g. PPSA9XXXX.
std::string generateSonyTitleId(const std::string& xboxTitleId);

// Resolves or constructs a TitleTrophySet given title information and Xbox Live achievements.
TitleTrophySet resolveTitleSet(const std::string& xboxTitleId, const std::string& productId,
                               const std::string& titleName, const std::vector<xcloud::Achievement>& achievements);

// Normalizes an achievement or trophy name for string correlation.
std::string normalizeTrophyName(const std::string& name);

}  // namespace xc::trophy
