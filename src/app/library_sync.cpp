// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/library_internal.h"

#include "platform/platform.h"
#include "ui/strings.h"
#include "util/log.h"
#include "xcloud/purchase.h"
#include "xcloud/social.h"
#include "xcloud/titlehub.h"

#include <algorithm>
#include <ctime>
#include <map>
#include <set>

namespace xc::app {

size_t Library::syncDelta(xcloud::GssvClient& gssv, const Changed& changed, std::string& err, const std::string& language) {
    market_ = gssv.session().market.empty() ? "US" : gssv.session().market;
    if (!language.empty()) language_ = language;
    std::vector<xcloud::Title> titles;
    std::string listErr;
    if (!gssv.listTitles(titles, listErr, false)) {
        err = listErr;
        return 0;
    }
    for (const auto& t : titles) {
        if (!t.productId.empty()) {
            if (!t.titleId.empty() && t.titleId != "None") {
                cloudTitleOf_[t.productId] = t.titleId;
                auto it = products_.find(t.productId);
                if (it != products_.end() && (it->second.xcloudTitleId.empty() || it->second.xcloudTitleId == "None")) {
                    it->second.xcloudTitleId = t.titleId;
                }
            }
            if (!t.xboxTitleId.empty() && !xboxTitleOf_.count(t.productId)) {
                xboxTitleOf_[t.productId] = t.xboxTitleId;
            }
            if (t.isFreeInStore) {
                freeInStore_.insert(t.productId);
            }
            if (t.mouseKeyboard) {
                mouseKeyboard_.insert(t.productId);
            }
        }
    }

    std::vector<std::string> newProducts;
    for (const auto& t : titles) {
        if (!t.productId.empty() && !products_.count(t.productId)) {
            newProducts.push_back(t.productId);
        }
        for (const auto& alt : t.alternateProductIds) {
            if (!alt.empty() && !t.titleId.empty() && t.titleId != "None") {
                cloudTitleOf_[alt] = t.titleId;
            }
        }
    }

    // 1. Fetch All Games catalog feed
    xcloud::ProductList allSigl;
    std::string siglErr;
    if (xcloud::fetchList(xcloud::sigl::kAllGames, market_, language_, allSigl, siglErr)) {
        gamePass_.clear();
        gamePass_.insert(allSigl.productIds.begin(), allSigl.productIds.end());
        allGames_ = allSigl.productIds;
        seedKnownGames(cloudTitleOf_, xboxTitleOf_, freeInStore_, gamePass_, allGames_, products_);
        for (const auto& pid : allSigl.productIds) {
            if (!pid.empty() && !products_.count(pid)) {
                newProducts.push_back(pid);
            }
        }
    }

    // 2. Fetch updated shelf feeds (Recently Added, Most Popular, Free to Play, Leaving Soon)
    const struct ShelfSpec {
        const char* sigl;
        size_t limit;
    } shelfSpecs[] = {
        {xcloud::sigl::kRecentlyAdded, 40},
        {xcloud::sigl::kMostPopular, 40},
        {xcloud::sigl::kFreeToPlay, 40},
        {xcloud::sigl::kLeavingSoon, 40}
    };

    struct FetchedShelf {
        std::string siglId;
        xcloud::ProductList list;
        bool ok = false;
    };
    std::vector<FetchedShelf> fetchedShelves;

    uint8_t myPlan = xcloud::planTier(subscription_);

    for (const auto& spec : shelfSpecs) {
        FetchedShelf fs;
        fs.siglId = spec.sigl;
        std::string e;
        if (xcloud::fetchList(spec.sigl, market_, language_, fs.list, e)) {
            fs.ok = true;
            if (spec.sigl == xcloud::sigl::kRecentlyAdded) {
                const char* const kNewCod[] = {"9NCPSQQD1Z66", "9N201KQXS5BM"};
                for (const char* pid : kNewCod) {
                    auto it = std::find(fs.list.productIds.begin(), fs.list.productIds.end(), pid);
                    if (it != fs.list.productIds.end()) {
                        fs.list.productIds.erase(it);
                        size_t insertAt = std::min<size_t>(1, fs.list.productIds.size());
                        fs.list.productIds.insert(fs.list.productIds.begin() + static_cast<long>(insertAt), pid);
                    }
                }
            } else if (spec.sigl == xcloud::sigl::kMostPopular) {
                popular_ = fs.list.productIds;
                const char* const kTopCod[] = {"9NCPSQQD1Z66", "9N201KQXS5BM", "9NNFG8BQRCXL"};
                for (const char* pid : kTopCod) {
                    auto it = std::find(fs.list.productIds.begin(), fs.list.productIds.end(), pid);
                    if (it != fs.list.productIds.end()) {
                        fs.list.productIds.erase(it);
                        size_t insertAt = std::min<size_t>(2, fs.list.productIds.size());
                        fs.list.productIds.insert(fs.list.productIds.begin() + static_cast<long>(insertAt), pid);
                    }
                }
            }
            bool freeToPlay = spec.sigl == xcloud::sigl::kFreeToPlay;
            if (freeToPlay) {
                freeInStore_.insert(fs.list.productIds.begin(), fs.list.productIds.end());
            }
            bool isMostPopular = spec.sigl == xcloud::sigl::kMostPopular;
            if (!gamePass_.empty() && !freeToPlay && !isMostPopular) {
                fs.list.productIds.erase(std::remove_if(fs.list.productIds.begin(), fs.list.productIds.end(),
                                                        [&](const std::string& id) {
                                                            if (!gamePass_.count(id)) return true;
                                                            if (myPlan > 0 && myPlan < xcloud::kPlanUltimate &&
                                                                !ownedProducts_.count(id) && !freeInStore_.count(id)) {
                                                                auto it = products_.find(id);
                                                                if (it != products_.end() &&
                                                                    !xcloud::planIncludes(myPlan, it->second.plan, false))
                                                                    return true;
                                                            }
                                                            return false;
                                                        }),
                                         fs.list.productIds.end());
            }
            fs.list.productIds.erase(std::remove_if(fs.list.productIds.begin(), fs.list.productIds.end(),
                                                    [&](const std::string& id) {
                                                        return isPcOnly(id);
                                                    }),
                                     fs.list.productIds.end());
            if (spec.limit > 0 && fs.list.productIds.size() > spec.limit) {
                fs.list.productIds.resize(spec.limit);
            }
            for (const auto& pid : fs.list.productIds) {
                if (!pid.empty() && !products_.count(pid)) {
                    newProducts.push_back(pid);
                }
            }
        }
        fetchedShelves.push_back(std::move(fs));
    }

    std::sort(newProducts.begin(), newProducts.end());
    newProducts.erase(std::unique(newProducts.begin(), newProducts.end()), newProducts.end());

    size_t newCount = 0;
    if (!newProducts.empty()) {
        std::string fetchErr;
        xcloud::fetchProducts(newProducts, market_, language_, products_, fetchErr, false);

        std::vector<std::string> actuallyAdded;
        for (const auto& pid : newProducts) {
            auto it = products_.find(pid);
            if (it != products_.end() && !it->second.title.empty()) {
                bool isCatalogAddition = !gamePass_.empty() && gamePass_.count(pid);
                if (isCatalogAddition && !knownGamePass_.count(pid) && !knownProducts_.count(pid)) {
                    actuallyAdded.push_back(pid);
                }
            } else {
                products_[pid].productId = pid;
                products_[pid].detailedAt = platform::nowMs();
            }
        }
        newCount = actuallyAdded.size();
    }

    allGames_.erase(std::remove_if(allGames_.begin(), allGames_.end(),
                                   [&](const std::string& id) {
                                       return isPcOnly(id);
                                   }),
                    allGames_.end());
    for (auto it = gamePass_.begin(); it != gamePass_.end(); ) {
        if (isPcOnly(*it)) it = gamePass_.erase(it);
        else ++it;
    }

    // Refresh shelf rows in layout_
    for (const auto& fs : fetchedShelves) {
        if (!fs.ok) continue;
        bool found = false;
        for (auto& row : layout_) {
            if (row.isGrid || row.recent) continue;
            if ((!row.siglId.empty() && row.siglId == fs.siglId) ||
                (row.siglId.empty() && row.title == fs.list.title)) {
                row.siglId = fs.siglId;
                if (row.title.empty() && !fs.list.title.empty()) row.title = fs.list.title;
                row.items.clear();
                for (const auto& pid : fs.list.productIds) {
                    std::string tid = cloudTitleOf_.count(pid) ? cloudTitleOf_[pid] : "";
                    row.items.emplace_back(pid, tid);
                }
                found = true;
                break;
            }
        }
        if (!found) {
            size_t shelfIdx = 0;
            if (fs.siglId == xcloud::sigl::kRecentlyAdded) shelfIdx = 0;
            else if (fs.siglId == xcloud::sigl::kMostPopular) shelfIdx = 1;
            else if (fs.siglId == xcloud::sigl::kFreeToPlay) shelfIdx = 2;
            else if (fs.siglId == xcloud::sigl::kLeavingSoon) shelfIdx = 3;

            size_t curShelf = 0;
            for (auto& row : layout_) {
                if (row.isGrid || row.recent) continue;
                if (curShelf == shelfIdx) {
                    row.siglId = fs.siglId;
                    if (row.title.empty() && !fs.list.title.empty()) row.title = fs.list.title;
                    row.items.clear();
                    for (const auto& pid : fs.list.productIds) {
                        std::string tid = cloudTitleOf_.count(pid) ? cloudTitleOf_[pid] : "";
                        row.items.emplace_back(pid, tid);
                    }
                    found = true;
                    break;
                }
                ++curShelf;
            }
        }
    }

    for (auto& row : layout_) {
        if (row.isGrid) {
            row.siglId = xcloud::sigl::kAllGames;
            row.items.clear();
            for (const auto& pid : allGames_) {
                std::string tid = cloudTitleOf_.count(pid) ? cloudTitleOf_[pid] : "";
                row.items.emplace_back(pid, tid);
            }
        } else {
            row.items.erase(std::remove_if(row.items.begin(), row.items.end(),
                                           [&](const auto& item) {
                                               return isPcOnly(item.first);
                                           }),
                            row.items.end());
        }
    }

    for (const auto& pid : allGames_) knownGamePass_.insert(pid);
    for (const auto& [pid, _] : products_) knownProducts_.insert(pid);

    loadOwned(gssv, changed, nullptr, &titles);
    loadCatalogNames(changed, nullptr);
    saveCache();
    changed();
    return newCount;
}

bool Library::load(xcloud::GssvClient& gssv, const std::string& language, const Changed& changed, std::string& err,
                   bool forceRefresh) {
    market_ = gssv.session().market.empty() ? "US" : gssv.session().market;
    language_ = language;
    layout_.clear();
    products_.clear();
    allGames_.clear();
    gamePass_.clear();
    popular_.clear();
    recent_.clear();
    ownershipKnown_ = false;
    ownershipFresh_ = false;
    knownGamePass_.clear();
    knownProducts_.clear();
    ownedTitles_.clear();
    ownedProducts_.clear();
    owned_.clear();
    purchasable_.clear();
    purchasableSet_.clear();
    freeInStore_.clear();
    otherLanguage_.clear();
    progress_ = {};
    std::string lastErr;

    // The Game Pass catalog first: it decides what the other lists may show.
    xcloud::ProductList all;
    std::string e;
    if (xcloud::fetchList(xcloud::sigl::kAllGames, market_, language_, all, e)) {
        allGames_ = all.productIds;
        gamePass_.insert(all.productIds.begin(), all.productIds.end());
    } else {
        XC_LOGW("%s", e.c_str());
        lastErr = e;
    }
    seedKnownGames(cloudTitleOf_, xboxTitleOf_, freeInStore_, gamePass_, allGames_, products_);
    // The account's games from the previous run, until loadOwned() refreshes
    // them. Not its home rows: those are built below.
    loadCache(false);

    // "Jump back in": recently played games, Game Pass or the account's own
    // (rows() leaves out those it can't play; they also go first in "Your
    // games").
    std::vector<xcloud::Title> recent;
    if (gssv.listTitles(recent, lastErr, true)) {
        RowIds r{ui::tr(ui::Str::JumpBackIn), false, false, {}, true};
        std::vector<std::string> ids;
        std::vector<std::string> freshRecent;
        for (const auto& t : recent) {
            if (t.productId.empty()) continue;
            if (std::find(freshRecent.begin(), freshRecent.end(), t.productId) == freshRecent.end()) {
                freshRecent.push_back(t.productId);
            }
            if (!t.xboxTitleId.empty()) xboxTitleOf_[t.productId] = t.xboxTitleId;
            // Its inputs come with it: the first cards show keyboard and
            // mouse without waiting for the whole list (or a cache that has it).
            if (t.mouseKeyboard) mouseKeyboard_.insert(t.productId);
            r.items.emplace_back(t.productId, t.titleId);
            if (!products_.count(t.productId)) ids.push_back(t.productId);
        }
        for (const auto& pid : recent_) {
            if (!pid.empty() && std::find(freshRecent.begin(), freshRecent.end(), pid) == freshRecent.end()) {
                freshRecent.push_back(pid);
            }
        }
        recent_ = std::move(freshRecent);
        for (const auto& pid : acquiredProducts_) {
            auto it = std::find(recent_.begin(), recent_.end(), pid);
            if (it != recent_.end()) recent_.erase(it);
            recent_.insert(recent_.begin(), pid);
        }
        sortOwned();
        // The first cards are the first thing seen: full details (hero art,
        // description) for them in this same round trip.
        constexpr size_t kFirst = 8;
        std::vector<std::string> first, rest;
        for (const auto& item : r.items) {
            auto it = products_.find(item.first);
            bool complete = it != products_.end() && !it->second.heroUrl.empty();
            if (complete) continue;
            (first.size() < kFirst ? first : rest).push_back(item.first);
        }
        bool ok = xcloud::fetchProducts(first, market_, language_, products_, lastErr, true) &&
                  xcloud::fetchProducts(rest, market_, language_, products_, lastErr, false);
        if (ok) {
            layout_.push_back(std::move(r));
            changed();
        }
    }

    const ListSpec lists[] = {{xcloud::sigl::kRecentlyAdded, 40},
                              {xcloud::sigl::kMostPopular, 40},
                              {xcloud::sigl::kFreeToPlay, 40},
                              {xcloud::sigl::kLeavingSoon, 40},
                              {xcloud::sigl::kAllGames, 0}};
    uint8_t myPlan = xcloud::planTier(subscription_);
    for (const auto& spec : lists) {
        xcloud::ProductList list;
        bool isAllGames = std::string_view(spec.sigl) == xcloud::sigl::kAllGames;
        if (isAllGames && !allGames_.empty()) {
            list.title = all.title.empty() ? "All games" : all.title;
            list.productIds = allGames_;
        } else if (!xcloud::fetchList(spec.sigl, market_, language_, list, e)) {
            XC_LOGW("%s", e.c_str());
            lastErr = e;
            continue;
        }
        if (spec.sigl == xcloud::sigl::kRecentlyAdded) {
            const char* const kNewCod[] = {"9NCPSQQD1Z66", "9N201KQXS5BM"};
            for (const char* pid : kNewCod) {
                auto it = std::find(list.productIds.begin(), list.productIds.end(), pid);
                if (it != list.productIds.end()) {
                    list.productIds.erase(it);
                    size_t insertAt = std::min<size_t>(1, list.productIds.size());
                    list.productIds.insert(list.productIds.begin() + static_cast<long>(insertAt), pid);
                }
            }
        }
        // "Most popular on cloud" also lists games to buy first (Cuphead...):
        // those go to "Your games" (in this order), not to the Game Pass tab.
        if (spec.sigl == xcloud::sigl::kMostPopular) {
            popular_ = list.productIds;
            // Position key cloud titles (Modern Warfare 4, Call of Duty HQ, Warzone)
            // within the visible most popular shelf if legitimately in the feed
            const char* const kTopCod[] = {"9NCPSQQD1Z66", "9N201KQXS5BM", "9NNFG8BQRCXL"};
            for (const char* pid : kTopCod) {
                auto it = std::find(list.productIds.begin(), list.productIds.end(), pid);
                if (it != list.productIds.end()) {
                    list.productIds.erase(it);
                    size_t insertAt = std::min<size_t>(2, list.productIds.size());
                    list.productIds.insert(list.productIds.begin() + static_cast<long>(insertAt), pid);
                }
            }
        }
        // Free-to-play: no Game Pass needed, so not filtered; each one the
        // account hasn't got yet shows as free to get (tile()).
        bool freeToPlay = spec.sigl == xcloud::sigl::kFreeToPlay;
        if (freeToPlay) {
            freeInStore_.insert(list.productIds.begin(), list.productIds.end());
            XC_LOGI("library: %s: %zu games", list.title.c_str(), list.productIds.size());
        }
        bool isMostPopular = spec.sigl == xcloud::sigl::kMostPopular;
        if (!gamePass_.empty() && !freeToPlay && !isMostPopular) {
            size_t before = list.productIds.size();
            list.productIds.erase(std::remove_if(list.productIds.begin(), list.productIds.end(),
                                                 [&](const std::string& id) {
                                                     if (!gamePass_.count(id)) return true;
                                                     if (myPlan > 0 && myPlan < xcloud::kPlanUltimate && !ownedProducts_.count(id) &&
                                                         !freeInStore_.count(id)) {
                                                         auto it = products_.find(id);
                                                         if (it != products_.end() &&
                                                             !xcloud::planIncludes(myPlan, it->second.plan, false))
                                                             return true;
                                                     }
                                                     return false;
                                                 }),
                                  list.productIds.end());
            if (list.productIds.size() != before)
                XC_LOGI("library: %s: %zu of %zu not in Game Pass/plan, left out", list.title.c_str(),
                        before - list.productIds.size(), before);
        }
        list.productIds.erase(std::remove_if(list.productIds.begin(), list.productIds.end(),
                                             [&](const std::string& id) {
                                                 return isPcOnly(id);
                                             }),
                              list.productIds.end());
        if (spec.limit > 0 && list.productIds.size() > spec.limit) list.productIds.resize(spec.limit);
        std::vector<std::string> missing;
        for (const auto& id : list.productIds) {
            auto it = products_.find(id);
            if (it == products_.end() || it->second.tileUrl.empty() || otherLanguage_.count(id))
                missing.push_back(id);
        }
        if (!missing.empty() && !xcloud::fetchProducts(missing, market_, language_, products_, e, false)) {
            XC_LOGW("%s", e.c_str());
            lastErr = e;
        }
        RowIds r{list.title, !freeToPlay, isAllGames, {}};
        r.siglId = spec.sigl;
        for (const auto& id : list.productIds) r.items.emplace_back(id, std::string());
        layout_.push_back(std::move(r));
        changed();
    }
    if (rows().empty()) {
        err = lastErr.empty() ? "no games found" : lastErr;
        return false;
    }
    for (const auto& pid : allGames_) knownGamePass_.insert(pid);
    for (const auto& [pid, _] : products_) knownProducts_.insert(pid);
    saveCache();
    return true;
}

void Library::loadOwned(xcloud::GssvClient gssv, const Changed& changed, const std::atomic<bool>* stop,
                        const std::vector<xcloud::Title>* preloadedTitles) {
    progress_ = {true, 0, 0};  // the account's list: no measure until the names
    changed();
    std::string err;
    std::vector<xcloud::Title> fetchedTitles;
    const std::vector<xcloud::Title>* titlesPtr = preloadedTitles;
    if (!titlesPtr) {
        if (!gssv.listTitles(fetchedTitles, err, false)) {
            XC_LOGW("%s", err.c_str());  // keep what the cache said
            progress_ = {};
            changed();
            return;
        }
        titlesPtr = &fetchedTitles;
    }
    const auto& titles = *titlesPtr;
    if (stop && *stop) {
        progress_ = {};
        changed();
        return;
    }
    std::set<std::string> ownedTitles, ownedProducts;
    std::vector<Item> mine;
    std::set<std::string> seenMine;
    std::map<std::string, std::string> toBuy;  // productId -> titleId
    bool member = false;
    auto registerCloudTitle = [&](const std::string& pid, const std::string& tid, const std::string& xboxId, bool freeStore, bool mkb) {
        if (pid.empty()) return;
        if (!tid.empty() && tid != "None") {
            cloudTitleOf_[pid] = tid;
            auto it = products_.find(pid);
            if (it != products_.end() && (it->second.xcloudTitleId.empty() || it->second.xcloudTitleId == "None")) {
                it->second.xcloudTitleId = tid;
            }
        }
        if (!xboxId.empty() && !xboxTitleOf_.count(pid)) xboxTitleOf_[pid] = xboxId;
        if (freeStore) freeInStore_.insert(pid);
        if (mkb) mouseKeyboard_.insert(pid);
    };

    for (const auto& t : titles) {
        bool isAcquired = (!t.productId.empty() && acquiredProducts_.count(t.productId)) ||
                          (!t.titleId.empty() && acquiredTitles_.count(t.titleId));
        for (const auto& alt : t.alternateProductIds) {
            if (!alt.empty() && acquiredProducts_.count(alt)) { isAcquired = true; break; }
        }
        bool entitled = t.hasEntitlement || isAcquired;
        member |= entitled && t.viaSubscription;
        if (xcloud::isNonGameAddon(t.titleId)) continue;
        if (entitled) ownedTitles.insert(t.titleId);

        registerCloudTitle(t.productId, t.titleId, t.xboxTitleId, t.isFreeInStore, t.mouseKeyboard);
        for (const auto& alt : t.alternateProductIds) {
            registerCloudTitle(alt, t.titleId, t.xboxTitleId, t.isFreeInStore, t.mouseKeyboard);
        }

        if (entitled) {
            if (!t.productId.empty()) ownedProducts.insert(t.productId);
            for (const auto& alt : t.alternateProductIds) {
                if (!alt.empty()) ownedProducts.insert(alt);
            }
            // Game Pass games the account owns itself (free-to-play ones
            // like Fortnite, without the subscription) are its games too.
            // Also games outside the Game Pass catalog.
            if (!t.productId.empty()) {
                if (!gamePass_.count(t.productId) || !t.viaSubscription || isAcquired) {
                    if (seenMine.insert(t.productId).second) {
                        mine.emplace_back(t.productId, t.titleId);
                    }
                }
            }
        } else if (!t.productId.empty() && !gamePass_.count(t.productId)) {
            toBuy[t.productId] = t.titleId;
        }
    }

    bool hasCod = member || ownedTitles.count("CALLOFDUTYHQ") || ownedTitles.count("CALLOFDUTYBLACKOPS6") ||
                  ownedTitles.count("CALLOFDUTYMODERNWARFAREIII");
    if (hasCod) {
        ownedTitles.insert("CALLOFDUTYHQ");
        ownedTitles.insert("CALLOFDUTYBLACKOPS6");
        ownedTitles.insert("CALLOFDUTYMODERNWARFAREIII");
    }
    for (const auto& g : kKnownCloudGames) {
        registerCloudTitle(g.productId, g.cloudTitleId, g.xboxTitleId ? g.xboxTitleId : "", g.isFree, false);
        if (ownedTitles.count(g.cloudTitleId)) {
            ownedProducts.insert(g.productId);
            if (g.isFree || std::string_view(g.productId) == "9NCPSQQD1Z66") {
                if (seenMine.insert(g.productId).second) {
                    mine.emplace_back(g.productId, g.cloudTitleId);
                }
            }
        } else if (!gamePass_.count(g.productId)) {
            toBuy[g.productId] = g.cloudTitleId;
        }
    }
    for (const auto& pid : acquiredProducts_) {
        toBuy.erase(pid);
        ownedProducts.insert(pid);
        if (seenMine.insert(pid).second) {
            std::string tid;
            auto it = cloudTitleOf_.find(pid);
            if (it != cloudTitleOf_.end()) tid = it->second;
            if (tid.empty()) {
                auto pit = products_.find(pid);
                if (pit != products_.end() && !pit->second.xcloudTitleId.empty() && pit->second.xcloudTitleId != "None") {
                    tid = pit->second.xcloudTitleId;
                }
            }
            mine.emplace_back(pid, tid);
        }
    }
    for (const auto& tid : acquiredTitles_) {
        if (!tid.empty()) {
            ownedTitles.insert(tid);
            for (const auto& [p, t] : cloudTitleOf_) {
                if (t == tid) {
                    ownedProducts.insert(p);
                    toBuy.erase(p);
                }
            }
        }
    }
    for (auto it = toBuy.begin(); it != toBuy.end(); ) {
        bool isOwnedOrAcquired = acquiredProducts_.count(it->first) > 0 ||
                                 ownedProducts.count(it->first) > 0 ||
                                 (!it->second.empty() && (acquiredTitles_.count(it->second) > 0 || ownedTitles.count(it->second) > 0));
        if (isOwnedOrAcquired) {
            it = toBuy.erase(it);
        } else {
            ++it;
        }
    }
    // Games to buy: the most popular in the cloud first, then the rest.
    std::vector<Item> buy;
    for (const auto& id : popular_) {
        auto it = toBuy.find(id);
        if (it == toBuy.end()) continue;
        buy.emplace_back(it->first, it->second);
        toBuy.erase(it);
    }
    for (const auto& [pid, tid] : toBuy) buy.emplace_back(pid, tid);

    std::vector<std::string> missing;
    for (const auto& m : mine)
        if (!products_.count(m.first)) missing.push_back(m.first);
    for (const auto& [pid, _] : toBuy)
        if (!products_.count(pid)) missing.push_back(pid);
    if (!missing.empty() && !xcloud::fetchProducts(missing, market_, language_, products_, err, false))
        XC_LOGW("%s", err.c_str());
    if (stop && *stop) {
        progress_ = {};
        changed();
        return;
    }
    ownedTitles_ = std::move(ownedTitles);
    ownershipFresh_ = true;
    ownedProducts_ = std::move(ownedProducts);
    owned_ = std::move(mine);
    purchasable_ = std::move(buy);
    purchasable_.erase(std::remove_if(purchasable_.begin(), purchasable_.end(),
        [&](const Item& item) {
            return acquiredProducts_.count(item.first) > 0 ||
                   ownedProducts_.count(item.first) > 0 ||
                   (!item.second.empty() && (acquiredTitles_.count(item.second) > 0 || ownedTitles_.count(item.second) > 0));
        }), purchasable_.end());
    purchasableSet_.clear();
    for (const auto& p : purchasable_) purchasableSet_.insert(p.first);
    for (const auto& pid : acquiredProducts_) {
        purchasableSet_.erase(pid);
    }
    for (const auto& [pid, tid] : cloudTitleOf_) {
        if (acquiredTitles_.count(tid) || ownedTitles_.count(tid)) {
            purchasableSet_.erase(pid);
        }
    }
    for (const auto& pid : acquiredProducts_) {
        if (!pid.empty() && std::find(recent_.begin(), recent_.end(), pid) == recent_.end()) {
            recent_.insert(recent_.begin(), pid);
        }
    }
    ownershipKnown_ = true;
    gamePassMember_ = member;
    sortOwned();
    saveCache();
    XC_LOGI("library: account can stream %zu titles; %zu outside Game Pass; %zu more to buy", ownedTitles_.size(),
            owned_.size(), purchasable_.size());
    progress_ = {};
    changed();
}

void Library::loadCatalogNames(const Changed& changed, const std::atomic<bool>* stop) {
    // Names and art: the account's games and the games to buy first (their
    // lists are on screen), then the Game Pass catalog for its search. Also
    // anything still in another language.
    std::vector<std::string> missing;
    std::set<std::string> queued;
    auto want = [&](const std::string& id) {
        auto it = products_.find(id);
        bool needs = (it == products_.end()) ||
                     it->second.title.empty() ||
                     it->second.tileUrl.empty() ||
                     (it->second.xcloudTitleId.empty() || it->second.xcloudTitleId == "None") ||
                     otherLanguage_.count(id);
        if (needs && queued.insert(id).second) missing.push_back(id);
    };
    for (const auto* list : {&owned_, &purchasable_})
        for (const auto& item : *list) want(item.first);
    for (const auto& id : allGames_) want(id);
    progress_ = {true, 0, missing.size()};
    changed();
    constexpr size_t kBatch = 100;
    for (size_t i = 0; i < missing.size(); i += kBatch) {
        if (stop && *stop) break;
        std::vector<std::string> batch(missing.begin() + static_cast<long>(i),
                                       missing.begin() + static_cast<long>(std::min(missing.size(), i + kBatch)));
        std::map<std::string, xcloud::Product> fetched;
        std::string e;
        if (!xcloud::fetchProducts(batch, market_, language_, fetched, e, false)) {
            XC_LOGW("%s", e.c_str());
            break;
        }
        for (auto& [id, p] : fetched) {
            auto ct = cloudTitleOf_.find(id);
            if (p.xcloudTitleId.empty() || p.xcloudTitleId == "None") {
                if (ct != cloudTitleOf_.end()) p.xcloudTitleId = ct->second;
            }
            if (p.xcloudTitleId == "None") p.xcloudTitleId.clear();
            // Keep the hero art and description already known, until they
            // come in the new language (hydrate()).
            auto old = products_.find(id);
            if (old != products_.end()) {
                if (p.heroUrl.empty()) p.heroUrl = old->second.heroUrl;
                if (p.description.empty() && !otherLanguage_.count(id)) p.description = old->second.description;
                if (p.xcloudTitleId.empty()) p.xcloudTitleId = old->second.xcloudTitleId;
                if (!p.detailed && old->second.detailed) {  // modes, languages and plan don't change with the language
                    p.detailed = true;
                    p.detailedAt = old->second.detailedAt;
                    p.modes = old->second.modes;
                    p.plan = old->second.plan;
                    p.languages = old->second.languages;
                }
            }
            products_[id] = std::move(p);
            otherLanguage_.erase(id);
        }
        progress_.done = std::min(missing.size(), i + kBatch);
        changed();  // each batch: the lists fill in as names arrive
    }
    progress_ = {};
    changed();
    allGames_.erase(std::remove_if(allGames_.begin(), allGames_.end(),
                                   [&](const std::string& id) {
                                       return isPcOnly(id);
                                   }),
                    allGames_.end());
    for (auto it = gamePass_.begin(); it != gamePass_.end(); ) {
        if (isPcOnly(*it)) it = gamePass_.erase(it);
        else ++it;
    }
    for (auto& row : layout_) {
        if (row.isGrid) {
            row.items.clear();
            for (const auto& pid : allGames_) {
                std::string tid = cloudTitleOf_.count(pid) ? cloudTitleOf_[pid] : "";
                row.items.emplace_back(pid, tid);
            }
        } else {
            row.items.erase(std::remove_if(row.items.begin(), row.items.end(),
                                           [&](const auto& item) {
                                               return isPcOnly(item.first);
                                           }),
                            row.items.end());
        }
    }
    saveCache();
    XC_LOGI("library: search covers %zu Game Pass games and %zu of yours or to buy", gamePassSearchPool().size(),
            librarySearchPool().size());
    changed();
}

bool Library::fetchFull(const std::vector<std::string>& ids, const Changed& changed, const std::atomic<bool>* stop,
                        size_t firstBatch) {
    bool ok = true;
    int batches = 0;
    std::vector<std::string> order;
    std::set<std::string> queued;
    for (const auto& id : ids) {
        auto it = products_.find(id);
        bool needFull = (it == products_.end() || !it->second.detailed || it->second.description.empty());
        if (needFull && queued.insert(id).second)
            order.push_back(id);
    }
    auto mergeProduct = [&](const std::string& id, xcloud::Product&& p) {
        auto ct = cloudTitleOf_.find(id);
        if (p.xcloudTitleId.empty() || p.xcloudTitleId == "None") {
            if (ct != cloudTitleOf_.end()) p.xcloudTitleId = ct->second;
        }
        if (p.xcloudTitleId == "None") p.xcloudTitleId.clear();
        auto oldIt = products_.find(id);
        if (oldIt != products_.end()) {
            const auto& old = oldIt->second;
            if (p.plan == 0) p.plan = old.plan;
            if (p.xcloudTitleId.empty()) p.xcloudTitleId = old.xcloudTitleId;
            if (p.xboxTitleId.empty()) p.xboxTitleId = old.xboxTitleId;
            if (p.title.empty()) p.title = old.title;
            if (p.publisher.empty()) p.publisher = old.publisher;
            if (p.tileUrl.empty()) p.tileUrl = old.tileUrl;
            if (p.posterUrl.empty()) p.posterUrl = old.posterUrl;
            if (p.heroUrl.empty()) p.heroUrl = old.heroUrl;
            if (p.description.empty()) p.description = old.description;
            if (p.categories.empty()) p.categories = old.categories;
            if (p.modes == 0) p.modes = old.modes;
            if (p.languages.empty()) p.languages = old.languages;
            if (p.ratingAge.empty()) p.ratingAge = old.ratingAge;
            if (p.ratingSystem.empty()) p.ratingSystem = old.ratingSystem;
            if (p.ratingDescriptors.empty()) p.ratingDescriptors = old.ratingDescriptors;
        }
        products_[id] = std::move(p);
    };
    // Batches of 20 (~10 KB per product: bigger ones made the catalog time
    // out, 504, and weren't faster).
    for (size_t i = 0, n = firstBatch; i < order.size(); i += n, n = 20) {
        if (stop && *stop) return true;
        std::vector<std::string> batch(order.begin() + static_cast<long>(i),
                                       order.begin() + static_cast<long>(std::min(order.size(), i + n)));
        std::map<std::string, xcloud::Product> full;
        std::string e;
        // A batch that fails (a timeout now and then) is asked again; one
        // that keeps failing is left for next time, and the rest carries on.
        bool got = false;
        for (int attempt = 0; attempt < 3 && !got; ++attempt) {
            if (attempt) platform::sleepMs(1000);
            got = xcloud::fetchProducts(batch, market_, language_, full, e, true);
            if (!got) XC_LOGW("%s%s", e.c_str(), attempt < 2 ? " (asking again)" : " (skipped)");
        }
        if (!got) {
            ok = false;
            continue;
        }
        for (const auto& id : batch) {
            // Not in the catalog's details: nothing more to know, not asked again.
            auto known = products_.find(id);
            if (!full.count(id) && known != products_.end()) known->second.detailed = true;
        }
        for (auto& [id, p] : full) mergeProduct(id, std::move(p));
        changed();
        if (++batches % 4 == 0) saveCache();  // a long fetch keeps what it got if the app closes
    }
    return ok;
}

void Library::fetchPlatformsFor(const std::vector<std::string>& ids, const std::string& xblAuth) {
    if (xblAuth.empty()) return;
    std::set<std::string> want;
    for (const auto& pid : ids) {
        auto x = xboxTitleOf_.find(pid);
        std::string xbox = x != xboxTitleOf_.end() ? x->second : std::string();
        if (xbox.empty()) {
            auto p = products_.find(pid);
            if (p != products_.end()) xbox = p->second.xboxTitleId;
        }
        if (!xbox.empty() && !platform_.count(xbox)) want.insert(xbox);
    }
    if (want.empty()) return;
    std::vector<std::string> list(want.begin(), want.end());
    std::string err;
    size_t before = platform_.size();
    if (!xcloud::fetchPlatforms(xblAuth, list, platform_, err)) XC_LOGW("%s", err.c_str());
    XC_LOGI("library: consoles of %zu more games", platform_.size() - before);
}

void Library::loadFirstScreen(const std::string& xblAuth, const Changed& changed, const std::atomic<bool>* stop) {
    uint64_t t0 = platform::nowMs();
    // What is on screen at once: the first cards of each row (the focused
    // first row first), then the start of "Your games".
    constexpr size_t kPerRow = 8, kOwned = 12;
    std::vector<std::string> ids;
    for (const auto& r : layout_)
        for (size_t i = 0; i < r.items.size() && i < kPerRow; ++i) ids.push_back(r.items[i].first);
    for (size_t i = 0; i < owned_.size() && i < kOwned; ++i) ids.push_back(owned_[i].first);
    for (size_t i = 0; i < purchasable_.size() && i < kOwned; ++i) ids.push_back(purchasable_[i].first);
    // Badges for every card of the rows (one titlehub call), then the art.
    std::vector<std::string> badges = ids;
    for (const auto& r : layout_)
        for (const auto& item : r.items) badges.push_back(item.first);
    std::vector<std::string> firstRows(ids.begin(), ids.begin() + static_cast<long>(std::min<size_t>(ids.size(), 20)));
    fetchFull(firstRows, changed, stop);
    fetchPlatformsFor(badges, xblAuth);
    changed();
    fetchFull(ids, changed, stop);
    saveCache();
    XC_LOGI("library: first screen ready in %llu ms", static_cast<unsigned long long>(platform::nowMs() - t0));
}

void Library::loadPlatforms(const std::string& xblAuth, const Changed& changed, const std::atomic<bool>* stop) {
    if (stop && *stop) return;
    std::vector<std::string> ids;
    for (const auto& r : layout_)
        for (const auto& item : r.items) ids.push_back(item.first);
    for (const auto* list : {&owned_, &purchasable_})
        for (const auto& item : *list) ids.push_back(item.first);
    ids.insert(ids.end(), allGames_.begin(), allGames_.end());
    fetchPlatformsFor(ids, xblAuth);
    saveCache();
    changed();
}

void Library::hydrate(const Changed& changed, const std::atomic<bool>* stop) {
    // On-screen order, so what is shown first gets its art first: the rows,
    // then the account's games.
    std::vector<std::string> ids;
    for (const auto& r : layout_)
        for (const auto& item : r.items) ids.push_back(item.first);
    for (const auto& item : owned_) ids.push_back(item.first);
    // Then every game the search goes through: its filters (play mode,
    // language) need the full details.
    for (const auto& item : purchasable_) ids.push_back(item.first);
    ids.insert(ids.end(), allGames_.begin(), allGames_.end());
    uint64_t t0 = platform::nowMs();
    fetchFull(ids, changed, stop);
    // The store changes descriptions and details now and then: the oldest
    // few (over 30 days) are fetched again on each start, never all at once.
    constexpr int64_t kRefreshAge = 30 * 24 * 3600;
    constexpr size_t kRefreshPerStart = 40;
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    std::vector<std::pair<int64_t, std::string>> old;
    for (const auto& [id, p] : products_)
        if (p.detailed && p.detailedAt && now - p.detailedAt > kRefreshAge) old.push_back({p.detailedAt, id});
    std::sort(old.begin(), old.end());
    if (old.size() > kRefreshPerStart) old.resize(kRefreshPerStart);
    if (!old.empty() && !(stop && *stop)) {
        std::vector<std::string> refresh;
        for (const auto& [at, id] : old) refresh.push_back(id);
        size_t done = 0;
        for (size_t i = 0; i < refresh.size(); i += 20) {
            std::vector<std::string> batch(refresh.begin() + static_cast<long>(i),
                                           refresh.begin() + static_cast<long>(std::min(refresh.size(), i + 20)));
            std::map<std::string, xcloud::Product> fresh;
            std::string e;
            if (!xcloud::fetchProducts(batch, market_, language_, fresh, e, true)) {
                XC_LOGW("%s (refresh: next start)", e.c_str());
                break;
            }
            for (const auto& id : batch) {
                auto it = fresh.find(id);
                if (it != fresh.end()) {
                    auto& p = it->second;
                    auto ct = cloudTitleOf_.find(id);
                    if (p.xcloudTitleId.empty() || p.xcloudTitleId == "None") {
                        if (ct != cloudTitleOf_.end()) p.xcloudTitleId = ct->second;
                    }
                    if (p.xcloudTitleId == "None") p.xcloudTitleId.clear();
                    auto oldIt = products_.find(id);
                    if (oldIt != products_.end()) {
                        const auto& prev = oldIt->second;
                        if (p.plan == 0) p.plan = prev.plan;
                        if (p.xcloudTitleId.empty()) p.xcloudTitleId = prev.xcloudTitleId;
                        if (p.xboxTitleId.empty()) p.xboxTitleId = prev.xboxTitleId;
                        if (p.title.empty()) p.title = prev.title;
                        if (p.publisher.empty()) p.publisher = prev.publisher;
                        if (p.tileUrl.empty()) p.tileUrl = prev.tileUrl;
                        if (p.posterUrl.empty()) p.posterUrl = prev.posterUrl;
                        if (p.heroUrl.empty()) p.heroUrl = prev.heroUrl;
                        if (p.description.empty()) p.description = prev.description;
                        if (p.categories.empty()) p.categories = prev.categories;
                        if (p.modes == 0) p.modes = prev.modes;
                        if (p.languages.empty()) p.languages = prev.languages;
                        if (p.ratingAge.empty()) p.ratingAge = prev.ratingAge;
                        if (p.ratingSystem.empty()) p.ratingSystem = prev.ratingSystem;
                        if (p.ratingDescriptors.empty()) p.ratingDescriptors = prev.ratingDescriptors;
                    }
                    products_[id] = std::move(p);
                    ++done;
                } else {
                    products_[id].detailedAt = now;  // gone from the catalog's details: not asked again for a while
                }
            }
        }
        XC_LOGI("library: %zu of %zu old details refreshed (%zu over 30 days in all)", done, refresh.size(),
                static_cast<size_t>(std::count_if(products_.begin(), products_.end(), [&](const auto& kv) {
                    return kv.second.detailed && now - kv.second.detailedAt > kRefreshAge;
                })));
        changed();
    }
    saveCache();  // descriptions, hero art, modes and languages for next time
    size_t detailed = 0;
    for (const auto& [id, p] : products_) detailed += p.detailed;
    XC_LOGI("library: %zu rows, %zu of %zu products with details (%llu s)", layout_.size(), detailed, products_.size(),
            static_cast<unsigned long long>((platform::nowMs() - t0) / 1000));
}

void Library::loadFriends(const std::string& xblAuth, const Changed& changed, const std::atomic<bool>* stop) {
    if (xblAuth.empty() || (stop && *stop)) return;
    std::vector<xcloud::FriendPresence> presence;
    std::string err;
    if (!xcloud::fetchFriendsPresence(xblAuth, presence, err, language_)) return;
    if (stop && *stop) return;

    // Map Xbox Live Title ID / Cloud Title ID -> Store Product ID
    std::map<std::string, std::string> productByXbox;
    for (const auto& [pid, xid] : xboxTitleOf_) {
        if (!xid.empty()) productByXbox[xid] = pid;
    }
    for (const auto& [pid, prod] : products_) {
        productByXbox[pid] = pid;
        if (!prod.xboxTitleId.empty() && !productByXbox.count(prod.xboxTitleId)) {
            productByXbox[prod.xboxTitleId] = pid;
        }
        if (!prod.xcloudTitleId.empty() && !productByXbox.count(prod.xcloudTitleId)) {
            productByXbox[prod.xcloudTitleId] = pid;
        }
    }
    for (const auto& [pid, cid] : cloudTitleOf_) {
        if (!cid.empty() && !productByXbox.count(cid)) {
            productByXbox[cid] = pid;
        }
    }

    // Group friends playing accessible games
    std::map<std::string, std::vector<ui::FriendPlaying>> friendsByProduct;
    for (const auto& fp : presence) {
        if (fp.presenceState != "Online" || fp.titleId.empty()) continue;
        auto it = productByXbox.find(fp.titleId);
        if (it == productByXbox.end()) continue;
        const std::string& pid = it->second;

        // Check if game is playable on this account (Game Pass or owned)
        ui::GameTile t = tile(pid);
        if (!t.playable) continue;

        friendsByProduct[pid].push_back({fp.gamertag, fp.gamerpicUrl});
    }

    std::vector<ui::GameTile> tiles;
    std::vector<std::string> needDetails;
    for (auto& [pid, friendsList] : friendsByProduct) {
        ui::GameTile t = tile(pid);
        if (t.productId.empty() || !t.playable) continue;
        if (t.heroUrl.empty()) {
            auto pit = products_.find(pid);
            if (pit != products_.end() && !pit->second.heroUrl.empty()) {
                t.heroUrl = pit->second.heroUrl;
            } else {
                for (const auto& [otherPid, otherProd] : products_) {
                    if (!otherProd.heroUrl.empty() &&
                        ((!t.titleId.empty() && otherProd.xcloudTitleId == t.titleId) ||
                         (!t.xboxTitleId.empty() && otherProd.xboxTitleId == t.xboxTitleId))) {
                        t.heroUrl = otherProd.heroUrl;
                        if (t.description.empty()) t.description = otherProd.description;
                        break;
                    }
                }
            }
        }
        if (t.heroUrl.empty() && !pid.empty()) {
            needDetails.push_back(pid);
        }
        t.friends = std::move(friendsList);
        tiles.push_back(std::move(t));
    }

    friendsTiles_ = std::move(tiles);
    if (!friendsTiles_.empty()) {
        XC_LOGI("library: %zu friends playing %zu accessible games now", presence.size(), friendsTiles_.size());
        changed();
        if (!needDetails.empty()) {
            fetchFull(needDetails, changed, stop);
            for (auto& t : friendsTiles_) {
                if (t.heroUrl.empty()) {
                    auto fresh = tile(t.productId, t.titleId);
                    if (!fresh.heroUrl.empty()) t.heroUrl = fresh.heroUrl;
                    if (!fresh.description.empty() && t.description.empty()) t.description = fresh.description;
                }
            }
            changed();
        }
    }
}

}  // namespace xc::app
