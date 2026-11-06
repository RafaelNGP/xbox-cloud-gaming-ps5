// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xc::trophy {

struct TitleDbInfo {
    std::string titleId;             // e.g. "PPSA01922", "PPSA81234"
    std::string titleName;           // e.g. "Fortnite", "Sea of Thieves"
    std::string contentId;           // e.g. "UP1477-PPSA01922_00-FORTNITECORE0001"
    uint32_t conceptId = 0;          // e.g. 210118
    std::string localConceptId;      // e.g. "cid:scp:00000000000334c6"
    std::string icon0Path;           // e.g. "/user/app/<titleId>/sce_sys/icon0.png"
    std::string pic0Path;            // e.g. "/user/app/<titleId>/sce_sys/pic0.png"
    std::string metaDataPath;        // e.g. "/system_data/priv/appmeta/<titleId>"
};

// Generates TitleDbInfo with sane defaults for a given titleId and titleName
TitleDbInfo makeTitleDbInfo(const std::string& titleId, const std::string& titleName,
                            const std::string& contentId = "");

// Registers a title into app.db and appinfo.db so the PS5 OS enumerates and displays its trophies.
// If appDbPath or appinfoDbPath are empty, defaults to "/system_data/priv/mms/app.db" and "/system_data/priv/mms/appinfo.db".
bool registerTitleInDatabase(const TitleDbInfo& info,
                             const std::string& appDbPath = "",
                             const std::string& appinfoDbPath = "");

// Removes a title from app.db and appinfo.db.
bool removeTitleFromDatabase(const std::string& titleId,
                             const std::string& appDbPath = "",
                             const std::string& appinfoDbPath = "");

}  // namespace xc::trophy
