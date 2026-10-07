// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/app_ui.h"

#include "ui/brand.h"
#include "ui/strings.h"

#include "qrcodegen.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace xc::ui {

namespace {

// A padlock centred on (cx, cy), about 24x30 px: a game the account can't play.
void drawLock(Canvas& c, int cx, int cy) {
    const Color white = rgba(255, 255, 255, 230);
    c.strokeArc(static_cast<float>(cx), static_cast<float>(cy - 4), 8, 3.5f, 3.1416f, 3.1416f, white);
    c.line(cx - 8.0f, cy - 4.0f, cx - 8.0f, cy + 1.0f, 3.5f, white);
    c.line(cx + 8.0f, cy - 4.0f, cx + 8.0f, cy + 1.0f, 3.5f, white);
    c.fillRect({cx - 12, cy, 24, 18}, white, 4);
}

constexpr int kW = 1920, kH = 1080;
constexpr int kMargin = 96;
constexpr int kCard = 240;
constexpr int kCardGap = 24;
constexpr int kCardPitch = kCard + kCardGap;
constexpr int kRowPitch = 340;
constexpr int kRowTop = 470;  // y of the focused row's title
constexpr uint64_t kHeroFadeMs = 300;

constexpr Color kBg = rgba(14, 14, 14);
constexpr Color kWhite = rgba(255, 255, 255);
constexpr Color kGray = rgba(190, 190, 190);
constexpr Color kDim = rgba(140, 140, 140);
constexpr Color kGreen = rgba(16, 124, 16);
constexpr Color kPanel = rgba(36, 36, 36);
constexpr Color kPlaceholder = rgba(44, 44, 44);

enum Icon { kIconCross, kIconCircle, kIconOptions, kIconTriangle, kIconTouchpad, kIconSquare, kIconR3, kIconL2R2 };

// The resolution list, lowest first: SettingsChoice::resolution value per
// position (0 = 1080p, 1 = 720p, 2 = 1440p, as saved).
constexpr int kResolutionOrder[3] = {1, 0, 2};

// Grids ("Your games", search results): cards with the name below.
constexpr int kGridPitchY = 300;
constexpr int kLibraryCols = 6;
constexpr int kResultCols = 4;
constexpr int kGridTop = 236;       // y of the first card row (search results)
constexpr int kLibraryTop = 160;    // y where "Your games" starts (its first heading)
constexpr int kResultsX = 760;
constexpr int kSectionHeaderH = 96;  // "Available to buy" heading in the grid
constexpr size_t kMaxResults = 60;
constexpr size_t kMaxFilterResults = 300;  // filters with no words: lists, not lookups

// The search keyboard: 6 x 6 letters and digits, then Space / Delete / Clear.
constexpr const char* kKeys = "abcdefghijklmnopqrstuvwxyz0123456789";
constexpr int kKeyCols = 6, kKeyRows = 6;  // + the row of wide keys
constexpr int kKeyW = 90, kKeyH = 62, kKeyGap = 10;
constexpr int kKeysY = 262;
constexpr int kWideCols[3] = {0, 3, 5};  // first column of Space, Delete, Clear
constexpr int kWideSpan[3] = {3, 2, 1};

// Lower case, accents dropped (Latin-1 range), anything else a space: what
// the search compares.
std::string fold(const std::string& s) {
    static const char kLatin1[] = "aaaaaaaceeeeiiiidnooooo ouuuuytsaaaaaaaceeeeiiiidnooooo ouuuuyty";
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = ' ';
        size_t n = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            n = 2;
        } else if ((c & 0xF0) == 0xE0) {
            n = 3;
        } else if ((c & 0xF8) == 0xF0) {
            n = 4;
        }
        i += n;
        char ch = ' ';
        if (cp < 0x80 && std::isalnum(static_cast<int>(cp))) ch = static_cast<char>(std::tolower(static_cast<int>(cp)));
        else if (cp >= 0xC0 && cp <= 0xFF) ch = kLatin1[cp - 0xC0];
        out += ch;
    }
    return out;
}

// "164 games", "1 game".
std::string gamesCount(size_t n) { return n == 1 ? std::string(tr(Str::OneGame)) : trf(Str::GamesCount, std::to_string(n)); }

std::string withoutSpaces(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    return s;
}

constexpr uint64_t kSignOutHoldMs = 5000;

}  // namespace

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

void AppUi::setProfile(const std::string& gamertag, const std::string& gamerpicUrl) {
    std::lock_guard<std::mutex> lock(mutex_);
    gamertag_ = gamertag;
    gamerpicUrl_ = gamerpicUrl;
    dirty_ = true;
}

void AppUi::setRows(std::vector<GameRow> rows) {
    std::lock_guard<std::mutex> lock(mutex_);
    allRows_ = std::move(rows);
    rebuild();
}

void AppUi::setRowsLocked(std::vector<GameRow> rows) {
    // Caller holds mutex_. Remember, per row title, which game had the focus
    // and the scroll.
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
    rebuild();
}

namespace {

int consoleRank(const std::string& platform) {
    return platform == "XS" ? 0 : platform == "ONE" ? 1 : platform == "360" ? 2 : 3;
}

}  // namespace

void AppUi::rebuild(bool keepPosition) {
    // Caller holds mutex_.
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
        for (const auto& t : in)
            if (!hidden_.count(t.productId)) out.push_back(t);
        return out;
    };
    // "Recent" keeps the order received: recently played first for the
    // account's games, most popular first for the games to buy.
    auto byName = [](const GameTile& a, const GameTile& b) { return fold(a.name) < fold(b.name); };
    auto sorted = [&](std::vector<GameTile> v) {
        if (librarySort_ == LibrarySort::AZ) {
            std::stable_sort(v.begin(), v.end(), byName);
        } else if (librarySort_ == LibrarySort::Console) {
            std::stable_sort(v.begin(), v.end(), [&](const GameTile& a, const GameTile& b) {
                int ra = consoleRank(a.platform), rb = consoleRank(b.platform);
                return ra != rb ? ra < rb : byName(a, b);
            });
        }
        return v;
    };
    owned_ = sorted(visible(allOwned_));
    purchasable_ = sorted(visible(allPurchasable_));
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
    std::stable_sort(hiddenTiles_.begin(), hiddenTiles_.end(), byName);
    GameRow hiddenRow;
    hiddenRow.title = tr(Str::HiddenSection);
    hiddenRow.gamePassBadges = true;
    for (const auto& row : allRows_)
        for (const auto& t : row.tiles) collect(hiddenRow.tiles, t);
    for (const auto& t : gamePassPool_) collect(hiddenRow.tiles, t);
    std::stable_sort(hiddenRow.tiles.begin(), hiddenRow.tiles.end(), byName);

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
    if (searching_) runSearch();
    refreshDetail();
    dirty_ = true;
}

void AppUi::setPrefs(const std::vector<std::string>& hidden, LibrarySort sort) {
    std::lock_guard<std::mutex> lock(mutex_);
    hidden_ = std::set<std::string>(hidden.begin(), hidden.end());
    librarySort_ = sort;
    rebuild();
}

void AppUi::prefsEvent(UiEvent& ev) const {
    // Caller holds mutex_.
    ev.action = Action::PrefsChanged;
    ev.hidden.assign(hidden_.begin(), hidden_.end());
    ev.librarySort = librarySort_;
}

void AppUi::toggleHidden(const GameTile& tile, UiEvent& ev) {
    // Caller holds mutex_.
    if (tile.productId.empty()) return;
    bool hide = !hidden_.count(tile.productId);
    if (hide) hidden_.insert(tile.productId);
    else hidden_.erase(tile.productId);
    toast_ = tr(hide ? Str::HiddenToast : Str::UnhiddenToast);
    toastUntil_ = 0;
    rebuild(true);  // the cursor stays put; the next game takes the place
    prefsEvent(ev);
}

void AppUi::setSearchPools(std::vector<GameTile> gamePass, std::vector<GameTile> library) {
    std::lock_guard<std::mutex> lock(mutex_);
    gamePassPool_ = std::move(gamePass);
    libraryPool_ = std::move(library);
    int keep = resultFocus_;
    rebuild();  // hidden games may be in the pools only
    if (searching_) resultFocus_ = std::min(keep, std::max(0, static_cast<int>(results_.size()) - 1));
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
    if (searching_ && searchOnKeys_ && tab_ == Tab::Library && (filterCheapest_ || filterSale_ || filterFree_))
        runSearch();
    dirty_ = true;
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
        LibraryLayout L = libraryLayout();
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
    if (screen_ != Screen::Details || detail_.productId.empty()) return {};
    if (!detail_.description.empty() && !detail_.heroUrl.empty()) return {};
    if (detail_.productId == detailAskedFor_ && nowMs - detailAskedAt_ < 5000) return {};
    detailAskedFor_ = detail_.productId;
    detailAskedAt_ = nowMs;
    return detail_.productId;
}

void AppUi::setDetailInfo(const std::string& productId, const std::string& description, const std::string& publisher,
                          const std::vector<std::string>& categories, const std::string& heroUrl) {
    std::lock_guard<std::mutex> lock(mutex_);
    detailInfo_[productId] = {description, publisher, heroUrl, categories};
    if (detail_.productId != productId) return;
    if (!description.empty()) detail_.description = description;
    if (!publisher.empty()) detail_.publisher = publisher;
    if (!categories.empty()) detail_.categories = categories;
    if (!heroUrl.empty()) detail_.heroUrl = heroUrl;
    dirty_ = true;
}

bool AppUi::purchasableAt(size_t index, GameTile& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= purchasable_.size()) return false;
    out = purchasable_[index];
    return true;
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

Tab AppUi::tab() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tab_;
}

void AppUi::openDetails(const GameTile& tile) {
    // Caller holds mutex_.
    detail_ = tile;
    auto info = detailInfo_.find(tile.productId);
    if (info != detailInfo_.end()) {
        if (detail_.description.empty()) detail_.description = info->second.description;
        if (detail_.heroUrl.empty()) detail_.heroUrl = info->second.heroUrl;
        if (detail_.publisher.empty()) detail_.publisher = info->second.publisher;
        if (detail_.categories.empty()) detail_.categories = info->second.categories;
    }
    screen_ = Screen::Details;
    dirty_ = true;
}

void AppUi::refreshDetail() {
    // Caller holds mutex_. Hero art and descriptions arrive after the page opened.
    if (screen_ != Screen::Details || detail_.productId.empty()) return;
    auto take = [&](const GameTile& t) {
        if (t.productId == detail_.productId && t.heroUrl.size() + t.description.size() >
                                                    detail_.heroUrl.size() + detail_.description.size())
            detail_ = t;
    };
    for (const auto& row : rows_)
        for (const auto& t : row.tiles) take(t);
    for (const auto* list : {&owned_, &purchasable_, &gamePassPool_, &libraryPool_})
        for (const auto& t : *list) take(t);
}

void AppUi::runSearch() {
    // Caller holds mutex_. The typed words narrow by name; the filters by
    // price and console. With filters and no words: every game that passes.
    results_.clear();
    resultFocus_ = 0;
    resultScroll_ = Anim{};
    std::string q = fold(query_);
    std::vector<std::string> words;
    for (size_t i = 0; i < q.size();) {
        size_t j = q.find(' ', i);
        if (j == std::string::npos) j = q.size();
        if (j > i) words.push_back(q.substr(i, j - i));
        i = j + 1;
    }
    bool filtering = anyFilter() && (tab_ == Tab::Library || filterConsole_);
    if (words.empty() && !filtering) return;
    std::string compact = withoutSpaces(q);
    static const char* kConsoleCodes[] = {"", "XS", "ONE", "360"};
    struct Hit {
        int score;
        double order;  // price or discount, by the active order filter
        const GameTile* tile;
    };
    std::vector<Hit> hits;
    for (const auto& t : tab_ == Tab::GamePass ? gamePassPool_ : libraryPool_) {
        if (hidden_.count(t.productId)) continue;
        if (filterConsole_ && t.platform != kConsoleCodes[filterConsole_]) continue;
        auto price = prices_.find(t.productId);
        bool priced = price != prices_.end();
        if (tab_ == Tab::Library) {
            // Free-to-play: free in the store and no regular price. A paid
            // game given away (price 0 now, regular price above 0) is a
            // sale, not free-to-play.
            bool zeroNow = t.purchasable && priced && price->second.list < 0.005;
            bool freeToPlay = t.freeInStore || (zeroNow && price->second.msrp < 0.005);
            if (filterFree_ && !freeToPlay && !zeroNow) continue;  // Free: both
            // Free-to-play games have their own filter: the price orders
            // leave them out, unless Free is on too. Giveaways stay (first).
            if ((filterCheapest_ || filterSale_) && freeToPlay && !filterFree_) continue;
            // Price filters look at games to buy only.
            if ((filterCheapest_ || filterSale_) && !t.purchasable) continue;
            if (filterSale_ && !(priced && price->second.msrp > price->second.list + 0.005)) continue;
        }
        std::string name = fold(t.name);
        int score = 0;
        if (!words.empty()) {
            bool all = true;
            for (const auto& w : words)
                if (name.find(w) == std::string::npos) all = false;
            if (!all && withoutSpaces(name).find(compact) == std::string::npos) continue;
            // Starts with the query, then a word starting with it, then the rest.
            score = name.rfind(words[0], 0) == 0 ? 0 : name.find(" " + words[0]) != std::string::npos ? 1 : 2;
        }
        if (!t.playable) score += 3;  // in "Your games": owned before games to buy
        double order = 0;
        if (filterCheapest_) order = priced ? price->second.list : 1e12;  // unknown prices last
        if (filterSale_ && priced) order = -(1.0 - price->second.list / price->second.msrp);  // biggest discount first
        hits.push_back({score, order, &t});
    }
    bool byOrder = tab_ == Tab::Library && (filterCheapest_ || filterSale_);
    std::stable_sort(hits.begin(), hits.end(), [&](const Hit& a, const Hit& b) {
        if (byOrder && a.order != b.order) return a.order < b.order;
        if (a.score != b.score) return a.score < b.score;
        return fold(a.tile->name) < fold(b.tile->name);
    });
    size_t max = words.empty() ? kMaxFilterResults : kMaxResults;
    for (size_t i = 0; i < hits.size() && i < max; ++i) results_.push_back(*hits[i].tile);
}

void AppUi::showHome(const std::string& toast) {
    std::lock_guard<std::mutex> lock(mutex_);
    screen_ = Screen::Home;
    if (!toast.empty()) {
        toast_ = toast;
        toastUntil_ = 0;  // set on first render
    }
    dirty_ = true;
}

void AppUi::showLaunching(const GameTile& game, const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
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

void AppUi::showError(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    screen_ = Screen::Error;
    error_ = message;
    dirty_ = true;
}

void AppUi::setSettings(const SettingsChoice& choice) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = choice;
    dirty_ = true;
}

void AppUi::setRegions(std::vector<std::string> regions, const std::string& defaultRegion) {
    std::lock_guard<std::mutex> lock(mutex_);
    regions_ = std::move(regions);
    defaultRegion_ = defaultRegion;
    dirty_ = true;
}

std::vector<std::string> AppUi::settingOptions(int row) const {
    // Caller holds mutex_.
    std::vector<std::string> out;
    auto withMs = [&](std::string label, const std::string& region) {
        auto ms = regionMs_.find(region);
        if (ms != regionMs_.end()) label += "  \xE2\x80\xA2  " + std::to_string(ms->second) + " ms";
        return label;
    };
    if (row == 0) {
        for (int i = 0; i < static_cast<int>(Language::Count); ++i) out.push_back(languageName(static_cast<Language>(i)));
    } else if (row == 1) {
        out = {tr(Str::Res720), tr(Str::Res1080), tr(Str::Res1440)};  // kResolutionOrder
    } else {
        // Automatic first, then the regions in the login's order.
        out.push_back(withMs(trf(Str::RegionAuto, defaultRegion_.empty() ? "-" : prettyRegion(defaultRegion_)),
                             defaultRegion_));
        for (const auto& r : regions_) out.push_back(withMs(prettyRegion(r), r));
    }
    return out;
}

int AppUi::settingSelected(int row) const {
    // Caller holds mutex_.
    if (row == 0) return settings_.language;
    if (row == 1)
        for (int i = 0; i < 3; ++i)
            if (kResolutionOrder[i] == settings_.resolution) return i;
    for (size_t i = 0; i < regions_.size(); ++i)
        if (regions_[i] == settings_.region) return static_cast<int>(i) + 1;
    return 0;
}

void AppUi::applySetting(int row, int index) {
    // Caller holds mutex_.
    if (row == 0) {
        settings_.language = index;
        setLanguage(static_cast<Language>(index));  // the UI switches right away
    } else if (row == 1) {
        settings_.resolution = kResolutionOrder[index];
    } else {
        settings_.region = index == 0 ? std::string() : regions_[static_cast<size_t>(index - 1)];
    }
    dirty_ = true;
}

void AppUi::changeSetting(int delta) {
    // Caller holds mutex_. Left / right: the previous / next choice.
    int n = static_cast<int>(settingOptions(settingsRow_).size());
    if (n > 0) applySetting(settingsRow_, (settingSelected(settingsRow_) + delta + n) % n);
}

Screen AppUi::screen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return screen_;
}

void AppUi::invalidate() {
    std::lock_guard<std::mutex> lock(mutex_);
    dirty_ = true;
}

const GameTile* AppUi::focusedTile() const {
    if (focusRow_ < 0 || focusRow_ >= static_cast<int>(rows_.size())) return nullptr;
    const auto& row = rows_[static_cast<size_t>(focusRow_)];
    int col = focusCol_[static_cast<size_t>(focusRow_)];
    if (col < 0 || col >= static_cast<int>(row.tiles.size())) return nullptr;
    return &row.tiles[static_cast<size_t>(col)];
}

void AppUi::retarget() {
    rowY_.target = static_cast<float>(focusRow_);
    const float span = kW - 2 * kMargin - kCard;
    for (size_t r = 0; r < rows_.size(); ++r) {
        float left = static_cast<float>(focusCol_[r] * kCardPitch);
        Anim& a = rowScroll_[r];
        if (left - a.target > span) a.target = left - span;
        if (left < a.target) a.target = left;
    }
}

// --- Input ---------------------------------------------------------------------

namespace {

// Moves `focus` in a grid of `count` items, `cols` wide. Returns true if it moved.
bool moveInGrid(int& focus, int count, int cols, const NavInput& in) {
    int before = focus;
    if (in.right && focus + 1 < count && (focus + 1) % cols != 0) ++focus;
    if (in.left && focus % cols != 0) --focus;
    if (in.down && focus + cols < count) focus += cols;
    else if (in.down && focus / cols < (count - 1) / cols) focus = count - 1;  // onto a short last row
    if (in.up && focus >= cols) focus -= cols;
    return focus != before;
}

// Keeps the focused grid row on screen: scrolls once it passes the second row.
float gridTarget(int focus, int cols) { return static_cast<float>(std::max(0, focus / cols - 1) * kGridPitchY); }

}  // namespace

AppUi::LibraryLayout AppUi::libraryLayout() const {
    // Caller holds mutex_. The account's games, then (after headers) the
    // games to buy and the hidden games, all kLibraryCols wide.
    LibraryLayout L;
    L.cols = kLibraryCols;
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
    return L;
}

void AppUi::handleHome(const NavInput& in, UiEvent& ev) {
    // Caller holds mutex_.
    if (in.l1 || in.r1) {
        tab_ = tab_ == Tab::GamePass ? Tab::Library : Tab::GamePass;
        searching_ = false;
        dirty_ = true;
        return;
    }
    if (in.options) {
        screen_ = Screen::Settings;
        settingsRow_ = 0;
        dropdownOpen_ = false;
        dirty_ = true;
        return;
    }
    if (in.triangle && !searching_) {  // search this tab
        searching_ = true;
        searchOnKeys_ = true;
        query_.clear();
        runSearch();
        dirty_ = true;
        return;
    }
    if (searching_) {
        if (searchOnKeys_) {
            if (in.back || in.triangle) {  // close the search
                searching_ = false;
                dirty_ = true;
                return;
            }
            handleSearchKeys(in);
            return;
        }
        if (in.back || in.triangle || (in.left && resultFocus_ % kResultCols == 0)) {  // back to the keys
            searchOnKeys_ = true;
            dirty_ = true;
            return;
        }
        if (in.square && !query_.empty()) {  // delete without going back to the keys
            query_.pop_back();
            runSearch();
            searchOnKeys_ = results_.empty();
            dirty_ = true;
            return;
        }
        if (moveInGrid(resultFocus_, static_cast<int>(results_.size()), kResultCols, in)) {
            resultScroll_.target = gridTarget(resultFocus_, kResultCols);
            dirty_ = true;
        }
        if (in.accept && resultFocus_ < static_cast<int>(results_.size()))
            openDetails(results_[static_cast<size_t>(resultFocus_)]);
        return;
    }
    switch (tab_) {
        case Tab::GamePass: {
            if (rows_.empty()) break;
            int rows = static_cast<int>(rows_.size());
            int& col = focusCol_[static_cast<size_t>(focusRow_)];
            int cols = static_cast<int>(rows_[static_cast<size_t>(focusRow_)].tiles.size());
            if (in.down && focusRow_ + 1 < rows) ++focusRow_;
            if (in.up && focusRow_ > 0) --focusRow_;
            if (in.right && col + 1 < cols) ++col;
            if (in.left && col > 0) --col;
            if (in.up || in.down || in.left || in.right) {
                retarget();
                dirty_ = true;
            }
            if (in.accept && focusedTile()) openDetails(*focusedTile());
            if (in.square && focusedTile()) toggleHidden(*focusedTile(), ev);
            break;
        }
        case Tab::Library: {
            if (!libraryTile(0)) break;
            LibraryLayout L = libraryLayout();
            int count = static_cast<int>(L.rowOf.size());
            int f = std::min(gridFocus_, count - 1), before = f;
            int row = L.rowOf[static_cast<size_t>(f)], col = L.colOf[static_cast<size_t>(f)];
            int rows = static_cast<int>(L.rowY.size());
            auto rowLen = [&](int r) {
                int next = r + 1 < rows ? L.rowFirst[static_cast<size_t>(r + 1)] : count;
                return next - L.rowFirst[static_cast<size_t>(r)];
            };
            if (in.right && f + 1 < count && L.rowOf[static_cast<size_t>(f + 1)] == row) ++f;
            if (in.left && col > 0) --f;
            if (in.down && row + 1 < rows) f = L.rowFirst[static_cast<size_t>(row + 1)] + std::min(col, rowLen(row + 1) - 1);
            if (in.up && row > 0) f = L.rowFirst[static_cast<size_t>(row - 1)] + std::min(col, rowLen(row - 1) - 1);
            if (f != before) {
                gridFocus_ = f;
                int y = L.rowY[static_cast<size_t>(L.rowOf[static_cast<size_t>(f)])];
                gridScroll_.target = static_cast<float>(std::max(0, y - kGridPitchY - 40));
                dirty_ = true;
            }
            if (in.accept && libraryTile(f)) openDetails(*libraryTile(f));
            if (in.square && libraryTile(f)) {
                GameTile t = *libraryTile(f);
                toggleHidden(t, ev);
            }
            if (in.l2 || in.r2) {
                // Jump between sections: the account's games, games to buy,
                // hidden games. L2 goes to the start of this section first.
                int starts[3] = {0, static_cast<int>(owned_.size()),
                                 static_cast<int>(owned_.size() + purchasable_.size())};
                int total = starts[2] + static_cast<int>(hiddenTiles_.size());
                int section = f >= starts[2] ? 2 : f >= starts[1] ? 1 : 0;
                int target = f;
                if (in.r2) {
                    for (int k = section + 1; k < 3; ++k)
                        if (starts[k] < total && (k == 2 ? !hiddenTiles_.empty() : starts[k] < starts[k + 1])) {
                            target = starts[k];
                            break;
                        }
                } else if (f != starts[section]) {
                    target = starts[section];
                } else {
                    for (int k = section - 1; k >= 0; --k)
                        if (starts[k] < starts[k + 1]) {
                            target = starts[k];
                            break;
                        }
                }
                if (target != f) {
                    gridFocus_ = target;
                    int section2 = target >= starts[2] ? 2 : target >= starts[1] ? 1 : 0;
                    gridScroll_.target = static_cast<float>(L.headerY[section2]);  // its heading at the top
                    dirty_ = true;
                }
            }
            if (in.r3) {  // next sort order
                librarySort_ = static_cast<LibrarySort>((static_cast<int>(librarySort_) + 1) %
                                                         static_cast<int>(LibrarySort::Count));
                rebuild(true);  // same place on screen, not back to the top
                prefsEvent(ev);
            }
            break;
        }
    }
}

std::vector<std::string> AppUi::filterLabels() const {
    // Caller holds mutex_. Prices only matter in "Your games" (Game Pass
    // games aren't bought): there, Free / Lowest price / On sale first.
    static const char* kConsoles[] = {nullptr, "SERIES X|S", "XBOX ONE", "XBOX 360"};
    std::string console = filterConsole_ ? kConsoles[filterConsole_] : tr(Str::FilterAllConsoles);
    if (tab_ == Tab::GamePass) return {console};
    return {tr(Str::FilterFree), tr(Str::FilterCheapest), tr(Str::FilterSale), console};
}

void AppUi::pressFilter(int index) {
    // Caller holds mutex_.
    int console = static_cast<int>(filterLabels().size()) - 1;  // always last
    if (index == console) {
        filterConsole_ = (filterConsole_ + 1) % 4;
    } else if (index == 0) {
        filterFree_ = !filterFree_;
    } else if (index == 1) {
        filterCheapest_ = !filterCheapest_;
        if (filterCheapest_) filterSale_ = false;  // one order at a time
    } else if (index == 2) {
        filterSale_ = !filterSale_;
        if (filterSale_) filterCheapest_ = false;
    }
    runSearch();
}

void AppUi::handleSearchKeys(const NavInput& in) {
    // Caller holds mutex_. keyRow_ == kKeyRows is the row of wide keys, where
    // keyCol_ is 0..2 (Space, Delete, Clear); kKeyRows + 1 the filters.
    bool wide = keyRow_ == kKeyRows, filters = keyRow_ == kKeyRows + 1;
    int filterCount = static_cast<int>(filterLabels().size());
    if (in.up && keyRow_ > 0) {
        if (wide) keyCol_ = kWideCols[keyCol_];
        if (filters) keyCol_ = std::min(keyCol_, 2);
        --keyRow_;
    } else if (in.down && keyRow_ < kKeyRows + 1) {
        ++keyRow_;
        if (keyRow_ == kKeyRows) keyCol_ = keyCol_ < 3 ? 0 : keyCol_ < 5 ? 1 : 2;
        else if (keyRow_ == kKeyRows + 1) keyCol_ = std::min(keyCol_, filterCount - 1);
    } else if (in.left && keyCol_ > 0) {
        --keyCol_;
    } else if (in.right) {
        int last = filters ? filterCount - 1 : wide ? 2 : kKeyCols - 1;
        if (keyCol_ < last) {
            ++keyCol_;
        } else if (!results_.empty()) {  // on to the results
            searchOnKeys_ = false;
            resultFocus_ = 0;
        }
    }
    bool edited = false;
    if (in.accept) {
        if (filters) {
            pressFilter(keyCol_);
        } else if (!wide) {
            query_ += kKeys[keyRow_ * kKeyCols + keyCol_];
        } else if (keyCol_ == 0) {
            if (!query_.empty() && query_.back() != ' ') query_ += ' ';
        } else if (keyCol_ == 1) {
            if (!query_.empty()) query_.pop_back();
        } else {
            query_.clear();
        }
        edited = !filters;
        dirty_ = true;
    }
    if (in.square && !query_.empty()) {
        query_.pop_back();
        edited = true;
    }
    if (edited) runSearch();
    if (in.up || in.down || in.left || in.right || edited) dirty_ = true;
}

UiEvent AppUi::handle(const NavInput& in) {
    std::lock_guard<std::mutex> lock(mutex_);
    UiEvent ev;
    switch (screen_) {
        case Screen::Home: handleHome(in, ev); break;
        case Screen::Settings:
            if (dropdownOpen_) {
                // The list: up / down, Cross picks, Circle closes it unchanged.
                int n = static_cast<int>(settingOptions(settingsRow_).size());
                if (in.down && dropdownIndex_ + 1 < n) ++dropdownIndex_;
                if (in.up && dropdownIndex_ > 0) --dropdownIndex_;
                if (in.accept) applySetting(settingsRow_, dropdownIndex_);
                if (in.accept || in.back) dropdownOpen_ = false;
                dirty_ = true;
                break;
            }
            if (in.down && settingsRow_ < 2) ++settingsRow_, dirty_ = true;
            if (in.up && settingsRow_ > 0) --settingsRow_, dirty_ = true;
            if (in.right) changeSetting(+1);
            if (in.left) changeSetting(-1);
            if (in.accept) {
                dropdownOpen_ = true;
                dropdownIndex_ = settingSelected(settingsRow_);
                dropdownTop_ = 0;
                dirty_ = true;
            }
            if (in.back || in.options) {
                screen_ = Screen::Home;
                dirty_ = true;
                ev.action = Action::SettingsChanged;
                ev.settings = settings_;
            }
            break;
        case Screen::Details:
            if (in.accept && detail_.playable && !detail_.titleId.empty()) {
                ev.action = Action::Play;
                ev.game = detail_;
            }
            if (in.square) toggleHidden(detail_, ev);  // the page stays open
            if (in.back) {
                screen_ = Screen::Home;
                dirty_ = true;
            }
            break;
        case Screen::Launching:
            if (in.back) ev.action = Action::CancelLaunch;
            break;
        case Screen::Error:
            if (in.accept) ev.action = Action::Retry;
            break;
        default: break;
    }
    // Sign out: hold TOUCHPAD for 5 s on the home or error screen.
    bool canSignOut = screen_ == Screen::Home || screen_ == Screen::Error;
    if (canSignOut && in.touchpad && !signOutLatched_) {
        if (!signOutHoldStart_) signOutHoldStart_ = in.nowMs ? in.nowMs : 1;
        if (in.nowMs - signOutHoldStart_ >= kSignOutHoldMs) {
            ev.action = Action::SignOut;
            signOutHoldStart_ = 0;
            signOutLatched_ = true;
        }
        dirty_ = true;
    } else if (!in.touchpad || !canSignOut) {
        if (signOutHoldStart_) dirty_ = true;
        signOutHoldStart_ = 0;
        if (!in.touchpad) signOutLatched_ = false;
    }
    return ev;
}

// --- Rendering -----------------------------------------------------------------

bool AppUi::needsRedraw(uint64_t nowMs) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (screen_ == Screen::Streaming) return false;
    if (dirty_ || animating_) return true;
    // Spinners keep moving (about 30 fps is enough).
    bool spinner = screen_ == Screen::Splash || screen_ == Screen::SignIn || screen_ == Screen::Launching;
    return spinner && nowMs - lastRender_ >= 33;
}

void AppUi::drawCentered(Canvas& c, const Font& f, const std::string& text, int y, int px, Color color) {
    f.draw(c, text, (kW - f.measure(text, px)) / 2, y, px, color);
}

void AppUi::drawBackground(Canvas& c) {
    c.clear(kBg);
    c.gradientV({0, 0, kW, kH}, rgba(24, 28, 24), kBg);
}

void AppUi::drawLogo(Canvas& c, float cx, float cy, float r) { drawBrandMark(c, cx, cy, r); }

void AppUi::drawTopBar(Canvas& c) {
    drawLogo(c, kMargin + 22, 72, 22);
    fonts_.semibold.draw(c, tr(Str::CloudGaming), kMargin + 64, 60, 22, kWhite);
    int x = kW - kMargin;
    constexpr int kAvatar = 52;
    x -= kAvatar;
    if (auto pic = images_.get(gamerpicUrl_, kAvatar, kAvatar)) {
        c.drawImage(*pic, x, 46, 255, -1);
    } else {
        c.fillCircle(x + kAvatar / 2.0f, 72, kAvatar / 2.0f, kPanel);
    }
    if (!gamertag_.empty()) {
        int w = fonts_.semibold.measure(gamertag_, 22);
        fonts_.semibold.draw(c, gamertag_, x - 16 - w, 59, 22, kWhite);
    }
}

void AppUi::drawHints(Canvas& c, const std::vector<std::pair<int, const char*>>& hints) {
    // Right-aligned PlayStation button glyphs with labels.
    constexpr int kPx = 22, kR = 14, kGap = 36;
    int x = kW - kMargin;
    const int y = kH - 64;
    for (auto it = hints.rbegin(); it != hints.rend(); ++it) {
        int w = fonts_.semibold.measure(it->second, kPx);
        x -= w;
        fonts_.semibold.draw(c, it->second, x, y, kPx, kGray);
        x -= 10 + 2 * kR;
        float cx = x + kR, cy = y + 13;
        if (it->first == kIconL2R2) {
            Rect pill{x - 40, static_cast<int>(cy) - 12, 2 * kR + 46, 24};
            c.fillRect(pill, rgba(255, 255, 255, 40), 8);
            fonts_.bold.draw(c, "L2 R2", pill.x + (pill.w - fonts_.bold.measure("L2 R2", 14)) / 2,
                             fonts_.bold.centeredY(pill.y, pill.h, 14), 14, kGray);
            x -= 34;
        } else if (it->first == kIconR3) {
            Rect pill{x - 6, static_cast<int>(cy) - 12, 2 * kR + 12, 24};
            c.fillRect(pill, rgba(255, 255, 255, 40), 8);
            fonts_.bold.draw(c, "R3", pill.x + (pill.w - fonts_.bold.measure("R3", 14)) / 2,
                             fonts_.bold.centeredY(pill.y, pill.h, 14), 14, kGray);
        } else if (it->first == kIconTouchpad) {
            Rect pad{x - 18, static_cast<int>(cy) - 10, 2 * kR + 12, 20};
            c.fillRect(pad, rgba(255, 255, 255, 40), 6);
            c.strokeRect(pad, kGray, 2, 6);
            x -= 10;
        } else if (it->first == kIconOptions) {
            c.fillRect({x - 4, static_cast<int>(cy) - 11, 2 * kR + 8, 22}, rgba(255, 255, 255, 40), 11);
            for (int k = -1; k <= 1; ++k) c.line(cx - 8, cy + k * 5, cx + 8, cy + k * 5, 2, kGray);
        } else {
            c.fillCircle(cx, cy, kR, rgba(255, 255, 255, 40));
            if (it->first == kIconCross) {
                c.line(cx - 6, cy - 6, cx + 6, cy + 6, 2.5f, rgba(124, 178, 232));
                c.line(cx + 6, cy - 6, cx - 6, cy + 6, 2.5f, rgba(124, 178, 232));
            } else if (it->first == kIconCircle) {
                c.strokeArc(cx, cy, 6.5f, 2.5f, 0, 6.2832f, rgba(255, 102, 102));
            } else if (it->first == kIconSquare) {
                c.strokeRect({static_cast<int>(cx) - 6, static_cast<int>(cy) - 6, 12, 12}, rgba(240, 130, 200), 2);
            } else {
                c.line(cx, cy - 7, cx - 7, cy + 5, 2.5f, rgba(64, 226, 160));
                c.line(cx, cy - 7, cx + 7, cy + 5, 2.5f, rgba(64, 226, 160));
                c.line(cx - 7, cy + 5, cx + 7, cy + 5, 2.5f, rgba(64, 226, 160));
            }
        }
        x -= kGap;
    }
}

void AppUi::drawSpinner(Canvas& c, float cx, float cy, float r, uint64_t nowMs) {
    float t = static_cast<float>(nowMs % 1000) / 1000.0f * 6.2832f;
    c.strokeArc(cx, cy, r, r * 0.18f, t, 4.4f, kWhite);
}

void AppUi::drawToast(Canvas& c, uint64_t nowMs) {
    if (toast_.empty()) return;
    if (toastUntil_ == 0) toastUntil_ = nowMs + 3500;
    if (nowMs >= toastUntil_) {
        toast_.clear();
        return;
    }
    int w = fonts_.semibold.measure(toast_, 24) + 64;
    Rect r{(kW - w) / 2, kH - 150, w, 56};
    c.fillRect(r, rgba(40, 40, 40, 235), 28);
    fonts_.semibold.draw(c, toast_, r.x + 32, r.y + 14, 24, kWhite);
}

bool AppUi::drawHero(Canvas& c, uint64_t nowMs, const std::string& url, bool details) {
    if (url != heroUrl_) {
        // The previous art may only cross-fade into a new one that is already
        // there; it is never kept up while the new one downloads.
        prevHeroUrl_ = heroShownAt_ ? heroUrl_ : std::string();
        heroUrl_ = url;
        heroSince_ = nowMs;
        heroShownAt_ = 0;
    }
    auto current = heroUrl_.empty() ? nullptr : images_.get(heroUrl_, kW, kH);
    if (current) {
        if (!heroShownAt_) heroShownAt_ = nowMs;
        uint64_t age = nowMs - heroShownAt_;
        // Arrived at once (cached): cross-fade from the previous art. Arrived
        // later: fade in from the plain background.
        bool crossFade = heroShownAt_ - heroSince_ < 150 && !prevHeroUrl_.empty();
        auto previous = crossFade && age < kHeroFadeMs ? images_.get(prevHeroUrl_, kW, kH) : nullptr;
        if (previous) c.drawImage(*previous, 0, 0);
        uint8_t alpha = age >= kHeroFadeMs ? 255 : static_cast<uint8_t>(age * 255 / kHeroFadeMs);
        c.drawImage(*current, 0, 0, alpha);
        if (alpha < 255) animating_ = true;
    } else {
        prevHeroUrl_.clear();  // nothing on screen to fade from any more
    }
    // Darken towards the left (text) and the bottom (rows).
    c.gradientH({0, 0, details ? 1400 : 1200, kH}, rgba(0, 0, 0, details ? 240 : 225), rgba(0, 0, 0, 0));
    if (details) {
        c.gradientV({0, 640, kW, 440}, withAlpha(kBg, 0), withAlpha(kBg, 230));
    } else {
        c.gradientV({0, 360, kW, 420}, withAlpha(kBg, 0), kBg);
        c.fillRect({0, 780, kW, kH - 780}, kBg);
    }
    c.gradientV({0, 0, kW, 160}, rgba(0, 0, 0, 150), rgba(0, 0, 0, 0));
    return current != nullptr;
}

void AppUi::drawSplash(Canvas& c, uint64_t nowMs) {
    drawBackground(c);
    drawLogo(c, kW / 2.0f, 400, 64);
    drawCentered(c, fonts_.bold, tr(Str::AppName), 500, 52, kWhite);
    drawCentered(c, fonts_.regular, status_, 590, 28, kGray);
    drawSpinner(c, kW / 2.0f, 720, 26, nowMs);
}

void AppUi::drawSignIn(Canvas& c, uint64_t nowMs) {
    drawBackground(c);
    const int left = 180;
    drawLogo(c, left + 26, 160, 26);
    fonts_.semibold.draw(c, tr(Str::AppName), left + 70, 144, 30, kWhite);
    fonts_.bold.draw(c, tr(Str::SignInTitle), left, 260, 72, kWhite);
    fonts_.regular.draw(c, tr(Str::SignInStep1), left, 400, 32, kGray);
    std::string shortUrl = codeUrl_;
    for (const char* p : {"https://", "http://", "www."})
        if (shortUrl.rfind(p, 0) == 0) shortUrl = shortUrl.substr(std::string(p).size());
    fonts_.semibold.draw(c, shortUrl, left, 450, 44, kWhite);
    fonts_.regular.draw(c, tr(Str::SignInStep2), left, 540, 32, kGray);

    // The code, letter by letter, in a panel.
    constexpr int kPx = 96;
    int letters = static_cast<int>(code_.size());
    int cell = 82;
    Rect panel{left, 600, std::max(400, letters * cell + 64), 150};
    c.fillRect(panel, kPanel, 18);
    int x = panel.x + 32;
    for (char ch : code_) {
        std::string s(1, ch);
        fonts_.bold.draw(c, s, x + (cell - fonts_.bold.measure(s, kPx)) / 2, panel.y + 18, kPx, kWhite);
        x += cell;
    }
    drawSpinner(c, left + 18, 830, 14, nowMs);
    fonts_.regular.draw(c, tr(Str::SignInWaiting), left + 48, 815, 26, kDim);

    // QR code for the sign-in page. Not "?otc=<code>": that prefilled link
    // goes through a consent flow that rejects this client id ("first party
    // application ... pre-authorization"), so the code is typed on the phone.
    std::string target = codeUrl_.empty() ? "https://www.microsoft.com/link" : codeUrl_;
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
    if (qrcodegen_encodeText(target.c_str(), tmp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                             qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
        int n = qrcodegen_getSize(qr);
        constexpr int kBox = 440;
        int module = (kBox - 60) / (n + 2);
        int size = module * (n + 2);
        Rect box{1280, 300, size + 40, size + 40};
        c.fillRect(box, kWhite, 24);
        int ox = box.x + 20 + module, oy = box.y + 20 + module;
        for (int yy = 0; yy < n; ++yy)
            for (int xx = 0; xx < n; ++xx)
                if (qrcodegen_getModule(qr, xx, yy)) c.fillRect({ox + xx * module, oy + yy * module, module, module}, kBg);
        int w = fonts_.regular.measure(tr(Str::SignInScan), 26);
        fonts_.regular.draw(c, tr(Str::SignInScan), box.x + (box.w - w) / 2, box.y + box.h + 24, 26, kGray);
    }
}

void AppUi::drawTabs(Canvas& c) {
    // Centred pills in the top bar, between "L1" and "R1".
    const char* labels[2] = {tr(Str::TabGamePass), tr(Str::YourGames)};
    constexpr int kPx = 22, kPad = 26, kGap = 12, kH = 44, kY = 50;
    int widths[2], total = 0;
    for (int i = 0; i < 2; ++i) {
        widths[i] = fonts_.semibold.measure(labels[i], kPx) + 2 * kPad;
        total += widths[i] + (i ? kGap : 0);
    }
    int x = (kW - total) / 2;
    auto shoulder = [&](const char* name, int cx) {
        Rect r{cx - 22, kY + 8, 44, 28};
        c.fillRect(r, rgba(255, 255, 255, 40), 8);
        fonts_.bold.draw(c, name, cx - fonts_.bold.measure(name, 16) / 2, fonts_.bold.centeredY(r.y, r.h, 16), 16, kGray);
    };
    shoulder("L1", x - 40);
    for (int i = 0; i < 2; ++i) {
        bool on = static_cast<int>(tab_) == i;
        Rect r{x, kY, widths[i], kH};
        if (on) c.fillRect(r, kWhite, kH / 2);
        fonts_.semibold.draw(c, labels[i], x + kPad, fonts_.semibold.centeredY(kY, kH, kPx), kPx, on ? kBg : kGray);
        x += widths[i] + kGap;
    }
    shoulder("R1", x - kGap + 40);
}

namespace {

const char* platformLabel(const std::string& code) {
    if (code == "360") return "XBOX 360";
    if (code == "ONE") return "XBOX ONE";
    if (code == "XS") return "SERIES X|S";
    return nullptr;
}

// A shopping bag centred on (cx, cy), about 22x24 px: a game to buy.
// A shopping bag (body + handle) centred on (cx, cy); `size` scales it
// (1 = 22 x 23 px).
void drawBag(Canvas& c, int cx, int cy, Color color, float size = 1.0f) {
    float w = 22 * size, h = 17 * size, r = 6 * size;
    float bodyTop = cy - (h + r) / 2 + r;
    c.fillRect({static_cast<int>(std::lround(cx - w / 2)), static_cast<int>(std::lround(bodyTop)),
                static_cast<int>(std::lround(w)), static_cast<int>(std::lround(h))},
               color, std::max(1, static_cast<int>(3 * size)));
    c.strokeArc(static_cast<float>(cx), bodyTop, r, std::max(1.5f, 2.5f * size), 3.1416f, 3.1416f, color);
}

}  // namespace

void AppUi::drawCard(Canvas& c, const GameTile& t, int x, int y, bool focused, bool gamePassBadge) {
    if (auto img = images_.get(t.tileUrl, kCard, kCard)) {
        c.drawImage(*img, x, y, 255, 10);
    } else {
        c.fillRect({x, y, kCard, kCard}, kPlaceholder, 10);
        auto lines = fonts_.semibold.wrap(t.name, 22, kCard - 32, 3);
        for (size_t k = 0; k < lines.size(); ++k)
            fonts_.semibold.draw(c, lines[k], x + 16, y + 16 + static_cast<int>(k) * 30, 22, kGray);
    }
    // Bottom-left: GAME PASS, or BUY with a bag; a padlock (top right) when
    // it can't be played or bought.
    if (t.purchasable) {
        c.fillRect({x, y, kCard, kCard}, rgba(0, 0, 0, 70), 10);
        auto price = prices_.find(t.productId);
        std::string label = price != prices_.end() ? price->second.now : tr(Str::BuyBadge);
        int w = fonts_.bold.measure(label, 13) + 42;
        Rect badge{x + 10, y + kCard - 34, w, 24};
        c.fillRect(badge, rgba(16, 124, 16, 235), 4);
        drawBag(c, badge.x + 15, badge.y + badge.h / 2, kWhite, 0.62f);
        fonts_.bold.draw(c, label, badge.x + 30, fonts_.bold.centeredY(badge.y, badge.h, 13), 13, kWhite);
    } else if (!t.playable) {
        c.fillRect({x, y, kCard, kCard}, rgba(0, 0, 0, 150), 10);
        drawLock(c, x + kCard - 34, y + 30);
    } else if (gamePassBadge) {
        Rect badge{x + 10, y + kCard - 34, 96, 24};
        c.fillRect(badge, rgba(0, 0, 0, 220), 4);
        fonts_.bold.draw(c, "GAME PASS", badge.x + (96 - fonts_.bold.measure("GAME PASS", 13)) / 2,
                         fonts_.bold.centeredY(badge.y, badge.h, 13), 13,
                         kWhite);
    }
    // Bottom-right: the console it was made for; one style and width for all.
    if (const char* p = platformLabel(t.platform)) {
        constexpr int kBadgeW = 96;
        Rect badge{x + kCard - 10 - kBadgeW, y + kCard - 34, kBadgeW, 24};
        c.fillRect(badge, rgba(0, 0, 0, 220), 4);
        fonts_.bold.draw(c, p, badge.x + (kBadgeW - fonts_.bold.measure(p, 13)) / 2,
                         fonts_.bold.centeredY(badge.y, badge.h, 13), 13, kWhite);
    }
    if (focused) c.strokeRect({x - 7, y - 7, kCard + 14, kCard + 14}, kWhite, 4, 16);
}

void AppUi::drawGrid(Canvas& c, const std::vector<GameTile>& tiles, int x0, int y0, int cols, float scroll, int focus,
                     int clipTop) {
    int offset = static_cast<int>(std::lround(scroll));
    for (size_t i = 0; i < tiles.size(); ++i) {
        int col = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
        int x = x0 + col * kCardPitch;
        int y = y0 + row * kGridPitchY - offset;
        if (y + kCard + 40 < clipTop) continue;
        if (y > kH) break;
        bool focused = static_cast<int>(i) == focus;
        drawCard(c, tiles[i], x, y, focused, false);
        auto name = fonts_.semibold.wrap(tiles[i].name, 20, kCard, 1);
        if (!name.empty()) fonts_.semibold.draw(c, name[0], x, y + kCard + 12, 20, focused ? kWhite : kGray);
    }
    // Rows scrolled up (cards and names) disappear under the header.
    c.fillRect({0, 0, kW, clipTop}, kBg);
    c.gradientV({0, clipTop, kW, 12}, kBg, withAlpha(kBg, 0));
}

void AppUi::drawLibrary(Canvas& c, uint64_t nowMs) {
    drawBackground(c);
    constexpr int kGridW = kLibraryCols * kCardPitch - kCardGap;
    const int left = (kW - kGridW) / 2;
    const int clipTop = kLibraryTop - 10;
    LibraryLayout L = libraryLayout();
    int offset = static_cast<int>(std::lround(gridScroll_.value));
    // Each section has its heading in the grid: a title, the count, a line
    // of explanation (the games to buy have a bag).
    auto heading = [&](int section, const char* title, size_t count, const char* line, bool bag) {
        int hy = kLibraryTop + L.headerY[section] - offset;
        if (hy < clipTop - kSectionHeaderH || hy > kH) return;
        int x = left;
        if (bag) {
            drawBag(c, left + 14, hy + 30, kGreen);
            x += 40;
        }
        fonts_.bold.draw(c, title, x, hy + 8, 34, kWhite);
        int w = fonts_.bold.measure(title, 34);
        fonts_.semibold.draw(c, gamesCount(count), x + w + 20, hy + 20, 22, kDim);
        if (line) fonts_.regular.draw(c, line, x, hy + 52, 22, kGray);
    };
    if (!owned_.empty()) heading(0, tr(Str::YourGames), owned_.size(), nullptr, false);
    if (!purchasable_.empty()) heading(1, tr(Str::AvailableToBuy), purchasable_.size(), tr(Str::BuyToPlay), true);
    if (!hiddenTiles_.empty()) heading(2, tr(Str::HiddenSection), hiddenTiles_.size(), tr(Str::HiddenHint), false);
    for (size_t i = 0; i < L.rowOf.size(); ++i) {
        int y = kLibraryTop + L.rowY[static_cast<size_t>(L.rowOf[i])] - offset;
        if (y + kCard + 40 < clipTop) continue;
        if (y > kH) break;
        int x = left + L.colOf[i] * kCardPitch;
        const GameTile* t = libraryTile(static_cast<int>(i));
        bool focused = static_cast<int>(i) == gridFocus_;
        drawCard(c, *t, x, y, focused, false);
        if (hidden_.count(t->productId)) c.fillRect({x, y, kCard, kCard}, rgba(0, 0, 0, 120), 10);
        auto name = fonts_.semibold.wrap(t->name, 20, kCard, 1);
        if (!name.empty()) fonts_.semibold.draw(c, name[0], x, y + kCard + 12, 20, focused ? kWhite : kGray);
    }
    c.fillRect({0, 0, kW, clipTop}, kBg);
    c.gradientV({0, clipTop, kW, 12}, kBg, withAlpha(kBg, 0));
    drawTopBar(c);
    drawTabs(c);
    if (owned_.empty() && purchasable_.empty() && hiddenTiles_.empty()) {
        if (!ownedKnown_) {
            drawSpinner(c, kW / 2.0f, 470, 26, nowMs);
            drawCentered(c, fonts_.semibold, tr(Str::LoadingGames), 530, 28, kGray);
            animating_ = true;
        } else {
            drawCentered(c, fonts_.semibold, tr(Str::NoGames), 500, 30, kGray);
        }
    }
    {
        // The order (the whole tab's), right-aligned under the tabs, with its button.
        Str sortName = librarySort_ == LibrarySort::AZ        ? Str::SortAZ
                       : librarySort_ == LibrarySort::Console ? Str::SortConsole
                                                              : Str::SortRecent;
        std::string label = trf(Str::SortLabel, tr(sortName));
        int w = fonts_.semibold.measure(label, 22);
        int right = left + kGridW;
        Rect pill{right - 44, 112, 44, 28};
        c.fillRect(pill, rgba(255, 255, 255, 40), 8);
        fonts_.bold.draw(c, "R3", pill.x + (pill.w - fonts_.bold.measure("R3", 16)) / 2,
                         fonts_.bold.centeredY(pill.y, pill.h, 16), 16, kGray);
        fonts_.semibold.draw(c, label, pill.x - 14 - w, fonts_.semibold.centeredY(pill.y, pill.h, 22), 22, kGray);
    }
    c.gradientV({0, kH - 190, kW, 110}, withAlpha(kBg, 0), withAlpha(kBg, 245));
    c.fillRect({0, kH - 80, kW, 80}, withAlpha(kBg, 245));
    const GameTile* focus = libraryTile(gridFocus_);
    bool focusHidden = focus && hidden_.count(focus->productId);
    drawHints(c, {{kIconCross, tr(Str::Select)},
                  {kIconSquare, tr(focusHidden ? Str::Unhide : Str::Hide)},
                  {kIconTriangle, tr(Str::TabSearch)},
                  {kIconR3, tr(Str::SortHint)},
                  {kIconL2R2, tr(Str::Sections)},
                  {kIconOptions, tr(Str::Settings)}});
    drawToast(c, nowMs);
}

void AppUi::drawSearch(Canvas& c, uint64_t nowMs) {
    drawBackground(c);
    // Results on the right.
    drawGrid(c, results_, kResultsX, kGridTop, kResultCols, resultScroll_.value, searchOnKeys_ ? -1 : resultFocus_,
             kGridTop - 20);
    drawTopBar(c);
    drawTabs(c);
    const char* scope = tab_ == Tab::GamePass ? tr(Str::TabGamePass) : tr(Str::YourGames);
    bool filtering = anyFilter() && (tab_ == Tab::Library || filterConsole_);
    if (query_.empty() && !filtering) {
        fonts_.semibold.draw(c, tr(Str::SearchHint), kResultsX, 150, 26, kDim);
    } else if (results_.empty()) {
        fonts_.semibold.draw(c, tr(Str::NoResults), kResultsX, 150, 26, kGray);
    } else {
        fonts_.semibold.draw(c, trf(Str::ResultsCount, std::to_string(results_.size())), kResultsX, 150, 26, kGray);
    }

    // The query.
    const int kbW = kKeyCols * (kKeyW + kKeyGap) - kKeyGap;
    Rect box{kMargin, 140, kbW, 76};
    c.fillRect(box, kPanel, 14);
    if (searchOnKeys_) c.strokeRect(box, rgba(255, 255, 255, 90), 2, 14);
    int tx = box.x + 24;
    if (query_.empty()) {
        fonts_.regular.draw(c, trf(Str::SearchIn, scope), tx, box.y + 22, 26, kDim);
    } else {
        fonts_.semibold.draw(c, query_, tx, box.y + 20, 30, kWhite);
        tx += fonts_.semibold.measure(query_, 30) + 4;
    }
    if (searchOnKeys_ && (nowMs / 530) % 2 == 0) c.fillRect({query_.empty() ? box.x + 20 : tx, box.y + 18, 3, 40}, kWhite);
    if (searchOnKeys_) animating_ = true;  // the caret blinks

    // The keys.
    for (int r = 0; r <= kKeyRows; ++r) {
        int n = r == kKeyRows ? 3 : kKeyCols;
        for (int k = 0; k < n; ++k) {
            int col = r == kKeyRows ? kWideCols[k] : k;
            int span = r == kKeyRows ? kWideSpan[k] : 1;
            Rect key{kMargin + col * (kKeyW + kKeyGap), kKeysY + r * (kKeyH + kKeyGap),
                     span * kKeyW + (span - 1) * kKeyGap, kKeyH};
            bool focused = searchOnKeys_ && keyRow_ == r && keyCol_ == k;
            c.fillRect(key, focused ? kWhite : kPanel, 10);
            std::string label = r == kKeyRows ? tr(k == 0 ? Str::KeySpace : k == 1 ? Str::KeyDelete : Str::KeyClear)
                                               : std::string(1, static_cast<char>(std::toupper(kKeys[r * kKeyCols + k])));
            int px = r == kKeyRows ? 22 : 28;
            int w = fonts_.semibold.measure(label, px);
            fonts_.semibold.draw(c, label, key.x + (key.w - w) / 2, fonts_.semibold.centeredY(key.y, kKeyH, px), px,
                                 focused ? kBg : kWhite);
        }
    }
    // The filters, one row under the keys: green when on.
    {
        auto labels = filterLabels();
        const int kbW = kKeyCols * (kKeyW + kKeyGap) - kKeyGap, gap = 10, px = 18;
        const int fy = kKeysY + (kKeyRows + 1) * (kKeyH + kKeyGap) + 6, fh = 54;
        int textTotal = 0;
        for (const auto& l : labels) textTotal += fonts_.semibold.measure(l, px);
        int spare = kbW - textTotal - gap * (static_cast<int>(labels.size()) - 1);
        int pad = std::max(8, spare / static_cast<int>(labels.size()));
        int x = kMargin;
        for (size_t k = 0; k < labels.size(); ++k) {
            bool console = k + 1 == labels.size();
            bool on = console ? filterConsole_ != 0
                              : (k == 0 ? filterFree_ : k == 1 ? filterCheapest_ : filterSale_);
            bool focused = searchOnKeys_ && keyRow_ == kKeyRows + 1 && keyCol_ == static_cast<int>(k);
            int w = fonts_.semibold.measure(labels[k], px) + pad;
            if (k + 1 == labels.size()) w = kMargin + kbW - x;  // the last one fills the row
            Rect chip{x, fy, w, fh};
            c.fillRect(chip, focused ? kWhite : on ? rgba(16, 124, 16, 255) : kPanel, fh / 2);
            if (on && focused) c.strokeRect({chip.x + 3, chip.y + 3, chip.w - 6, chip.h - 6}, kGreen, 3, fh / 2 - 3);
            std::string text = fonts_.semibold.fit(labels[k], px, w - 16);
            fonts_.semibold.draw(c, text, chip.x + (chip.w - fonts_.semibold.measure(text, px)) / 2,
                                 fonts_.semibold.centeredY(chip.y, chip.h, px), px, focused ? kBg : kWhite);
            x += w + gap;
        }
    }
    c.gradientV({0, kH - 190, kW, 110}, withAlpha(kBg, 0), withAlpha(kBg, 245));
    c.fillRect({0, kH - 80, kW, 80}, withAlpha(kBg, 245));
    if (searchOnKeys_)
        drawHints(c, {{kIconCross, tr(Str::KeyType)}, {kIconSquare, tr(Str::KeyDelete)}, {kIconCircle, tr(Str::Back)}});
    else
        drawHints(c, {{kIconCross, tr(Str::Select)}, {kIconSquare, tr(Str::KeyDelete)}, {kIconCircle, tr(Str::Back)}});
    drawToast(c, nowMs);
}

void AppUi::drawHome(Canvas& c, uint64_t nowMs) {
    if (searching_) return drawSearch(c, nowMs);
    if (tab_ == Tab::Library) return drawLibrary(c, nowMs);
    const GameTile* focus = focusedTile();
    c.clear(kBg);
    drawHero(c, nowMs, focus ? focus->heroUrl : std::string(), false);
    drawTopBar(c);
    drawTabs(c);

    if (rows_.empty()) {
        drawCentered(c, fonts_.semibold, tr(Str::NoGames), 500, 32, kGray);
        drawHints(c, {{kIconOptions, tr(Str::Settings)}, {kIconTouchpad, tr(Str::HoldSignOut)}});
        return;
    }

    if (focus) {
        auto title = fonts_.bold.wrap(focus->name, 60, 1100, 1);
        if (!title.empty()) fonts_.bold.draw(c, title[0], kMargin, 190, 60, kWhite);
        std::string meta = focus->publisher;
        for (size_t i = 0; i < focus->categories.size() && i < 2; ++i)
            meta += (meta.empty() ? "" : "  \xE2\x80\xA2  ") + focus->categories[i];
        fonts_.regular.draw(c, meta, kMargin, 272, 24, kGray);
        auto desc = fonts_.regular.wrap(focus->description, 22, 860, 2);
        for (size_t i = 0; i < desc.size(); ++i)
            fonts_.regular.draw(c, desc[i], kMargin, 318 + static_cast<int>(i) * 32, 22, kGray);
    }

    for (size_t r = 0; r < rows_.size(); ++r) {
        float rel = static_cast<float>(r) - rowY_.value;
        if (rel < -0.7f) continue;
        int y = kRowTop + static_cast<int>(std::lround(rel * kRowPitch));
        if (y > kH) break;
        const GameRow& row = rows_[r];
        fonts_.semibold.draw(c, row.title, kMargin, y, 30, kWhite);
        int cardY = y + 48;
        int scroll = static_cast<int>(std::lround(rowScroll_[r].value));
        int firstCol = std::max(0, scroll / kCardPitch - 1);
        for (size_t col = static_cast<size_t>(firstCol); col < row.tiles.size(); ++col) {
            int x = kMargin + static_cast<int>(col) * kCardPitch - scroll;
            if (x > kW) break;
            if (x + kCard < 0) continue;
            bool focused = static_cast<int>(r) == focusRow_ && static_cast<int>(col) == focusCol_[r];
            drawCard(c, row.tiles[col], x, cardY, focused, row.gamePassBadges);
            if (hidden_.count(row.tiles[col].productId)) c.fillRect({x, cardY, kCard, kCard}, rgba(0, 0, 0, 120), 10);
        }
    }
    // Fade the rows out under the button hints.
    c.gradientV({0, kH - 190, kW, 110}, withAlpha(kBg, 0), withAlpha(kBg, 245));
    c.fillRect({0, kH - 80, kW, 80}, withAlpha(kBg, 245));
    drawHints(c, {{kIconCross, tr(Str::Select)},
                  {kIconSquare, tr(focus && hidden_.count(focus->productId) ? Str::Unhide : Str::Hide)},
                  {kIconTriangle, tr(Str::TabSearch)},
                  {kIconOptions, tr(Str::Settings)},
                  {kIconTouchpad, tr(Str::HoldSignOut)}});
    drawToast(c, nowMs);
}

void AppUi::drawDetails(Canvas& c, uint64_t nowMs) {
    const GameTile* g = &detail_;
    c.clear(kBg);
    if (g->productId.empty()) return;
    // Without its hero art (games to buy, search results, or not downloaded
    // yet): the cover on the right, never another game's backdrop.
    constexpr int kCover = 420;
    const Rect cover{kW - kMargin - kCover, 170, kCover, kCover};
    if (!drawHero(c, nowMs, g->heroUrl, true)) {
        // No hero art (yet): the cover, until the art arrives and fades in.
        drawBackground(c);
        if (!g->purchasable) {  // a game to buy has its QR code there instead
            if (auto img = images_.get(g->tileUrl, kCover, kCover)) c.drawImage(*img, cover.x, cover.y, 255, 16);
            else c.fillRect(cover, kPlaceholder, 16);
        }
    }
    drawTopBar(c);
    int y = 260;
    for (const auto& line : fonts_.bold.wrap(g->name, 68, 960, 2)) {
        fonts_.bold.draw(c, line, kMargin, y, 68, kWhite);
        y += 82;
    }
    y += 6;
    if (!g->publisher.empty()) {
        fonts_.semibold.draw(c, trf(Str::PublisherBy, g->publisher), kMargin, y, 26, kGray);
        y += 40;
    }
    std::string cats;
    if (const char* p = platformLabel(g->platform)) cats = p;
    for (const auto& cat : g->categories) cats += (cats.empty() ? "" : "  \xE2\x80\xA2  ") + cat;
    if (!cats.empty()) {
        fonts_.regular.draw(c, cats, kMargin, y, 24, kDim);
        y += 44;
    }
    y += 10;
    for (const auto& line : fonts_.regular.wrap(g->description, 24, 880, 6)) {
        fonts_.regular.draw(c, line, kMargin, y, 24, kGray);
        y += 34;
    }
    if (g->purchasable) {
        // Streams once bought: the price and how to buy it on the left, a
        // large QR code of the store page (for the phone) on the right. The
        // console's browser can't run the xbox.com store, so no button.
        int ty = y + 30;
        auto price = prices_.find(g->productId);
        if (price != prices_.end()) {
            // The price, and the regular one crossed out while on sale.
            int px = kMargin;
            fonts_.bold.draw(c, price->second.now, px, ty, 52, kWhite);
            px += fonts_.bold.measure(price->second.now, 52) + 24;
            if (!price->second.was.empty()) {
                int w = fonts_.semibold.measure(price->second.was, 30);
                fonts_.semibold.draw(c, price->second.was, px, ty + 14, 30, kDim);
                c.line(static_cast<float>(px), static_cast<float>(ty + 32), static_cast<float>(px + w),
                       static_cast<float>(ty + 32), 2, kDim);
            }
            ty += 84;
        }
        drawBag(c, kMargin + 12, ty + 16, kGreen);
        fonts_.semibold.draw(c, tr(Str::BuyToPlay), kMargin + 36, ty, 28, kWhite);
        ty += 48;
        for (const auto& line : fonts_.regular.wrap(tr(Str::BuyHint), 24, 820, 3)) {
            fonts_.regular.draw(c, line, kMargin, ty, 24, kGray);
            ty += 34;
        }
        std::string url = "https://www.xbox.com/games/store/p/" + g->productId;
        uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
        if (qrcodegen_encodeText(url.c_str(), tmp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                 qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
            int n = qrcodegen_getSize(qr);
            int module = 440 / (n + 2), size = module * (n + 2);
            Rect box{kW - kMargin - size - 40, 190, size + 40, size + 40};
            c.fillRect(box, kWhite, 20);
            int ox = box.x + 20 + module, oy = box.y + 20 + module;
            for (int yy = 0; yy < n; ++yy)
                for (int xx = 0; xx < n; ++xx)
                    if (qrcodegen_getModule(qr, xx, yy)) c.fillRect({ox + xx * module, oy + yy * module, module, module}, kBg);
            int w = fonts_.semibold.measure(tr(Str::ScanToBuy), 26);
            fonts_.semibold.draw(c, tr(Str::ScanToBuy), box.x + (box.w - w) / 2, box.y + box.h + 22, 26, kWhite);
        }
        drawHints(c, {{kIconSquare, tr(hidden_.count(g->productId) ? Str::Unhide : Str::Hide)}, {kIconCircle, tr(Str::Back)}});
        drawToast(c, nowMs);
        return;
    }
    if (!g->playable) {
        // Bought separately or outside the subscription: no Play button.
        Rect note{kMargin, std::max(y + 48, 620), 560, 76};
        c.fillRect(note, rgba(255, 255, 255, 40), 38);
        drawLock(c, note.x + 52, note.y + note.h / 2);
        fonts_.semibold.draw(c, tr(Str::NotPlayable), note.x + 90, note.y + 22, 28, kGray);
        drawHints(c, {{kIconSquare, tr(hidden_.count(g->productId) ? Str::Unhide : Str::Hide)}, {kIconCircle, tr(Str::Back)}});
        drawToast(c, nowMs);
        return;
    }
    // Play button (focused).
    Rect play{kMargin, std::max(y + 48, 620), 300, 76};
    c.strokeRect({play.x - 7, play.y - 7, play.w + 14, play.h + 14}, kWhite, 4, 45);
    c.fillRect(play, kGreen, 38);
    float tx = play.x + 70, ty = play.y + play.h / 2.0f;
    c.line(tx - 8, ty - 13, tx - 8, ty + 13, 4, kWhite);
    c.line(tx - 8, ty - 13, tx + 13, ty, 4, kWhite);
    c.line(tx - 8, ty + 13, tx + 13, ty, 4, kWhite);
    fonts_.semibold.draw(c, tr(Str::Play), play.x + 108, play.y + 20, 32, kWhite);
    drawHints(c, {{kIconCross, tr(Str::Play)},
                  {kIconSquare, tr(hidden_.count(g->productId) ? Str::Unhide : Str::Hide)},
                  {kIconCircle, tr(Str::Back)}});
    drawToast(c, nowMs);
}

void AppUi::drawLaunching(Canvas& c, uint64_t nowMs) {
    c.clear(kBg);
    if (auto hero = images_.get(launching_.heroUrl, kW, kH)) c.drawImage(*hero, 0, 0);
    c.fillRect({0, 0, kW, kH}, rgba(0, 0, 0, 175));
    drawSpinner(c, kW / 2.0f, 420, 40, nowMs);
    drawCentered(c, fonts_.bold, launching_.name, 500, 48, kWhite);
    drawCentered(c, fonts_.semibold, tr(Str::GettingReady), 576, 30, kGray);
    drawCentered(c, fonts_.regular, status_, 630, 26, kDim);
    drawCentered(c, fonts_.regular, tr(Str::LeaveHint), 900, 22, kDim);
    drawHints(c, {{kIconCircle, tr(Str::Cancel)}});
}

void AppUi::drawError(Canvas& c) {
    drawBackground(c);
    auto lines = fonts_.regular.wrap(error_, 26, 1040 - 112, 6);
    int h = 132 + static_cast<int>(lines.size()) * 38 + 56;
    Rect panel{(kW - 1040) / 2, (kH - h) / 2 - 40, 1040, h};
    c.fillRect(panel, kPanel, 24);
    fonts_.bold.draw(c, tr(Str::ErrorTitle), panel.x + 56, panel.y + 52, 44, kWhite);
    int y = panel.y + 132;
    for (const auto& line : lines) {
        fonts_.regular.draw(c, line, panel.x + 56, y, 26, kGray);
        y += 38;
    }
    drawHints(c, {{kIconCross, tr(Str::TryAgain)}, {kIconTouchpad, tr(Str::HoldSignOut)}});
}

void AppUi::drawSettings(Canvas& c) {
    drawBackground(c);
    drawTopBar(c);
    fonts_.bold.draw(c, tr(Str::Settings), kMargin, 170, 60, kWhite);
    const char* labels[3] = {tr(Str::Language), tr(Str::Resolution), tr(Str::Region)};
    constexpr int kRowW = 1200, kRowH = 92;
    Rect rows[3];
    int y = 290;
    for (int i = 0; i < 3; ++i) {
        Rect r{kMargin, y, kRowW, kRowH};
        rows[i] = r;
        bool focused = i == settingsRow_;
        c.fillRect(r, focused ? rgba(255, 255, 255, 36) : rgba(255, 255, 255, 14), 16);
        if (focused && !dropdownOpen_) c.strokeRect({r.x - 5, r.y - 5, r.w + 10, r.h + 10}, kWhite, 3, 20);
        int ty = fonts_.semibold.centeredY(r.y, r.h, 30);
        fonts_.semibold.draw(c, labels[i], r.x + 36, ty, 30, kWhite);
        // The value, then a chevron: this opens a list.
        auto options = settingOptions(i);
        int sel = settingSelected(i);
        std::string value = sel < static_cast<int>(options.size()) ? options[static_cast<size_t>(sel)] : "";
        float cx = static_cast<float>(r.x + r.w - 44), cy = static_cast<float>(r.y + r.h / 2);
        c.line(cx - 9, cy - 4, cx, cy + 5, 3, focused ? kWhite : kGray);
        c.line(cx, cy + 5, cx + 9, cy - 4, 3, focused ? kWhite : kGray);
        int w = fonts_.regular.measure(value, 30);
        fonts_.regular.draw(c, value, r.x + r.w - 76 - w, fonts_.regular.centeredY(r.y, r.h, 30), 30,
                            focused ? kWhite : kGray);
        y += kRowH + 22;
    }
    for (const auto& line : fonts_.regular.wrap(tr(Str::SettingsNote), 24, kRowW, 2)) {
        fonts_.regular.draw(c, line, kMargin, y + 20, 24, kDim);
        y += 34;
    }

    if (dropdownOpen_) {
        // The list under (or, near the bottom, over) the row, right-aligned
        // with it; scrolls past kVisible choices.
        auto options = settingOptions(settingsRow_);
        int sel = settingSelected(settingsRow_);
        constexpr int kItemH = 62, kVisible = 8, kListW = 640;
        int n = static_cast<int>(options.size());
        int visible = std::min(n, kVisible);
        if (dropdownIndex_ < dropdownTop_) dropdownTop_ = dropdownIndex_;
        if (dropdownIndex_ >= dropdownTop_ + visible) dropdownTop_ = dropdownIndex_ - visible + 1;
        const Rect& row = rows[settingsRow_];
        Rect panel{row.x + row.w - kListW, row.y + row.h + 8, kListW, visible * kItemH + 16};
        if (panel.y + panel.h > kH - 100) panel.y = std::max(120, kH - 100 - panel.h);
        c.fillRect({panel.x + 6, panel.y + 10, panel.w, panel.h}, rgba(0, 0, 0, 120), 18);  // shadow
        c.fillRect(panel, rgba(38, 38, 38, 250), 18);
        c.strokeRect(panel, rgba(255, 255, 255, 50), 2, 18);
        for (int k = 0; k < visible; ++k) {
            int i = dropdownTop_ + k;
            Rect item{panel.x + 8, panel.y + 8 + k * kItemH, panel.w - 16, kItemH};
            bool on = i == dropdownIndex_;
            if (on) c.fillRect(item, kWhite, 12);
            Color text = on ? kBg : (i == sel ? kWhite : kGray);
            fonts_.semibold.draw(c, options[static_cast<size_t>(i)], item.x + 24,
                                 fonts_.semibold.centeredY(item.y, item.h, 26), 26, text);
            if (i == sel) {  // a check mark on the current choice
                float cx = static_cast<float>(item.x + item.w - 36), cy = static_cast<float>(item.y + item.h / 2);
                c.line(cx - 10, cy, cx - 3, cy + 7, 3, on ? kBg : kGreen);
                c.line(cx - 3, cy + 7, cx + 10, cy - 7, 3, on ? kBg : kGreen);
            }
        }
        // Scroll hints: more choices above / below.
        if (dropdownTop_ > 0) {
            float cx = static_cast<float>(panel.x + panel.w / 2), cy = static_cast<float>(panel.y - 14);
            c.line(cx - 10, cy + 4, cx, cy - 4, 3, kGray);
            c.line(cx, cy - 4, cx + 10, cy + 4, 3, kGray);
        }
        if (dropdownTop_ + visible < n) {
            float cx = static_cast<float>(panel.x + panel.w / 2), cy = static_cast<float>(panel.y + panel.h + 14);
            c.line(cx - 10, cy - 4, cx, cy + 4, 3, kGray);
            c.line(cx, cy + 4, cx + 10, cy - 4, 3, kGray);
        }
        drawHints(c, {{kIconCross, tr(Str::Select)}, {kIconCircle, tr(Str::Back)}});
    } else {
        drawHints(c, {{kIconCross, tr(Str::Change)}, {kIconCircle, tr(Str::Back)}});
    }
}

void AppUi::drawSignOutHold(Canvas& c, uint64_t nowMs) {
    if (!signOutHoldStart_) return;
    float progress = std::min(1.0f, static_cast<float>(nowMs - signOutHoldStart_) / kSignOutHoldMs);
    int secondsLeft = static_cast<int>((kSignOutHoldMs - std::min<uint64_t>(kSignOutHoldMs, nowMs - signOutHoldStart_) + 999) / 1000);
    std::string text = trf(Str::SigningOutIn, std::to_string(secondsLeft));
    int tw = fonts_.semibold.measure(text, 28);
    Rect panel{(kW - (tw + 170)) / 2, kH - 250, tw + 170, 110};
    c.fillRect(panel, rgba(30, 30, 30, 240), 55);
    c.strokeRect(panel, rgba(255, 255, 255, 60), 2, 55);
    float cx = panel.x + 62, cy = panel.y + panel.h / 2.0f;
    c.strokeArc(cx, cy, 30, 6, 0, 6.2832f, rgba(255, 255, 255, 50));
    // Progress ring from 12 o'clock, clockwise, in red as it nears the end.
    Color ring = progress < 0.8f ? kWhite : rgba(255, 110, 110);
    if (progress > 0.01f) c.strokeArc(cx, cy, 30, 6, -1.5708f, 6.2832f * progress, ring);
    fonts_.bold.draw(c, std::to_string(secondsLeft), static_cast<int>(cx) - fonts_.bold.measure(std::to_string(secondsLeft), 26) / 2,
                     static_cast<int>(cy) - 16, 26, kWhite);
    fonts_.semibold.draw(c, text, panel.x + 120, panel.y + 38, 28, kWhite);
    animating_ = true;
}

void AppUi::render(Canvas& c, uint64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    float dt = lastRender_ ? std::min(0.1f, (nowMs - lastRender_) / 1000.0f) : 0.0f;
    lastRender_ = nowMs;
    animating_ = rowY_.step(dt);
    for (auto& a : rowScroll_) animating_ |= a.step(dt);
    animating_ |= gridScroll_.step(dt);
    animating_ |= resultScroll_.step(dt);
    dirty_ = false;
    switch (screen_) {
        case Screen::Splash: drawSplash(c, nowMs); break;
        case Screen::SignIn: drawSignIn(c, nowMs); break;
        case Screen::Home: drawHome(c, nowMs); break;
        case Screen::Details: drawDetails(c, nowMs); break;
        case Screen::Launching: drawLaunching(c, nowMs); break;
        case Screen::Error: drawError(c); break;
        case Screen::Settings: drawSettings(c); break;
        case Screen::Streaming: break;
    }
    if (screen_ == Screen::Home || screen_ == Screen::Error) drawSignOutHold(c, nowMs);
    if (!toast_.empty()) animating_ = true;  // to expire it
}

}  // namespace xc::ui
