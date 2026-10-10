// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/library.h"

#include "platform/platform.h"
#include "ui/strings.h"
#include "util/json.h"
#include "util/log.h"
#include "xcloud/catalog.h"
#include "xcloud/social.h"
#include "xcloud/titlehub.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <map>

namespace xc::app {

namespace {

constexpr int kCacheVersion = 2;

struct ListSpec {
    const char* sigl;
    size_t limit;
};

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
        json::Value langs = json::Value::object();
        for (const auto& [lang, bits] : p.languages) langs.set(lang, static_cast<int64_t>(bits));
        v.set("langs", langs);
    }
    return v;
}

xcloud::Product productFromJson(const std::string& id, const json::Value& v) {
    xcloud::Product p;
    p.productId = id;
    p.xcloudTitleId = v["xt"].str();
    p.xboxTitleId = v["xbox"].str();
    p.title = v["title"].str();
    p.publisher = v["publisher"].str();
    p.description = v["description"].str();
    p.tileUrl = v["tile"].str();
    p.posterUrl = v["poster"].str();
    p.heroUrl = v["hero"].str();
    for (const auto& c : v["categories"].items()) p.categories.push_back(xcloud::categoryName(c.str()));
    p.detailed = v["d"].asBool(false);
    // Details from before their date was kept count from now (not all due at once).
    p.detailedAt = v["at"].asInt(static_cast<int64_t>(std::time(nullptr)));
    p.modes = static_cast<uint32_t>(v["modes"].asInt(0));
    for (const auto& [lang, bits] : v["langs"].members()) p.languages[lang] = static_cast<uint8_t>(bits.asInt(0));
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

bool namesSeries(const std::string& title) {
    return title.find("Series X|S") != std::string::npos || title.find("Series X/S") != std::string::npos;
}

bool endsWith(const std::string& s, const char* tail) {
    size_t n = std::strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

// Which version of a cross-gen pair (two products, one Xbox title id) a
// product is, from its cloud title id or its name: +1 Series X|S, -1 Xbox
// One, 0 can't tell. "HOGWARTSLEGACYXBOXONEVERSION", "...XBOXSERIESXSVERSION",
// "RUSTCONSOLEEDITIONXS" / "Rust Console Edition X|S", "... - Xbox Series X|S".
int pairVersion(const xcloud::Product& p) {
    const std::string& c = p.xcloudTitleId;
    if (c.find("XBOXONE") != std::string::npos) return -1;
    if (c.find("XBOXSERIES") != std::string::npos || c.find("SERIESXS") != std::string::npos || endsWith(c, "XS"))
        return 1;
    if (namesSeries(p.title) || endsWith(p.title, "X|S") || endsWith(p.title, "X/S")) return 1;
    return 0;
}

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
    t.detailed = p.detailed;
    t.modes = p.modes;
    if (auto lang = p.languages.find(language_.substr(0, language_.find('-'))); lang != p.languages.end())
        t.languages = lang->second;  // in the app's language
    if (ownershipKnown_) t.playable = ownedTitles_.count(t.titleId) || ownedProducts_.count(t.productId);
    // Added to Game Pass since the last start: the account's list from then
    // doesn't have it yet. Not locked until this start's list arrives.
    if (!t.playable && !ownershipFresh_ && !knownGamePass_.empty() && gamePass_.count(productId) &&
        !knownGamePass_.count(productId))
        t.playable = true;
    t.freeInStore = freeInStore_.count(productId) > 0;
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
    else if (namesSeries(p.title)) t.platform = xcloud::kPlatformSeries;
    else if (t.platform == xcloud::kPlatformSeries && seriesSiblings_.count(xbox)) t.platform = xcloud::kPlatformOne;
    return t;
}

std::vector<ui::GameTile> Library::tiles(const std::vector<Item>& items) const {
    std::vector<ui::GameTile> out;
    for (const auto& [pid, tid] : items) {
        ui::GameTile t = tile(pid, tid);
        if (xcloud::isNonGameAddon(t.titleId, t.name)) continue;
        if (!t.productId.empty() && !t.titleId.empty() && !t.name.empty()) out.push_back(std::move(t));
    }
    dedupeByTitle(out);
    return out;
}

std::vector<ui::GameRow> Library::rows() const {
    std::vector<ui::GameRow> rows;
    for (const auto& r : layout_) {
        ui::GameRow row;
        row.title = r.title;
        row.gamePassBadges = r.badges;
        row.isGrid = r.isGrid;
        for (const auto& [pid, tid] : r.items) {
            ui::GameTile t = tile(pid, tid);
            if (t.productId.empty() || t.titleId.empty() || xcloud::isNonGameAddon(t.titleId, t.name)) continue;
            row.tiles.push_back(std::move(t));
        }
        if (!row.tiles.empty()) rows.push_back(std::move(row));
    }

    // Insert "Friends playing now" dynamically if any friend is playing an accessible title
    if (!friendsTiles_.empty()) {
        ui::GameRow friendsRow;
        friendsRow.title = ui::tr(ui::Str::FriendsPlayingNow);
        friendsRow.gamePassBadges = false;
        friendsRow.isGrid = false;
        friendsRow.tiles = friendsTiles_;

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
    return tiles(items);
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
    if (!j || (*j)["version"].asInt() != kCacheVersion || (*j)["market"].str() != market_) return false;
    // Names and descriptions in another language (the language changed):
    // still shown, so the lists stay whole, and fetched again in this one.
    bool sameLanguage = (*j)["language"].str() == language_;
    for (const auto& [id, v] : (*j)["products"].members())
        if (!products_.count(id)) {
            products_[id] = productFromJson(id, v);
            if (!sameLanguage) otherLanguage_.insert(id);
        }
    for (const auto& [id, code] : (*j)["platforms"].members()) platform_[id] = code.str();
    for (const auto& [pid, xbox] : (*j)["xboxTitles"].members()) xboxTitleOf_[pid] = xbox.str();
    for (const auto& id : (*j)["freeInStore"].items()) freeInStore_.insert(id.str());
    for (const auto& t : (*j)["ownedTitles"].items()) ownedTitles_.insert(t.str());
    for (const auto& p : (*j)["ownedProducts"].items()) ownedProducts_.insert(p.str());
    for (const auto& p : (*j)["gamePass"].items()) knownGamePass_.insert(p.str());
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
    ownershipKnown_ = !ownedTitles_.empty();
    sortOwned();
    XC_LOGI("library: cache: %zu of the account's games, %zu to buy, %zu consoles known", owned_.size(),
            purchasable_.size(), platform_.size());
    return ownershipKnown_;
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
    root.set("ownedProducts", prods);
    root.set("owned", itemsToJson(owned_));
    json::Value gamePass = json::Value::array();
    for (const auto& id : allGames_) gamePass.push(id);
    root.set("gamePass", gamePass);  // the next start tells the games added since
    root.set("purchasable", itemsToJson(purchasable_));
    // Details of everything on the home screen, in "Your games" and in the
    // Game Pass search: the next launch shows them, hero art and all, before
    // the network answers, and the search's filters (play modes, languages)
    // work without asking again.
    json::Value details = json::Value::object();
    auto keep = [&](const std::string& pid) {
        auto it = products_.find(pid);
        if (it != products_.end()) details.set(pid, productToJson(it->second));
    };
    for (const auto& r : layout_)
        for (const auto& item : r.items) keep(item.first);
    for (const auto* list : {&owned_, &purchasable_})
        for (const auto& [pid, tid] : *list) keep(pid);
    for (const auto& pid : allGames_) keep(pid);
    root.set("products", details);
    json::Value platforms = json::Value::object(), xboxTitles = json::Value::object();
    for (const auto& [id, code] : platform_) platforms.set(id, code);
    for (const auto& [pid, xbox] : xboxTitleOf_) xboxTitles.set(pid, xbox);
    root.set("platforms", platforms);
    root.set("xboxTitles", xboxTitles);
    json::Value free = json::Value::array();
    for (const auto& id : freeInStore_) free.push(id);
    root.set("freeInStore", free);
    if (!platform::writeFileAtomic(cachePath_, root.dump())) XC_LOGW("library: could not write %s", cachePath_.c_str());
}

bool Library::load(xcloud::GssvClient& gssv, const std::string& language, const Changed& changed, std::string& err) {
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
    // The account's games from the previous run, until loadOwned() refreshes them.
    loadCache();

    // "Jump back in": recently played Game Pass games (the others go first
    // in "Your games").
    std::vector<xcloud::Title> recent;
    if (gssv.listTitles(recent, lastErr, true)) {
        RowIds r{ui::tr(ui::Str::JumpBackIn), false, false, {}};
        std::vector<std::string> ids;
        for (const auto& t : recent) {
            if (t.productId.empty()) continue;
            recent_.push_back(t.productId);
            if (!t.xboxTitleId.empty()) xboxTitleOf_[t.productId] = t.xboxTitleId;
            if (!gamePass_.empty() && !gamePass_.count(t.productId)) continue;
            r.items.emplace_back(t.productId, t.titleId);
            if (!products_.count(t.productId)) ids.push_back(t.productId);
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
    for (const auto& spec : lists) {
        xcloud::ProductList list;
        bool isAllGames = std::string_view(spec.sigl) == xcloud::sigl::kAllGames;
        if (isAllGames && !all.productIds.empty()) {
            list = all;
        } else if (!xcloud::fetchList(spec.sigl, market_, language_, list, e)) {
            XC_LOGW("%s", e.c_str());
            lastErr = e;
            continue;
        }
        // "Most popular on cloud" also lists games to buy first (Cuphead...):
        // those go to "Your games" (in this order), not to the Game Pass tab.
        if (spec.sigl == xcloud::sigl::kMostPopular) popular_ = list.productIds;
        // Free-to-play: no Game Pass needed, so not filtered; each one the
        // account hasn't got yet shows as free to get (tile()).
        bool freeToPlay = spec.sigl == xcloud::sigl::kFreeToPlay;
        if (freeToPlay) {
            freeInStore_.insert(list.productIds.begin(), list.productIds.end());
            XC_LOGI("library: %s: %zu games", list.title.c_str(), list.productIds.size());
        }
        if (!gamePass_.empty() && !freeToPlay) {
            size_t before = list.productIds.size();
            list.productIds.erase(std::remove_if(list.productIds.begin(), list.productIds.end(),
                                                 [&](const std::string& id) { return !gamePass_.count(id); }),
                                  list.productIds.end());
            if (list.productIds.size() != before)
                XC_LOGI("library: %s: %zu of %zu not in Game Pass, left out", list.title.c_str(),
                        before - list.productIds.size(), before);
        }
        if (spec.limit > 0 && list.productIds.size() > spec.limit) list.productIds.resize(spec.limit);
        std::vector<std::string> missing;
        for (const auto& id : list.productIds)
            if (!products_.count(id)) missing.push_back(id);
        if (!missing.empty() && !xcloud::fetchProducts(missing, market_, language_, products_, e, false)) {
            XC_LOGW("%s", e.c_str());
            lastErr = e;
            continue;
        }
        RowIds r{list.title, !freeToPlay, isAllGames, {}};
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
    progress_ = {true, 0, 0};  // the account's list: no measure until the names
    changed();
    std::vector<xcloud::Title> titles;
    std::string err;
    if (!gssv.listTitles(titles, err, false)) {
        XC_LOGW("%s", err.c_str());  // keep what the cache said
        return;
    }
    if (stop && *stop) return;
    std::set<std::string> ownedTitles, ownedProducts;
    std::vector<Item> mine;
    std::map<std::string, std::string> toBuy;  // productId -> titleId
    for (const auto& t : titles) {
        if (xcloud::isNonGameAddon(t.titleId)) continue;
        if (!t.productId.empty() && !t.xboxTitleId.empty()) xboxTitleOf_[t.productId] = t.xboxTitleId;
        if (t.isFreeInStore && !t.productId.empty()) freeInStore_.insert(t.productId);
        if (t.hasEntitlement) {
            ownedTitles.insert(t.titleId);
            if (t.productId.empty()) continue;
            ownedProducts.insert(t.productId);
            // Game Pass games the account owns itself (free-to-play ones
            // like Fortnite, without the subscription) are its games too.
            if (!gamePass_.count(t.productId) || !t.viaSubscription) mine.emplace_back(t.productId, t.titleId);
        } else if (!t.productId.empty() && !gamePass_.count(t.productId)) {
            toBuy[t.productId] = t.titleId;
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
    if (!missing.empty() && !xcloud::fetchProducts(missing, market_, language_, products_, err, false))
        XC_LOGW("%s", err.c_str());
    if (stop && *stop) return;
    ownedTitles_ = std::move(ownedTitles);
    ownershipFresh_ = true;
    ownedProducts_ = std::move(ownedProducts);
    owned_ = std::move(mine);
    purchasable_ = std::move(buy);
    purchasableSet_.clear();
    for (const auto& p : purchasable_) purchasableSet_.insert(p.first);
    ownershipKnown_ = true;
    sortOwned();
    saveCache();
    XC_LOGI("library: account can stream %zu titles; %zu outside Game Pass; %zu more to buy", ownedTitles_.size(),
            owned_.size(), purchasable_.size());
    changed();
}

void Library::loadCatalogNames(const Changed& changed, const std::atomic<bool>* stop) {
    // Names and art: the account's games and the games to buy first (their
    // lists are on screen), then the Game Pass catalog for its search. Also
    // anything still in another language.
    std::vector<std::string> missing;
    std::set<std::string> queued;
    auto want = [&](const std::string& id) {
        if ((!products_.count(id) || otherLanguage_.count(id)) && queued.insert(id).second) missing.push_back(id);
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
            // Keep the hero art and description already known, until they
            // come in the new language (hydrate()).
            auto old = products_.find(id);
            if (old != products_.end()) {
                if (p.heroUrl.empty()) p.heroUrl = old->second.heroUrl;
                if (p.description.empty() && !otherLanguage_.count(id)) p.description = old->second.description;
                if (!p.detailed && old->second.detailed) {  // modes and languages don't change with the language
                    p.detailed = true;
                    p.detailedAt = old->second.detailedAt;
                    p.modes = old->second.modes;
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
        if ((it == products_.end() || it->second.heroUrl.empty() || it->second.description.empty() ||
             !it->second.detailed) &&
            queued.insert(id).second)
            order.push_back(id);
    }
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
        for (auto& [id, p] : full) products_[id] = std::move(p);
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
                if (it != fresh.end()) products_[id] = std::move(it->second), ++done;
                else products_[id].detailedAt = now;  // gone from the catalog's details: not asked again for a while
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

    // Map Xbox Live Title ID -> Store Product ID
    std::map<std::string, std::string> productByXbox;
    for (const auto& [pid, xid] : xboxTitleOf_) {
        if (!xid.empty()) productByXbox[xid] = pid;
    }
    for (const auto& [pid, prod] : products_) {
        if (!prod.xboxTitleId.empty() && !productByXbox.count(prod.xboxTitleId)) {
            productByXbox[prod.xboxTitleId] = pid;
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
    for (auto& [pid, friendsList] : friendsByProduct) {
        ui::GameTile t = tile(pid);
        if (t.productId.empty() || !t.playable) continue;
        t.friends = std::move(friendsList);
        tiles.push_back(std::move(t));
    }

    friendsTiles_ = std::move(tiles);
    if (!friendsTiles_.empty()) {
        XC_LOGI("library: %zu friends playing %zu accessible games now", presence.size(), friendsTiles_.size());
        changed();
    }
}

}  // namespace xc::app
