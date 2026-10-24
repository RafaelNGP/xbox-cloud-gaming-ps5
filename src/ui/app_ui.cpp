// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/app_ui_internal.h"

namespace xc::ui {

std::vector<SettingsRowPos> visibleSettingsRows(const UiCaps& caps) {
    return filterRows(kSettingsOrder, kSettingsGroupStart, [&](int row) {
        switch (row) {
        case 4: return caps.triggerRumble || caps.adaptiveTriggers;  // trigger vibration and its tester
        case 21: return caps.triggerRumble || caps.adaptiveTriggers; // audio-to-haptics
        case 6: return caps.lightBar;
        case 9: return caps.highRefresh;
        case 13: return caps.desktopWindow;
        case 15: return caps.gyro;
        default: return true;
        }
    });
}

int settingsRowPosition(const UiCaps& caps, int row) {
    auto rows = visibleSettingsRows(caps);
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].row == row) return static_cast<int>(i);
    return -1;
}

std::vector<SettingsRowPos> visibleGameSettingsRows(const UiCaps& caps, bool custom, bool mouseSupported) {
    return filterRows(kGameRowOrder, kGameGroupStart, [&](int row) {
        switch (row) {
        case 1: return caps.upscalers;
        case 2: return caps.sharpen;
        case 3: return caps.deband;
        case 6: return caps.triggerRumble || caps.adaptiveTriggers;
        case 16: return caps.triggerRumble || caps.adaptiveTriggers; // audio-to-haptics
        case 8: return caps.highRefresh;
        case 10: return custom;
        case 12: return caps.gyro;
        case 13: return caps.touchpad && mouseSupported;
        default: return true;
        }
    });
}

std::string prettyRegion(const std::string& name) {
    // Longer words first where one starts another (SOUTHEAST before SOUTH).
    static const char* kWords[] = {"SOUTHEAST", "NORTHEAST", "SWITZERLAND", "CENTRAL", "AUSTRALIA", "GERMANY",
                                   "EUROPE",    "BRAZIL",    "CANADA",      "FRANCE",  "MEXICO",    "SWEDEN",
                                   "NORWAY",    "POLAND",    "ITALY",       "SPAIN",   "CHILE",     "JAPAN",
                                   "KOREA",     "INDIA",     "QATAR",       "ISRAEL",  "AFRICA",    "NORTH",
                                   "SOUTH",     "EAST",      "WEST",        "ASIA",    "UAE",       "UK",
                                   "US"};
    std::string out;
    size_t i = 0;
    while (i < name.size()) {
        bool matched = false;
        for (const char* w : kWords) {
            size_t n = std::char_traits<char>::length(w);
            if (name.compare(i, n, w) == 0) {
                bool acronym = n <= 3;  // UK, US, UAE
                std::string word = acronym ? std::string(w) : std::string(1, w[0]);
                if (!acronym)
                    for (size_t k = 1; k < n; ++k) word += static_cast<char>(w[k] - 'A' + 'a');
                out += (out.empty() ? "" : " ") + word;
                i += n;
                matched = true;
                break;
            }
        }
        if (!matched) {
            if (std::isdigit(static_cast<unsigned char>(name[i])) && !out.empty() && !std::isdigit(static_cast<unsigned char>(out.back())))
                out += ' ';
            out += name[i++];
        }
    }
    return out;
}

std::string formatPlaytime(uint64_t seconds) {
    if (seconds < 60) return "< 1 min";
    uint64_t minutes = (seconds + 30) / 60;
    if (minutes < 60) return std::to_string(minutes) + " min";
    uint64_t hours = minutes / 60;
    uint64_t remMinutes = minutes % 60;
    if (remMinutes == 0) return std::to_string(hours) + " h";
    return std::to_string(hours) + " h " + std::to_string(remMinutes) + " min";
}

bool AppUi::Anim::step(float dt) {
    float d = target - value;
    if (std::fabs(d) < 0.5f) {
        value = target;
        return false;
    }
    value += d * std::min(1.0f, dt * 14.0f);
    return true;
}

AppUi::AppUi(const Fonts& fonts, ImageCache& images) : fonts_(fonts), images_(images) {}

// --- Model ---------------------------------------------------------------------

void AppUi::showSplash(const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    screen_ = Screen::Splash;
    status_ = status;
    dirty_ = true;
}

void AppUi::showSignIn(const std::string& code, const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    screen_ = Screen::SignIn;
    code_ = code;
    codeUrl_ = url;
    dirty_ = true;
}

std::string AppUi::gamertag() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return gamertag_;
}

std::vector<std::string> AppUi::consoleNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    for (const auto& c : consoles_)
        if (!c.name.empty()) out.push_back(c.name);
    return out;
}

void AppUi::showReport(const std::string& url, const std::string& heading, const std::string& detail) {
    std::lock_guard<std::mutex> lock(mutex_);
    reportOpen_ = true;
    reportUrl_ = url;
    reportHeading_ = heading;
    reportDetail_ = detail;
    dirty_ = true;
}

bool AppUi::reportOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return reportOpen_;
}

void AppUi::setProfile(const std::string& gamertag, const std::string& gamerpicUrl, const std::string& gamerscore) {
    std::lock_guard<std::mutex> lock(mutex_);
    gamertag_ = gamertag;
    gamerpicUrl_ = gamerpicUrl;
    gamerscore_ = gamerscore;
    dirty_ = true;
}

void AppUi::setSubscription(const std::string& plan) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscription_ = plan;
    if (searching_) runSearch(true);
    dirty_ = true;
}

void AppUi::setMembership(bool member) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!subscription_.empty() && subscription_ != "GamePass") return;  // the plan is known
    subscription_ = member ? "GamePass" : "";
    dirty_ = true;
}

bool AppUi::isFreeAccountLocked() const {
    return subscription_.empty() || (subscription_ != "GamePass" && xcloud::planTier(subscription_) == 0);
}

bool AppUi::isFreeAccount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return isFreeAccountLocked();
}

bool AppUi::isEffectivePlayableLocked(const GameTile& t) const {
    if (!t.playable) return false;
    if (!isFreeAccountLocked()) return true;
    return isFreeGame(t) || isAcquired(t);
}

bool AppUi::isEffectivePlayable(const GameTile& t) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return isEffectivePlayableLocked(t);
}

void AppUi::setRows(std::vector<GameRow> rows) {
    std::lock_guard<std::mutex> lock(mutex_);
    allRows_ = std::move(rows);
    rebuild(true);
}

void AppUi::setRowsLocked(std::vector<GameRow> rows) {
    // Caller holds mutex_. Remember, per row title, which game had the focus
    // and the scroll.
    libLayoutValid_ = false;
    struct Kept {
        std::string productId;
        Anim scroll;
    };
    std::map<std::string, Kept> kept;
    std::string focusedRowTitle;
    for (size_t r = 0; r < rows_.size(); ++r) {
        const auto& tiles = rows_[r].tiles;
        int col = focusCol_[r];
        kept[rows_[r].title] = {col < static_cast<int>(tiles.size()) ? tiles[static_cast<size_t>(col)].productId : "",
                                rowScroll_[r]};
        if (static_cast<int>(r) == focusRow_) focusedRowTitle = rows_[r].title;
    }
    bool first = rows_.empty();

    rows_ = std::move(rows);
    rows_.erase(std::remove_if(rows_.begin(), rows_.end(), [](const GameRow& r) { return r.tiles.empty(); }),
                rows_.end());
    focusCol_.assign(rows_.size(), 0);
    rowScroll_.assign(rows_.size(), Anim{});
    focusRow_ = 0;
    for (size_t r = 0; r < rows_.size(); ++r) {
        auto it = kept.find(rows_[r].title);
        if (it == kept.end()) continue;
        rowScroll_[r] = it->second.scroll;
        for (size_t c = 0; c < rows_[r].tiles.size(); ++c)
            if (rows_[r].tiles[c].productId == it->second.productId) focusCol_[r] = static_cast<int>(c);
        if (rows_[r].title == focusedRowTitle) focusRow_ = static_cast<int>(r);
    }
    retarget();
    if (first) {
        for (auto& a : rowScroll_) a.value = a.target;
        rowY_.value = rowY_.target;
    }
    refreshDetail();
    dirty_ = true;
}

void AppUi::setOwned(std::vector<GameTile> owned, std::vector<GameTile> purchasable, bool known) {
    std::lock_guard<std::mutex> lock(mutex_);
    allOwned_ = std::move(owned);
    allPurchasable_ = std::move(purchasable);
    ownedKnown_ = known;
    rebuild(true);
}

void AppUi::setCatalogState(std::vector<GameRow> rows, std::vector<GameTile> owned, std::vector<GameTile> purchasable,
                            bool ownedKnown, bool member, std::vector<GameTile> gpPool, std::vector<GameTile> libPool,
                            bool loadingActive, size_t loadingDone, size_t loadingTotal) {
    std::lock_guard<std::mutex> lock(mutex_);
    allRows_ = std::move(rows);
    allOwned_ = std::move(owned);
    allPurchasable_ = std::move(purchasable);
    ownedKnown_ = ownedKnown;
    if (subscription_.empty() || subscription_ == "GamePass") {
        subscription_ = member ? "GamePass" : "";
    }
    gamePassPool_ = std::move(gpPool);
    libraryPool_ = std::move(libPool);
    loading_ = loadingActive;
    loadingDone_ = loadingDone;
    loadingTotal_ = loadingTotal;
    rebuild(true);
}

void AppUi::rebuild(bool keepPosition) {
    // Caller holds mutex_.
    libLayoutValid_ = false;
    const GameTile* was = libraryTile(gridFocus_);
    std::string focused = was ? was->productId : "";
    // Where the cursor is: section of "Your games" and place in it; Game
    // Pass row (by title) and column.
    int oldStarts[3] = {0, static_cast<int>(owned_.size()), static_cast<int>(owned_.size() + purchasable_.size())};
    int oldSection = gridFocus_ >= oldStarts[2] ? 2 : gridFocus_ >= oldStarts[1] ? 1 : 0;
    int oldOffset = gridFocus_ - oldStarts[oldSection];
    std::string oldRowTitle = focusRow_ < static_cast<int>(rows_.size()) ? rows_[static_cast<size_t>(focusRow_)].title : "";
    int oldCol = focusRow_ < static_cast<int>(focusCol_.size()) ? focusCol_[static_cast<size_t>(focusRow_)] : 0;
    auto visible = [&](const std::vector<GameTile>& in) {
        std::vector<GameTile> out;
        out.reserve(in.size());
        for (const auto& t : in)
            if (!hidden_.count(t.productId)) out.push_back(t);
        return out;
    };
    auto sortByName = [](std::vector<GameTile>& v) {
        if (v.size() <= 1) return;
        struct Keyed {
            std::string key;
            size_t idx;
        };
        std::vector<Keyed> keys(v.size());
        for (size_t i = 0; i < v.size(); ++i) keys[i] = {fold(v[i].name), i};
        std::stable_sort(keys.begin(), keys.end(), [](const Keyed& a, const Keyed& b) { return a.key < b.key; });
        std::vector<GameTile> out;
        out.reserve(v.size());
        for (const auto& k : keys) out.push_back(std::move(v[k.idx]));
        v = std::move(out);
    };
    // "Recent" keeps the order received: recently played first for the
    // account's games, most popular first for the games to buy.
    auto sorted = [&](std::vector<GameTile> v) {
        if (librarySort_ == LibrarySort::AZ) {
            sortByName(v);
        } else if (librarySort_ == LibrarySort::Console && v.size() > 1) {
            struct Keyed {
                int rank;
                std::string key;
                size_t idx;
            };
            std::vector<Keyed> keys(v.size());
            for (size_t i = 0; i < v.size(); ++i) keys[i] = {consoleRank(v[i].platform), fold(v[i].name), i};
            std::stable_sort(keys.begin(), keys.end(), [](const Keyed& a, const Keyed& b) {
                return a.rank != b.rank ? a.rank < b.rank : a.key < b.key;
            });
            std::vector<GameTile> out;
            out.reserve(v.size());
            for (const auto& k : keys) out.push_back(std::move(v[k.idx]));
            v = std::move(out);
        }
        return v;
    };
    if (!wishlist_.empty()) {
        std::unordered_set<std::string> ownedPids, ownedTids;
        ownedPids.reserve(allOwned_.size());
        ownedTids.reserve(allOwned_.size());
        for (const auto& o : allOwned_) {
            if (!o.productId.empty()) ownedPids.insert(o.productId);
            if (!o.titleId.empty()) ownedTids.insert(o.titleId);
        }
        std::unordered_map<std::string, std::string> purchTid;
        purchTid.reserve(allPurchasable_.size());
        for (const auto& p : allPurchasable_) {
            if (!p.productId.empty() && !p.titleId.empty()) purchTid.emplace(p.productId, p.titleId);
        }
        for (auto it = wishlist_.begin(); it != wishlist_.end(); ) {
            bool acq = ownedPids.count(*it) > 0;
            if (!acq) {
                auto pit = purchTid.find(*it);
                if (pit != purchTid.end() && ownedTids.count(pit->second) > 0) acq = true;
            }
            if (acq) it = wishlist_.erase(it);
            else ++it;
        }
    }
    owned_ = sorted(visible(allOwned_));
    purchasable_ = sorted(visible(allPurchasable_));
    std::stable_partition(purchasable_.begin(), purchasable_.end(), [&](const GameTile& t) {
        return wishlist_.count(t.productId) > 0;
    });
    // Hidden games stay in their tab, at its end: the account's games and
    // games to buy in "Your games", Game Pass games in a last Game Pass row
    // (a few rows away, not past ~2000 games to buy). Once each, A-Z.
    std::set<std::string> seen;
    auto collect = [&](std::vector<GameTile>& into, const GameTile& t) {
        if (hidden_.count(t.productId) && seen.insert(t.productId).second) into.push_back(t);
    };
    hiddenTiles_.clear();
    for (const auto* list : {&allOwned_, &allPurchasable_})
        for (const auto& t : *list) collect(hiddenTiles_, t);
    sortByName(hiddenTiles_);
    GameRow hiddenRow;
    hiddenRow.title = tr(Str::HiddenSection);
    hiddenRow.gamePassBadges = true;
    for (const auto& row : allRows_)
        for (const auto& t : row.tiles) collect(hiddenRow.tiles, t);
    for (const auto& t : gamePassPool_) collect(hiddenRow.tiles, t);
    sortByName(hiddenRow.tiles);

    std::vector<GameRow> rows;
    for (const auto& row : allRows_) {
        GameRow r = row;
        r.tiles = visible(row.tiles);
        rows.push_back(std::move(r));
    }
    if (!hiddenRow.tiles.empty()) rows.push_back(std::move(hiddenRow));
    setRowsLocked(std::move(rows));

    if (keepPosition) {
        // The same place in the same section (the next game slides into a
        // hidden one's place); if that section is now empty, the nearest.
        const std::vector<GameTile>* lists[3] = {&owned_, &purchasable_, &hiddenTiles_};
        int starts[3] = {0, static_cast<int>(owned_.size()), static_cast<int>(owned_.size() + purchasable_.size())};
        int section = oldSection;
        while (section > 0 && lists[section]->empty()) --section;
        while (section < 2 && lists[section]->empty()) ++section;
        int size = static_cast<int>(lists[section]->size());
        gridFocus_ = size ? starts[section] + std::min(oldOffset, size - 1) : 0;
        for (size_t r = 0; r < rows_.size(); ++r)
            if (rows_[r].title == oldRowTitle) {
                focusRow_ = static_cast<int>(r);
                focusCol_[r] = std::min(oldCol, static_cast<int>(rows_[r].tiles.size()) - 1);
            }
        retarget();
    } else {
        gridFocus_ = 0;
        for (int i = 0; libraryTile(i); ++i)
            if (libraryTile(i)->productId == focused) gridFocus_ = i;
    }
    if (searching_) runSearch(true);
    refreshDetail();
    dirty_ = true;
}

void AppUi::setAllow1440(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    allow1440_ = on;
    dirty_ = true;
}

void AppUi::setConsoles(std::vector<ConsoleTile> consoles, bool known) {
    std::lock_guard<std::mutex> lock(mutex_);
    consoles_ = std::move(consoles);
    consolesKnown_ = known;
    consoleFocus_ = std::min(consoleFocus_, std::max(0, static_cast<int>(consoles_.size()) - 1));
    dirty_ = true;
}

void AppUi::setLoading(bool active, size_t done, size_t total) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active == loading_ && done == loadingDone_ && total == loadingTotal_) return;
    loading_ = active;
    loadingDone_ = done;
    loadingTotal_ = total;
    dirty_ = true;
}

void AppUi::drawLoadingBar(Canvas& c, int x, int y, int w, uint64_t nowMs) {
    // "Updating the game list..." over a thin bar: the share done, or a
    // sliding segment while there's no measure yet.
    if (!loading_) return;
    std::string text = tr(Str::UpdatingList);
    if (loadingTotal_) text += "  " + std::to_string(loadingDone_ * 100 / loadingTotal_) + "%";
    fonts_.semibold.draw(c, text, x, y, 20, kGray);
    Rect track{x, y + 32, w, 6};
    c.fillRect(track, rgba(255, 255, 255, 40), 3);
    if (loadingTotal_) {
        int fill = static_cast<int>(static_cast<double>(w) * loadingDone_ / loadingTotal_);
        if (fill > 0) c.fillRect({x, track.y, fill, track.h}, kGreen, 3);
    } else {
        int seg = w / 4;
        int pos = static_cast<int>((nowMs % 1400) * static_cast<uint64_t>(w + seg) / 1400) - seg;
        int a = std::max(x, x + pos), b = std::min(x + w, x + pos + seg);
        if (b > a) c.fillRect({a, track.y, b - a, track.h}, kGreen, 3);
    }
    animating_ = true;
}

void AppUi::setPrefs(const std::vector<std::string>& hidden, LibrarySort sort, const std::vector<std::string>& favorites, const std::vector<std::string>& wishlist) {
    std::lock_guard<std::mutex> lock(mutex_);
    hidden_ = std::set<std::string>(hidden.begin(), hidden.end());
    favorites_ = std::set<std::string>(favorites.begin(), favorites.end());
    wishlist_ = std::set<std::string>(wishlist.begin(), wishlist.end());
    librarySort_ = sort;
    rebuild(true);
}

void AppUi::setPlaytime(int64_t remainingSeconds, int64_t maxSeconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    playtimeRemaining_ = remainingSeconds;
    playtimeMax_ = maxSeconds;
    dirty_ = true;
}

void AppUi::prefsEvent(UiEvent& ev) const {
    // Caller holds mutex_.
    ev.action = Action::PrefsChanged;
    ev.hidden.assign(hidden_.begin(), hidden_.end());
    ev.favorites.assign(favorites_.begin(), favorites_.end());
    ev.wishlist.assign(wishlist_.begin(), wishlist_.end());
    ev.librarySort = librarySort_;
}

void AppUi::toggleHidden(const GameTile& tile, UiEvent& ev) {
    // Caller holds mutex_.
    if (tile.productId.empty()) return;
    bool hide = !hidden_.count(tile.productId);
    if (hide) hidden_.insert(tile.productId);
    else hidden_.erase(tile.productId);
    if (!caps_.console) {
        toast_ = tr(hide ? Str::HiddenToast : Str::UnhiddenToast);
        toastUntil_ = 0;
    }
    rebuild(true);  // the cursor stays put; the next game takes the place
    prefsEvent(ev);
}

void AppUi::toggleFavorite(const GameTile& tile, UiEvent& ev) {
    // Caller holds mutex_.
    if (tile.productId.empty()) return;
    bool fav = !favorites_.count(tile.productId);
    if (fav) favorites_.insert(tile.productId);
    else favorites_.erase(tile.productId);
    if (!caps_.console) {
        toast_ = tr(fav ? Str::AddedToFavorites : Str::RemovedFromFavorites);
        toastUntil_ = 0;
    }
    prefsEvent(ev);
    dirty_ = true;
}

bool AppUi::isAcquired(const GameTile& t) const {
    if (!t.productId.empty()) {
        for (const auto& o : allOwned_)
            if (o.productId == t.productId) return true;
    }
    if (!t.titleId.empty()) {
        for (const auto& o : allOwned_)
            if (o.titleId == t.titleId) return true;
    }
    return false;
}

bool AppUi::isFreeGame(const GameTile& t) const {
    if (unclaimable_.count(t.productId)) return false;
    if (t.freeInStore) return true;
    auto price = prices_.find(t.productId);
    if (price != prices_.end()) {
        if (price->second.list < 0.005 && price->second.msrp < 0.005) {
            return true;
        }
    }
    return false;
}

void AppUi::toggleWishlist(const GameTile& tile, UiEvent& ev) {
    // Caller holds mutex_.
    if (tile.productId.empty()) return;
    if (isAcquired(tile)) return;  // Only unacquired games can be added to the wishlist
    bool wish = !wishlist_.count(tile.productId);
    if (wish) wishlist_.insert(tile.productId);
    else wishlist_.erase(tile.productId);
    if (!caps_.console) {
        toast_ = tr(wish ? Str::AddedToWishlist : Str::RemovedFromWishlist);
        toastUntil_ = 0;
    }
    rebuild();
    prefsEvent(ev);
    dirty_ = true;
}

void AppUi::setSearchPools(std::vector<GameTile> gamePass, std::vector<GameTile> library) {
    std::lock_guard<std::mutex> lock(mutex_);
    gamePassPool_ = std::move(gamePass);
    libraryPool_ = std::move(library);
    rebuild(true);  // hidden games may be in the pools only; keep position in search and library
}

void AppUi::setRegionLatency(std::map<std::string, int> ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    regionMs_ = std::move(ms);
    dirty_ = true;
}

void AppUi::setPrices(const std::map<std::string, PriceInfo>& prices) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, p] : prices) prices_[id] = p;
    // A list ordered or filtered by price fills in as prices arrive (not
    // while the user moves through it).
    if (searching_ && tab_ == Tab::Library && (filterCheapest_ || filterFree_ || filterWishlist_))
        runSearch(true);
    dirty_ = true;
}

void AppUi::pricesFailed(const std::vector<std::string>& productIds) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& id : productIds) pricesAsked_.erase(id);
}

std::vector<std::string> AppUi::pricesWanted(size_t max, bool background) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    auto want = [&](const GameTile& t) {
        if (out.size() < max && t.purchasable && !prices_.count(t.productId) && pricesAsked_.insert(t.productId).second)
            out.push_back(t.productId);
    };
    if (screen_ == Screen::Details) want(detail_);
    if (screen_ == Screen::Streaming || screen_ == Screen::Launching) return out;
    if (screen_ != Screen::Home && screen_ != Screen::Details) return out;
    if (searching_) {
        for (const auto& t : results_) want(t);
    } else if (tab_ == Tab::Library && !purchasable_.empty()) {
        // The rows on screen and the two below.
        const LibraryLayout& L = libraryLayout();
        float top = gridScroll_.target - kGridPitchY, bottom = gridScroll_.target + kH + 2 * kGridPitchY;
        for (size_t i = owned_.size(); i < L.rowOf.size(); ++i) {
            int y = L.rowY[static_cast<size_t>(L.rowOf[i])];
            if (y >= top && y <= bottom && i - owned_.size() < purchasable_.size())
                want(purchasable_[i - owned_.size()]);
        }
    }
    // The rest, for the price filters and orders.
    if (background && out.empty())
        for (const auto& t : allPurchasable_) want(t);
    return out;
}

std::string AppUi::detailWanted(uint64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (screen_ != Screen::Details) return {};
    std::string pid = detail_.productId;
    if (pid.empty() && !detail_.titleId.empty()) {
        for (const auto* list : {&gamePassPool_, &libraryPool_, &allOwned_, &allPurchasable_}) {
            for (const auto& t : *list) {
                if (!t.productId.empty() && t.titleId == detail_.titleId) {
                    pid = t.productId;
                    break;
                }
            }
            if (!pid.empty()) break;
        }
        if (pid.empty()) {
            for (const auto& row : allRows_) {
                for (const auto& t : row.tiles) {
                    if (!t.productId.empty() && t.titleId == detail_.titleId) {
                        pid = t.productId;
                        break;
                    }
                }
                if (!pid.empty()) break;
            }
        }
        if (!pid.empty()) detail_.productId = pid;
    }
    if (pid.empty()) return {};
    if (detailInfo_.find(pid) != detailInfo_.end()) return {};
    if (!detail_.description.empty() && !detail_.heroUrl.empty() && !detail_.ratingAge.empty()) return {};
    if (pid == detailAskedFor_ && nowMs - detailAskedAt_ < 5000) return {};
    detailAskedFor_ = pid;
    detailAskedAt_ = nowMs;
    return pid;
}

std::string AppUi::heroWanted(uint64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (screen_ != Screen::Home || searching_ || tab_ != Tab::GamePass) return {};
    const GameTile* focus = focusedTile();
    if (!focus || !focus->heroUrl.empty()) return {};

    std::string pid = focus->productId;
    if (pid.empty() && !focus->titleId.empty()) {
        for (const auto* list : {&gamePassPool_, &libraryPool_, &allOwned_, &allPurchasable_}) {
            for (const auto& t : *list) {
                if (!t.productId.empty() && t.titleId == focus->titleId) {
                    pid = t.productId;
                    break;
                }
            }
            if (!pid.empty()) break;
        }
        if (pid.empty()) {
            for (const auto& row : allRows_) {
                for (const auto& t : row.tiles) {
                    if (!t.productId.empty() && t.titleId == focus->titleId) {
                        pid = t.productId;
                        break;
                    }
                }
                if (!pid.empty()) break;
            }
        }
        if (!pid.empty()) {
            if (focusRow_ >= 0 && focusRow_ < static_cast<int>(rows_.size())) {
                int col = focusCol_[static_cast<size_t>(focusRow_)];
                if (col >= 0 && col < static_cast<int>(rows_[static_cast<size_t>(focusRow_)].tiles.size())) {
                    rows_[static_cast<size_t>(focusRow_)].tiles[static_cast<size_t>(col)].productId = pid;
                }
            }
            if (focusRow_ >= 0 && focusRow_ < static_cast<int>(allRows_.size())) {
                int col = focusCol_[static_cast<size_t>(focusRow_)];
                if (col >= 0 && col < static_cast<int>(allRows_[static_cast<size_t>(focusRow_)].tiles.size())) {
                    allRows_[static_cast<size_t>(focusRow_)].tiles[static_cast<size_t>(col)].productId = pid;
                }
            }
        }
    }
    if (pid.empty()) return {};

    auto it = detailInfo_.find(pid);
    if (it != detailInfo_.end() && !it->second.heroUrl.empty()) return {};
    if (pid == heroAskedFor_ && nowMs - heroAskedAt_ < 5000) return {};
    heroAskedFor_ = pid;
    heroAskedAt_ = nowMs;
    return pid;
}

std::string AppUi::queueWanted(uint64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (screen_ != Screen::Details || !detail_.playable || detail_.purchasable || detail_.titleId.empty()) return {};
    if (detail_.titleId == queueAskedFor_ && nowMs - queueAskedAt_ < 60000) return {};
    queueAskedFor_ = detail_.titleId;
    queueAskedAt_ = nowMs;
    return detail_.titleId;
}

void AppUi::setQueueTime(const std::string& titleId, int seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    queueTitle_ = titleId;
    queueSeconds_ = seconds;
    if (detail_.titleId == titleId) dirty_ = true;
}

std::string AppUi::achievementsWanted(uint64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (screen_ != Screen::Details || detail_.xboxTitleId.empty()) return {};
    if (detail_.xboxTitleId == achievementsAskedFor_ && nowMs - achievementsAskedAt_ < 60000) return {};
    achievementsAskedFor_ = detail_.xboxTitleId;
    achievementsAskedAt_ = nowMs;
    return detail_.xboxTitleId;
}

void AppUi::setTitleAchievements(const std::string& xboxTitleId, const xcloud::TitleAchievements& ach) {
    std::lock_guard<std::mutex> lock(mutex_);
    achievements_[xboxTitleId] = ach;
    if (detail_.xboxTitleId == xboxTitleId) dirty_ = true;
}

void AppUi::openAchievements() {
    // Caller holds mutex_.
    achievementsOpen_ = true;
    achScroll_ = 0;
    dirty_ = true;
}

std::string AppUi::tileName(const std::string& productId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& row : rows_) {
        for (const auto& t : row.tiles) {
            if (t.productId == productId) return t.name;
        }
    }
    for (const auto* list : {&owned_, &purchasable_, &gamePassPool_, &libraryPool_}) {
        for (const auto& t : *list) {
            if (t.productId == productId) return t.name;
        }
    }
    return {};
}

void AppUi::syncHeroToRowsLocked(const std::string& productId, const std::string& titleId, const std::string& heroUrl,
                                 const std::string& description, const std::string& publisher,
                                 const std::vector<std::string>& categories) {
    // Caller holds mutex_.
    if (productId.empty() && titleId.empty()) return;
    auto updateTile = [&](GameTile& t) {
        bool match = (!productId.empty() && t.productId == productId) ||
                     (!titleId.empty() && t.titleId == titleId && (t.productId.empty() || productId.empty()));
        if (match) {
            if (t.productId.empty() && !productId.empty()) t.productId = productId;
            if (t.titleId.empty() && !titleId.empty()) t.titleId = titleId;
            if (!heroUrl.empty()) t.heroUrl = heroUrl;
            if (!description.empty() && t.description.empty()) t.description = description;
            if (!publisher.empty() && t.publisher.empty()) t.publisher = publisher;
            if (!categories.empty() && t.categories.empty()) t.categories = categories;
        }
    };
    for (auto& row : rows_) {
        for (auto& t : row.tiles) updateTile(t);
    }
    for (auto& row : allRows_) {
        for (auto& t : row.tiles) updateTile(t);
    }
    for (auto* list : {&owned_, &purchasable_, &allOwned_, &allPurchasable_, &gamePassPool_, &libraryPool_, &results_}) {
        for (auto& t : *list) updateTile(t);
    }
}

void AppUi::setDetailInfo(const std::string& productId, const std::string& description, const std::string& publisher,
                          const std::vector<std::string>& categories, const std::string& heroUrl,
                          const std::string& ratingAge, const std::string& ratingSystem,
                          const std::vector<std::string>& ratingDescriptors) {
    std::lock_guard<std::mutex> lock(mutex_);
    detailInfo_[productId] = {description, publisher, heroUrl, categories, ratingAge, ratingSystem, ratingDescriptors};
    std::string matchingTitleId;
    if (detail_.productId == productId || (!detail_.titleId.empty() && !productId.empty() && detail_.productId.empty())) {
        if (detail_.productId.empty()) detail_.productId = productId;
        if (!description.empty()) detail_.description = description;
        if (!publisher.empty()) detail_.publisher = publisher;
        if (!categories.empty()) detail_.categories = categories;
        if (!heroUrl.empty()) detail_.heroUrl = heroUrl;
        if (!ratingAge.empty()) detail_.ratingAge = ratingAge;
        if (!ratingSystem.empty()) detail_.ratingSystem = ratingSystem;
        if (!ratingDescriptors.empty()) detail_.ratingDescriptors = ratingDescriptors;
        matchingTitleId = detail_.titleId;
    }
    syncHeroToRowsLocked(productId, matchingTitleId, heroUrl, description, publisher, categories);
    if (!ratingAge.empty() || !ratingSystem.empty() || !ratingDescriptors.empty()) {
        auto updateRating = [&](GameTile& t) {
            if (t.productId == productId || (!matchingTitleId.empty() && t.titleId == matchingTitleId && t.productId.empty())) {
                if (!ratingAge.empty() && t.ratingAge.empty()) t.ratingAge = ratingAge;
                if (!ratingSystem.empty() && t.ratingSystem.empty()) t.ratingSystem = ratingSystem;
                if (!ratingDescriptors.empty() && t.ratingDescriptors.empty()) t.ratingDescriptors = ratingDescriptors;
            }
        };
        for (auto& row : rows_) for (auto& t : row.tiles) updateRating(t);
        for (auto& row : allRows_) for (auto& t : row.tiles) updateRating(t);
        for (auto* list : {&owned_, &purchasable_, &allOwned_, &allPurchasable_, &gamePassPool_, &libraryPool_, &results_}) {
            for (auto& t : *list) updateRating(t);
        }
    }
    if (searching_ && (filterMode_ || filterLanguage_)) runSearch(true);
    dirty_ = true;
}

bool AppUi::findTile(const std::string& titleId, GameTile& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<const GameTile*> all;
    for (const auto& row : rows_)
        for (const auto& t : row.tiles) all.push_back(&t);
    for (const auto& row : allRows_)
        for (const auto& t : row.tiles) all.push_back(&t);
    for (const auto* list : {&owned_, &purchasable_, &allOwned_, &allPurchasable_, &gamePassPool_, &libraryPool_})
        for (const auto& t : *list) all.push_back(&t);
    for (const GameTile* t : all) {
        if (t->titleId == titleId || t->productId == titleId || t->name == titleId) {
            out = *t;
            return true;
        }
    }
    return false;
}

bool AppUi::purchasableAt(size_t index, GameTile& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= purchasable_.size()) return false;
    out = purchasable_[index];
    return true;
}

bool AppUi::firstTile(GameTile& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& row : rows_) {
        if (!row.tiles.empty()) {
            out = row.tiles.front();
            return true;
        }
    }
    if (!owned_.empty()) {
        out = owned_.front();
        return true;
    }
    if (!purchasable_.empty()) {
        out = purchasable_.front();
        return true;
    }
    return false;
}

void AppUi::showDetails(const GameTile& tile) {
    std::lock_guard<std::mutex> lock(mutex_);
    openDetails(tile);
}

const GameTile* AppUi::libraryTile(int index) const {
    if (index < 0) return nullptr;
    size_t i = static_cast<size_t>(index);
    if (i < owned_.size()) return &owned_[i];
    i -= owned_.size();
    if (i < purchasable_.size()) return &purchasable_[i];
    i -= purchasable_.size();
    return i < hiddenTiles_.size() ? &hiddenTiles_[i] : nullptr;
}

void AppUi::showFilteredSearch(Tab tab, bool cheapest, int mode, const std::string& genre, int language, int openList) {
    std::lock_guard<std::mutex> lock(mutex_);
    screen_ = Screen::Home;
    tab_ = tab;
    searching_ = true;
    searchOnKeys_ = true;
    query_.clear();
    filterFree_ = false;
    filterWishlist_ = false;
    filterMouseKeyboard_ = false;
    filterCheapest_ = cheapest;
    filterPlan_ = 0;
    filterConsole_ = 0;
    filterMode_ = mode;
    filterGenre_ = genre;
    filterLanguage_ = language;
    filterPublisher_ = 0;
    runSearch();
    keyRow_ = kKeyRows + 2;  // row 1 of filters
    keyCol_ = std::max(0, openList);
    if (openList >= 0) {
        Filter targets[] = {Filter::Mode, Filter::Genre, Filter::Publisher, Filter::Language};
        if (static_cast<size_t>(openList) < sizeof(targets) / sizeof(targets[0])) {
            Filter want = targets[openList];
            bool found = false;
            for (int r = 0; r < 3 && !found; ++r) {
                auto chips = filterRow(r);
                for (size_t c = 0; c < chips.size(); ++c) {
                    if (chips[c] == want) {
                        keyRow_ = kKeyRows + 1 + r;
                        keyCol_ = static_cast<int>(c);
                        pressFilter(want);
                        found = true;
                        break;
                    }
                }
            }
        }
    }
    dirty_ = true;
}

std::vector<std::pair<std::string, PriceInfo>> AppUi::searchResults(size_t max) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, PriceInfo>> out;
    for (size_t i = 0; i < results_.size() && i < max; ++i) {
        auto p = prices_.find(results_[i].productId);
        out.push_back({results_[i].name, p == prices_.end() ? PriceInfo{} : p->second});
    }
    return out;
}

std::string AppUi::focusDescription() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tab_ == Tab::GamePass)
        return "Game Pass row " + std::to_string(focusRow_) + " card " +
               std::to_string(focusCol_.empty() ? 0 : focusCol_[static_cast<size_t>(focusRow_)]);
    if (tab_ == Tab::Library) {
        const LibraryLayout& L = libraryLayout();
        int f = gridFocus_;
        if (f >= 0 && f < static_cast<int>(L.rowOf.size()))
            return "My games tile " + std::to_string(f) + " (row " + std::to_string(L.rowOf[static_cast<size_t>(f)]) + ")";
        return "My games tile " + std::to_string(f);
    }
    return "consoles";
}

void AppUi::showTab(Tab t) {
    std::lock_guard<std::mutex> lock(mutex_);
    tab_ = t;
    searching_ = false;
    dirty_ = true;
}

Tab AppUi::tab() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tab_;
}

void AppUi::openDetails(const GameTile& tile) {
    // Caller holds mutex_.
    detail_ = tile;
    if (detail_.productId.empty() && !detail_.titleId.empty()) {
        for (const auto* list : {&gamePassPool_, &libraryPool_, &allOwned_, &allPurchasable_}) {
            for (const auto& t : *list) {
                if (!t.productId.empty() && t.titleId == detail_.titleId) {
                    detail_.productId = t.productId;
                    break;
                }
            }
            if (!detail_.productId.empty()) break;
        }
        if (detail_.productId.empty()) {
            for (const auto& row : allRows_) {
                for (const auto& t : row.tiles) {
                    if (!t.productId.empty() && t.titleId == detail_.titleId) {
                        detail_.productId = t.productId;
                        break;
                    }
                }
                if (!detail_.productId.empty()) break;
            }
        }
    }
    gameSettingsOpen_ = false;
    tester_ = Tester::None;
    detailButtonFocus_ = 0;
    playRect_ = {};
    achRect_ = {};
    claimRect_ = {};
    screen_ = Screen::Details;
    refreshDetail();
    dirty_ = true;
}

void AppUi::refreshDetail() {
    // Caller holds mutex_. Hero art and descriptions arrive after the page opened.
    if (screen_ != Screen::Details || (detail_.productId.empty() && detail_.titleId.empty())) return;
    bool changed = false;
    if (!detail_.productId.empty()) {
        auto info = detailInfo_.find(detail_.productId);
        if (info != detailInfo_.end()) {
            if (detail_.description.empty() && !info->second.description.empty()) {
                detail_.description = info->second.description;
                changed = true;
            }
            if (detail_.heroUrl.empty() && !info->second.heroUrl.empty()) {
                detail_.heroUrl = info->second.heroUrl;
                changed = true;
            }
            if (detail_.publisher.empty() && !info->second.publisher.empty()) {
                detail_.publisher = info->second.publisher;
                changed = true;
            }
            if (detail_.categories.empty() && !info->second.categories.empty()) {
                detail_.categories = info->second.categories;
                changed = true;
            }
            if (detail_.ratingAge.empty() && !info->second.ratingAge.empty()) {
                detail_.ratingAge = info->second.ratingAge;
                detail_.ratingSystem = info->second.ratingSystem;
                detail_.ratingDescriptors = info->second.ratingDescriptors;
                changed = true;
            }
        }
    }
    auto take = [&](const GameTile& t) {
        bool match = (!detail_.productId.empty() && t.productId == detail_.productId) ||
                     (!detail_.titleId.empty() && t.titleId == detail_.titleId && (detail_.productId.empty() || t.productId.empty()));
        if (match) {
            if (detail_.productId.empty() && !t.productId.empty()) {
                detail_.productId = t.productId;
                changed = true;
            }
            if (detail_.xboxTitleId.empty() && !t.xboxTitleId.empty()) {
                detail_.xboxTitleId = t.xboxTitleId;
                changed = true;
            }
            if (detail_.description.empty() && !t.description.empty()) {
                detail_.description = t.description;
                changed = true;
            } else if (!t.description.empty() && t.description.size() > detail_.description.size()) {
                detail_.description = t.description;
                changed = true;
            }
            if (detail_.heroUrl.empty() && !t.heroUrl.empty()) {
                detail_.heroUrl = t.heroUrl;
                changed = true;
            }
            if (detail_.publisher.empty() && !t.publisher.empty()) {
                detail_.publisher = t.publisher;
                changed = true;
            }
            if (detail_.categories.empty() && !t.categories.empty()) {
                detail_.categories = t.categories;
                changed = true;
            }
            if (detail_.ratingAge.empty() && !t.ratingAge.empty()) {
                detail_.ratingAge = t.ratingAge;
                detail_.ratingSystem = t.ratingSystem;
                detail_.ratingDescriptors = t.ratingDescriptors;
                changed = true;
            }
        }
    };
    for (const auto& row : rows_)
        for (const auto& t : row.tiles) take(t);
    for (const auto& row : allRows_)
        for (const auto& t : row.tiles) take(t);
    for (const auto* list : {&owned_, &purchasable_, &allOwned_, &allPurchasable_, &gamePassPool_, &libraryPool_})
        for (const auto& t : *list) take(t);
    if (changed) dirty_ = true;
}

void AppUi::showHome(const std::string& toast) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (screen_ == Screen::Details) {
        syncHeroToRowsLocked(detail_.productId, detail_.titleId, detail_.heroUrl, detail_.description, detail_.publisher, detail_.categories);
    }
    screen_ = Screen::Home;
    if (!caps_.console && !toast.empty()) {
        toast_ = toast;
        toastUntil_ = 0;  // set on first render
    }
    dirty_ = true;
}

void AppUi::showToast(const std::string& toast) {
    if (caps_.console) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!toast.empty()) {
        toast_ = toast;
        toastUntil_ = 0;
        dirty_ = true;
    }
}

void AppUi::markTileOwned(const std::string& productId, const std::string& titleId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (detail_.productId == productId || (!titleId.empty() && detail_.titleId == titleId)) {
        detail_.playable = true;
        detail_.purchasable = false;
        dirty_ = true;
    }
    auto updateTileFlags = [&](GameTile& t) {
        if ((!productId.empty() && t.productId == productId) || (!titleId.empty() && t.titleId == titleId)) {
            t.playable = true;
            t.purchasable = false;
        }
    };
    for (auto& row : rows_) {
        for (auto& t : row.tiles) updateTileFlags(t);
    }
    for (auto& row : allRows_) {
        for (auto& t : row.tiles) updateTileFlags(t);
    }
    for (auto* list : {&gamePassPool_, &libraryPool_, &results_}) {
        for (auto& t : *list) updateTileFlags(t);
    }

    GameTile acquiredTile;
    if ((!productId.empty() && detail_.productId == productId) || (!titleId.empty() && detail_.titleId == titleId)) {
        acquiredTile = detail_;
    }
    for (auto it = allPurchasable_.begin(); it != allPurchasable_.end(); ) {
        if ((!productId.empty() && it->productId == productId) || (!titleId.empty() && it->titleId == titleId)) {
            if (acquiredTile.productId.empty()) acquiredTile = *it;
            it = allPurchasable_.erase(it);
        } else {
            ++it;
        }
    }
    if (acquiredTile.productId.empty()) {
        for (const auto* list : {&results_, &libraryPool_, &gamePassPool_}) {
            for (const auto& t : *list) {
                if ((!productId.empty() && t.productId == productId) || (!titleId.empty() && t.titleId == titleId)) {
                    acquiredTile = t;
                    break;
                }
            }
            if (!acquiredTile.productId.empty()) break;
        }
    }
    if (!acquiredTile.productId.empty()) {
        acquiredTile.playable = true;
        acquiredTile.purchasable = false;
        for (auto it = allOwned_.begin(); it != allOwned_.end(); ++it) {
            if (it->productId == acquiredTile.productId || (!acquiredTile.titleId.empty() && it->titleId == acquiredTile.titleId)) {
                if (acquiredTile.name.empty()) acquiredTile.name = it->name;
                if (acquiredTile.heroUrl.empty()) acquiredTile.heroUrl = it->heroUrl;
                if (acquiredTile.tileUrl.empty()) acquiredTile.tileUrl = it->tileUrl;
                allOwned_.erase(it);
                break;
            }
        }
        allOwned_.insert(allOwned_.begin(), acquiredTile);
    }
    if (!productId.empty()) wishlist_.erase(productId);
    rebuild(true);
    dirty_ = true;
}

void AppUi::markUnclaimable(const std::string& productId) {
    std::lock_guard<std::mutex> lock(mutex_);
    unclaimable_.insert(productId);
    if (detail_.productId == productId) {
        detail_.freeInStore = false;
    }
    auto clearFree = [&](GameTile& t) {
        if (!productId.empty() && t.productId == productId) {
            t.freeInStore = false;
        }
    };
    for (auto& row : rows_) for (auto& t : row.tiles) clearFree(t);
    for (auto& row : allRows_) for (auto& t : row.tiles) clearFree(t);
    for (auto* list : {&owned_, &purchasable_, &allOwned_, &allPurchasable_, &gamePassPool_, &libraryPool_, &results_}) {
        for (auto& t : *list) clearFree(t);
    }
    dirty_ = true;
}

void AppUi::showLaunching(const GameTile& game, const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (screen_ != Screen::Launching) {
        launchingStartMs_ = 0;
    }
    screen_ = Screen::Launching;
    launching_ = game;
    status_ = status;
    dirty_ = true;
}

void AppUi::setLaunchStatus(const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = status;
    dirty_ = true;
}

void AppUi::showStreaming() {
    std::lock_guard<std::mutex> lock(mutex_);
    screen_ = Screen::Streaming;
}

void AppUi::showError(const std::string& message, const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    screen_ = Screen::Error;
    error_ = message;
    errorUrl_ = url;
    errorGame_ = {};
    loading_ = false;
    regionRefused_ = countryPickerOpen_ = false;
    dirty_ = true;
}

void AppUi::showRegionRefused(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    screen_ = Screen::Error;
    error_ = message;
    errorUrl_.clear();
    errorGame_ = {};
    loading_ = false;
    regionRefused_ = true;
    countryPickerOpen_ = false;
    dirty_ = true;
}

void AppUi::showPlayError(const std::string& message, const GameTile& game, const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    screen_ = Screen::Error;
    error_ = message;
    errorUrl_ = url;
    errorGame_ = game;
    loading_ = false;
    regionRefused_ = countryPickerOpen_ = false;
    dirty_ = true;
}

void AppUi::setUpdateState(const std::string& current, const std::string& available, bool checking, const std::string& notes) {
    std::lock_guard<std::mutex> lock(mutex_);
    updateCurrent_ = current;
    updateAvailable_ = available;
    updateChecking_ = checking;
    if (!notes.empty()) {
        updateNotes_ = notes;
        formatUpdateNotes();
    }
    dirty_ = true;
}

void AppUi::setUpdateNotes(const std::string& notes) {
    std::lock_guard<std::mutex> lock(mutex_);
    updateNotes_ = notes;
    formatUpdateNotes();
    dirty_ = true;
}

void AppUi::setSignInCheckState(const SignInCheckState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    signInCheck_ = state;
    dirty_ = true;
}

SignInCheckState AppUi::signInCheckState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return signInCheck_;
}

void AppUi::setNetworkTestState(const NetworkTestState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    networkTest_ = state;
    dirty_ = true;
}

NetworkTestState AppUi::networkTestState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return networkTest_;
}

void AppUi::offerUpdate(const std::string& notes) {
    std::lock_guard<std::mutex> lock(mutex_);
    updatePrompt_ = true;
    showingUpdateNotes_ = false;
    updateNotesScroll_ = 0;
    promptFocus_ = 0;
    if (!notes.empty()) updateNotes_ = notes;
    formatUpdateNotes();
    dirty_ = true;
}

bool AppUi::updateOffered() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return updatePrompt_ && screen_ == Screen::Home;
}

bool AppUi::showingUpdateNotes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return updatePrompt_ && showingUpdateNotes_;
}

void AppUi::showUpdateNotes(bool show) {
    std::lock_guard<std::mutex> lock(mutex_);
    showingUpdateNotes_ = show;
    updateNotesScroll_ = 0;
    if (show) formatUpdateNotes();
    dirty_ = true;
}

void AppUi::showUpdating(const std::string& version) {
    std::lock_guard<std::mutex> lock(mutex_);
    screen_ = Screen::Updating;
    updatePrompt_ = false;
    updatingTo_ = version;
    updateStatus_.clear();
    updateFraction_ = -1;
    dirty_ = true;
}

void AppUi::setUpdateStatus(const std::string& status, float fraction) {
    std::lock_guard<std::mutex> lock(mutex_);
    updateStatus_ = status;
    updateFraction_ = fraction;
    dirty_ = true;
}

void AppUi::setSettings(const SettingsChoice& choice) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = choice;
    gyroMode_ = choice.gyroMode;
    gyroLevel_ = choice.gyroLevel;
    dirty_ = true;
}

void AppUi::setFullscreen(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (settings_.fullscreen == on) return;
    settings_.fullscreen = on;
    dirty_ = true;
}

SettingsChoice AppUi::settings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_;
}

int AppUi::settingsFocus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settingsRow_;
}

void AppUi::setWebUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    webUrl_ = url;
    dirty_ = true;
}

std::string AppUi::webUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return webUrl_;
}

void AppUi::setCircleConfirms(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    circleConfirms_ = on;
    dirty_ = true;
}

bool AppUi::circleConfirms() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return circleConfirms_;
}

void AppUi::setPads(const PadSlots& pads) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool changed = false;
    for (size_t i = 0; i < pads.size(); ++i)
        changed |= pads[i].connected != pads_[i].connected || pads[i].name != pads_[i].name ||
                   pads[i].keyboard != pads_[i].keyboard || pads[i].device != pads_[i].device;
    if (!changed) return;
    pads_ = pads;
    dirty_ = true;
}

bool AppUi::accentColor(Color& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    const GameTile* t = nullptr;
    switch (screen_) {
    case Screen::Details: t = &detail_; break;
    case Screen::Launching:
    case Screen::Streaming: t = &launching_; break;
    case Screen::Home:
        if (searching_) {
            if (!searchOnKeys_ && resultFocus_ >= 0 && resultFocus_ < static_cast<int>(results_.size()))
                t = &results_[static_cast<size_t>(resultFocus_)];
        } else {
            t = tab_ == Tab::Library ? libraryTile(gridFocus_) : tab_ == Tab::GamePass ? focusedTile() : nullptr;
        }
        break;
    default: break;
    }
    if (!t || t->tileUrl.empty()) return false;
    auto it = accents_.find(t->tileUrl);
    if (it == accents_.end()) {
        auto img = images_.get(t->tileUrl, kCard, kCard);  // the card's own, already loaded
        if (!img) return false;
        Color c;
        // No colour in the art (black and white): the Xbox green.
        if (!dominantColor(*img, c)) c = rgba(40, 210, 40);
        it = accents_.emplace(t->tileUrl, c).first;
    }
    out = it->second;
    return true;
}

Screen AppUi::screen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return screen_;
}

void AppUi::invalidate() {
    std::lock_guard<std::mutex> lock(mutex_);
    dirty_ = true;
}

void AppUi::setCaps(const UiCaps& caps) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (caps == caps_) return;
    caps_ = caps;
    dirty_ = true;
    // A pad without what a row is for came in (or the one with it left): the
    // focus goes to the row after it, and what the row had open closes.
    const auto rows = visibleSettingsRows(caps_);
    auto shown = [&](int row) {
        return std::any_of(rows.begin(), rows.end(), [&](const SettingsRowPos& r) { return r.row == row; });
    };
    if (!shown(settingsRow_)) {
        int pos = 0, next = -1;
        for (int i = 0; i < kSettingsCount; ++i)
            if (kSettingsOrder[i] == settingsRow_) pos = i;
        for (int i = pos; i < kSettingsCount && next < 0; ++i)
            if (shown(kSettingsOrder[i])) next = kSettingsOrder[i];
        for (int i = pos; i >= 0 && next < 0; --i)
            if (shown(kSettingsOrder[i])) next = kSettingsOrder[i];
        settingsRow_ = next < 0 ? 0 : next;  // the language row is always there
        dropdownOpen_ = pickerOpen_ = false;
        if (tester_ == Tester::Triggers && !gameSettingsOpen_) tester_ = Tester::None, settings_ = testerBackup_;
    }
    if (tester_ == Tester::Triggers && !caps_.triggerRumble && !caps_.adaptiveTriggers) {
        if (gameSettingsOpen_) settings_ = testerBackup_;
        tester_ = Tester::None;
    }
    testerRow_ = std::min(testerRow_, triggerTesterRows() - 1);
    int gameRows = static_cast<int>(visibleGameSettingsRows(caps_, activeGameHasCustom_, activeGameMouseSupported_).size());
    gameSettingsRow_ = std::min(gameSettingsRow_, gameRows - 1);
}

UiCaps AppUi::caps() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return caps_;
}

const char* AppUi::loginCountryOffText() const {
    return tr(caps_.console ? Str::LoginCountryOff : Str::LoginCountryOffPc);
}

const GameTile* AppUi::focusedTile() const {
    if (focusRow_ < 0 || focusRow_ >= static_cast<int>(rows_.size())) return nullptr;
    const auto& row = rows_[static_cast<size_t>(focusRow_)];
    int col = focusCol_[static_cast<size_t>(focusRow_)];
    if (col < 0 || col >= static_cast<int>(row.tiles.size())) return nullptr;
    return &row.tiles[static_cast<size_t>(col)];
}

void AppUi::retarget() {
    if (focusRow_ < 0 || focusRow_ >= static_cast<int>(rows_.size())) return;
    const GameRow& curRow = rows_[static_cast<size_t>(focusRow_)];

    if (curRow.isGrid) {
        constexpr float kGridShift = static_cast<float>(kRowTop - 170) / static_cast<float>(kRowPitch);
        rowY_.target = static_cast<float>(focusRow_) + kGridShift;

        int gridRow = focusCol_[static_cast<size_t>(focusRow_)] / kLibraryCols;
        rowScroll_[static_cast<size_t>(focusRow_)].target = static_cast<float>(std::max(0, gridRow - 1) * kGridPitchY);
    } else {
        rowY_.target = static_cast<float>(focusRow_);
        const float span = kW - 2 * kMargin - kCard;
        for (size_t r = 0; r < rows_.size(); ++r) {
            if (rows_[r].isGrid) continue;
            float left = static_cast<float>(focusCol_[r] * kCardPitch);
            Anim& a = rowScroll_[r];
            if (left - a.target > span) a.target = left - span;
            if (left < a.target) a.target = left;
        }
    }
}

const AppUi::LibraryLayout& AppUi::libraryLayout() const {
    // Caller holds mutex_. The account's games, then (after headers) the
    // games to buy and the hidden games, all kLibraryCols wide.
    if (libLayoutValid_) return cachedLibLayout_;
    LibraryLayout L;
    L.cols = kLibraryCols;
    size_t totalTiles = owned_.size() + purchasable_.size() + hiddenTiles_.size();
    L.rowOf.reserve(totalTiles);
    L.colOf.reserve(totalTiles);
    int y = 0, row = 0, index = 0;
    for (int section = 0; section < 3; ++section) {
        const auto& list = section == 0 ? owned_ : section == 1 ? purchasable_ : hiddenTiles_;
        if (list.empty()) continue;
        if (row > 0) y += 30;
        L.headerY[section] = y;
        y += kSectionHeaderH;
        for (size_t i = 0; i < list.size(); ++i, ++index) {
            int col = static_cast<int>(i) % L.cols;
            if (col == 0) {
                L.rowY.push_back(y + static_cast<int>(i) / L.cols * kGridPitchY);
                L.rowFirst.push_back(index);
                ++row;
            }
            L.rowOf.push_back(row - 1);
            L.colOf.push_back(col);
        }
        y = L.rowY.back() + kGridPitchY;
    }
    cachedLibLayout_ = std::move(L);
    libLayoutValid_ = true;
    return cachedLibLayout_;
}

}  // namespace xc::ui
