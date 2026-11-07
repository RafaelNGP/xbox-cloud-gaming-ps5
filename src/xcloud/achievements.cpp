// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "xcloud/achievements.h"

#include "net/http.h"
#include "util/json.h"
#include "util/log.h"

#include <algorithm>
#include <cstring>

namespace xc::xcloud {

namespace {

std::string normalizeHttpsImageUrl(std::string url) {
    if (url.empty()) return url;
    if (url.rfind("http://images-eds.xboxlive.com/", 0) == 0) {
        url = "https://images-eds-ssl.xboxlive.com/" + url.substr(std::strlen("http://images-eds.xboxlive.com/"));
    } else if (url.rfind("http://", 0) == 0) {
        url = "https://" + url.substr(7);
    } else if (url.rfind("//", 0) == 0) {
        url = "https:" + url;
    }
    return url;
}

}  // namespace

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
                    ach.iconUrl = normalizeHttpsImageUrl(ma["url"].str());
                    if (type == "Icon" || name == "Icon") break;
                }
            }
            if (ach.iconUrl.empty() && a["imageUnlocked"].isString()) {
                ach.iconUrl = normalizeHttpsImageUrl(a["imageUnlocked"].str());
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
            h.displayImage = normalizeHttpsImageUrl(item["displayImage"].str());
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
    (void)err;
    if (xblAuth.empty() || titleIds.empty()) return true;

    auto runBatch = [&](size_t startIdx, size_t endIdx) -> bool {
        json::Value body = json::Value::object();
        body.set("pfns", json::Value::array());
        body.set("windowsPhoneProductIds", json::Value::array());
        json::Value arr = json::Value::array();
        for (size_t k = startIdx; k < endIdx; ++k) {
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
        if (!r.ok() || !j || !(*j)["titles"].isArray()) {
            return false;
        }
        for (const auto& item : (*j)["titles"].items()) {
            std::string tid;
            if (item["titleId"].isNumber()) {
                tid = std::to_string(item["titleId"].asInt());
            } else if (item["titleId"].isString()) {
                tid = item["titleId"].str();
            }
            std::string modernTid;
            if (item["modernTitleId"].isNumber()) {
                modernTid = std::to_string(item["modernTitleId"].asInt());
            } else if (item["modernTitleId"].isString()) {
                modernTid = item["modernTitleId"].str();
            }
            std::string displayImage = normalizeHttpsImageUrl(item["displayImage"].str());
            if (displayImage.empty()) {
                for (const auto& img : item["images"].items()) {
                    std::string t = img["type"].str();
                    if (t == "BoxArt" || t == "Poster" || t == "Tile" || t == "Logo" || t == "Hero") {
                        displayImage = normalizeHttpsImageUrl(img["url"].str());
                        if (!displayImage.empty()) break;
                    }
                }
            }
            if (!displayImage.empty()) {
                if (!tid.empty()) outImages[tid] = displayImage;
                if (!modernTid.empty() && modernTid != "0") outImages[modernTid] = displayImage;
            }
        }
        return true;
    };

    // Process in batches of 80; if a batch fails, split into sub-batches of 16
    constexpr size_t kBatchSize = 80;
    for (size_t i = 0; i < titleIds.size(); i += kBatchSize) {
        size_t end = std::min(i + kBatchSize, titleIds.size());
        if (!runBatch(i, end) && (end - i) > 16) {
            for (size_t sub = i; sub < end; sub += 16) {
                runBatch(sub, std::min(sub + 16, end));
            }
        }
    }
    return true;
}

}  // namespace xc::xcloud
