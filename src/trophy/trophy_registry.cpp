// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "trophy/trophy_registry.h"
#include "trophy/trophy_rules.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace xc::trophy {

namespace {

const KnownTitleMapping s_knownTitles[] = {
    // Fortnite -> Official PS5 NPWR
    {"267695549", "BT5P2X999VH2", "Fortnite", "PPSA01922", "NPWR20561_00"},
    // Minecraft -> Official PS5 NPWR
    {"1828326430", "9NBLGGH537BL", "Minecraft", "PPSA03350", "NPWR18283_00"},
    // Sea of Thieves -> Official PS5 NPWR
    {"1717113201", "9P2N57MC619K", "Sea of Thieves", "PPSA16155", "NPWR17171_00"},
    // Resident Evil 2 / Biohazard
    {"2108722049", "9N5953FMWP5L", "Resident Evil", "PPSA04400", "NPWR53376_00"},
    // Cyberpunk 2077
    {"222473492", "BX3M8ZSZMFN5", "Cyberpunk 2077", "PPSA03974", "NPWR17597_00"},
    {"1492080358", "BX3M8ZSZMFN5", "Cyberpunk 2077", "PPSA03974", "NPWR17597_00"},
    // Animal Well
    {"22520", "ANIMALWELL001", "ANIMAL WELL", "PPSA22520", "NPWR36759_00"},
};

uint32_t fnv1a(const std::string& str) {
    uint32_t hash = 2166136261u;
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

}  // namespace

const KnownTitleMapping* findKnownTitle(const std::string& xboxTitleId, const std::string& productId) {
    for (const auto& item : s_knownTitles) {
        if (!xboxTitleId.empty() && item.xboxTitleId == xboxTitleId) return &item;
        if (!productId.empty() && item.productId == productId) return &item;
    }
    return nullptr;
}

std::string generateNpCommId(const std::string& xboxTitleId) {
    uint32_t h = fnv1a(xboxTitleId);
    uint32_t num = 80000 + (h % 10000); // NPWR80000_00 .. NPWR89999_00
    char buf[16];
    snprintf(buf, sizeof(buf), "NPWR%05u_00", num);
    return std::string(buf);
}

std::string generateSonyTitleId(const std::string& xboxTitleId) {
    uint32_t h = fnv1a(xboxTitleId);
    uint32_t num = 80000 + (h % 10000);
    char buf[16];
    snprintf(buf, sizeof(buf), "PPSA%05u", num);
    return std::string(buf);
}

std::string normalizeTrophyName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

TitleTrophySet resolveTitleSet(const std::string& xboxTitleId, const std::string& productId,
                                const std::string& titleName, const std::vector<xcloud::Achievement>& achievements) {
    TitleTrophySet set;
    set.xboxTitleId = xboxTitleId;
    set.productId = productId;
    set.titleName = titleName.empty() ? ("Xbox Title " + xboxTitleId) : titleName;

    const KnownTitleMapping* known = findKnownTitle(xboxTitleId, productId);
    if (known) {
        set.npCommId = known->npCommId;
        set.sonyTitleId = known->sonyTitleId;
        if (!known->titleName.empty()) set.titleName = known->titleName;
    } else {
        std::string seed = xboxTitleId.empty() ? (productId.empty() ? titleName : productId) : xboxTitleId;
        set.npCommId = generateNpCommId(seed);
        set.sonyTitleId = generateSonyTitleId(seed);
    }

    set.items.reserve(achievements.size());
    for (size_t i = 0; i < achievements.size(); ++i) {
        const auto& ach = achievements[i];
        TrophyDef def;
        def.trophyId = static_cast<int>(i + 1); // 1-based trophy id
        def.name = ach.name;
        def.description = ach.description.empty() ? ach.lockedDescription : ach.description;
        def.iconUrl = ach.iconUrl;
        def.gamerscore = ach.gamerscore;
        def.grade = scoreToGrade(ach.gamerscore);
        def.unlocked = ach.unlocked;
        def.timeUnlocked = ach.timeUnlocked;

        if (def.unlocked) {
            set.unlockedCount++;
        }
        set.items.push_back(std::move(def));
    }
    set.totalCount = static_cast<int>(set.items.size());

    return set;
}

}  // namespace xc::trophy
