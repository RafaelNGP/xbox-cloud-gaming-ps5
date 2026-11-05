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
    for (size_t i = 0; i < name.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (c == 0xc3 && i + 1 < name.size()) {
            unsigned char c2 = static_cast<unsigned char>(name[i + 1]);
            // Latin-1 Supplement in UTF-8: fold accented vowels and c-cedilla
            if ((c2 >= 0xa0 && c2 <= 0xa5) || (c2 >= 0x80 && c2 <= 0x85)) { out.push_back('a'); ++i; continue; }
            if ((c2 >= 0xa8 && c2 <= 0xab) || (c2 >= 0x88 && c2 <= 0x8b)) { out.push_back('e'); ++i; continue; }
            if ((c2 >= 0xac && c2 <= 0xaf) || (c2 >= 0x8c && c2 <= 0x8f)) { out.push_back('i'); ++i; continue; }
            if ((c2 >= 0xb2 && c2 <= 0xb6) || (c2 >= 0x92 && c2 <= 0x96)) { out.push_back('o'); ++i; continue; }
            if ((c2 >= 0xb9 && c2 <= 0xbc) || (c2 >= 0x99 && c2 <= 0x9c)) { out.push_back('u'); ++i; continue; }
            if (c2 == 0xa7 || c2 == 0x87) { out.push_back('c'); ++i; continue; }
        }
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return out;
}

namespace {

bool isFortniteTitle(const std::string& xboxTitleId, const std::string& productId,
                     const std::string& npCommId, const std::string& titleName) {
    if (xboxTitleId == "267695549") return true;
    if (productId == "BT5P2X999VH2") return true;
    if (npCommId == "NPWR20561_00") return true;
    std::string n = normalizeTrophyName(titleName);
    if (n.find("fortnite") != std::string::npos) return true;
    return false;
}

TrophyGrade resolveFortniteGrade(int trophyId, const std::string& name, int gamerscore, size_t totalCount) {
    std::string norm = normalizeTrophyName(name);

    // Official Gold trophies (3)
    // 1. All Tied Up (Twine Peaks Storm Shield Defense 6) - ID 29
    // 2. Guardian Angel (Save 10,000 survivors in successful missions) - ID 40
    // 3. Plays Well with Others (Complete 1,000 "Play With Others" missions) - ID 44
    if (norm == "alltiedup" || norm == "tudoarrumado" ||
        norm == "guardianangel" || norm == "anjodaguarda" ||
        norm == "playswellwithothers" || norm == "jogabememconjunto" ||
        (totalCount == 45 && (trophyId == 29 || trophyId == 40 || trophyId == 44))) {
        return TrophyGrade::Gold;
    }

    // Official Silver trophies (10)
    // 1. Many Planks (Plankerton main quests) - ID 23
    // 2. Amped for Expansion (Canny Valley Storm Shield Defense 6) - ID 26
    // 3. Uncanny! (Canny Valley main quests) - ID 27
    // 4. Into the Night (Twine Peaks Storm Shield Defense 1) - ID 28
    // 5. Evil Things with Crazy Teeth (Toxic Treasures - Stage 10) - ID 37
    // 6. Talented Builder (Build 500,000 structures in successful missions) - ID 38
    // 7. Go Gnome! (Destroy 100 Garden Gnomes in successful missions) - ID 39
    // 8. Loot Legend (Loot 300 Treasure Chests in successful missions) - ID 41
    // 9. World Explorer (Fully explore 1,500 zones in successful missions) - ID 42
    // 10. Unspeakable Horrors (Kill 20,000 Mist Monsters in successful missions) - ID 43
    if (norm == "manyplanks" || norm == "plankertonlivre" ||
        norm == "ampedforexpansion" || norm == "preparadoparaexpandir" ||
        norm == "uncanny" || norm == "desencana" ||
        norm == "intothenight" || norm == "pelanoite" ||
        norm == "evilthingswithcrazyteeth" || norm == "coisasmalvadascomunsdentoes" || norm == "coisasmalvadascomunsdentes" ||
        norm == "talentedbuilder" || norm == "construtortalentoso" ||
        norm == "gognome" || norm == "vaignomo" ||
        norm == "lootlegend" || norm == "lendadosaque" ||
        norm == "worldexplorer" || norm == "exploradormundial" ||
        norm == "unspeakablehorrors" || norm == "horroresinenarraveis" || norm == "horroresinenarrveis" ||
        (totalCount == 45 && (trophyId == 23 || trophyId == 26 || trophyId == 27 || trophyId == 28 ||
                              trophyId == 37 || trophyId == 38 || trophyId == 39 || trophyId == 41 ||
                              trophyId == 42 || trophyId == 43))) {
        return TrophyGrade::Silver;
    }

    // When the canonical 45 Fortnite Save the World achievements are present
    if (totalCount == 45 && trophyId >= 1 && trophyId <= 45) {
        return TrophyGrade::Bronze;
    }

    // Fallback to scoreToGrade for synthetic tests or custom inputs
    return scoreToGrade(gamerscore);
}

}  // namespace

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

    bool isFortnite = isFortniteTitle(xboxTitleId, productId, set.npCommId, set.titleName);

    set.items.reserve(achievements.size());
    for (size_t i = 0; i < achievements.size(); ++i) {
        const auto& ach = achievements[i];
        TrophyDef def;
        def.trophyId = static_cast<int>(i + 1); // 1-based trophy id
        def.name = ach.name;
        def.description = ach.description.empty() ? ach.lockedDescription : ach.description;
        def.iconUrl = ach.iconUrl;
        def.gamerscore = ach.gamerscore;
        if (isFortnite) {
            def.grade = resolveFortniteGrade(def.trophyId, def.name, ach.gamerscore, achievements.size());
        } else {
            def.grade = scoreToGrade(ach.gamerscore);
        }
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
