// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "xcloud/social.h"

#include "net/http.h"
#include "util/json.h"
#include "util/log.h"

namespace xc::xcloud {

bool fetchFriendsPresence(const std::string& xblAuth, std::vector<FriendPresence>& out, std::string& err,
                          const std::string& language) {
    if (xblAuth.empty()) {
        err = "xblAuth empty";
        return false;
    }
    net::Request req;
    req.method = "GET";
    req.url = "https://peoplehub.xboxlive.com/users/me/people/social/decoration/detail,presencedetail,preferredcolor";
    req.headers = {
        {"Authorization", xblAuth},
        {"x-xbl-contract-version", "2"},
        {"Accept-Language", language.empty() ? "en-US" : language}
    };

    auto r = net::perform(req);
    auto j = json::parse(r.body);
    if (!r.ok() || !j) {
        err = "peoplehub: " + (r.status ? "HTTP " + std::to_string(r.status) : r.error);
        XC_LOGW("%s", err.c_str());
        return false;
    }

    for (const auto& p : (*j)["people"].items()) {
        FriendPresence fp;
        fp.xuid = p["xuid"].str();
        fp.gamertag = p["gamertag"].str();
        fp.gamerpicUrl = p["displayPicRaw"].str();
        fp.presenceState = p["presenceState"].str();
        fp.presenceText = p["presenceText"].str();

        // Examine presenceDetails for the active game title
        for (const auto& d : p["presenceDetails"].items()) {
            std::string tid;
            if (d["TitleId"].isString()) {
                tid = d["TitleId"].str();
            } else if (d["TitleId"].isNumber()) {
                tid = std::to_string(d["TitleId"].asInt());
            }

            // Exclude Xbox dashboard / Home shell ("750323071", "1022622766")
            if (tid.empty() || tid == "750323071" || tid == "1022622766") continue;

            bool isGame = d["IsGame"].asBool();
            std::string state = d["State"].str();

            if (isGame || state == "Active") {
                fp.titleId = tid;
                fp.device = d["Device"].str();
                fp.isGame = isGame;
                if (isGame) break;  // Prefer explicit game entry
            }
        }

        out.push_back(std::move(fp));
    }

    XC_LOGI("peoplehub: retrieved %zu friends", out.size());
    return true;
}

}  // namespace xc::xcloud
