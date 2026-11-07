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
#include <condition_variable>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <optional>
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

namespace {

bool isNetworkTransportError(const std::string& err) {
    if (err.empty()) return false;
    if (err.find("HTTP 0") != std::string::npos ||
        err.find("HTTP 429") != std::string::npos ||
        err.find("HTTP 500") != std::string::npos ||
        err.find("HTTP 502") != std::string::npos ||
        err.find("HTTP 503") != std::string::npos ||
        err.find("HTTP 504") != std::string::npos ||
        err.find("timeout") != std::string::npos ||
        err.find("Timeout") != std::string::npos ||
        err.find("timed out") != std::string::npos ||
        err.find("resolve") != std::string::npos ||
        err.find("connect") != std::string::npos ||
        err.find("connection") != std::string::npos ||
        err.find("refused") != std::string::npos ||
        err.find("reset") != std::string::npos ||
        err.find("SSL") != std::string::npos ||
        err.find("TLS") != std::string::npos ||
        err.find("network") != std::string::npos ||
        err.find("Network") != std::string::npos) {
        return true;
    }
    return false;
}

bool probeNetworkConnectivity() {
    net::Request req;
    req.url = "https://login.live.com/";
    req.method = "GET";
    req.timeoutMs = 3000;
    auto r = net::perform(req);
    return r.ok() || r.status > 0;
}

std::string normalizeHttpsUrl(std::string url) {
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

bool downloadAndConvert512Png(const std::string& rawUrl, std::vector<uint8_t>& outPng) {
    std::string url = normalizeHttpsUrl(rawUrl);
    if (url.empty()) return false;

    std::vector<std::string> candidates;
    if (url.find("store-images.s-microsoft.com") != std::string::npos && url.find("w=") == std::string::npos) {
        candidates.push_back(url + (url.find('?') == std::string::npos ? "?" : "&") +
                             "w=512&h=512&mode=scale&q=85&format=jpg");
        candidates.push_back(url);
    } else if (url.find("images-eds-ssl.xboxlive.com") != std::string::npos && url.find("format=") == std::string::npos) {
        candidates.push_back(url + (url.find('?') == std::string::npos ? "?" : "&") + "format=jpg");
        candidates.push_back(url);
    } else {
        candidates.push_back(url);
    }

    for (const auto& cand : candidates) {
        net::Request req;
        req.method = "GET";
        req.url = cand;
        auto r = net::perform(req);
        if (r.ok() && !r.body.empty()) {
            const auto* udata = reinterpret_cast<const uint8_t*>(r.body.data());
            if (is512x512Png(udata, r.body.size())) {
                outPng.assign(udata, udata + r.body.size());
                return true;
            }
            if (convertTo512Png(udata, r.body.size(), outPng)) {
                return true;
            }
        }
    }
    return false;
}

uint32_t legacyFnv1aSlot(const std::string& str) {
    uint32_t hash = 2166136261u;
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    return 80000 + (hash % 10000);
}

bool isCollidedLegacySlot(const std::string& cacheDir, const std::string& npCommId) {
    if (npCommId.size() < 9 || npCommId.rfind("NPWR", 0) != 0) return false;
    uint32_t slot = static_cast<uint32_t>(std::strtoul(npCommId.c_str() + 4, nullptr, 10));
    if (slot < 80000 || slot >= 90000) return false;
    int matchCount = 0;
    DIR* d = ::opendir(cacheDir.c_str());
    if (!d) return false;
    struct dirent* de = nullptr;
    while ((de = ::readdir(d)) != nullptr) {
        std::string fname = de->d_name;
        if (fname.size() > 5 && fname != "id_map.json" && fname.substr(fname.size() - 5) == ".json") {
            std::string seed = fname.substr(0, fname.size() - 5);
            if (legacyFnv1aSlot(seed) == slot) {
                if (++matchCount > 1) break;
            }
        }
    }
    ::closedir(d);
    return (matchCount > 1);
}

bool tryLoadCachedAchievements(const std::string& cacheContent, int expectedEarned, int expectedGamerscore,
                               xcloud::TitleAchievements& outAch) {
    if (expectedEarned <= 0 || cacheContent.empty()) return false;
    auto j = json::parse(cacheContent);
    if (!j || !j->isObject()) return false;
    int schemaVer = static_cast<int>((*j)["schemaVersion"].asInt(0));
    int cachedUnlocked = static_cast<int>((*j)["unlockedCount"].asInt(-1));
    int cachedTotal = static_cast<int>((*j)["totalCount"].asInt(-1));
    int cachedGs = static_cast<int>((*j)["unlockedGamerscore"].asInt(-1));
    if (cachedUnlocked != expectedEarned || cachedTotal <= 0) return false;
    if (cachedTotal > 127 && schemaVer != 2) return false;
    if (expectedGamerscore >= 0 && cachedGs != expectedGamerscore) return false;

    const auto& tArr = (*j)["trophies"];
    int expectedArrSize = std::min(cachedTotal, 127);
    if (!tArr.isArray() || static_cast<int>(tArr.items().size()) != expectedArrSize) return false;

    outAch = xcloud::TitleAchievements{};
    outAch.unlockedCount = cachedUnlocked;
    outAch.totalCount = cachedTotal;
    outAch.unlockedGamerscore = cachedGs;
    outAch.totalGamerscore = static_cast<int>((*j)["totalGamerscore"].asInt(0));
    outAch.items.reserve(tArr.items().size());
    for (const auto& t : tArr.items()) {
        xcloud::Achievement a;
        a.id = std::to_string(t["id"].asInt(0));
        a.name = t["name"].str();
        a.description = t["description"].str();
        a.gamerscore = static_cast<int>(t["gamerscore"].asInt(0));
        a.unlocked = t["unlocked"].asBool();
        a.iconUrl = normalizeHttpsUrl(t["iconUrl"].str());
        a.timeUnlocked = t["timeUnlocked"].str();
        if (a.name.empty()) return false;
        outAch.items.push_back(std::move(a));
    }
    return !outAch.items.empty();
}

}  // namespace

bool TrophySyncEngine::syncTitleAchievements(const std::string& xblAuth, const std::string& xuid,
                                             const std::string& xboxTitleId, const std::string& titleName,
                                             int& newlyUnlocked, std::string& err,
                                             const std::string& tileUrl,
                                             bool fetchTrophyIcons,
                                             int expectedEarned,
                                             int expectedGamerscore) {
    newlyUnlocked = 0;
    if (xblAuth.empty()) {
        err = "xblAuth empty";
        return false;
    }
    if (xboxTitleId.empty()) {
        err = "xboxTitleId empty";
        return false;
    }

    struct SyncDepthGuard {
        std::atomic<int>& d;
        explicit SyncDepthGuard(std::atomic<int>& depth) : d(depth) { d.fetch_add(1); }
        ~SyncDepthGuard() { d.fetch_sub(1); }
    } guard(syncingDepth_);

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        if (!backend_) backend_.reset(createPlatformTrophyBackend());
        if (backend_) initialized_ = backend_->init(0);
    }

    // Load local cache snapshot first so unchanged titles can reuse cached definitions
    std::set<std::string> prevUnlocked;
    std::string cacheFile = trophyCachePath(xboxTitleId);
    std::string cacheContent;
    if (platform::readFile(cacheFile, cacheContent) && !cacheContent.empty()) {
        if (auto j = json::parse(cacheContent); j && j->isObject()) {
            for (const auto& item : (*j)["unlockedIds"].items()) {
                if (item.isString()) prevUnlocked.insert(item.str());
            }
        }
    }

    // 1. Query Xbox Live achievements (or reuse verified cached achievement definitions)
    xcloud::TitleAchievements ach;
    if (!tryLoadCachedAchievements(cacheContent, expectedEarned, expectedGamerscore, ach)) {
        if (!xcloud::fetchTitleAchievements(xblAuth, xuid, xboxTitleId, ach, err, ui::gameLocale())) {
            XC_LOGW("trophy sync: fetchTitleAchievements failed for %s (%s): %s",
                    titleName.c_str(), xboxTitleId.c_str(), err.c_str());
            return false;
        }
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

    // 2b. Cache trophy and game icons on disk (and repair any tainted synthetic slot from legacy collisions)
    std::string iconsBase = trophyCacheDir() + "/icons";
    ::mkdir(iconsBase.c_str(), 0777);
    std::string iconsDir = iconsBase + "/" + titleSet.npCommId;
    ::mkdir(iconsDir.c_str(), 0777);

    const bool isSyntheticSlot = (titleSet.npCommId.rfind("NPWR8", 0) == 0 || titleSet.npCommId.rfind("NPWR9", 0) == 0) &&
                                 !isRetailInstalledGame(titleSet.sonyTitleId);
    if (isSyntheticSlot) {
        std::string ownerFile = iconsDir + "/.owner";
        std::string expectedOwner = xboxTitleId + ":" + std::to_string(titleSet.items.size()) + ":v2";
        std::string currentOwner;
        platform::readFile(ownerFile, currentOwner);
        if (currentOwner != expectedOwner) {
            bool tainted = (!currentOwner.empty() && currentOwner.rfind(xboxTitleId + ":", 0) != 0) ||
                           (ach.totalCount > 127 || titleSet.items.size() == 127) ||
                           isCollidedLegacySlot(trophyCacheDir(), titleSet.npCommId);
            std::string datPath = "/user/home/1ea2f4da/trophy2/nobackup/data/" + titleSet.npCommId + "/TRPTITLE.DAT";
            std::string datRaw;
            if (!tainted && platform::readFile(datPath, datRaw) && !datRaw.empty()) {
                std::map<uint32_t, ExistingTrophyRecord> diskRecs;
                if (parseTrpTitleDat(reinterpret_cast<const uint8_t*>(datRaw.data()), datRaw.size(), diskRecs)) {
                    if (diskRecs.size() != titleSet.items.size() + 1) {
                        tainted = true;
                    }
                }
            }
            if (tainted) {
                XC_LOGW("trophy sync: cleaning tainted synthetic slot %s for %s (%s)",
                        titleSet.npCommId.c_str(), titleSet.titleName.c_str(), xboxTitleId.c_str());
                DIR* d = ::opendir(iconsDir.c_str());
                if (d) {
                    struct dirent* de = nullptr;
                    while ((de = ::readdir(d)) != nullptr) {
                        std::string fn = de->d_name;
                        if (fn.size() > 4 && fn.substr(fn.size() - 4) == ".png") {
                            ::unlink((iconsDir + "/" + fn).c_str());
                        }
                    }
                    ::closedir(d);
                }
                ::unlink(datPath.c_str());
                ::unlink(("/user/trophy2/nobackup/conf/" + titleSet.npCommId + "/TROPHY.UCP").c_str());
                if (!titleSet.sonyTitleId.empty() && titleSet.sonyTitleId != "PPSA99810") {
                    ::unlink(("/system_data/priv/appmeta/" + titleSet.sonyTitleId + "/icon0.png").c_str());
                    ::unlink(("/user/appmeta/" + titleSet.sonyTitleId + "/icon0.png").c_str());
                    ::unlink(("/user/app/" + titleSet.sonyTitleId + "/sce_sys/icon0.png").c_str());
                }
                prevUnlocked.clear();
            }
            platform::writeFileAtomic(ownerFile, expectedOwner);
        }
    }

    bool anyIconDownloaded = false;
    std::string icon0File = iconsDir + "/icon0.png";
    struct stat st0{};
    bool icon0Valid = (::stat(icon0File.c_str(), &st0) == 0 && st0.st_size > 100 &&
                       st0.st_size != kDefaultIconPngSize && st0.st_size != kDefaultTropPngSize);
    if (!icon0Valid) {
        std::string candidatePaths[] = {
            "/system_data/priv/appmeta/" + titleSet.sonyTitleId + "/icon0.png",
            "/user/appmeta/" + titleSet.sonyTitleId + "/icon0.png",
            "/user/app/" + titleSet.sonyTitleId + "/sce_sys/icon0.png"
        };
        for (const auto& cp : candidatePaths) {
            std::string raw;
            if (platform::readFile(cp, raw) && raw.size() > 100 &&
                raw.size() != kDefaultIconPngSize && raw.size() != kDefaultTropPngSize) {
                std::vector<uint8_t> png512;
                const auto* udata = reinterpret_cast<const uint8_t*>(raw.data());
                if (is512x512Png(udata, raw.size())) {
                    platform::writeFileAtomic(icon0File, raw);
                    icon0Valid = true;
                    anyIconDownloaded = true;
                    ::stat(icon0File.c_str(), &st0);
                    break;
                } else if (convertTo512Png(udata, raw.size(), png512)) {
                    platform::writeFileAtomic(icon0File, std::string(reinterpret_cast<const char*>(png512.data()), png512.size()));
                    icon0Valid = true;
                    anyIconDownloaded = true;
                    ::stat(icon0File.c_str(), &st0);
                    break;
                }
            }
        }

        // Fetch from tileUrl if not found locally
        if (!icon0Valid) {
            std::string effectiveTileUrl = normalizeHttpsUrl(tileUrl);
            if (effectiveTileUrl.empty() && titleSet.npCommId == "NPWR20561_00") {
                effectiveTileUrl = "https://m.exophase.com/psn/games/l/d41b74.png";
            }
            if (!effectiveTileUrl.empty()) {
                std::vector<uint8_t> png512;
                if (downloadAndConvert512Png(effectiveTileUrl, png512)) {
                    platform::writeFileAtomic(icon0File, std::string(reinterpret_cast<const char*>(png512.data()), png512.size()));
                    XC_LOGI("trophy sync: cached cover icon for %s (%zu bytes)", titleSet.titleName.c_str(), png512.size());
                    icon0Valid = true;
                    anyIconDownloaded = true;
                    ::stat(icon0File.c_str(), &st0);
                }
            }
        }
    }

    // Platinum icon (trop0000.png)
    std::string platFile = iconsDir + "/trop0000.png";
    struct stat stPlat{};
    bool platValid = (::stat(platFile.c_str(), &stPlat) == 0 && stPlat.st_size > 100 &&
                      stPlat.st_size != kDefaultIconPngSize && stPlat.st_size != kDefaultTropPngSize);
    if (!platValid) {
        if (titleSet.npCommId == "NPWR20561_00") {
            std::vector<uint8_t> png512;
            if (downloadAndConvert512Png("https://m.exophase.com/psn/awards/l/d41b74.png", png512)) {
                platform::writeFileAtomic(platFile, std::string(reinterpret_cast<const char*>(png512.data()), png512.size()));
                XC_LOGI("trophy sync: cached genuine Platinum icon for %s (%zu bytes)", titleSet.npCommId.c_str(), png512.size());
                anyIconDownloaded = true;
            }
        } else if (icon0Valid) {
            std::string icon0Data;
            if (platform::readFile(icon0File, icon0Data) && icon0Data.size() > 100 &&
                icon0Data.size() != kDefaultIconPngSize && icon0Data.size() != kDefaultTropPngSize) {
                platform::writeFileAtomic(platFile, icon0Data);
                anyIconDownloaded = true;
            }
        }
    }

    if (fetchTrophyIcons) {
        std::vector<size_t> missingIdx;
        for (size_t i = 0; i < titleSet.items.size(); ++i) {
            const auto& def = titleSet.items[i];
            if (def.iconUrl.empty()) continue;

            char nameBuf[32];
            snprintf(nameBuf, sizeof(nameBuf), "trop%04d.png", def.trophyId);
            std::string iconFile = iconsDir + "/" + nameBuf;
            struct stat st{};
            if (::stat(iconFile.c_str(), &st) == 0 && st.st_size > 100 &&
                st.st_size != kDefaultTropPngSize &&
                st.st_size != kDefaultIconPngSize &&
                (st0.st_size == 0 || st.st_size != st0.st_size)) {
                continue;
            }
            missingIdx.push_back(i);
        }

        if (missingIdx.size() == 1) {
            const auto& def = titleSet.items[missingIdx[0]];
            char nameBuf[32];
            snprintf(nameBuf, sizeof(nameBuf), "trop%04d.png", def.trophyId);
            std::string iconFile = iconsDir + "/" + nameBuf;
            std::vector<uint8_t> png512;
            if (downloadAndConvert512Png(def.iconUrl, png512)) {
                platform::writeFileAtomic(iconFile, std::string(reinterpret_cast<const char*>(png512.data()), png512.size()));
                anyIconDownloaded = true;
                XC_LOGI("trophy sync: cached icon %s (%zu bytes) for %s", nameBuf, png512.size(), titleSet.titleName.c_str());
            }
        } else if (missingIdx.size() > 1) {
            std::atomic<size_t> nextJob{0};
            std::atomic<int> downloadedCount{0};
            const size_t numWorkers = std::min<size_t>(4, missingIdx.size());
            std::vector<std::thread> workers;
            workers.reserve(numWorkers);
            for (size_t w = 0; w < numWorkers; ++w) {
                workers.emplace_back([&]() {
                    while (true) {
                        size_t j = nextJob.fetch_add(1);
                        if (j >= missingIdx.size()) break;
                        const auto& def = titleSet.items[missingIdx[j]];
                        char nameBuf[32];
                        snprintf(nameBuf, sizeof(nameBuf), "trop%04d.png", def.trophyId);
                        std::string iconFile = iconsDir + "/" + nameBuf;
                        std::vector<uint8_t> png512;
                        if (downloadAndConvert512Png(def.iconUrl, png512)) {
                            platform::writeFileAtomic(iconFile, std::string(reinterpret_cast<const char*>(png512.data()), png512.size()));
                            downloadedCount.fetch_add(1);
                        }
                    }
                });
            }
            for (auto& t : workers) {
                if (t.joinable()) t.join();
            }
            if (downloadedCount.load() > 0) {
                anyIconDownloaded = true;
                XC_LOGI("trophy sync: cached %d/%zu trophy icons for %s",
                        downloadedCount.load(), missingIdx.size(), titleSet.titleName.c_str());
            }
        }
    }

    // 4. Register title with backend
    if (backend_) {
        backend_->registerTitle(titleSet);
    }

    // 5. Compare achievements and unlock new trophies
    bool platinumAwarded = prevUnlocked.count("__PLATINUM__") > 0;
    for (size_t i = 0; i < titleSet.items.size(); ++i) {
        const auto& def = titleSet.items[i];
        if (def.unlocked) {
            std::string key = std::to_string(def.trophyId);
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

    // 5b. Automatic Platinum award on 100% completion
    if (!platinumAwarded && hasEarnedPlatinum(titleSet.unlockedCount, titleSet.totalCount)) {
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

    // 6. Flush backend state to commit TROPHY.UCP & TRPTITLE.DAT when needed
    if (backend_) {
        bool needsDeploy = (newlyUnlocked > 0 || anyIconDownloaded);
        if (!needsDeploy) {
            std::string ucpPath = "/user/trophy2/nobackup/conf/" + titleSet.npCommId + "/TROPHY.UCP";
            struct stat stUcp{};
            if (::stat(ucpPath.c_str(), &stUcp) != 0 || stUcp.st_size < 1024) {
                needsDeploy = true;
            } else {
                off_t cachedIconsBytes = 0;
                if (icon0Valid) cachedIconsBytes += st0.st_size;
                for (const auto& def : titleSet.items) {
                    char nameBuf[32];
                    snprintf(nameBuf, sizeof(nameBuf), "/trop%04d.png", def.trophyId);
                    struct stat stIcon{};
                    if (::stat((iconsDir + nameBuf).c_str(), &stIcon) == 0 && stIcon.st_size > 100) {
                        cachedIconsBytes += stIcon.st_size;
                    }
                }
                if (stUcp.st_size < cachedIconsBytes) {
                    needsDeploy = true;
                } else if (icon0Valid && !titleSet.sonyTitleId.empty() && titleSet.sonyTitleId != "PPSA99810") {
                    std::string appmetaIcon = "/system_data/priv/appmeta/" + titleSet.sonyTitleId + "/icon0.png";
                    struct stat stAppIcon{};
                    if (::stat(appmetaIcon.c_str(), &stAppIcon) != 0 ||
                        stAppIcon.st_size == 0 ||
                        stAppIcon.st_size == static_cast<off_t>(kDefaultIconPngSize)) {
                        needsDeploy = true;
                    }
                }
            }
        }
        if (needsDeploy) {
            backend_->flush();
        }
    }

    // 7. Save updated cache atomically with rich profile information
    json::Value out = json::Value::object();
    out.set("schemaVersion", static_cast<int64_t>(2));
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
    return dispatchStreamPoll(xblAuth, xuid, xboxTitleId, titleName, tileUrl);
}

bool TrophySyncEngine::syncAllAccountTrophies(const std::string& xblAuth, const std::string& xuid,
                                             const std::vector<ui::GameTile>& ownedGames,
                                             int& totalTitles, int& totalTrophies, std::string& err,
                                             const std::function<bool()>& cancelCheck,
                                             const std::function<void(int done, int total, const std::string& title)>& progressCb,
                                             bool forceFetchMissingIcons,
                                             const std::function<bool(const std::string& xboxTitleId, const std::string& titleName)>& isTitleEnabled) {
    (void)forceFetchMissingIcons;
    totalTitles = 0;
    totalTrophies = 0;
    if (xblAuth.empty()) {
        err = "Conta não conectada / Account not signed in";
        return false;
    }

    struct SyncDepthGuard {
        std::atomic<int>& d;
        explicit SyncDepthGuard(std::atomic<int>& depth) : d(depth) { d.fetch_add(1); }
        ~SyncDepthGuard() { d.fetch_sub(1); }
    } guard(syncingDepth_);

    struct SyncTitleItem {
        std::string xboxTitleId;
        std::string titleName;
        std::string tileUrl;
        int earnedAchievements = -1;
        int currentGamerscore = -1;
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

    // 2. Query TitleHub batch images for history titles with earned achievements
    std::unordered_map<std::string, std::string> titleImages;
    if (!historyTitles.empty()) {
        std::vector<std::string> idsForBatch;
        idsForBatch.reserve(historyTitles.size());
        idsForBatch.push_back("267695549");
        for (const auto& h : historyTitles) {
            if (h.earnedAchievements > 0 && h.titleId != "267695549") {
                idsForBatch.push_back(h.titleId);
            }
        }
        std::string imgErr;
        xcloud::fetchTitleHubBatchImages(xblAuth, idsForBatch, titleImages, imgErr, ui::gameLocale());
        XC_LOGI("trophy back-sync: fetched %zu cover images via TitleHub batch", titleImages.size());
    }

    // Always prioritize Fortnite first for focused verification (AGENTS.md §8.1)
    seenXboxTitles.insert("267695549");
    titlesToSync.push_back({"267695549", "Fortnite", "", -1, -1});

    auto itFnImg = titleImages.find("267695549");
    if (itFnImg != titleImages.end()) {
        titlesToSync[0].tileUrl = itFnImg->second;
    }

    // Add titles from Xbox Live achievement history where earnedAchievements > 0
    for (const auto& h : historyTitles) {
        if (h.earnedAchievements <= 0) continue;
        if (h.titleId == "267695549") {
            titlesToSync[0].earnedAchievements = h.earnedAchievements;
            titlesToSync[0].currentGamerscore = h.currentGamerscore;
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
            titlesToSync.push_back({
                h.titleId,
                h.name.empty() ? ("Xbox Title " + h.titleId) : h.name,
                img,
                h.earnedAchievements,
                h.currentGamerscore
            });
        }
    }

    // Supplement tileUrl or titleName from owned library (matching by xboxTitleId or normalized titleName)
    std::unordered_map<std::string, std::string> catalogUrlByNormName;
    for (const auto& game : ownedGames) {
        if (!game.name.empty() && !game.tileUrl.empty()) {
            std::string norm = normalizeTrophyName(game.name);
            if (!norm.empty()) catalogUrlByNormName[norm] = normalizeHttpsUrl(game.tileUrl);
        }
        std::string xid = game.xboxTitleId;
        if (xid.empty() && !game.productId.empty()) {
            if (const auto* k = findKnownTitle("", game.productId)) {
                xid = k->xboxTitleId;
            }
        }
        if (xid.empty()) continue;

        if (xid == "267695549") {
            if (!game.tileUrl.empty()) titlesToSync[0].tileUrl = normalizeHttpsUrl(game.tileUrl);
            if (!game.name.empty()) titlesToSync[0].titleName = game.name;
        } else {
            for (auto& item : titlesToSync) {
                if (item.xboxTitleId == xid) {
                    if (item.tileUrl.empty() && !game.tileUrl.empty()) item.tileUrl = normalizeHttpsUrl(game.tileUrl);
                    if (item.titleName.empty() || item.titleName.find("Xbox Title") == 0) {
                        if (!game.name.empty()) item.titleName = game.name;
                    }
                    break;
                }
            }
        }
    }
    for (auto& item : titlesToSync) {
        if (item.tileUrl.empty() && !item.titleName.empty()) {
            auto it = catalogUrlByNormName.find(normalizeTrophyName(item.titleName));
            if (it != catalogUrlByNormName.end()) {
                item.tileUrl = it->second;
            }
        }
    }

    XC_LOGI("trophy back-sync: starting synchronization for %d titles", static_cast<int>(titlesToSync.size()));
    for (size_t i = 0; i < titlesToSync.size(); ++i) {
        if (cancelCheck && cancelCheck()) {
            XC_LOGI("trophy back-sync: cancelled by user after %d titles", totalTitles);
            break;
        }
        const auto& item = titlesToSync[i];
        if (isTitleEnabled && !isTitleEnabled(item.xboxTitleId, item.titleName)) {
            XC_LOGI("trophy back-sync: skipping %s (%s) (disabled in per-game settings)",
                    item.titleName.c_str(), item.xboxTitleId.c_str());
            std::string cPath = trophyCachePath(item.xboxTitleId);
            std::string cRaw;
            if (platform::readFile(cPath, cRaw) && !cRaw.empty()) {
                if (auto j = json::parse(cRaw); j && j->isObject()) {
                    std::string np = (*j)["npCommId"].str();
                    std::string sony = (*j)["sonyTitleId"].str();
                    if (!np.empty() && !isRetailInstalledGame(sony)) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (!backend_) backend_.reset(createPlatformTrophyBackend());
                        if (backend_) {
                            int rem = 0;
                            std::string remErr;
                            backend_->removeAllTrophies({{np, sony}}, rem, remErr);
                        }
                    }
                }
                ::unlink(cPath.c_str());
            }
            continue;
        }
        if (progressCb) {
            progressCb(static_cast<int>(i), static_cast<int>(titlesToSync.size()), item.titleName);
        }
        static const int kBackoffSeconds[] = { 1, 2, 4 };
        const int kMaxRetries = 3;

        for (int attempt = 0; attempt <= kMaxRetries; ++attempt) {
            if (cancelCheck && cancelCheck()) break;
            int newlyUnlocked = 0;
            std::string titleErr;
            if (syncTitleAchievements(xblAuth, xuid, item.xboxTitleId, item.titleName, newlyUnlocked, titleErr,
                                      item.tileUrl, true, item.earnedAchievements, item.currentGamerscore)) {
                totalTitles++;
                totalTrophies += newlyUnlocked;
                break;
            }

            bool isTransport = isNetworkTransportError(titleErr);
            XC_LOGW("trophy back-sync: title %s (%s) attempt %d/%d failed: %s (transport=%d)",
                    item.titleName.c_str(), item.xboxTitleId.c_str(), attempt + 1, kMaxRetries + 1,
                    titleErr.c_str(), isTransport ? 1 : 0);

            if (attempt < kMaxRetries) {
                int backoff = kBackoffSeconds[attempt];
                for (int s = 0; s < backoff * 10; ++s) {
                    if (cancelCheck && cancelCheck()) break;
                    platform::sleepMs(100);
                }
            } else if (isTransport) {
                XC_LOGW("trophy back-sync: persistent network loss for %s. Entering PausedWaitingForNetwork...",
                        item.titleName.c_str());
                if (progressCb) {
                    std::string pLabel = std::string(ui::tr(ui::Str::SyncingTrophies)) + " (Aguardando rede...)";
                    progressCb(static_cast<int>(i), static_cast<int>(titlesToSync.size()), pLabel);
                }
                while (!cancelCheck || !cancelCheck()) {
                    if (probeNetworkConnectivity()) {
                        XC_LOGI("trophy back-sync: network connectivity restored, retrying %s...", item.titleName.c_str());
                        attempt = -1; // Fresh retries on reconnection
                        break;
                    }
                    for (int s = 0; s < 20; ++s) {
                        if (cancelCheck && cancelCheck()) break;
                        platform::sleepMs(100);
                    }
                }
            }
        }
    }
    if (progressCb) {
        progressCb(static_cast<int>(titlesToSync.size()), static_cast<int>(titlesToSync.size()), "");
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
            if (fname == "id_map.json") {
                std::string content;
                if (platform::readFile(cacheDir + "/" + fname, content)) {
                    if (auto j = json::parse(content); j && j->isObject()) {
                        for (const auto& [seed, val] : j->members()) {
                            uint32_t slot = static_cast<uint32_t>(val.asInt(0));
                            if (slot >= 80000 && slot <= 99799) {
                                char npBuf[16], sonyBuf[16];
                                snprintf(npBuf, sizeof(npBuf), "NPWR%05u_00", slot);
                                snprintf(sonyBuf, sizeof(sonyBuf), "PPSA%05u", slot);
                                titleSet.insert({npBuf, sonyBuf});
                            }
                        }
                    }
                }
                continue;
            }
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

bool TrophySyncEngine::isSyncing() const {
    return syncingDepth_.load() > 0 || xc::trophy::isSyncing();
}

namespace {

enum class TaskType {
    RealtimePoll, // Urgent: real-time in-stream achievement poll
    BatchTitle,   // Individual title in batch
    RemoveAll     // Removal of local trophies
};

struct TrophyTask {
    TaskType type = TaskType::BatchTitle;
    std::string xblAuth;
    std::string xuid;
    std::string xboxTitleId;
    std::string titleName;
    std::string tileUrl;
    int expectedEarned = -1;
    int expectedGamerscore = -1;
    bool forceFetchIcons = true;
    int retries = 0;
};

platform::Thread g_trophyThread;
std::mutex g_trophyLifecycleMutex;
std::mutex g_trophyQueueMutex;
std::condition_variable g_trophyQueueCv;
std::deque<TrophyTask> g_trophyUrgentQueue;
std::deque<TrophyTask> g_trophyBatchQueue;

std::atomic<bool> g_trophyThreadRunning{false};
std::atomic<bool> g_trophyStopRequested{false};
std::atomic<bool> g_trophyCancelRequested{false};
std::atomic<bool> g_trophyBatchSyncPending{false};
std::atomic<bool> g_trophyForceFetchPending{true};
std::atomic<bool> g_trophySyncing{false};
std::atomic<int> g_trophyTitlesDone{0};
std::atomic<int> g_trophyTitlesTotal{0};

std::mutex g_batchParamMutex;
std::string g_explicitAuth;
std::string g_explicitXuid;
std::vector<ui::GameTile> g_explicitOwnedGames;
bool g_hasExplicitParams = false;

std::mutex g_providerMutex;
TrophyContextProvider g_contextProvider;

bool processUrgentTasksLocked(std::unique_lock<std::mutex>& lock, TrophySyncContext* ctx = nullptr) {
    bool hadRemoveAll = false;
    while (!g_trophyUrgentQueue.empty() && !g_trophyStopRequested.load()) {
        TrophyTask urgent = std::move(g_trophyUrgentQueue.front());
        g_trophyUrgentQueue.pop_front();
        lock.unlock();

        if (urgent.type == TaskType::RemoveAll) {
            hadRemoveAll = true;
            XC_LOGI("trophy thread: urgent task is RemoveAll, removing local trophies...");
            int removedCount = 0;
            std::string err;
            bool ok = syncEngine().removeAllTrophies(removedCount, err);
            XC_LOGI("trophy thread: urgent removeAllTrophies done (removed=%d, ok=%d)", removedCount, ok ? 1 : 0);
            if (ctx) {
                if (!ctx->completeCb) {
                    std::lock_guard<std::mutex> pLock(g_providerMutex);
                    if (g_contextProvider) g_contextProvider(*ctx);
                }
                if (ctx->completeCb) {
                    ctx->completeCb(removedCount, 0, ok ? "" : err);
                }
            } else {
                TrophySyncContext fallbackCtx;
                {
                    std::lock_guard<std::mutex> pLock(g_providerMutex);
                    if (g_contextProvider) g_contextProvider(fallbackCtx);
                }
                if (fallbackCtx.completeCb) {
                    fallbackCtx.completeCb(removedCount, 0, ok ? "" : err);
                }
            }
        } else if (urgent.type == TaskType::RealtimePoll) {
            int newlyUnlocked = 0;
            std::string err;
            bool ok = syncEngine().syncTitleAchievements(urgent.xblAuth, urgent.xuid,
                                                         urgent.xboxTitleId, urgent.titleName,
                                                         newlyUnlocked, err, urgent.tileUrl,
                                                         /*fetchTrophyIcons=*/true);
            if (ok && newlyUnlocked > 0) {
                XC_LOGI("trophy poll: unlocked %d trophies during stream for %s!",
                        newlyUnlocked, urgent.titleName.c_str());
            }
        } else {
            XC_LOGW("trophy thread: unexpected task type in urgent queue: %d", static_cast<int>(urgent.type));
        }

        lock.lock();
    }
    return hadRemoveAll;
}

void trophyThreadMain() {
    platform::setThreadAffinity(platform::ThreadRole::Worker, "xcloud_trophy");
    XC_LOGI("trophy thread: started and pinned to ThreadRole::Worker");

    TrophySyncContext ctx;
    int totalBatchTitles = 0;
    int totalBatchTrophies = 0;

    while (!g_trophyStopRequested.load()) {
        TrophyTask currentTask;
        bool hasTask = false;
        bool hasBatchPending = false;

        {
            std::unique_lock<std::mutex> lock(g_trophyQueueMutex);
            g_trophyQueueCv.wait(lock, [&] {
                return g_trophyStopRequested.load() ||
                       !g_trophyUrgentQueue.empty() ||
                       !g_trophyBatchQueue.empty() ||
                       g_trophyBatchSyncPending.load();
            });

            if (g_trophyStopRequested.load()) break;

            // 1. FAST-PATH: Urgent stream poll tasks always execute FIRST
            if (!g_trophyUrgentQueue.empty()) {
                currentTask = std::move(g_trophyUrgentQueue.front());
                g_trophyUrgentQueue.pop_front();
                hasTask = true;
            } else if (g_trophyBatchSyncPending.load()) {
                g_trophyBatchSyncPending.store(false);
                hasBatchPending = true;
            } else if (!g_trophyBatchQueue.empty()) {
                currentTask = std::move(g_trophyBatchQueue.front());
                g_trophyBatchQueue.pop_front();
                hasTask = true;
            }
        }

        if (g_trophyStopRequested.load()) break;

        // Handle batch discovery when a new batch sync is initiated
        if (hasBatchPending) {
            bool forceFetch = g_trophyForceFetchPending.load();
            ctx = TrophySyncContext();
            bool hasCtx = false;

            {
                std::lock_guard<std::mutex> bLock(g_batchParamMutex);
                if (g_hasExplicitParams) {
                    ctx.xblAuth = g_explicitAuth;
                    ctx.xuid = g_explicitXuid;
                    ctx.ownedGames = g_explicitOwnedGames;
                    g_hasExplicitParams = false;
                    hasCtx = true;
                }
            }

            if (!hasCtx) {
                std::lock_guard<std::mutex> pLock(g_providerMutex);
                if (g_contextProvider) hasCtx = g_contextProvider(ctx);
            } else if (!ctx.isStreaming) {
                std::lock_guard<std::mutex> pLock(g_providerMutex);
                if (g_contextProvider) {
                    TrophySyncContext cbCtx;
                    if (g_contextProvider(cbCtx)) {
                        if (!ctx.isStreaming) ctx.isStreaming = cbCtx.isStreaming;
                        if (!ctx.progressCb) ctx.progressCb = cbCtx.progressCb;
                        if (!ctx.completeCb) ctx.completeCb = cbCtx.completeCb;
                        if (!ctx.isTitleEnabled) ctx.isTitleEnabled = cbCtx.isTitleEnabled;
                    }
                }
            }

            if (!hasCtx || ctx.xblAuth.empty()) {
                XC_LOGW("trophy thread: batch sync cannot run (no auth available)");
                continue;
            }

            // If active game streaming is in progress, pause batch discovery.
            // Urgent tasks (RealtimePoll) continue executing with top priority while paused.
            while (!g_trophyStopRequested.load() && !g_trophyCancelRequested.load() &&
                   ctx.isStreaming && ctx.isStreaming()) {
                std::unique_lock<std::mutex> lock(g_trophyQueueMutex);
                g_trophyQueueCv.wait_for(lock, std::chrono::seconds(1), [&] {
                    return g_trophyStopRequested.load() || g_trophyCancelRequested.load() ||
                           !g_trophyUrgentQueue.empty() || (!ctx.isStreaming || !ctx.isStreaming());
                });
                if (g_trophyStopRequested.load() || g_trophyCancelRequested.load()) break;
                if (!g_trophyUrgentQueue.empty()) {
                    processUrgentTasksLocked(lock, &ctx);
                }
            }
            if (g_trophyStopRequested.load() || g_trophyCancelRequested.load()) continue;

            g_trophyCancelRequested.store(false);
            g_trophySyncing.store(true);
            totalBatchTitles = 0;
            totalBatchTrophies = 0;

            if (ctx.progressCb && (!ctx.isStreaming || !ctx.isStreaming())) {
                ctx.progressCb(true, 0, 1, ui::tr(ui::Str::SyncingTrophies));
            }

            // Discovery
            std::vector<xcloud::AchievementTitleHistoryItem> historyTitles;
            std::string histErr;
            if (xcloud::fetchAchievementHistoryTitles(ctx.xblAuth, ctx.xuid, historyTitles, histErr, ui::gameLocale())) {
                XC_LOGI("trophy thread: discovered %zu titles in Xbox achievement history", historyTitles.size());
            } else {
                XC_LOGW("trophy thread: fetchAchievementHistoryTitles failed: %s", histErr.c_str());
            }

            std::unordered_map<std::string, std::string> titleImages;
            if (!historyTitles.empty()) {
                std::vector<std::string> idsForBatch;
                idsForBatch.reserve(historyTitles.size());
                idsForBatch.push_back("267695549");
                for (const auto& h : historyTitles) {
                    if (h.earnedAchievements > 0 && h.titleId != "267695549") {
                        idsForBatch.push_back(h.titleId);
                    }
                }
                std::string imgErr;
                xcloud::fetchTitleHubBatchImages(ctx.xblAuth, idsForBatch, titleImages, imgErr, ui::gameLocale());
                XC_LOGI("trophy thread: fetched %zu cover images via TitleHub batch", titleImages.size());
            }

            std::unordered_set<std::string> seenXboxTitles;
            std::vector<TrophyTask> discoveredTasks;

            // Always prioritize Fortnite first for focused verification (AGENTS.md §8.1)
            seenXboxTitles.insert("267695549");
            TrophyTask fnTask;
            fnTask.type = TaskType::BatchTitle;
            fnTask.xboxTitleId = "267695549";
            fnTask.titleName = "Fortnite";
            fnTask.forceFetchIcons = forceFetch;
            auto itFnImg = titleImages.find("267695549");
            if (itFnImg != titleImages.end()) fnTask.tileUrl = itFnImg->second;
            discoveredTasks.push_back(fnTask);

            for (const auto& h : historyTitles) {
                if (h.earnedAchievements <= 0) continue;
                if (h.titleId == "267695549") {
                    discoveredTasks[0].expectedEarned = h.earnedAchievements;
                    discoveredTasks[0].expectedGamerscore = h.currentGamerscore;
                    if (!h.name.empty()) discoveredTasks[0].titleName = h.name;
                    if (discoveredTasks[0].tileUrl.empty() && !h.displayImage.empty()) {
                        discoveredTasks[0].tileUrl = h.displayImage;
                    }
                    continue;
                }
                if (seenXboxTitles.insert(h.titleId).second) {
                    std::string img = h.displayImage;
                    if (img.empty()) {
                        auto it = titleImages.find(h.titleId);
                        if (it != titleImages.end()) img = it->second;
                    }
                    TrophyTask t;
                    t.type = TaskType::BatchTitle;
                    t.xboxTitleId = h.titleId;
                    t.titleName = h.name.empty() ? ("Xbox Title " + h.titleId) : h.name;
                    t.tileUrl = img;
                    t.expectedEarned = h.earnedAchievements;
                    t.expectedGamerscore = h.currentGamerscore;
                    t.forceFetchIcons = forceFetch;
                    discoveredTasks.push_back(t);
                }
            }

            // Match artwork & names from ownedGames
            std::unordered_map<std::string, std::string> catalogUrlByNormName;
            for (const auto& game : ctx.ownedGames) {
                if (!game.name.empty() && !game.tileUrl.empty()) {
                    std::string norm = normalizeTrophyName(game.name);
                    if (!norm.empty()) catalogUrlByNormName[norm] = normalizeHttpsUrl(game.tileUrl);
                }
                std::string xid = game.xboxTitleId;
                if (xid.empty() && !game.productId.empty()) {
                    if (const auto* k = findKnownTitle("", game.productId)) {
                        xid = k->xboxTitleId;
                    }
                }
                if (xid.empty()) continue;

                if (xid == "267695549") {
                    if (!game.tileUrl.empty()) discoveredTasks[0].tileUrl = normalizeHttpsUrl(game.tileUrl);
                    if (!game.name.empty()) discoveredTasks[0].titleName = game.name;
                } else {
                    for (auto& item : discoveredTasks) {
                        if (item.xboxTitleId == xid) {
                            if (item.tileUrl.empty() && !game.tileUrl.empty()) item.tileUrl = normalizeHttpsUrl(game.tileUrl);
                            if (item.titleName.empty() || item.titleName.find("Xbox Title") == 0) {
                                if (!game.name.empty()) item.titleName = game.name;
                            }
                            break;
                        }
                    }
                }
            }
            for (auto& item : discoveredTasks) {
                if (item.tileUrl.empty() && !item.titleName.empty()) {
                    auto it = catalogUrlByNormName.find(normalizeTrophyName(item.titleName));
                    if (it != catalogUrlByNormName.end()) {
                        item.tileUrl = it->second;
                    }
                }
            }

            // Populate batchQueue under lock
            {
                std::lock_guard<std::mutex> lock(g_trophyQueueMutex);
                g_trophyBatchQueue.clear();
                for (auto& t : discoveredTasks) {
                    g_trophyBatchQueue.push_back(std::move(t));
                }
                g_trophyTitlesTotal.store(static_cast<int>(g_trophyBatchQueue.size()));
                g_trophyTitlesDone.store(0);
            }

            XC_LOGI("trophy thread: enqueued %zu titles for background synchronization", discoveredTasks.size());
            continue;
        }

        if (!hasTask) continue;

        // Process Task
        if (currentTask.type == TaskType::RealtimePoll) {
            int newlyUnlocked = 0;
            std::string err;
            bool ok = syncEngine().syncTitleAchievements(currentTask.xblAuth, currentTask.xuid,
                                                         currentTask.xboxTitleId, currentTask.titleName,
                                                         newlyUnlocked, err, currentTask.tileUrl,
                                                         /*fetchTrophyIcons=*/true);
            if (ok && newlyUnlocked > 0) {
                XC_LOGI("trophy poll: unlocked %d trophies during stream for %s!",
                        newlyUnlocked, currentTask.titleName.c_str());
            }
        } else if (currentTask.type == TaskType::RemoveAll) {
            XC_LOGI("trophy thread: removing all local trophies...");
            int removedCount = 0;
            std::string err;
            bool ok = syncEngine().removeAllTrophies(removedCount, err);
            XC_LOGI("trophy thread: removeAllTrophies done (removed=%d, ok=%d)", removedCount, ok ? 1 : 0);
            if (!ctx.completeCb) {
                std::lock_guard<std::mutex> pLock(g_providerMutex);
                if (g_contextProvider) g_contextProvider(ctx);
            }
            if (ctx.completeCb) {
                ctx.completeCb(removedCount, 0, ok ? "" : err);
            }
            g_trophyCancelRequested.store(false);
            g_trophySyncing.store(false);
        } else if (currentTask.type == TaskType::BatchTitle) {
            if (!ctx.isStreaming) {
                std::lock_guard<std::mutex> pLock(g_providerMutex);
                if (g_contextProvider) {
                    TrophySyncContext cbCtx;
                    if (g_contextProvider(cbCtx)) {
                        if (!ctx.isStreaming) ctx.isStreaming = cbCtx.isStreaming;
                        if (!ctx.progressCb) ctx.progressCb = cbCtx.progressCb;
                        if (!ctx.completeCb) ctx.completeCb = cbCtx.completeCb;
                        if (!ctx.isTitleEnabled) ctx.isTitleEnabled = cbCtx.isTitleEnabled;
                    }
                }
            }

            // If active game streaming is in progress, pause batch processing completely.
            // Urgent tasks (RealtimePoll) continue executing with top priority while paused.
            while (!g_trophyStopRequested.load() && !g_trophyCancelRequested.load() &&
                   ctx.isStreaming && ctx.isStreaming()) {
                std::unique_lock<std::mutex> lock(g_trophyQueueMutex);
                g_trophyQueueCv.wait_for(lock, std::chrono::seconds(1), [&] {
                    return g_trophyStopRequested.load() || g_trophyCancelRequested.load() ||
                           !g_trophyUrgentQueue.empty() || (!ctx.isStreaming || !ctx.isStreaming());
                });
                if (g_trophyStopRequested.load() || g_trophyCancelRequested.load()) break;
                if (!g_trophyUrgentQueue.empty()) {
                    processUrgentTasksLocked(lock, &ctx);
                }
            }
            if (g_trophyStopRequested.load()) break;

            if (g_trophyCancelRequested.load()) {
                XC_LOGI("trophy thread: batch sync cancelled by user");
                {
                    std::lock_guard<std::mutex> lock(g_trophyQueueMutex);
                    g_trophyBatchQueue.clear();
                }
                g_trophySyncing.store(false);
                g_trophyTitlesDone.store(0);
                g_trophyTitlesTotal.store(0);
                if (ctx.completeCb) ctx.completeCb(totalBatchTitles, totalBatchTrophies, "Cancelled");
                g_trophyCancelRequested.store(false);
                continue;
            }

            // Check if title is enabled
            if (ctx.isTitleEnabled && !ctx.isTitleEnabled(currentTask.xboxTitleId, currentTask.titleName)) {
                XC_LOGI("trophy thread: skipping %s (%s) (disabled in settings)",
                        currentTask.titleName.c_str(), currentTask.xboxTitleId.c_str());
                std::string cPath = syncEngine().trophyCachePath(currentTask.xboxTitleId);
                std::string cRaw;
                if (platform::readFile(cPath, cRaw) && !cRaw.empty()) {
                    if (auto j = json::parse(cRaw); j && j->isObject()) {
                        std::string np = (*j)["npCommId"].str();
                        std::string sony = (*j)["sonyTitleId"].str();
                        if (!np.empty() && !isRetailInstalledGame(sony)) {
                            int rem = 0;
                            std::string remErr;
                            if (syncEngine().backend()) {
                                syncEngine().backend()->removeAllTrophies({{np, sony}}, rem, remErr);
                            }
                        }
                    }
                    ::unlink(cPath.c_str());
                }
                g_trophyTitlesDone.fetch_add(1);
                continue;
            }

            // Execute single title with 3-attempt exponential backoff retry loop (1s, 2s, 4s)
            static const int kBackoffSeconds[] = { 1, 2, 4 };
            const int kMaxRetries = 3;
            bool titleOk = false;
            bool removedDuringTitle = false;

            for (int attempt = 0; attempt <= kMaxRetries; ++attempt) {
                while (!g_trophyStopRequested.load() && !g_trophyCancelRequested.load() &&
                       ctx.isStreaming && ctx.isStreaming()) {
                    std::unique_lock<std::mutex> lock(g_trophyQueueMutex);
                    g_trophyQueueCv.wait_for(lock, std::chrono::seconds(1), [&] {
                        return g_trophyStopRequested.load() || g_trophyCancelRequested.load() ||
                               !g_trophyUrgentQueue.empty() || (!ctx.isStreaming || !ctx.isStreaming());
                    });
                    if (g_trophyStopRequested.load() || g_trophyCancelRequested.load()) break;
                    if (!g_trophyUrgentQueue.empty()) {
                        processUrgentTasksLocked(lock, &ctx);
                    }
                }
                if (g_trophyStopRequested.load() || g_trophyCancelRequested.load()) break;

                // Preemptively process any urgent real-time poll before executing title attempt
                {
                    std::unique_lock<std::mutex> lock(g_trophyQueueMutex);
                    if (!g_trophyUrgentQueue.empty()) {
                        if (processUrgentTasksLocked(lock, &ctx)) {
                            removedDuringTitle = true;
                            g_trophyCancelRequested.store(true);
                        }
                    }
                }
                if (g_trophyStopRequested.load() || g_trophyCancelRequested.load()) break;

                int newlyUnlocked = 0;
                std::string titleErr;
                titleOk = syncEngine().syncTitleAchievements(ctx.xblAuth, ctx.xuid,
                                                             currentTask.xboxTitleId, currentTask.titleName,
                                                             newlyUnlocked, titleErr,
                                                             currentTask.tileUrl, currentTask.forceFetchIcons,
                                                             currentTask.expectedEarned, currentTask.expectedGamerscore);
                if (titleOk) {
                    totalBatchTitles++;
                    totalBatchTrophies += newlyUnlocked;
                    break;
                }

                bool isTransport = isNetworkTransportError(titleErr);
                XC_LOGW("trophy thread: title %s (%s) attempt %d/%d failed: %s (transport=%d)",
                        currentTask.titleName.c_str(), currentTask.xboxTitleId.c_str(),
                        attempt + 1, kMaxRetries + 1, titleErr.c_str(), isTransport ? 1 : 0);

                if (attempt < kMaxRetries) {
                    int backoff = kBackoffSeconds[attempt];
                    std::unique_lock<std::mutex> lock(g_trophyQueueMutex);
                    g_trophyQueueCv.wait_for(lock, std::chrono::seconds(backoff), [&] {
                        return g_trophyStopRequested.load() || g_trophyCancelRequested.load() || !g_trophyUrgentQueue.empty();
                    });
                    if (!g_trophyUrgentQueue.empty()) {
                        if (processUrgentTasksLocked(lock, &ctx)) {
                            removedDuringTitle = true;
                            g_trophyCancelRequested.store(true);
                        }
                    }
                } else if (isTransport) {
                    // 3 failed retries due to transport error -> PausedWaitingForNetwork
                    XC_LOGW("trophy thread: persistent network loss for %s. Entering PausedWaitingForNetwork...",
                            currentTask.titleName.c_str());

                    if (ctx.progressCb && (!ctx.isStreaming || !ctx.isStreaming())) {
                        std::string pausedLabel = std::string(ui::tr(ui::Str::SyncingTrophies)) + " (Aguardando rede...)";
                        ctx.progressCb(true, g_trophyTitlesDone.load(), g_trophyTitlesTotal.load(), pausedLabel);
                    }

                    // 2-second reconnect probe loop
                    while (!g_trophyStopRequested.load() && !g_trophyCancelRequested.load()) {
                        {
                            std::unique_lock<std::mutex> lock(g_trophyQueueMutex);
                            g_trophyQueueCv.wait_for(lock, std::chrono::seconds(2), [&] {
                                return g_trophyStopRequested.load() || g_trophyCancelRequested.load() || !g_trophyUrgentQueue.empty();
                            });

                            if (g_trophyStopRequested.load() || g_trophyCancelRequested.load()) break;

                            if (!g_trophyUrgentQueue.empty()) {
                                if (processUrgentTasksLocked(lock, &ctx)) {
                                    removedDuringTitle = true;
                                    g_trophyCancelRequested.store(true);
                                }
                            }
                        }

                        if (g_trophyStopRequested.load() || g_trophyCancelRequested.load()) break;

                        if (probeNetworkConnectivity()) {
                            XC_LOGI("trophy thread: network connectivity restored! Resuming sync for %s...",
                                    currentTask.titleName.c_str());
                            // Refresh tokens
                            {
                                std::lock_guard<std::mutex> pLock(g_providerMutex);
                                if (g_contextProvider) g_contextProvider(ctx);
                            }
                            // Reset attempt counter so current title gets fresh attempts
                            attempt = -1;
                            break;
                        }
                    }
                }
            }

            if (g_trophyStopRequested.load()) {
                g_trophySyncing.store(false);
                break;
            }

            if (g_trophyCancelRequested.load()) {
                XC_LOGI("trophy thread: batch sync cancelled during title processing (%s)",
                        currentTask.titleName.c_str());
                {
                    std::lock_guard<std::mutex> lock(g_trophyQueueMutex);
                    g_trophyBatchQueue.clear();
                }
                g_trophySyncing.store(false);
                g_trophyTitlesDone.store(0);
                g_trophyTitlesTotal.store(0);
                if (!removedDuringTitle && ctx.completeCb) {
                    ctx.completeCb(totalBatchTitles, totalBatchTrophies, "Cancelled");
                }
                g_trophyCancelRequested.store(false);
                continue;
            }

            int done = g_trophyTitlesDone.fetch_add(1) + 1;
            int total = g_trophyTitlesTotal.load();

            if (ctx.progressCb && (!ctx.isStreaming || !ctx.isStreaming())) {
                ctx.progressCb(true, done, total, currentTask.titleName);
            }

            // Check if batch is finished
            bool batchDone = false;
            {
                std::lock_guard<std::mutex> lock(g_trophyQueueMutex);
                batchDone = g_trophyBatchQueue.empty();
            }

            if (batchDone) {
                g_trophySyncing.store(false);
                bool wasCancelled = g_trophyCancelRequested.load();
                if (wasCancelled) {
                    g_trophyTitlesDone.store(0);
                    g_trophyTitlesTotal.store(0);
                }
                XC_LOGI("trophy thread: background batch sync %s. %d titles checked, %d trophies unlocked.",
                        wasCancelled ? "cancelled" : "complete",
                        totalBatchTitles, totalBatchTrophies);
                if (ctx.completeCb) {
                    ctx.completeCb(totalBatchTitles, totalBatchTrophies, wasCancelled ? "Cancelled" : "");
                }
            }
        }
    }

    g_trophySyncing.store(false);
    g_trophyThreadRunning.store(false);
    XC_LOGI("trophy thread: terminated cleanly");
}

} // namespace

void startTrophyThread() {
    std::lock_guard<std::mutex> lock(g_trophyLifecycleMutex);
    if (g_trophyThreadRunning.load()) return;
    if (g_trophyThread.joinable()) {
        g_trophyThread.join();
    }
    g_trophyStopRequested.store(false);
    g_trophyCancelRequested.store(false);
    g_trophyTitlesDone.store(0);
    g_trophyTitlesTotal.store(0);
    g_trophyThreadRunning.store(true);
    if (!platform::startThread(g_trophyThread, trophyThreadMain)) {
        g_trophyThreadRunning.store(false);
        XC_LOGW("trophy thread: failed to start thread");
    }
}

bool stopTrophyThread(uint32_t timeoutMs) {
    std::lock_guard<std::mutex> lock(g_trophyLifecycleMutex);
    if (!g_trophyThreadRunning.load() && !g_trophyThread.joinable()) return true;
    g_trophyStopRequested.store(true);
    g_trophyQueueCv.notify_all();

    uint64_t start = platform::nowMs();
    while (g_trophyThreadRunning.load()) {
        if (platform::nowMs() - start >= timeoutMs) break;
        platform::sleepMs(20);
    }

    if (!g_trophyThreadRunning.load() && g_trophyThread.joinable()) {
        g_trophyThread.join();
    }
    if (!g_trophyThreadRunning.load()) {
        g_trophySyncing.store(false);
        g_trophyTitlesDone.store(0);
        g_trophyTitlesTotal.store(0);
    }
    return !g_trophyThreadRunning.load();
}

void resumeTrophySync() {
    g_trophyQueueCv.notify_all();
}

void requestTrophySync(bool forceFetchIcons) {
    g_trophyCancelRequested.store(false);
    g_trophyForceFetchPending.store(forceFetchIcons);
    g_trophyBatchSyncPending.store(true);
    g_trophyQueueCv.notify_one();
}

void requestTrophySync(const std::string& xblAuth, const std::string& xuid,
                       const std::vector<ui::GameTile>& ownedGames,
                       bool forceFetchIcons) {
    {
        std::lock_guard<std::mutex> lock(g_batchParamMutex);
        g_explicitAuth = xblAuth;
        g_explicitXuid = xuid;
        g_explicitOwnedGames = ownedGames;
        g_hasExplicitParams = true;
    }
    requestTrophySync(forceFetchIcons);
}

void cancelTrophySync() {
    g_trophyCancelRequested.store(true);
    {
        std::lock_guard<std::mutex> lock(g_trophyQueueMutex);
        g_trophyBatchQueue.clear();
        g_trophyTitlesDone.store(0);
        g_trophyTitlesTotal.store(0);
    }
    g_trophyQueueCv.notify_all();
}

void requestRemoveTrophies() {
    g_trophyCancelRequested.store(true);
    {
        std::lock_guard<std::mutex> lock(g_trophyQueueMutex);
        g_trophyBatchQueue.clear();
        g_trophyTitlesDone.store(0);
        g_trophyTitlesTotal.store(0);
        TrophyTask task;
        task.type = TaskType::RemoveAll;
        g_trophyUrgentQueue.push_back(std::move(task));
    }
    g_trophyQueueCv.notify_all();
}

bool dispatchStreamPoll(const std::string& xblAuth, const std::string& xuid,
                        const std::string& xboxTitleId, const std::string& titleName,
                        const std::string& tileUrl) {
    if (xboxTitleId.empty() || xblAuth.empty()) return false;

    if (!g_trophyThreadRunning.load()) {
        int newlyUnlocked = 0;
        std::string err;
        return syncEngine().syncTitleAchievements(xblAuth, xuid, xboxTitleId, titleName,
                                                  newlyUnlocked, err, tileUrl, true);
    }

    {
        std::lock_guard<std::mutex> lock(g_trophyQueueMutex);
        for (const auto& t : g_trophyUrgentQueue) {
            if (t.type == TaskType::RealtimePoll && t.xboxTitleId == xboxTitleId) {
                return true;
            }
        }
        TrophyTask task;
        task.type = TaskType::RealtimePoll;
        task.xblAuth = xblAuth;
        task.xuid = xuid;
        task.xboxTitleId = xboxTitleId;
        task.titleName = titleName;
        task.tileUrl = tileUrl;
        g_trophyUrgentQueue.push_back(std::move(task));
    }
    g_trophyQueueCv.notify_one();
    return true;
}

bool isSyncing() {
    return g_trophySyncing.load();
}

int titlesDone() {
    return g_trophyTitlesDone.load();
}

int titlesTotal() {
    return g_trophyTitlesTotal.load();
}

void setTrophyContextProvider(TrophyContextProvider provider) {
    std::lock_guard<std::mutex> lock(g_providerMutex);
    g_contextProvider = std::move(provider);
}

}  // namespace xc::trophy
