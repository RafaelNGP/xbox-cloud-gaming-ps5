// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "xcloud/achievements.h"

#include "net/http.h"
#include "util/json.h"
#include "util/log.h"

namespace xc::xcloud {

bool fetchTitleAchievements(const std::string& xblAuth, const std::string& xuid,
                            const std::string& xboxTitleId, TitleAchievements& out,
                            std::string& err, const std::string& language) {
    if (xblAuth.empty()) {
        err = "xblAuth empty";
        return false;
    }
    if (xboxTitleId.empty()) {
        err = "xboxTitleId empty";
        return false;
    }

    net::Request req;
    req.method = "GET";
    if (!xuid.empty()) {
        req.url = "https://achievements.xboxlive.com/users/xuid(" + xuid + ")/achievements?titleId=" + xboxTitleId + "&maxItems=100";
    } else {
        req.url = "https://achievements.xboxlive.com/users/me/achievements?titleId=" + xboxTitleId + "&maxItems=100";
    }
    req.headers = {
        {"Authorization", xblAuth},
        {"x-xbl-contract-version", "2"},
        {"Accept-Language", language.empty() ? "en-US" : language}
    };

    auto r = net::perform(req);
    auto j = json::parse(r.body);
    if (!r.ok() || !j) {
        err = "achievements: " + (r.status ? "HTTP " + std::to_string(r.status) : r.error);
        XC_LOGW("%s", err.c_str());
        return false;
    }

    out = TitleAchievements{};
    for (const auto& a : (*j)["achievements"].items()) {
        Achievement ach;
        ach.id = a["id"].str();
        ach.name = a["name"].str();
        ach.description = a["description"].str();
        ach.lockedDescription = a["lockedDescription"].str();
        ach.isSecret = a["isSecret"].asBool();

        std::string progressState = a["progressState"].str();
        ach.unlocked = (progressState == "Achieved");

        auto prog = a["progression"];
        if (prog.isObject()) {
            ach.timeUnlocked = prog["timeUnlocked"].str();
        }

        for (const auto& rw : a["rewards"].items()) {
            std::string type = rw["type"].str();
            std::string name = rw["name"].str();
            if (type == "Gamerscore" || name == "Gamerscore") {
                if (rw["value"].isNumber()) {
                    ach.gamerscore = rw["value"].asInt();
                } else if (rw["value"].isString()) {
                    try {
                        ach.gamerscore = std::stoi(rw["value"].str());
                    } catch (...) {
                        ach.gamerscore = 0;
                    }
                }
            }
        }

        out.totalCount++;
        out.totalGamerscore += ach.gamerscore;
        if (ach.unlocked) {
            out.unlockedCount++;
            out.unlockedGamerscore += ach.gamerscore;
        }
        out.items.push_back(std::move(ach));
    }

    return true;
}

}  // namespace xc::xcloud
