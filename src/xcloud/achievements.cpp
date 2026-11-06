// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "xcloud/achievements.h"

#include "net/http.h"
#include "util/json.h"
#include "util/log.h"

#include <algorithm>

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

    out = TitleAchievements{};
    std::string continuationToken;
    int page = 0;
    constexpr int kMaxPages = 30; // Up to 3000 achievements

    while (page < kMaxPages) {
        net::Request req;
        req.method = "GET";
        std::string baseUrl = !xuid.empty() ?
            "https://achievements.xboxlive.com/users/xuid(" + xuid + ")/achievements?titleId=" + xboxTitleId + "&maxItems=100" :
            "https://achievements.xboxlive.com/users/me/achievements?titleId=" + xboxTitleId + "&maxItems=100";
        if (!continuationToken.empty()) {
            baseUrl += "&continuationToken=" + continuationToken;
        }
        req.url = baseUrl;
        req.headers = {
            {"Authorization", xblAuth},
            {"x-xbl-contract-version", "2"},
            {"Accept-Language", language.empty() ? "en-US" : language}
        };

        auto r = net::perform(req);
        auto j = json::parse(r.body);
        if (!r.ok() || !j) {
            if (page == 0) {
                err = "achievements: " + (r.status ? "HTTP " + std::to_string(r.status) : r.error);
                XC_LOGW("%s", err.c_str());
                return false;
            }
            break;
        }

        const auto& achArr = (*j)["achievements"];
        if (!achArr.isArray() || achArr.items().empty()) {
            break;
        }

        for (const auto& a : achArr.items()) {
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

            for (const auto& ma : a["mediaAssets"].items()) {
                std::string type = ma["type"].str();
                std::string name = ma["name"].str();
                if (type == "Icon" || name == "Icon" || ach.iconUrl.empty()) {
                    ach.iconUrl = ma["url"].str();
                    if (type == "Icon" || name == "Icon") break;
                }
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

        const auto& paging = (*j)["pagingInfo"];
        if (paging.isObject() && paging["continuationToken"].isString()) {
            std::string nextTok = paging["continuationToken"].str();
            if (nextTok.empty() || nextTok == continuationToken) {
                break;
            }
            continuationToken = nextTok;
        } else {
            break;
        }
        page++;
    }

    return true;
}

bool fetchAchievementHistoryTitles(const std::string& xblAuth, const std::string& xuid,
                                   std::vector<AchievementTitleHistoryItem>& outTitles,
                                   std::string& err, const std::string& language) {
    outTitles.clear();
    if (xblAuth.empty()) {
        err = "xblAuth empty";
        return false;
    }

    std::string continuationToken;
    int page = 0;
    constexpr int kMaxPages = 30; // Up to 3000 titles

    while (page < kMaxPages) {
        net::Request req;
        req.method = "GET";
        std::string baseUrl = xuid.empty() ?
            "https://achievements.xboxlive.com/users/me/history/titles?maxItems=100" :
            "https://achievements.xboxlive.com/users/xuid(" + xuid + ")/history/titles?maxItems=100";
        if (!continuationToken.empty()) {
            baseUrl += "&continuationToken=" + continuationToken;
        }
        req.url = baseUrl;
        req.headers = {
            {"Authorization", xblAuth},
            {"x-xbl-contract-version", "2"},
            {"Accept-Language", language.empty() ? "en-US" : language}
        };

        auto r = net::perform(req);
        auto j = json::parse(r.body);
        if (!r.ok() || !j) {
            err = "history/titles: " + (r.status ? "HTTP " + std::to_string(r.status) : r.error);
            XC_LOGW("%s", err.c_str());
            return false;
        }

        const auto& titlesArr = (*j)["titles"];
        if (!titlesArr.isArray() || titlesArr.items().empty()) {
            break;
        }

        for (const auto& item : titlesArr.items()) {
            AchievementTitleHistoryItem h;
            if (item["titleId"].isNumber()) {
                h.titleId = std::to_string(item["titleId"].asInt());
            } else if (item["titleId"].isString()) {
                h.titleId = item["titleId"].str();
            }
            if (h.titleId.empty() || h.titleId == "0") continue;

            h.name = item["name"].str();
            h.titleType = item["titleType"].str();
            h.platform = item["platform"].str();
            h.earnedAchievements = item["earnedAchievements"].isNumber() ? static_cast<int>(item["earnedAchievements"].asInt()) : 0;
            h.currentGamerscore = item["currentGamerscore"].isNumber() ? static_cast<int>(item["currentGamerscore"].asInt()) : 0;
            h.maxGamerscore = item["maxGamerscore"].isNumber() ? static_cast<int>(item["maxGamerscore"].asInt()) : 0;
            h.lastUnlock = item["lastUnlock"].str();

            // Filter out non-games / apps like YouTube, Twitch, Netflix, TrueAchievements
            if (h.titleType == "LiveApp" || h.titleType == "App") {
                continue;
            }

            outTitles.push_back(std::move(h));
        }

        const auto& paging = (*j)["pagingInfo"];
        if (paging.isObject() && paging["continuationToken"].isString()) {
            std::string nextTok = paging["continuationToken"].str();
            if (nextTok.empty() || nextTok == continuationToken) {
                break;
            }
            continuationToken = nextTok;
        } else {
            break;
        }
        page++;
    }

    return true;
}

bool fetchTitleHubBatchImages(const std::string& xblAuth,
                              const std::vector<std::string>& titleIds,
                              std::unordered_map<std::string, std::string>& outImages,
                              std::string& err, const std::string& language) {
    if (xblAuth.empty() || titleIds.empty()) return true;

    // Process in batches of 100
    constexpr size_t kBatchSize = 100;
    for (size_t i = 0; i < titleIds.size(); i += kBatchSize) {
        size_t end = std::min(i + kBatchSize, titleIds.size());
        json::Value body = json::Value::object();
        body.set("pfns", json::Value::array());
        body.set("windowsPhoneProductIds", json::Value::array());
        json::Value arr = json::Value::array();
        for (size_t k = i; k < end; ++k) {
            arr.push(titleIds[k]);
        }
        body.set("titleIds", std::move(arr));

        net::Request req;
        req.method = "POST";
        req.url = "https://titlehub.xboxlive.com/titles/batch/decoration/detail,image";
        req.headers = {
            {"Authorization", xblAuth},
            {"x-xbl-contract-version", "2"},
            {"Content-Type", "application/json"},
            {"Accept-Language", language.empty() ? "en-US" : language}
        };
        req.body = body.dump();

        auto r = net::perform(req);
        auto j = json::parse(r.body);
        if (r.ok() && j && (*j)["titles"].isArray()) {
            for (const auto& item : (*j)["titles"].items()) {
                std::string tid;
                if (item["titleId"].isNumber()) {
                    tid = std::to_string(item["titleId"].asInt());
                } else if (item["titleId"].isString()) {
                    tid = item["titleId"].str();
                }
                std::string displayImage = item["displayImage"].str();
                if (displayImage.empty()) {
                    for (const auto& img : item["images"].items()) {
                        std::string t = img["type"].str();
                        if (t == "BoxArt" || t == "Poster" || t == "Logo") {
                            displayImage = img["url"].str();
                            if (!displayImage.empty()) break;
                        }
                    }
                }
                if (!tid.empty() && !displayImage.empty()) {
                    outImages[tid] = displayImage;
                }
            }
        }
    }
    return true;
}

}  // namespace xc::xcloud
