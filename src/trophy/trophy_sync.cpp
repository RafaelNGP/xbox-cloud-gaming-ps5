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
#include <dirent.h>
#include <set>
#include <thread>
#include <unistd.h>
#include <unordered_map>
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
                                             int& newlyUnlocked, std::string& err,
                                             const std::string& tileUrl,
                                             bool fetchTrophyIcons) {
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

    if (ach.items.empty()) {
        XC_LOGI("trophy sync: %s (%s) has no achievements; skipping",
                titleName.c_str(), xboxTitleId.c_str());
        return true;
    }

    if (ach.unlockedCount == 0 && xboxTitleId != "267695549") {
        XC_LOGI("trophy sync: %s (%s) has 0 unlocked achievements; skipping package build",
                titleName.c_str(), xboxTitleId.c_str());
        return true;
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

        // Fetch from tileUrl if not found locally
        if (::stat(icon0File.c_str(), &st0) != 0 || st0.st_size == 0) {
            std::string effectiveTileUrl = tileUrl;
            if (effectiveTileUrl.empty() && titleSet.npCommId == "NPWR20561_00") {
                effectiveTileUrl = "https://m.exophase.com/psn/games/l/d41b74.png";
            }
            if (!effectiveTileUrl.empty()) {
                std::string fetchUrl = effectiveTileUrl;
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
                        platform::writeFileAtomic(icon0File, std::string(reinterpret_cast<const char*>(png512.data()), png512.size()));
                        XC_LOGI("trophy sync: cached cover icon for %s (%zu bytes)", titleSet.titleName.c_str(), png512.size());
                        ::stat(icon0File.c_str(), &st0);
                    }
                }
            }
        }
    }

    // Platinum icon (trop0000.png)
    std::string platFile = iconsDir + "/trop0000.png";
    struct stat stPlat{};
    if (::stat(platFile.c_str(), &stPlat) != 0 || stPlat.st_size == 0) {
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
        } else {
            // For other titles: if icon0File exists, copy as platinum icon
            std::string icon0Data;
            if (platform::readFile(icon0File, icon0Data) && !icon0Data.empty()) {
                platform::writeFileAtomic(platFile, icon0Data);
            }
        }
    }

    if (fetchTrophyIcons) {
        for (size_t i = 0; i < ach.items.size() && i < titleSet.items.size(); ++i) {
            const auto& a = ach.items[i];
            const auto& def = titleSet.items[i];
            if (a.iconUrl.empty()) continue;
            // Optimization: only download icon over network for unlocked achievements.
            // For locked achievements, trophy_builder uses the embedded kDefaultTropPng.
            if (!a.unlocked) continue;
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
                                        const std::string& xboxTitleId, const std::string& titleName,
                                        const std::string& tileUrl) {
    if (xboxTitleId.empty() || xblAuth.empty()) return false;
    if (pollInFlight_.exchange(true)) return false;

    std::thread([this, xblAuth, xuid, xboxTitleId, titleName, tileUrl]() {
        int newlyUnlocked = 0;
        std::string err;
        bool ok = syncTitleAchievements(xblAuth, xuid, xboxTitleId, titleName, newlyUnlocked, err, tileUrl, true);
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

    struct SyncTitleItem {
        std::string xboxTitleId;
        std::string titleName;
        std::string tileUrl;
        int earnedAchievements = 0;
    };

    std::unordered_set<std::string> seenXboxTitles;
    std::vector<SyncTitleItem> titlesToSync;

    // 1. Discover full Xbox Live achievement history across the user's account
    std::vector<xcloud::AchievementTitleHistoryItem> historyTitles;
    std::string histErr;
    if (xcloud::fetchAchievementHistoryTitles(xblAuth, xuid, historyTitles, histErr, ui::gameLocale())) {
        XC_LOGI("trophy back-sync: discovered %zu titles in Xbox achievement history", historyTitles.size());
    } else {
        XC_LOGW("trophy back-sync: fetchAchievementHistoryTitles failed: %s", histErr.c_str());
    }

    // 2. Query TitleHub batch images for history titles
    std::unordered_map<std::string, std::string> titleImages;
    if (!historyTitles.empty()) {
        std::vector<std::string> idsForBatch;
        idsForBatch.reserve(historyTitles.size());
        for (const auto& h : historyTitles) {
            idsForBatch.push_back(h.titleId);
        }
        std::string imgErr;
        xcloud::fetchTitleHubBatchImages(xblAuth, idsForBatch, titleImages, imgErr, ui::gameLocale());
        XC_LOGI("trophy back-sync: fetched %zu cover images via TitleHub batch", titleImages.size());
    }

    // Always prioritize Fortnite first for focused verification (AGENTS.md §8.1)
    seenXboxTitles.insert("267695549");
    titlesToSync.push_back({"267695549", "Fortnite", "", 0});

    auto itFnImg = titleImages.find("267695549");
    if (itFnImg != titleImages.end()) {
        titlesToSync[0].tileUrl = itFnImg->second;
    }

    // Add titles from Xbox Live achievement history where earnedAchievements > 0
    for (const auto& h : historyTitles) {
        if (h.earnedAchievements <= 0) continue;
        if (h.titleId == "267695549") {
            titlesToSync[0].earnedAchievements = h.earnedAchievements;
            if (!h.name.empty()) titlesToSync[0].titleName = h.name;
            if (titlesToSync[0].tileUrl.empty() && !h.displayImage.empty()) {
                titlesToSync[0].tileUrl = h.displayImage;
            }
            continue;
        }
        if (seenXboxTitles.insert(h.titleId).second) {
            std::string img = h.displayImage;
            if (img.empty()) {
                auto it = titleImages.find(h.titleId);
                if (it != titleImages.end()) img = it->second;
            }
            titlesToSync.push_back({h.titleId, h.name.empty() ? ("Xbox Title " + h.titleId) : h.name, img, h.earnedAchievements});
        }
    }

    // Supplement tileUrl or titleName from owned library for known titles
    for (const auto& game : ownedGames) {
        std::string xid = game.xboxTitleId;
        if (xid.empty() && !game.productId.empty()) {
            if (const auto* k = findKnownTitle("", game.productId)) {
                xid = k->xboxTitleId;
            }
        }
        if (xid.empty()) continue;

        if (xid == "267695549") {
            if (!game.tileUrl.empty()) titlesToSync[0].tileUrl = game.tileUrl;
            if (!game.name.empty()) titlesToSync[0].titleName = game.name;
        } else {
            for (auto& item : titlesToSync) {
                if (item.xboxTitleId == xid) {
                    if (item.tileUrl.empty() && !game.tileUrl.empty()) item.tileUrl = game.tileUrl;
                    if (item.titleName.empty() || item.titleName.find("Xbox Title") == 0) {
                        if (!game.name.empty()) item.titleName = game.name;
                    }
                    break;
                }
            }
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
        const auto& item = titlesToSync[i];
        int newlyUnlocked = 0;
        std::string titleErr;
        // Fetch genuine individual icons for the pilot title (Fortnite); for bulk history, use box art + default icon
        bool isFortnite = (item.xboxTitleId == "267695549");
        if (syncTitleAchievements(xblAuth, xuid, item.xboxTitleId, item.titleName, newlyUnlocked, titleErr, item.tileUrl, isFortnite)) {
            totalTitles++;
            totalTrophies += newlyUnlocked;
        } else {
            XC_LOGW("trophy back-sync: title %s (%s) sync warning: %s", item.titleName.c_str(), item.xboxTitleId.c_str(), titleErr.c_str());
        }
    }
    if (progressCb) {
        progressCb(static_cast<int>(titlesToSync.size()), static_cast<int>(titlesToSync.size()));
    }

    XC_LOGI("trophy back-sync: completed. %d titles checked, %d trophies unlocked.", totalTitles, totalTrophies);
    return true;
}

bool TrophySyncEngine::removeAllTrophies(int& removedCount, std::string& err) {
    removedCount = 0;
    std::lock_guard<std::mutex> lock(mutex_);

    std::set<std::pair<std::string, std::string>> titleSet;
    titleSet.insert({"NPWR20561_00", "PPSA01922"}); // Fortnite
    titleSet.insert({"NPWR18283_00", "PPSA03350"}); // Minecraft
    titleSet.insert({"NPWR17171_00", "PPSA16155"}); // Sea of Thieves
    titleSet.insert({"NPWR53376_00", "PPSA04400"}); // Resident Evil
    titleSet.insert({"NPWR17597_00", "PPSA03974"}); // Cyberpunk 2077
    titleSet.insert({"NPWR90210_00", "PPSA99810"}); // Selftest

    std::string cacheDir = trophyCacheDir();
    DIR* d = ::opendir(cacheDir.c_str());
    if (d) {
        struct dirent* de;
        while ((de = ::readdir(d)) != nullptr) {
            std::string fname = de->d_name;
            if (fname.size() > 5 && fname.substr(fname.size() - 5) == ".json") {
                std::string content;
                if (platform::readFile(cacheDir + "/" + fname, content)) {
                    if (auto j = json::parse(content); j && j->isObject()) {
                        std::string np = (*j)["npCommId"].str();
                        std::string sony = (*j)["sonyTitleId"].str();
                        if (!np.empty()) titleSet.insert({np, sony});
                    }
                }
                ::unlink((cacheDir + "/" + fname).c_str());
            }
        }
        ::closedir(d);
    }

    // Clean up cached icons
    std::string iconsDir = trophyCacheDir() + "/icons";
    DIR* dIcons = ::opendir(iconsDir.c_str());
    if (dIcons) {
        struct dirent* de;
        while ((de = ::readdir(dIcons)) != nullptr) {
            std::string sub = de->d_name;
            if (sub == "." || sub == "..") continue;
            std::string subPath = iconsDir + "/" + sub;
            struct stat st{};
            if (::lstat(subPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                DIR* dSub = ::opendir(subPath.c_str());
                if (dSub) {
                    struct dirent* deSub;
                    while ((deSub = ::readdir(dSub)) != nullptr) {
                        std::string f = deSub->d_name;
                        if (f == "." || f == "..") continue;
                        ::unlink((subPath + "/" + f).c_str());
                    }
                    ::closedir(dSub);
                }
                ::rmdir(subPath.c_str());
            }
        }
        ::closedir(dIcons);
    }

    std::vector<std::pair<std::string, std::string>> titles(titleSet.begin(), titleSet.end());

    if (!backend_) backend_.reset(createPlatformTrophyBackend());
    if (backend_) {
        backend_->removeAllTrophies(titles, removedCount, err);
    }

    XC_LOGI("trophy sync engine: removeAllTrophies removed %d titles", removedCount);
    return true;
}

TrophySyncEngine& syncEngine() {
    static TrophySyncEngine s_engine;
    return s_engine;
}

}  // namespace xc::trophy
