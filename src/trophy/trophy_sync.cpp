// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "trophy/trophy_sync.h"
#include "trophy/trophy_builder.h"
#include "trophy/trophy_registry.h"
#include "trophy/trophy_rules.h"
#include "net/http.h"
#include "platform/platform.h"
#include "ui/strings.h"
#include "util/json.h"
#include "util/log.h"
#include "xcloud/achievements.h"

#include <chrono>
#include <set>
#include <thread>
#include <unordered_set>
#include <sys/stat.h>

namespace xc::trophy {

TrophySyncEngine::TrophySyncEngine()
    : backend_(createPlatformTrophyBackend()) {}

TrophySyncEngine::TrophySyncEngine(std::unique_ptr<ITrophyBackend> backend)
    : backend_(std::move(backend)) {}

TrophySyncEngine::~TrophySyncEngine() = default;

void TrophySyncEngine::setBackend(std::unique_ptr<ITrophyBackend> backend) {
    std::lock_guard<std::mutex> lock(mutex_);
    backend_ = std::move(backend);
    initialized_ = false;
}

std::string TrophySyncEngine::trophyCacheDir() const {
    std::string base = platform::dataDir();
    if (base.empty()) base = ".";
    std::string dir = base + "/trophies";
    ::mkdir(dir.c_str(), 0777);
    return dir;
}

std::string TrophySyncEngine::trophyCachePath(const std::string& xboxTitleId) const {
    return trophyCacheDir() + "/" + xboxTitleId + ".json";
}

bool TrophySyncEngine::init(uint32_t userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!backend_) backend_.reset(createPlatformTrophyBackend());
    if (backend_) {
        initialized_ = backend_->init(userId);
    }
    return initialized_;
}

bool TrophySyncEngine::syncTitleAchievements(const std::string& xblAuth, const std::string& xuid,
                                             const std::string& xboxTitleId, const std::string& titleName,
                                             int& newlyUnlocked, std::string& err) {
    newlyUnlocked = 0;
    if (xblAuth.empty()) {
        err = "xblAuth empty";
        return false;
    }
    if (xboxTitleId.empty()) {
        err = "xboxTitleId empty";
        return false;
    }

    struct SyncGuard {
        std::atomic<bool>& f;
        SyncGuard(std::atomic<bool>& flag) : f(flag) { f = true; }
        ~SyncGuard() { f = false; }
    } guard(syncing_);

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        if (!backend_) backend_.reset(createPlatformTrophyBackend());
        if (backend_) initialized_ = backend_->init(0);
    }

    // 1. Query Xbox Live achievements
    xcloud::TitleAchievements ach;
    if (!xcloud::fetchTitleAchievements(xblAuth, xuid, xboxTitleId, ach, err, ui::gameLocale())) {
        XC_LOGW("trophy sync: fetchTitleAchievements failed for %s (%s): %s",
                titleName.c_str(), xboxTitleId.c_str(), err.c_str());
        return false;
    }

    // 2. Resolve trophy set mapping
    TitleTrophySet titleSet = resolveTitleSet(xboxTitleId, "", titleName, ach.items);

    // 2b. Cache trophy and game icons on disk
    std::string iconsBase = trophyCacheDir() + "/icons";
    ::mkdir(iconsBase.c_str(), 0777);
    std::string iconsDir = iconsBase + "/" + titleSet.npCommId;
    ::mkdir(iconsDir.c_str(), 0777);

    std::string icon0File = iconsDir + "/icon0.png";
    struct stat st0{};
    if (::stat(icon0File.c_str(), &st0) != 0 || st0.st_size == 0) {
        std::string candidatePaths[] = {
            "/system_data/priv/appmeta/" + titleSet.sonyTitleId + "/icon0.png",
            "/user/appmeta/" + titleSet.sonyTitleId + "/icon0.png",
            "/user/app/" + titleSet.sonyTitleId + "/sce_sys/icon0.png"
        };
        for (const auto& cp : candidatePaths) {
            std::string raw;
            if (platform::readFile(cp, raw) && raw.size() > 100) {
                std::vector<uint8_t> png512;
                const auto* udata = reinterpret_cast<const uint8_t*>(raw.data());
                if (is512x512Png(udata, raw.size())) {
                    platform::writeFileAtomic(icon0File, raw);
                    break;
                } else if (convertTo512Png(udata, raw.size(), png512)) {
                    platform::writeFileAtomic(icon0File, std::string(reinterpret_cast<const char*>(png512.data()), png512.size()));
                    break;
                }
            }
        }
    }

    // Platinum icon (trop0000.png) for titles with dedicated Platinum like Fortnite
    std::string platFile = iconsDir + "/trop0000.png";
    struct stat stPlat{};
    if (::stat(platFile.c_str(), &stPlat) != 0 || stPlat.st_size == 0 || (st0.st_size > 0 && stPlat.st_size == st0.st_size)) {
        if (titleSet.npCommId == "NPWR20561_00") {
            net::Request req;
            req.method = "GET";
            req.url = "https://m.exophase.com/psn/awards/l/d41b74.png";
            auto r = net::perform(req);
            if (r.ok() && !r.body.empty()) {
                std::vector<uint8_t> png512;
                const auto* udata = reinterpret_cast<const uint8_t*>(r.body.data());
                if (convertTo512Png(udata, r.body.size(), png512)) {
                    platform::writeFileAtomic(platFile, std::string(reinterpret_cast<const char*>(png512.data()), png512.size()));
                    XC_LOGI("trophy sync: cached genuine Platinum icon for %s (%zu bytes)", titleSet.npCommId.c_str(), png512.size());
                }
            }
        }
    }

    for (size_t i = 0; i < ach.items.size() && i < titleSet.items.size(); ++i) {
        const auto& a = ach.items[i];
        const auto& def = titleSet.items[i];
        if (a.iconUrl.empty()) continue;
        char nameBuf[32];
        snprintf(nameBuf, sizeof(nameBuf), "trop%04d.png", def.trophyId);
        std::string iconFile = iconsDir + "/" + nameBuf;
        struct stat st{};
        // Skip only if existing file is valid and not an accidental duplicate of cover art
        if (::stat(iconFile.c_str(), &st) == 0 && st.st_size > 0 && (st0.st_size == 0 || st.st_size != st0.st_size)) continue;

        std::string fetchUrl = a.iconUrl;
        if (fetchUrl.find("images-eds-ssl.xboxlive.com") != std::string::npos &&
            fetchUrl.find("format=") == std::string::npos) {
            fetchUrl += (fetchUrl.find('?') == std::string::npos) ? "?format=jpg" : "&format=jpg";
        }
        net::Request req;
        req.method = "GET";
        req.url = fetchUrl;
        auto r = net::perform(req);
        if (r.ok() && !r.body.empty()) {
            std::vector<uint8_t> png512;
            const auto* udata = reinterpret_cast<const uint8_t*>(r.body.data());
            if (convertTo512Png(udata, r.body.size(), png512)) {
                platform::writeFileAtomic(iconFile, std::string(reinterpret_cast<const char*>(png512.data()), png512.size()));
                XC_LOGI("trophy sync: cached icon %s (%zu bytes)", nameBuf, png512.size());
            }
        }
    }

    // 3. Load local cache snapshot
    std::set<std::string> prevUnlocked;
    std::string cacheFile = trophyCachePath(xboxTitleId);
    std::string cacheContent;
    if (platform::readFile(cacheFile, cacheContent)) {
        if (auto j = json::parse(cacheContent); j && j->isObject()) {
            for (const auto& item : (*j)["unlockedIds"].items()) {
                if (item.isString()) prevUnlocked.insert(item.str());
            }
        }
    }

    // 4. Register title with backend
    if (backend_) {
        backend_->registerTitle(titleSet);
    }

    // 5. Compare achievements and unlock new trophies
    bool platinumAwarded = prevUnlocked.count("__PLATINUM__") > 0;
    for (size_t i = 0; i < ach.items.size() && i < titleSet.items.size(); ++i) {
        const auto& a = ach.items[i];
        const auto& def = titleSet.items[i];
        if (a.unlocked) {
            std::string key = a.id.empty() ? std::to_string(def.trophyId) : a.id;
            bool alreadyInBackend = backend_ ? backend_->isTrophyUnlocked(titleSet.npCommId, def.trophyId) : false;
            if (prevUnlocked.count(key) == 0 || !alreadyInBackend) {
                bool unlockedOk = true;
                if (backend_) {
                    unlockedOk = backend_->unlockTrophy(titleSet.npCommId, def.trophyId, def.grade, def.name);
                }
                if (unlockedOk) {
                    prevUnlocked.insert(key);
                    newlyUnlocked++;
                }
            }
        }
    }

    // 5. Automatic Platinum award on 100% completion
    if (!platinumAwarded && hasEarnedPlatinum(ach.unlockedCount, ach.totalCount)) {
        XC_LOGI("trophy sync: 100%% achievements obtained for %s! Awarding Platinum trophy.",
                titleSet.titleName.c_str());
        bool platOk = true;
        if (backend_) {
            platOk = backend_->unlockTrophy(titleSet.npCommId, kPlatinumTrophyId, TrophyGrade::Platinum, "Platina: " + titleSet.titleName);
        }
        if (platOk) {
            prevUnlocked.insert("__PLATINUM__");
            newlyUnlocked++;
        }
    }

    // 6. Flush backend state to commit TRPTITLE.DAT to the PlayStation profile database
    if (backend_) {
        backend_->flush();
    }

    // 7. Save updated cache atomically with rich profile information
    json::Value out = json::Value::object();
    out.set("xboxTitleId", xboxTitleId);
    out.set("npCommId", titleSet.npCommId);
    out.set("sonyTitleId", titleSet.sonyTitleId);
    out.set("titleName", titleSet.titleName);
    out.set("unlockedCount", static_cast<int64_t>(ach.unlockedCount));
    out.set("totalCount", static_cast<int64_t>(ach.totalCount));
    out.set("unlockedGamerscore", static_cast<int64_t>(ach.unlockedGamerscore));
    out.set("totalGamerscore", static_cast<int64_t>(ach.totalGamerscore));
    out.set("lastSync", static_cast<int64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())));

    json::Value arr = json::Value::array();
    for (const auto& id : prevUnlocked) {
        arr.push(id);
    }
    out.set("unlockedIds", std::move(arr));

    json::Value trophiesArr = json::Value::array();
    for (const auto& def : titleSet.items) {
        json::Value t = json::Value::object();
        t.set("id", static_cast<int64_t>(def.trophyId));
        t.set("name", def.name);
        t.set("description", def.description);
        t.set("grade", gradeToString(def.grade));
        t.set("gamerscore", static_cast<int64_t>(def.gamerscore));
        t.set("unlocked", def.unlocked);
        if (!def.iconUrl.empty()) t.set("iconUrl", def.iconUrl);
        if (!def.timeUnlocked.empty()) t.set("timeUnlocked", def.timeUnlocked);
        trophiesArr.push(std::move(t));
    }
    out.set("trophies", std::move(trophiesArr));

    platform::writeFileAtomic(cacheFile, out.dump());
    return true;
}

bool TrophySyncEngine::pollActiveStream(const std::string& xblAuth, const std::string& xuid,
                                        const std::string& xboxTitleId, const std::string& titleName) {
    if (xboxTitleId.empty() || xblAuth.empty()) return false;
    if (pollInFlight_.exchange(true)) return false;

    std::thread([this, xblAuth, xuid, xboxTitleId, titleName]() {
        int newlyUnlocked = 0;
        std::string err;
        bool ok = syncTitleAchievements(xblAuth, xuid, xboxTitleId, titleName, newlyUnlocked, err);
        if (ok && newlyUnlocked > 0) {
            XC_LOGI("trophy poll: unlocked %d trophies during stream for %s!", newlyUnlocked, titleName.c_str());
        }
        pollInFlight_ = false;
    }).detach();
    return true;
}

bool TrophySyncEngine::syncAllAccountTrophies(const std::string& xblAuth, const std::string& xuid,
                                             const std::vector<ui::GameTile>& ownedGames,
                                             int& totalTitles, int& totalTrophies, std::string& err,
                                             const std::function<bool()>& cancelCheck,
                                             const std::function<void(int done, int total)>& progressCb) {
    totalTitles = 0;
    totalTrophies = 0;
    if (xblAuth.empty()) {
        err = "Conta não conectada / Account not signed in";
        return false;
    }

    struct SyncGuard {
        std::atomic<bool>& f;
        SyncGuard(std::atomic<bool>& flag) : f(flag) { f = true; }
        ~SyncGuard() { f = false; }
    } guard(syncing_);

    std::unordered_set<std::string> seenXboxTitles;
    std::vector<std::pair<std::string, std::string>> titlesToSync; // pair<xboxTitleId, titleName>

    // Always prioritize Fortnite first for focused verification (AGENTS.md §8.1)
    seenXboxTitles.insert("267695549");
    titlesToSync.emplace_back("267695549", "Fortnite");

    // Add titles from owned library, resolving known titles if xboxTitleId is missing
    for (const auto& game : ownedGames) {
        std::string xid = game.xboxTitleId;
        if (xid.empty() && !game.productId.empty()) {
            if (const auto* k = findKnownTitle("", game.productId)) {
                xid = k->xboxTitleId;
            }
        }
        if (!xid.empty() && seenXboxTitles.insert(xid).second) {
            titlesToSync.emplace_back(xid, game.name.empty() ? ("Xbox Title " + xid) : game.name);
        }
    }

    XC_LOGI("trophy back-sync: starting synchronization for %d titles", static_cast<int>(titlesToSync.size()));
    for (size_t i = 0; i < titlesToSync.size(); ++i) {
        if (cancelCheck && cancelCheck()) {
            XC_LOGI("trophy back-sync: cancelled by user after %d titles", totalTitles);
            break;
        }
        if (progressCb) {
            progressCb(static_cast<int>(i), static_cast<int>(titlesToSync.size()));
        }
        const auto& [tid, name] = titlesToSync[i];
        int newlyUnlocked = 0;
        std::string titleErr;
        if (syncTitleAchievements(xblAuth, xuid, tid, name, newlyUnlocked, titleErr)) {
            totalTitles++;
            totalTrophies += newlyUnlocked;
        } else {
            XC_LOGW("trophy back-sync: title %s (%s) sync warning: %s", name.c_str(), tid.c_str(), titleErr.c_str());
        }
    }
    if (progressCb) {
        progressCb(static_cast<int>(titlesToSync.size()), static_cast<int>(titlesToSync.size()));
    }

    XC_LOGI("trophy back-sync: completed. %d titles checked, %d trophies unlocked.", totalTitles, totalTrophies);
    return true;
}

TrophySyncEngine& syncEngine() {
    static TrophySyncEngine s_engine;
    return s_engine;
}

}  // namespace xc::trophy
