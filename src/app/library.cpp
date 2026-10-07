// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/library.h"

#include "platform/platform.h"
#include "ui/strings.h"
#include "util/json.h"
#include "util/log.h"
#include "xcloud/catalog.h"

#include <algorithm>
#include <map>

namespace xc::app {

namespace {

constexpr int kCacheVersion = 1;

struct ListSpec {
    const char* sigl;
    size_t limit;
};

json::Value productToJson(const xcloud::Product& p) {
    json::Value v = json::Value::object();
    v.set("xt", p.xcloudTitleId);
    v.set("title", p.title);
    v.set("publisher", p.publisher);
    v.set("description", p.description);
    v.set("tile", p.tileUrl);
    v.set("poster", p.posterUrl);
    v.set("hero", p.heroUrl);
    json::Value cats = json::Value::array();
    for (const auto& c : p.categories) cats.push(c);
    v.set("categories", cats);
    return v;
}

xcloud::Product productFromJson(const std::string& id, const json::Value& v) {
    xcloud::Product p;
    p.productId = id;
    p.xcloudTitleId = v["xt"].str();
    p.title = v["title"].str();
    p.publisher = v["publisher"].str();
    p.description = v["description"].str();
    p.tileUrl = v["tile"].str();
    p.posterUrl = v["poster"].str();
    p.heroUrl = v["hero"].str();
    for (const auto& c : v["categories"].items()) p.categories.push_back(c.str());
    return p;
}

}  // namespace

ui::GameTile Library::tile(const std::string& productId, const std::string& titleId) const {
    ui::GameTile t;
    auto it = products_.find(productId);
    if (it == products_.end()) return t;
    const xcloud::Product& p = it->second;
    t.productId = p.productId;
    t.titleId = titleId.empty() ? p.xcloudTitleId : titleId;
    t.name = p.title;
    t.publisher = p.publisher;
    t.description = p.description;
    t.tileUrl = p.tileUrl.empty() ? p.posterUrl : p.tileUrl;
    t.heroUrl = p.heroUrl;
    t.categories = p.categories;
    if (ownershipKnown_) t.playable = ownedTitles_.count(t.titleId) || ownedProducts_.count(t.productId);
    return t;
}

std::vector<ui::GameRow> Library::rows() const {
    std::vector<ui::GameRow> rows;
    for (const auto& r : layout_) {
        ui::GameRow row;
        row.title = r.title;
        row.gamePassBadges = r.badges;
        for (const auto& [pid, tid] : r.items) {
            ui::GameTile t = tile(pid, tid);
            if (t.productId.empty() || t.titleId.empty()) continue;
            row.tiles.push_back(std::move(t));
        }
        if (!row.tiles.empty()) rows.push_back(std::move(row));
    }
    return rows;
}

namespace {

// Editions of one game ("Forza Horizon 5", "... Standard Edition") are
// separate products that start the same cloud title: keep one, the shortest
// name, where the first one was.
void dedupeByTitle(std::vector<ui::GameTile>& tiles) {
    std::map<std::string, size_t> first;
    std::vector<ui::GameTile> out;
    for (auto& t : tiles) {
        auto it = first.find(t.titleId);
        if (it == first.end()) {
            first[t.titleId] = out.size();
            out.push_back(std::move(t));
        } else if (t.name.size() < out[it->second].name.size()) {
            out[it->second] = std::move(t);
        }
    }
    tiles = std::move(out);
}

}  // namespace

std::vector<ui::GameTile> Library::owned() const {
    std::vector<ui::GameTile> out;
    for (const auto& [pid, tid] : owned_) {
        ui::GameTile t = tile(pid, tid);
        if (!t.productId.empty() && !t.titleId.empty()) out.push_back(std::move(t));
    }
    dedupeByTitle(out);
    return out;
}

std::vector<ui::GameTile> Library::searchPool() const {
    std::vector<ui::GameTile> out;
    std::set<std::string> seen;
    auto add = [&](const std::string& pid, const std::string& tid) {
        if (!seen.insert(pid).second) return;
        ui::GameTile t = tile(pid, tid);
        if (!t.productId.empty() && !t.titleId.empty() && !t.name.empty()) out.push_back(std::move(t));
    };
    for (const auto& [pid, tid] : owned_) add(pid, tid);
    for (const auto& id : allGames_) add(id, std::string());
    dedupeByTitle(out);
    return out;
}

void Library::sortOwned() {
    // Recently played first (newest first), then alphabetical.
    auto rank = [&](const std::string& pid) {
        auto it = std::find(recent_.begin(), recent_.end(), pid);
        return it == recent_.end() ? recent_.size() : static_cast<size_t>(it - recent_.begin());
    };
    auto name = [&](const std::string& pid) {
        auto it = products_.find(pid);
        return it == products_.end() ? std::string() : it->second.title;
    };
    std::stable_sort(owned_.begin(), owned_.end(), [&](const auto& a, const auto& b) {
        size_t ra = rank(a.first), rb = rank(b.first);
        if (ra != rb) return ra < rb;
        return name(a.first) < name(b.first);
    });
}

bool Library::loadCache() {
    std::string text;
    if (cachePath_.empty() || !platform::readFile(cachePath_, text)) return false;
    auto j = json::parse(text);
    if (!j || (*j)["version"].asInt() != kCacheVersion || (*j)["market"].str() != market_ ||
        (*j)["language"].str() != language_)
        return false;
    for (const auto& [id, v] : (*j)["products"].members())
        if (!products_.count(id)) products_[id] = productFromJson(id, v);
    for (const auto& t : (*j)["ownedTitles"].items()) ownedTitles_.insert(t.str());
    for (const auto& p : (*j)["ownedProducts"].items()) ownedProducts_.insert(p.str());
    owned_.clear();
    for (const auto& o : (*j)["owned"].items())
        if (products_.count(o["p"].str())) owned_.emplace_back(o["p"].str(), o["t"].str());
    ownershipKnown_ = !ownedTitles_.empty();
    sortOwned();
    XC_LOGI("library: cache: %zu of the account's games, %zu titles playable", owned_.size(), ownedTitles_.size());
    return ownershipKnown_;
}

void Library::saveCache() const {
    if (cachePath_.empty()) return;
    json::Value root = json::Value::object();
    root.set("version", kCacheVersion);
    root.set("market", market_);
    root.set("language", language_);
    json::Value titles = json::Value::array(), prods = json::Value::array(), owned = json::Value::array();
    for (const auto& t : ownedTitles_) titles.push(t);
    for (const auto& p : ownedProducts_) prods.push(p);
    json::Value details = json::Value::object();
    for (const auto& [pid, tid] : owned_) {
        json::Value o = json::Value::object();
        o.set("p", pid);
        o.set("t", tid);
        owned.push(o);
        auto it = products_.find(pid);
        if (it != products_.end()) details.set(pid, productToJson(it->second));
    }
    root.set("ownedTitles", titles);
    root.set("ownedProducts", prods);
    root.set("owned", owned);
    root.set("products", details);
    if (!platform::writeFileAtomic(cachePath_, root.dump())) XC_LOGW("library: could not write %s", cachePath_.c_str());
}

bool Library::load(xcloud::GssvClient& gssv, const std::string& language, const Changed& changed, std::string& err) {
    market_ = gssv.session().market.empty() ? "US" : gssv.session().market;
    language_ = language;
    layout_.clear();
    products_.clear();
    allGames_.clear();
    gamePass_.clear();
    recent_.clear();
    ownershipKnown_ = false;
    ownedTitles_.clear();
    ownedProducts_.clear();
    owned_.clear();
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
    // The account's games from the previous run, until loadOwned() refreshes them.
    loadCache();

    // "Jump back in": recently played Game Pass games (the others go first
    // in "Your games").
    std::vector<xcloud::Title> recent;
    if (gssv.listTitles(recent, lastErr, true)) {
        RowIds r{ui::tr(ui::Str::JumpBackIn), false, {}};
        std::vector<std::string> ids;
        for (const auto& t : recent) {
            if (t.productId.empty()) continue;
            recent_.push_back(t.productId);
            if (!gamePass_.empty() && !gamePass_.count(t.productId)) continue;
            r.items.emplace_back(t.productId, t.titleId);
            if (!products_.count(t.productId)) ids.push_back(t.productId);
        }
        sortOwned();
        if (xcloud::fetchProducts(ids, market_, language_, products_, lastErr, false)) {
            layout_.push_back(std::move(r));
            changed();
        }
    }

    const ListSpec lists[] = {{xcloud::sigl::kRecentlyAdded, 40},
                              {xcloud::sigl::kMostPopular, 40},
                              {xcloud::sigl::kLeavingSoon, 40},
                              {xcloud::sigl::kAllGames, 120}};
    for (const auto& spec : lists) {
        xcloud::ProductList list;
        if (spec.sigl == xcloud::sigl::kAllGames && !all.productIds.empty()) {
            list = all;
        } else if (!xcloud::fetchList(spec.sigl, market_, language_, list, e)) {
            XC_LOGW("%s", e.c_str());
            lastErr = e;
            continue;
        }
        // "Most popular on cloud" also lists games to buy first (Cuphead...).
        if (!gamePass_.empty()) {
            size_t before = list.productIds.size();
            list.productIds.erase(std::remove_if(list.productIds.begin(), list.productIds.end(),
                                                 [&](const std::string& id) { return !gamePass_.count(id); }),
                                  list.productIds.end());
            if (list.productIds.size() != before)
                XC_LOGI("library: %s: %zu of %zu not in Game Pass, left out", list.title.c_str(),
                        before - list.productIds.size(), before);
        }
        if (list.productIds.size() > spec.limit) list.productIds.resize(spec.limit);
        std::vector<std::string> missing;
        for (const auto& id : list.productIds)
            if (!products_.count(id)) missing.push_back(id);
        if (!missing.empty() && !xcloud::fetchProducts(missing, market_, language_, products_, e, false)) {
            XC_LOGW("%s", e.c_str());
            lastErr = e;
            continue;
        }
        RowIds r{list.title, true, {}};
        for (const auto& id : list.productIds) r.items.emplace_back(id, std::string());
        layout_.push_back(std::move(r));
        changed();
    }
    if (rows().empty()) {
        err = lastErr.empty() ? "no games found" : lastErr;
        return false;
    }
    return true;
}

void Library::loadOwned(xcloud::GssvClient gssv, const Changed& changed, const std::atomic<bool>* stop) {
    std::vector<xcloud::Title> titles;
    std::string err;
    if (!gssv.listTitles(titles, err, false)) {
        XC_LOGW("%s", err.c_str());  // keep what the cache said
        return;
    }
    if (stop && *stop) return;
    std::set<std::string> ownedTitles, ownedProducts;
    std::vector<std::pair<std::string, std::string>> mine;
    for (const auto& t : titles) {
        if (!t.hasEntitlement) continue;
        ownedTitles.insert(t.titleId);
        if (t.productId.empty()) continue;
        ownedProducts.insert(t.productId);
        if (!gamePass_.count(t.productId)) mine.emplace_back(t.productId, t.titleId);
    }
    std::vector<std::string> missing;
    for (const auto& m : mine)
        if (!products_.count(m.first)) missing.push_back(m.first);
    if (!missing.empty() && !xcloud::fetchProducts(missing, market_, language_, products_, err, false))
        XC_LOGW("%s", err.c_str());
    if (stop && *stop) return;
    // Products the catalog doesn't know have no name or art.
    mine.erase(std::remove_if(mine.begin(), mine.end(), [&](const auto& m) { return !products_.count(m.first); }),
               mine.end());
    ownedTitles_ = std::move(ownedTitles);
    ownedProducts_ = std::move(ownedProducts);
    owned_ = std::move(mine);
    ownershipKnown_ = true;
    sortOwned();
    saveCache();
    XC_LOGI("library: account can stream %zu titles; %zu outside Game Pass", ownedTitles_.size(), owned_.size());
    changed();
}

void Library::loadCatalogNames(const Changed& changed, const std::atomic<bool>* stop) {
    std::vector<std::string> missing;
    for (const auto& id : allGames_)
        if (!products_.count(id)) missing.push_back(id);
    constexpr size_t kBatch = 100;
    for (size_t i = 0; i < missing.size(); i += kBatch) {
        if (stop && *stop) return;
        std::vector<std::string> batch(missing.begin() + static_cast<long>(i),
                                       missing.begin() + static_cast<long>(std::min(missing.size(), i + kBatch)));
        std::string e;
        if (!xcloud::fetchProducts(batch, market_, language_, products_, e, false)) {
            XC_LOGW("%s", e.c_str());
            return;
        }
    }
    XC_LOGI("library: search covers %zu games", searchPool().size());
    changed();
}

void Library::hydrate(const Changed& changed, const std::atomic<bool>* stop) {
    // On-screen order, so what is shown first gets its art first: the rows,
    // then the account's games.
    std::vector<std::string> order;
    auto want = [&](const std::string& id) {
        auto it = products_.find(id);
        if (it != products_.end() && it->second.heroUrl.empty() &&
            std::find(order.begin(), order.end(), id) == order.end())
            order.push_back(id);
    };
    for (const auto& r : layout_)
        for (const auto& item : r.items) want(item.first);
    for (const auto& item : owned_) want(item.first);
    // A small first batch: the cards on screen get their hero art quickly.
    bool ownedChanged = false;
    for (size_t i = 0, n = 6; i < order.size(); i += n, n = 20) {
        if (stop && *stop) return;
        std::vector<std::string> batch(order.begin() + static_cast<long>(i),
                                       order.begin() + static_cast<long>(std::min(order.size(), i + n)));
        std::map<std::string, xcloud::Product> full;
        std::string e;
        if (!xcloud::fetchProducts(batch, market_, language_, full, e, true)) {
            XC_LOGW("%s", e.c_str());
            return;
        }
        for (auto& [id, p] : full) {
            if (ownedProducts_.count(id)) ownedChanged = true;
            products_[id] = std::move(p);
        }
        if (stop && *stop) return;
        changed();
    }
    if (ownedChanged) saveCache();  // descriptions and hero art for next time
    XC_LOGI("library: %zu rows, %zu products with details", layout_.size(), products_.size());
}

}  // namespace xc::app
