// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/library_internal.h"

#include "platform/platform.h"
#include "ui/strings.h"
#include "util/json.h"
#include "util/log.h"
#include "xcloud/purchase.h"
#include "xcloud/titlehub.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <map>

namespace xc::app {

namespace {

json::Value productToJson(const xcloud::Product& p) {
    json::Value v = json::Value::object();
    v.set("xt", p.xcloudTitleId);
    v.set("xbox", p.xboxTitleId);
    v.set("title", p.title);
    v.set("publisher", p.publisher);
    v.set("description", p.description);
    v.set("tile", p.tileUrl);
    v.set("poster", p.posterUrl);
    v.set("hero", p.heroUrl);
    json::Value cats = json::Value::array();
    for (const auto& c : p.categories) cats.push(c);
    v.set("categories", cats);
    if (p.detailed) {
        v.set("d", true);
        v.set("at", p.detailedAt);
        v.set("modes", static_cast<int64_t>(p.modes));
        if (p.plan) v.set("plan", static_cast<int64_t>(p.plan));
        json::Value langs = json::Value::object();
        for (const auto& [lang, bits] : p.languages) langs.set(lang, static_cast<int64_t>(bits));
        v.set("langs", langs);
    }
    if (!p.ratingAge.empty()) {
        v.set("rage", p.ratingAge);
        v.set("rsys", p.ratingSystem);
        json::Value rdesc = json::Value::array();
        for (const auto& d : p.ratingDescriptors) rdesc.push(d);
        v.set("rdesc", rdesc);
    }
    if (p.isPcOnly) v.set("isPc", true);
    return v;
}

xcloud::Product productFromJson(const std::string& id, const json::Value& v) {
    xcloud::Product p;
    p.productId = id;
    p.xcloudTitleId = v["xt"].str();
    if (p.xcloudTitleId == "None") p.xcloudTitleId.clear();
    p.xboxTitleId = v["xbox"].str();
    p.title = v["title"].str();
    p.publisher = v["publisher"].str();
    p.description = v["description"].str();
    p.tileUrl = v["tile"].str();
    p.posterUrl = v["poster"].str();
    p.heroUrl = v["hero"].str();
    for (const auto& c : v["categories"].items()) p.categories.push_back(xcloud::categoryName(c.str()));
    p.isPcOnly = v["isPc"].asBool(false);
    p.detailed = v["d"].asBool(false) && !p.description.empty();
    // Details from before their date was kept count from now (not all due at once).
    p.detailedAt = v["at"].asInt(static_cast<int64_t>(std::time(nullptr)));
    p.modes = static_cast<uint32_t>(v["modes"].asInt(0));
    p.plan = static_cast<uint8_t>(v["plan"].asInt(0));
    for (const auto& [lang, bits] : v["langs"].members()) p.languages[lang] = static_cast<uint8_t>(bits.asInt(0));
    p.ratingAge = v["rage"].str();
    p.ratingSystem = v["rsys"].str();
    for (const auto& d : v["rdesc"].items()) p.ratingDescriptors.push_back(d.str());
    return p;
}

json::Value itemsToJson(const std::vector<std::pair<std::string, std::string>>& items) {
    json::Value a = json::Value::array();
    for (const auto& [pid, tid] : items) {
        json::Value o = json::Value::object();
        o.set("p", pid);
        o.set("t", tid);
        a.push(o);
    }
    return a;
}

}  // namespace

bool isStoreBigId(const std::string& s) {
    if (s.size() != 12) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    }
    return true;
}

namespace {

static int tileQualityScore(const ui::GameTile& t) {
    int score = 0;
    // Platform priority: Xbox Series X|S gets absolute highest priority over Xbox One and 360
    if (t.platform == xcloud::kPlatformSeries || t.platform == "Series" || t.platform == "XS") {
        score += 10000;
    } else if (namesSeries(t.name) || endsWith(t.name, "X|S") || endsWith(t.name, "X/S")) {
        score += 10000;
    } else if (t.titleId.find("XBOXSERIES") != std::string::npos ||
               t.titleId.find("SERIESXS") != std::string::npos ||
               endsWith(t.titleId, "XS")) {
        score += 10000;
    } else if (t.platform == xcloud::kPlatformOne || t.platform == "XboxOne" || t.platform == "ONE") {
        score += 5000;
    } else if (t.titleId.find("XBOXONE") != std::string::npos || t.name.find("Xbox One") != std::string::npos) {
        score += 5000;
    } else if (t.platform == xcloud::kPlatform360 || t.platform == "Xbox360" || t.platform == "360") {
        score += 1000;
    } else if (t.titleId.find("XBOX360") != std::string::npos || t.name.find("Xbox 360") != std::string::npos) {
        score += 1000;
    } else {
        score += 3000;  // Unknown console
    }

    if (t.playable) score += 500;
    if (t.detailed) score += 10;
    if (!t.tileUrl.empty()) score += 10;
    if (!t.heroUrl.empty()) score += 5;
    if (!t.description.empty()) score += 5;

    // Prefer shorter / cleaner title names when scores are otherwise tied
    score -= static_cast<int>(std::min<size_t>(t.name.size(), 40));
    return score;
}

}  // namespace

// Universal deduplication: normalized base titles (stripping Xbox Series X|S,
// Xbox One, PC, editions, etc.) and matching cloud title IDs.
// Absolute priority to Xbox Series X|S over Xbox One; preserves playable & store capabilities.
void dedupeByTitle(std::vector<ui::GameTile>& tiles) {
    std::map<std::string, size_t> keyMap;
    std::map<std::string, size_t> tidMap;
    std::map<std::string, size_t> xboxIdMap;
    std::vector<ui::GameTile> out;

    for (auto& t : tiles) {
        if (t.productId.empty() || t.name.empty()) continue;
        if (isStoreBigId(t.name) || (t.name == t.productId && isStoreBigId(t.productId))) continue;

        std::string norm = xcloud::normalizeBaseTitle(t.name);
        std::string tid = t.titleId;

        // Call of Duty HQ special sub-experience keying
        if (tid == "CALLOFDUTYHQ") {
            std::string lower;
            lower.reserve(t.name.size());
            for (char c : t.name) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            if (lower.find("modern warfare 4") != std::string::npos ||
                lower.find("modern warfare® 4") != std::string::npos ||
                lower.find("modern warfare iv") != std::string::npos ||
                lower.find("call of duty 4") != std::string::npos) {
                norm = "codhq:mw4";
            } else if (lower.find("warzone") != std::string::npos) {
                norm = "codhq:warzone";
            } else if (lower.find("black ops 7") != std::string::npos) {
                norm = "codhq:bo7";
            } else if (lower.find("black ops 6") != std::string::npos) {
                norm = "codhq:bo6";
            } else if (lower == "call of duty" || lower == "call of duty®") {
                norm = "codhq:hub";
            } else {
                norm = "codhq:" + t.productId;
            }
        }

        size_t matchIdx = static_cast<size_t>(-1);

        if (!norm.empty()) {
            auto it = keyMap.find(norm);
            if (it != keyMap.end()) matchIdx = it->second;
        }

        if (matchIdx == static_cast<size_t>(-1) && tid != "CALLOFDUTYHQ" && !tid.empty() && tid != "None" && !isStoreBigId(tid)) {
            auto it = tidMap.find(tid);
            if (it != tidMap.end()) matchIdx = it->second;
        }

        if (matchIdx == static_cast<size_t>(-1) && tid != "CALLOFDUTYHQ" && !t.xboxTitleId.empty() && !isStoreBigId(t.xboxTitleId)) {
            auto it = xboxIdMap.find(t.xboxTitleId);
            if (it != xboxIdMap.end()) matchIdx = it->second;
        }

        if (matchIdx == static_cast<size_t>(-1)) {
            size_t idx = out.size();
            if (!norm.empty()) keyMap[norm] = idx;
            if (tid != "CALLOFDUTYHQ" && !tid.empty() && tid != "None" && !isStoreBigId(tid)) {
                tidMap[tid] = idx;
            }
            if (tid != "CALLOFDUTYHQ" && !t.xboxTitleId.empty() && !isStoreBigId(t.xboxTitleId)) {
                xboxIdMap[t.xboxTitleId] = idx;
            }
            out.push_back(std::move(t));
        } else {
            auto& exist = out[matchIdx];
            bool anyPlayable = exist.playable || t.playable;
            bool anyPurchasable = exist.purchasable || t.purchasable;
            bool anyFree = exist.freeInStore || t.freeInStore;
            bool anyMkb = exist.mouseKeyboard || t.mouseKeyboard;

            int scoreNew = tileQualityScore(t);
            int scoreExist = tileQualityScore(exist);

            if (scoreNew > scoreExist) {
                // New tile is higher priority (e.g. Series X|S over Xbox One)
                if (t.tileUrl.empty()) t.tileUrl = exist.tileUrl;
                if (t.heroUrl.empty()) t.heroUrl = exist.heroUrl;
                if (t.description.empty()) t.description = exist.description;
                if (t.publisher.empty()) t.publisher = exist.publisher;
                if (t.categories.empty()) t.categories = exist.categories;
                if (t.ratingAge.empty()) t.ratingAge = exist.ratingAge;
                if (t.ratingSystem.empty()) t.ratingSystem = exist.ratingSystem;
                if (t.ratingDescriptors.empty()) t.ratingDescriptors = exist.ratingDescriptors;
                if (t.platform.empty()) t.platform = exist.platform;
                exist = std::move(t);
            } else {
                // Existing tile is higher or equal priority; absorb any metadata from new tile
                if (exist.tileUrl.empty()) exist.tileUrl = t.tileUrl;
                if (exist.heroUrl.empty()) exist.heroUrl = t.heroUrl;
                if (exist.description.empty()) exist.description = t.description;
                if (exist.publisher.empty()) exist.publisher = t.publisher;
                if (exist.categories.empty()) exist.categories = t.categories;
                if (exist.ratingAge.empty()) exist.ratingAge = t.ratingAge;
                if (exist.ratingSystem.empty()) exist.ratingSystem = t.ratingSystem;
                if (exist.ratingDescriptors.empty()) exist.ratingDescriptors = t.ratingDescriptors;
                if (exist.platform.empty()) exist.platform = t.platform;
            }
            exist.playable = anyPlayable;
            exist.purchasable = anyPurchasable;
            exist.freeInStore = anyFree;
            exist.mouseKeyboard = anyMkb;
            if (!exist.detailed && !exist.description.empty() && !exist.tileUrl.empty() && !exist.heroUrl.empty()) {
                exist.detailed = true;
            }

            if (!norm.empty()) keyMap[norm] = matchIdx;
            if (exist.titleId != "CALLOFDUTYHQ" && !exist.titleId.empty() && exist.titleId != "None" && !isStoreBigId(exist.titleId)) {
                tidMap[exist.titleId] = matchIdx;
            }
            if (exist.titleId != "CALLOFDUTYHQ" && !exist.xboxTitleId.empty() && !isStoreBigId(exist.xboxTitleId)) {
                xboxIdMap[exist.xboxTitleId] = matchIdx;
            }
        }
    }
    tiles = std::move(out);
}

ui::GameTile Library::tile(const std::string& productId, const std::string& titleId) const {
    ui::GameTile t;
    t.productId = productId;
    t.titleId = titleId;
    static const xcloud::Product s_dummy{};
    auto it = products_.find(productId);
    if (it == products_.end()) {
        if (!titleId.empty()) {
            for (auto pit = products_.begin(); pit != products_.end(); ++pit) {
                if (pit->second.xcloudTitleId == titleId) {
                    it = pit;
                    if (t.productId.empty()) t.productId = pit->first;
                    break;
                }
            }
            if (it == products_.end()) {
                for (const auto& [cpid, ctid] : cloudTitleOf_) {
                    if (ctid == titleId) {
                        it = products_.find(cpid);
                        if (it != products_.end()) {
                            if (t.productId.empty()) t.productId = cpid;
                            break;
                        }
                    }
                }
            }
        }
        if (it == products_.end()) {
            for (const auto& g : kKnownCloudGames) {
                bool match = (!productId.empty() && productId == g.productId) ||
                             (!titleId.empty() && (titleId == g.cloudTitleId ||
                              (titleId == "CALLOFDUTYCOLDWAR" && std::string_view(g.cloudTitleId) == "CALLOFDUTYBLACKOPSCOLDWAR")));
                if (match) {
                    if (t.productId.empty()) t.productId = g.productId;
                    if (t.titleId.empty() || t.titleId == "None" || isStoreBigId(t.titleId)) {
                        if (g.cloudTitleId) t.titleId = g.cloudTitleId;
                    }
                    if (t.name.empty() && g.fallbackTitle) t.name = g.fallbackTitle;
                    if (t.publisher.empty() && g.publisher) t.publisher = g.publisher;
                    if (t.tileUrl.empty() && g.tileUrl) t.tileUrl = g.tileUrl;
                    if (t.heroUrl.empty() && g.heroUrl) t.heroUrl = g.heroUrl;
                    if (t.description.empty() && g.description) t.description = g.description;
                    if (t.categories.empty() && g.category) t.categories.push_back(g.category);
                    if (t.ratingAge.empty() && g.ratingAge) t.ratingAge = g.ratingAge;
                    if (t.ratingSystem.empty() && g.ratingSystem) t.ratingSystem = g.ratingSystem;
                    if (t.ratingDescriptors.empty() && g.ratingDescriptors && g.ratingDescriptors[0]) {
                        t.ratingDescriptors.push_back(g.ratingDescriptors);
                    }
                    if (!t.description.empty() && !t.tileUrl.empty() && !t.heroUrl.empty()) t.detailed = true;
                    break;
                }
            }
        }
    }
    const xcloud::Product& p = (it != products_.end()) ? it->second : s_dummy;
    if (t.titleId.empty() || t.titleId == "None") {
        auto ct = cloudTitleOf_.find(productId);
        if (ct != cloudTitleOf_.end()) t.titleId = ct->second;
    }
    if (it != products_.end()) {
        if (t.titleId.empty() || t.titleId == "None") t.titleId = p.xcloudTitleId;
        if (t.titleId == "None") t.titleId.clear();
        if (t.titleId.empty() && !productId.empty()) t.titleId = productId;
        t.name = p.title;
        if (t.name.empty() && !isStoreBigId(productId)) {
            t.name = productId;
        }
        t.publisher = p.publisher;
        t.description = p.description;
        t.tileUrl = p.tileUrl.empty() ? p.posterUrl : p.tileUrl;
        t.heroUrl = p.heroUrl;
        t.categories = p.categories;
        t.detailed = p.detailed;
        t.modes = p.modes;
        t.ratingAge = p.ratingAge;
        t.ratingSystem = p.ratingSystem;
        t.ratingDescriptors = p.ratingDescriptors;
        if (auto lang = p.languages.find(language_.substr(0, language_.find('-'))); lang != p.languages.end())
            t.languages = lang->second;  // in the app's language
    } else {
        if (t.name.empty()) {
            if (isStoreBigId(productId)) t.name.clear();
            else t.name = productId;
        }
    }
    t.freeInStore = freeInStore_.count(productId) > 0;
    // Free-to-play games are open to every plan and to no plan, even those
    // also tagged with a Pass product id in the catalog.
    t.plan = (!t.freeInStore && gamePass_.count(productId)) ? p.plan : 0;
    uint8_t myPlan = xcloud::planTier(subscription_);
    if (ownershipKnown_) {
        t.playable = ownedTitles_.count(t.titleId) || ownedProducts_.count(t.productId);
        if (!t.playable && (t.titleId == "CALLOFDUTYHQ" || t.titleId.find("CALLOFDUTY") != std::string::npos)) {
            for (const auto& ot : ownedTitles_) {
                if (ot.find("CALLOFDUTY") != std::string::npos) {
                    t.playable = true;
                    break;
                }
            }
        }
        if (!t.playable && (gamePassMember_ || myPlan >= xcloud::kPlanEssential) && gamePass_.count(productId)) {
            if (myPlan == 0 || xcloud::planIncludes(myPlan, t.plan, t.freeInStore)) {
                t.playable = true;
            }
        }
    }
    // Added to Game Pass since the last start: the account's list from then
    // doesn't have it yet. Not locked until this start's list arrives, if the
    // account's plan includes it.
    if (!t.playable && !ownershipFresh_ && !knownGamePass_.empty() && gamePass_.count(productId) &&
        !knownGamePass_.count(productId) && (myPlan == 0 || xcloud::planIncludes(myPlan, t.plan, t.freeInStore)))
        t.playable = true;
    if (t.playable && !ownershipKnown_) {
        if (myPlan > 0 && !xcloud::planIncludes(myPlan, t.plan, t.freeInStore)) t.playable = false;
    }
    t.mouseKeyboard = mouseKeyboard_.count(productId) > 0;
    // A free-to-play game not on the account yet is got like one bought
    // (the store page's QR code), only free.
    t.purchasable = !t.playable && (purchasableSet_.count(productId) || t.freeInStore);
    if (siblingsFor_ != products_.size()) {
        seriesSiblings_.clear();
        sharedXbox_.clear();
        std::set<std::string> seen;
        for (const auto& [id, prod] : products_) {
            auto xs = xboxTitleOf_.find(id);
            const std::string& xbox = xs != xboxTitleOf_.end() ? xs->second : prod.xboxTitleId;
            if (!xbox.empty() && !seen.insert(xbox).second) sharedXbox_.insert(xbox);
        }
        // The pairs with a Series X|S side: the other side is the Xbox One one.
        for (const auto& [id, prod] : products_) {
            auto xs = xboxTitleOf_.find(id);
            const std::string& xbox = xs != xboxTitleOf_.end() ? xs->second : prod.xboxTitleId;
            if (namesSeries(prod.title) || (sharedXbox_.count(xbox) && pairVersion(prod) > 0)) seriesSiblings_.insert(xbox);
        }
        siblingsFor_ = products_.size();
    }
    auto x = xboxTitleOf_.find(productId);
    std::string xbox = x != xboxTitleOf_.end() ? x->second : p.xboxTitleId;
    t.xboxTitleId = xbox;
    auto pl = platform_.find(xbox);
    if (pl != platform_.end()) t.platform = pl->second;
    // Cross-gen pairs ("Call of Duty: Vanguard" and "... - Xbox Series X|S")
    // are two cloud titles sharing one Xbox title id: the name tells them apart.
    // The cloud title id names the version where the store's name doesn't
    // say it in English ("Hogwarts Legacy" / "... Versão Xbox One":
    // HOGWARTSLEGACYXBOXSERIESXSVERSION / ...XBOXONEVERSION).
    int version = sharedXbox_.count(xbox) ? pairVersion(p) : 0;
    if (version < 0) t.platform = xcloud::kPlatformOne;
    else if (version > 0) t.platform = xcloud::kPlatformSeries;
    else if (namesSeries(p.title) || namesSeries(t.name)) t.platform = xcloud::kPlatformSeries;
    else if (t.name.find("Xbox One") != std::string::npos || p.title.find("Xbox One") != std::string::npos) t.platform = xcloud::kPlatformOne;
    else if (t.name.find("Xbox 360") != std::string::npos || p.title.find("Xbox 360") != std::string::npos) t.platform = xcloud::kPlatform360;
    else if (t.platform == xcloud::kPlatformSeries && seriesSiblings_.count(xbox)) t.platform = xcloud::kPlatformOne;
    if (t.tileUrl.empty() || t.heroUrl.empty() || t.description.empty() ||
        t.name.empty() || isStoreBigId(t.name) || (t.name == t.productId && isStoreBigId(t.productId)) ||
        t.titleId.empty() || t.titleId == "None" || isStoreBigId(t.titleId)) {
        for (const auto& g : kKnownCloudGames) {
            bool match = (!t.productId.empty() && t.productId == g.productId) ||
                         (!t.titleId.empty() && (t.titleId == g.cloudTitleId ||
                          (t.titleId == "CALLOFDUTYCOLDWAR" && std::string_view(g.cloudTitleId) == "CALLOFDUTYBLACKOPSCOLDWAR")));
            if (match) {
                if (t.productId.empty()) t.productId = g.productId;
                if ((t.titleId.empty() || t.titleId == "None" || isStoreBigId(t.titleId)) && g.cloudTitleId) {
                    t.titleId = g.cloudTitleId;
                }
                if ((t.name.empty() || isStoreBigId(t.name) || t.name == t.productId) && g.fallbackTitle) {
                    t.name = g.fallbackTitle;
                }
                if (t.tileUrl.empty() && g.tileUrl) t.tileUrl = g.tileUrl;
                if (t.heroUrl.empty() && g.heroUrl) t.heroUrl = g.heroUrl;
                if (t.description.empty() && g.description) t.description = g.description;
                if (t.publisher.empty() && g.publisher) t.publisher = g.publisher;
                if (t.categories.empty() && g.category) t.categories.push_back(g.category);
                if (t.ratingAge.empty() && g.ratingAge) t.ratingAge = g.ratingAge;
                if (t.ratingSystem.empty() && g.ratingSystem) t.ratingSystem = g.ratingSystem;
                if (t.ratingDescriptors.empty() && g.ratingDescriptors && g.ratingDescriptors[0]) {
                    t.ratingDescriptors.push_back(g.ratingDescriptors);
                }
                if (!t.description.empty() && !t.tileUrl.empty() && !t.heroUrl.empty()) t.detailed = true;
                break;
            }
        }
    }
    return t;
}

bool Library::isPcOnly(const std::string& productId) const {
    auto it = products_.find(productId);
    if (it != products_.end()) {
        if (xcloud::isPcProduct(it->second)) return true;
    }
    return false;
}

bool Library::hasStreamingSupport(const ui::GameTile& t) const {
    if ((!t.productId.empty() && acquiredProducts_.count(t.productId)) ||
        (!t.titleId.empty() && acquiredTitles_.count(t.titleId))) {
        return true;
    }
    if (t.titleId.empty() || t.titleId == "None") return false;
    if (t.titleId == t.productId && isStoreBigId(t.productId)) {
        auto ct = cloudTitleOf_.find(t.productId);
        if (ct == cloudTitleOf_.end() || ct->second.empty() || ct->second == "None") return false;
    }
    return true;
}

std::vector<ui::GameTile> Library::tiles(const std::vector<Item>& items) const {
    std::vector<ui::GameTile> out;
    for (const auto& [pid, tid] : items) {
        ui::GameTile t = tile(pid, tid);
        if (xcloud::isNonGameAddon(t.titleId, t.name)) continue;
        if (t.productId.empty() || t.titleId.empty() || t.name.empty()) continue;
        if (isStoreBigId(t.name) || (t.name == t.productId && isStoreBigId(t.productId))) {
            auto pit = products_.find(t.productId);
            if (pit != products_.end() && !pit->second.title.empty() && !isStoreBigId(pit->second.title)) {
                t.name = pit->second.title;
            }
        }
        if (isStoreBigId(t.name) || (t.name == t.productId && isStoreBigId(t.productId))) continue;
        if (isPcOnly(t.productId) || xcloud::isPcTitle(t.name)) continue;
        if (!hasStreamingSupport(t)) continue;
        bool isAcq = (!t.productId.empty() && acquiredProducts_.count(t.productId)) ||
                     (!t.titleId.empty() && acquiredTitles_.count(t.titleId));
        if (isAcq) {
            if (t.titleId.empty() || t.titleId == "None") {
                auto ct = cloudTitleOf_.find(t.productId);
                if (ct != cloudTitleOf_.end()) t.titleId = ct->second;
            }
            t.playable = true;
            t.purchasable = false;
        }
        out.push_back(std::move(t));
    }
    dedupeByTitle(out);
    for (auto& t : out) {
        if (t.playable) t.purchasable = false;
    }
    return out;
}

std::vector<ui::GameRow> Library::rows() const {
    std::vector<ui::GameRow> rows;
    uint8_t myPlan = xcloud::planTier(subscription_);
    for (const auto& r : layout_) {
        ui::GameRow row;
        row.title = r.title;
        row.gamePassBadges = r.badges;
        row.isGrid = r.isGrid;
        for (const auto& [pid, tid] : r.items) {
            ui::GameTile t = tile(pid, tid);
            if (t.productId.empty() || t.titleId.empty() || t.name.empty() ||
                isStoreBigId(t.name) || (t.name == t.productId && isStoreBigId(t.productId)) ||
                xcloud::isNonGameAddon(t.titleId, t.name)) continue;
            if (isPcOnly(t.productId) || xcloud::isPcTitle(t.name)) continue;
            if (!hasStreamingSupport(t)) continue;
            // "Jump back in": Game Pass games and the account's own (bought,
            // free-to-play); not one it can't start any more.
            if (r.recent && !gamePass_.empty() && !gamePass_.count(pid) && !t.playable) continue;
            // Rows show only what the account's plan plays (free-to-play is
            // open to every plan).
            if (!t.playable && !t.freeInStore && !r.isGrid &&
                (myPlan > 0 && !xcloud::planIncludes(myPlan, t.plan, t.freeInStore)))
                continue;
            row.tiles.push_back(std::move(t));
        }
        dedupeByTitle(row.tiles);
        if (!row.tiles.empty()) rows.push_back(std::move(row));
    }

    // Insert "Friends playing now" dynamically if any friend is playing an accessible title
    if (!friendsTiles_.empty()) {
        ui::GameRow friendsRow;
        friendsRow.title = ui::tr(ui::Str::FriendsPlayingNow);
        friendsRow.gamePassBadges = false;
        friendsRow.isGrid = false;
        friendsRow.tiles = friendsTiles_;
        for (auto& t : friendsRow.tiles) {
            ui::GameTile fresh = tile(t.productId, t.titleId);
            if (!fresh.heroUrl.empty()) t.heroUrl = fresh.heroUrl;
            if (!fresh.description.empty() && t.description.empty()) t.description = fresh.description;
            if (!fresh.name.empty() && t.name.empty()) t.name = fresh.name;
            if (!fresh.tileUrl.empty() && t.tileUrl.empty()) t.tileUrl = fresh.tileUrl;
            if (!fresh.publisher.empty() && t.publisher.empty()) t.publisher = fresh.publisher;
            if (!fresh.categories.empty() && t.categories.empty()) t.categories = fresh.categories;
        }
        dedupeByTitle(friendsRow.tiles);

        // Place right after "Jump back in" if present, otherwise at index 0
        size_t insertIdx = 0;
        if (!rows.empty() && rows[0].title == ui::tr(ui::Str::JumpBackIn)) {
            insertIdx = 1;
        }
        if (insertIdx < rows.size()) {
            rows.insert(rows.begin() + static_cast<long>(insertIdx), std::move(friendsRow));
        } else {
            rows.push_back(std::move(friendsRow));
        }
    }

    // Insert "Favorites" row at the top of the library if any are marked
    if (!favorites_.empty()) {
        ui::GameRow favRow;
        favRow.title = ui::tr(ui::Str::Favorites);
        favRow.gamePassBadges = false;
        favRow.isGrid = false;
        for (const auto& pid : favorites_) {
            ui::GameTile t = tile(pid);
            if (!t.productId.empty() && !t.titleId.empty() && !t.name.empty() &&
                !isStoreBigId(t.name) && !(t.name == t.productId && isStoreBigId(t.productId)) &&
                !xcloud::isNonGameAddon(t.titleId, t.name)) {
                favRow.tiles.push_back(std::move(t));
            }
        }
        dedupeByTitle(favRow.tiles);
        if (!favRow.tiles.empty()) {
            rows.insert(rows.begin(), std::move(favRow));
        }
    }

    return rows;
}

std::vector<ui::GameTile> Library::owned() const { return tiles(owned_); }
std::vector<ui::GameTile> Library::purchasable() const { return tiles(purchasable_); }

std::vector<ui::GameTile> Library::gamePassSearchPool() const {
    std::vector<Item> items;
    for (const auto& id : allGames_) items.emplace_back(id, std::string());
    return tiles(items);
}

std::vector<ui::GameTile> Library::librarySearchPool() const {
    std::vector<Item> items = owned_;
    items.insert(items.end(), purchasable_.begin(), purchasable_.end());
    for (const auto& id : allGames_) items.emplace_back(id, std::string());
    return tiles(items);
}

void Library::setPlaytime(const std::map<std::string, uint64_t>& playtime) {
    playtime_ = playtime;
    sortOwned();
}

void Library::recordRecent(const std::string& productIdOrTitleId) {
    if (productIdOrTitleId.empty()) return;
    std::string pid = productIdOrTitleId;
    for (const auto& [p, t] : cloudTitleOf_) {
        if (t == productIdOrTitleId) { pid = p; break; }
    }
    auto it = std::find(recent_.begin(), recent_.end(), pid);
    if (it != recent_.end()) recent_.erase(it);
    recent_.insert(recent_.begin(), pid);
    sortOwned();
    saveCache();
}

void Library::sortOwned() {
    // Recently played first (newest first in recent_), then titles with accumulated playtime, then alphabetical.
    auto rank = [&](const std::string& pid, const std::string& tid) -> size_t {
        auto it = std::find(recent_.begin(), recent_.end(), pid);
        if (it != recent_.end()) return static_cast<size_t>(it - recent_.begin());
        if (!tid.empty()) {
            auto itT = std::find(recent_.begin(), recent_.end(), tid);
            if (itT != recent_.end()) return static_cast<size_t>(itT - recent_.begin());
        }
        return static_cast<size_t>(1000000000);
    };
    auto playtime = [&](const std::string& pid, const std::string& tid) -> uint64_t {
        if (!playtime_.empty()) {
            auto itP = playtime_.find(pid);
            if (itP != playtime_.end() && itP->second > 0) return itP->second;
            if (!tid.empty()) {
                auto itT = playtime_.find(tid);
                if (itT != playtime_.end() && itT->second > 0) return itT->second;
            }
        }
        return 0;
    };
    auto name = [&](const std::string& pid) {
        auto it = products_.find(pid);
        return it == products_.end() ? std::string() : it->second.title;
    };
    std::stable_sort(owned_.begin(), owned_.end(), [&](const auto& a, const auto& b) {
        size_t ra = rank(a.first, a.second), rb = rank(b.first, b.second);
        if (ra != rb) return ra < rb;
        if (ra >= 1000000000 && rb >= 1000000000) {
            uint64_t pa = playtime(a.first, a.second);
            uint64_t pb = playtime(b.first, b.second);
            if (pa != pb) return pa > pb;
        }
        return name(a.first) < name(b.first);
    });
}

bool Library::loadCache(bool withLayout) {
    std::string text;
    if (cachePath_.empty() || !platform::readFile(cachePath_, text)) {
        std::string fallback;
        if (!cachePath_.empty()) {
            auto pos = cachePath_.rfind('/');
            if (pos != std::string::npos) {
                fallback = cachePath_.substr(0, pos + 1) + "library.json";
            }
        }
        if (fallback.empty() || !platform::readFile(fallback, text)) {
            fallback = platform::dataDir().empty() ? "library.json" : (platform::dataDir() + "/library.json");
            if (!platform::readFile(fallback, text)) return false;
        }
    }
    auto j = json::parse(text);
    if (!j) return false;
    int ver = (*j)["version"].asInt();
    if (ver != 2 && ver != kCacheVersion) return false;
    if (market_.empty()) market_ = (*j)["market"].str();
    else if ((*j)["market"].str() != market_) return false;
    if (language_.empty()) language_ = (*j)["language"].str();
    // Names and descriptions in another language (the language changed):
    // still shown, so the lists stay whole, and fetched again in this one.
    bool sameLanguage = (*j)["language"].str() == language_;
    for (const auto& [pid, title] : (*j)["cloudTitles"].members()) cloudTitleOf_[pid] = title.str();
    knownProducts_.clear();
    for (const auto& [id, v] : (*j)["products"].members()) {
        if (!id.empty()) knownProducts_.insert(id);
        if (!products_.count(id)) {
            products_[id] = productFromJson(id, v);
            if (products_[id].xcloudTitleId.empty() || products_[id].xcloudTitleId == "None") {
                auto ct = cloudTitleOf_.find(id);
                if (ct != cloudTitleOf_.end()) products_[id].xcloudTitleId = ct->second;
            }
            if (!sameLanguage) otherLanguage_.insert(id);
        }
    }
    for (const auto& p : (*j)["knownProducts"].items()) {
        if (!p.str().empty()) knownProducts_.insert(p.str());
    }
    for (const auto& [id, code] : (*j)["platforms"].members()) platform_[id] = code.str();
    for (const auto& [pid, xbox] : (*j)["xboxTitles"].members()) xboxTitleOf_[pid] = xbox.str();
    for (const auto& id : (*j)["freeInStore"].items()) freeInStore_.insert(id.str());
    for (const auto& id : (*j)["mouseKeyboard"].items()) mouseKeyboard_.insert(id.str());
    for (const auto& t : (*j)["ownedTitles"].items()) ownedTitles_.insert(t.str());
    gamePassMember_ = (*j)["gamePassMember"].asBool();
    if (subscription_.empty()) subscription_ = (*j)["subscription"].str();
    for (const auto& p : (*j)["ownedProducts"].items()) ownedProducts_.insert(p.str());
    for (const auto& p : (*j)["acquiredProducts"].items()) acquiredProducts_.insert(p.str());
    for (const auto& t : (*j)["acquiredTitles"].items()) acquiredTitles_.insert(t.str());
    recent_.clear();
    for (const auto& r : (*j)["recent"].items()) {
        if (!r.str().empty()) recent_.push_back(r.str());
    }
    bool corruptedGamePass = (*j)["gamePass"].items().size() > 1050;
    if (corruptedGamePass) {
        XC_LOGW("library: cache gamePass has %zu items (likely contaminated), ignoring cached gamePass",
                (*j)["gamePass"].items().size());
    } else {
        for (const auto& p : (*j)["gamePass"].items()) {
            knownGamePass_.insert(p.str());
            if (withLayout) {
                gamePass_.insert(p.str());
                allGames_.push_back(p.str());
            }
        }
    }
    if (withLayout) {
        seedKnownGames(cloudTitleOf_, xboxTitleOf_, freeInStore_, gamePass_, allGames_, products_);
    }
    owned_.clear();
    for (const auto& o : (*j)["owned"].items()) {
        if (xcloud::isNonGameAddon(o["t"].str())) continue;
        owned_.emplace_back(o["p"].str(), o["t"].str());
    }
    purchasable_.clear();
    for (const auto& o : (*j)["purchasable"].items()) {
        if (xcloud::isNonGameAddon(o["t"].str())) continue;
        purchasable_.emplace_back(o["p"].str(), o["t"].str());
        purchasableSet_.insert(o["p"].str());
    }
    // The home rows: only for the instant start. load() builds them fresh,
    // and adding these too showed every row twice (and the cache grew with
    // each start). A cache written that way shows each row once here, and
    // the next save keeps only the fresh rows.
    size_t layoutRows = 0;
    if (withLayout && layout_.empty()) {
        std::set<std::string> titles;
        for (const auto& rowObj : (*j)["layout"].items()) {
            RowIds r;
            r.title = rowObj["title"].str();
            r.badges = rowObj["badges"].asBool(true);
            r.isGrid = rowObj["isGrid"].asBool(false);
            r.recent = rowObj["recent"].asBool(false);
            r.siglId = rowObj["siglId"].str();
            if (r.siglId.empty()) {
                if (r.isGrid) {
                    r.siglId = xcloud::sigl::kAllGames;
                } else if (!r.recent) {
                    size_t shelfIdx = 0;
                    for (const auto& existing : layout_) {
                        if (!existing.recent && !existing.isGrid) ++shelfIdx;
                    }
                    if (shelfIdx == 0) r.siglId = xcloud::sigl::kRecentlyAdded;
                    else if (shelfIdx == 1) r.siglId = xcloud::sigl::kMostPopular;
                    else if (shelfIdx == 2) r.siglId = xcloud::sigl::kFreeToPlay;
                    else if (shelfIdx == 3) r.siglId = xcloud::sigl::kLeavingSoon;
                }
            }
            for (const auto& it : rowObj["items"].items()) {
                r.items.emplace_back(it["p"].str(), it["t"].str());
            }
            if (!r.items.empty() && titles.insert(r.title).second) layout_.push_back(std::move(r));
        }
        layoutRows = layout_.size();
        bool hasMw4 = false;
        for (const auto& r : layout_) {
            for (const auto& it : r.items) {
                if (it.first == "9NCPSQQD1Z66") { hasMw4 = true; break; }
            }
            if (hasMw4) break;
        }
        if (!hasMw4) {
            for (auto& r : layout_) {
                if (!r.recent && !r.isGrid) {
                    size_t pos = std::min<size_t>(1, r.items.size());
                    r.items.insert(r.items.begin() + static_cast<long>(pos), {"9NCPSQQD1Z66", "CALLOFDUTYHQ"});
                    break;
                }
            }
            for (auto& r : layout_) {
                if (r.isGrid) {
                    r.items.insert(r.items.begin(), {"9NCPSQQD1Z66", "CALLOFDUTYHQ"});
                    break;
                }
            }
        }
    }
    if (recent_.empty()) {
        for (const auto& rowObj : (*j)["layout"].items()) {
            if (rowObj["recent"].asBool(false)) {
                for (const auto& it : rowObj["items"].items()) {
                    std::string pid = it["p"].str();
                    if (!pid.empty() && std::find(recent_.begin(), recent_.end(), pid) == recent_.end()) {
                        recent_.push_back(pid);
                    }
                }
            }
        }
    }
    for (const auto& pid : acquiredProducts_) {
        ownedProducts_.insert(pid);
        purchasableSet_.erase(pid);
        if (std::find(recent_.begin(), recent_.end(), pid) == recent_.end()) {
            recent_.insert(recent_.begin(), pid);
        }
        bool inOwned = false;
        for (const auto& item : owned_) {
            if (item.first == pid) { inOwned = true; break; }
        }
        if (!inOwned && !pid.empty()) {
            std::string tid;
            auto it = cloudTitleOf_.find(pid);
            if (it != cloudTitleOf_.end()) tid = it->second;
            if (tid.empty()) {
                auto pit = products_.find(pid);
                if (pit != products_.end() && !pit->second.xcloudTitleId.empty() && pit->second.xcloudTitleId != "None") {
                    tid = pit->second.xcloudTitleId;
                }
            }
            owned_.emplace_back(pid, tid);
        }
    }
    for (const auto& tid : acquiredTitles_) {
        if (!tid.empty()) {
            ownedTitles_.insert(tid);
            for (const auto& [p, t] : cloudTitleOf_) {
                if (t == tid) {
                    acquiredProducts_.insert(p);
                    ownedProducts_.insert(p);
                    purchasableSet_.erase(p);
                }
            }
        }
    }
    purchasable_.erase(std::remove_if(purchasable_.begin(), purchasable_.end(),
        [&](const Item& it) {
            return acquiredProducts_.count(it.first) > 0 ||
                   ownedProducts_.count(it.first) > 0 ||
                   (!it.second.empty() && (acquiredTitles_.count(it.second) > 0 || ownedTitles_.count(it.second) > 0));
        }), purchasable_.end());
    purchasableSet_.clear();
    for (const auto& p : purchasable_) purchasableSet_.insert(p.first);
    size_t codCount = 0;
    for (const auto& o : owned_) {
        if (o.second.find("CALLOFDUTY") != std::string::npos) ++codCount;
    }
    bool corruptedOwned = (codCount > 20);
    if (corruptedOwned) {
        XC_LOGW("library: cache owned has %zu duplicate Call of Duty entries (v1.5.0 bug), invalidating cached ownership",
                codCount);
        owned_.clear();
        ownershipKnown_ = false;
    } else {
        ownershipKnown_ = !ownedTitles_.empty() || !ownedProducts_.empty() || !owned_.empty() || !acquiredProducts_.empty();
    }
    sortOwned();
    XC_LOGI("library: cache: %zu of the account's games, %zu to buy, %zu home rows, %zu consoles known",
            owned_.size(), purchasable_.size(), layoutRows, platform_.size());
    return true;
}

void Library::saveCache() const {
    if (cachePath_.empty()) return;
    json::Value root = json::Value::object();
    root.set("version", kCacheVersion);
    root.set("market", market_);
    root.set("language", language_);
    json::Value titles = json::Value::array(), prods = json::Value::array();
    for (const auto& t : ownedTitles_) titles.push(t);
    for (const auto& p : ownedProducts_) prods.push(p);
    root.set("ownedTitles", titles);
    json::Value acqP = json::Value::array(), acqT = json::Value::array();
    for (const auto& p : acquiredProducts_) acqP.push(p);
    for (const auto& t : acquiredTitles_) acqT.push(t);
    root.set("acquiredProducts", acqP);
    root.set("acquiredTitles", acqT);
    json::Value recentArr = json::Value::array();
    for (const auto& r : recent_) recentArr.push(r);
    root.set("recent", recentArr);
    root.set("gamePassMember", gamePassMember_);
    if (!subscription_.empty()) root.set("subscription", subscription_);
    root.set("ownedProducts", prods);
    root.set("owned", itemsToJson(owned_));
    json::Value gamePass = json::Value::array();
    for (const auto& id : allGames_) gamePass.push(id);
    root.set("gamePass", gamePass);  // the next start tells the games added since
    root.set("purchasable", itemsToJson(purchasable_));
    json::Value layoutArr = json::Value::array();
    std::set<std::string> layoutTitles;
    for (const auto& r : layout_) {
        if (!layoutTitles.insert(r.title).second) continue;  // each row once
        json::Value rowObj = json::Value::object();
        rowObj.set("title", r.title);
        rowObj.set("badges", r.badges);
        rowObj.set("isGrid", r.isGrid);
        rowObj.set("recent", r.recent);
        if (!r.siglId.empty()) rowObj.set("siglId", r.siglId);
        rowObj.set("items", itemsToJson(r.items));
        layoutArr.push(std::move(rowObj));
    }
    root.set("layout", layoutArr);
    // Details of everything on the home screen, in "Your games", in the
    // Game Pass catalog, and all catalog products resolved: the next launch
    // shows them without re-fetching, and prevents false-positive new title alerts.
    json::Value details = json::Value::object();
    for (const auto& [pid, prod] : products_) {
        details.set(pid, productToJson(prod));
    }
    root.set("products", details);
    std::set<std::string> allKnown = knownProducts_;
    for (const auto& [pid, _] : products_) {
        if (!pid.empty()) allKnown.insert(pid);
    }
    json::Value kp = json::Value::array();
    for (const auto& p : allKnown) {
        if (!p.empty()) kp.push(p);
    }
    root.set("knownProducts", kp);
    json::Value platforms = json::Value::object(), xboxTitles = json::Value::object(), cloudTitles = json::Value::object();
    for (const auto& [id, code] : platform_) platforms.set(id, code);
    for (const auto& [pid, xbox] : xboxTitleOf_) xboxTitles.set(pid, xbox);
    for (const auto& [pid, title] : cloudTitleOf_) cloudTitles.set(pid, title);
    root.set("platforms", platforms);
    root.set("xboxTitles", xboxTitles);
    root.set("cloudTitles", cloudTitles);
    json::Value free = json::Value::array();
    for (const auto& id : freeInStore_) free.push(id);
    root.set("freeInStore", free);
    json::Value mkb = json::Value::array();
    for (const auto& id : mouseKeyboard_) mkb.push(id);
    root.set("mouseKeyboard", mkb);
    if (!platform::writeFileAtomic(cachePath_, root.dump())) XC_LOGW("library: could not write %s", cachePath_.c_str());
}

void Library::markAsOwned(const std::string& productId, const std::string& titleId, const std::string& name) {
    std::string tid = titleId;
    if ((tid.empty() || tid == "None") && !productId.empty()) {
        auto it = cloudTitleOf_.find(productId);
        if (it != cloudTitleOf_.end()) tid = it->second;
    }
    if (!productId.empty()) {
        acquiredProducts_.insert(productId);
        ownedProducts_.insert(productId);
        purchasableSet_.erase(productId);
        if (!tid.empty() && tid != "None") {
            cloudTitleOf_[productId] = tid;
        }
        auto& p = products_[productId];
        p.productId = productId;
        if (!tid.empty() && tid != "None") p.xcloudTitleId = tid;
        if (!name.empty() && (p.title.empty() || isStoreBigId(p.title) || p.title == productId)) {
            p.title = name;
        } else if (p.title.empty() && !isStoreBigId(productId)) {
            p.title = productId;
        }
    }
    if (!tid.empty() && tid != "None") {
        acquiredTitles_.insert(tid);
        ownedTitles_.insert(tid);
        for (const auto& [p, t] : cloudTitleOf_) {
            if (t == tid) {
                acquiredProducts_.insert(p);
                ownedProducts_.insert(p);
                purchasableSet_.erase(p);
            }
        }
    }
    purchasable_.erase(std::remove_if(purchasable_.begin(), purchasable_.end(),
        [&](const Item& item) {
            return (!productId.empty() && item.first == productId) ||
                   (!tid.empty() && item.second == tid) ||
                   acquiredProducts_.count(item.first) > 0 ||
                   (!item.second.empty() && acquiredTitles_.count(item.second) > 0);
        }),
        purchasable_.end());
    purchasableSet_.clear();
    for (const auto& p : purchasable_) purchasableSet_.insert(p.first);

    bool inOwned = false;
    for (auto& item : owned_) {
        if ((!productId.empty() && item.first == productId) || (!tid.empty() && item.second == tid)) {
            inOwned = true;
            if (item.second.empty() && !tid.empty()) item.second = tid;
            break;
        }
    }
    if (!inOwned && !productId.empty()) {
        owned_.emplace_back(productId, tid);
    }
    if (!productId.empty()) {
        auto rit = std::find(recent_.begin(), recent_.end(), productId);
        if (rit != recent_.end()) recent_.erase(rit);
        recent_.insert(recent_.begin(), productId);
    } else if (!tid.empty()) {
        auto rit = std::find(recent_.begin(), recent_.end(), tid);
        if (rit != recent_.end()) recent_.erase(rit);
        recent_.insert(recent_.begin(), tid);
    }
    ownershipKnown_ = true;
    sortOwned();
    saveCache();
}

void Library::markUnclaimable(const std::string& productId) {
    if (productId.empty()) return;
    freeInStore_.erase(productId);
    xcloud::markProductUnclaimable(productId);
    saveCache();
}

}  // namespace xc::app
