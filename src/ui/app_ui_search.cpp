// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/app_ui_internal.h"

namespace xc::ui {

namespace {

bool containsWordBoundary(const std::string& text, const std::string& token) {
    size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::string::npos) {
        bool startBound = (pos == 0 || !isalnum((unsigned char)text[pos - 1]));
        bool endBound = (pos + token.size() >= text.size() || !isalnum((unsigned char)text[pos + token.size()]));
        if (startBound && endBound) return true;
        pos += token.size();
    }
    return false;
}

bool matchSearchWord(const std::string& name, const std::string& word) {
    if (name.find(word) != std::string::npos) return true;
    static const std::pair<const char*, const char*> kRoman[] = {
        {"1", "i"}, {"i", "1"},
        {"2", "ii"}, {"ii", "2"},
        {"3", "iii"}, {"iii", "3"},
        {"4", "iv"}, {"iv", "4"},
        {"4", "iiii"}, {"iiii", "4"},
        {"5", "v"}, {"v", "5"},
        {"6", "vi"}, {"vi", "6"},
    };
    for (const auto& [num, rom] : kRoman) {
        if (word == num && containsWordBoundary(name, rom)) return true;
    }
    static const std::pair<const char*, const char*> kAcronyms[] = {
        {"cod", "call of duty"},
        {"cod4", "call of duty 4"},
        {"codmw", "modern warfare"},
        {"codmw4", "modern warfare 4"},
        {"mw", "modern warfare"},
        {"mw1", "modern warfare"},
        {"mw2", "modern warfare ii"},
        {"mwii", "modern warfare ii"},
        {"mw3", "modern warfare iii"},
        {"mwiii", "modern warfare iii"},
        {"mw4", "modern warfare 4"},
        {"mwiv", "modern warfare 4"},
        {"bo", "black ops"},
        {"bo1", "black ops"},
        {"bo2", "black ops ii"},
        {"bo3", "black ops iii"},
        {"bo4", "black ops iiii"},
        {"bo6", "black ops 6"},
        {"bops", "black ops"},
        {"gta", "grand theft auto"},
        {"rdr", "red dead"},
        {"re", "resident evil"},
        {"ac", "assassin"},
        {"gow", "gears of war"},
        {"nfs", "need for speed"},
        {"bf", "battlefield"},
    };
    for (const auto& [acro, full] : kAcronyms) {
        if (word == acro && name.find(full) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

void AppUi::runSearch(bool keepPosition) {
    // Caller holds mutex_. The typed words narrow by name; the filters by
    // price and console. With filters and no words: every game that passes.
    std::string focusedProductId;
    std::string focusedTitleId;
    int oldFocus = resultFocus_;
    float oldScrollVal = resultScroll_.value;
    if (keepPosition && resultFocus_ >= 0 && resultFocus_ < static_cast<int>(results_.size())) {
        focusedProductId = results_[static_cast<size_t>(resultFocus_)].productId;
        focusedTitleId = results_[static_cast<size_t>(resultFocus_)].titleId;
    }

    results_.clear();
    std::string q = fold(query_);
    std::vector<std::string> words;
    for (size_t i = 0; i < q.size();) {
        size_t j = q.find(' ', i);
        if (j == std::string::npos) j = q.size();
        if (j > i) words.push_back(q.substr(i, j - i));
        i = j + 1;
    }
    bool filtering = anyFilter();
    if (words.empty() && !filtering) {
        resultFocus_ = 0;
        resultScroll_ = Anim{};
        return;
    }
    std::string compact = withoutSpaces(q);
    static const char* kConsoleCodes[] = {"", "XS", "ONE", "360"};
    struct Hit {
        int score;
        double order;     // the price now (with any discount), by the active order filter
        double discount;  // the bigger first among equal prices
        const GameTile* tile;
    };
    std::vector<Hit> hits;
    for (const auto& t : tab_ == Tab::GamePass ? gamePassPool_ : libraryPool_) {
        if (hidden_.count(t.productId)) continue;
        if (tab_ == Tab::GamePass && filterPlan_ != 4) {
            if (filterPlan_ == 0) {
                uint8_t myPlan = xcloud::planTier(subscription_);
                bool inPlan = (myPlan > 0) ? xcloud::planIncludes(myPlan, t.plan, t.freeInStore) : true;
                if (!t.freeInStore && !t.playable && !inPlan) continue;
            } else {
                uint8_t want = filterPlan_ == 1 ? xcloud::kPlanUltimate
                             : filterPlan_ == 2 ? xcloud::kPlanPremium
                                                : xcloud::kPlanEssential;
                if (!t.freeInStore && (want < xcloud::kPlanUltimate && t.plan == 0)) continue;
                if (!xcloud::planIncludes(want, t.plan, t.freeInStore)) continue;
            }
        }
        if (filterConsole_ && t.platform != kConsoleCodes[filterConsole_]) continue;
        if (filterMouseKeyboard_ && !t.mouseKeyboard) continue;
        // Play mode and language need the full details: a game without them
        // yet comes in when they arrive.
        static constexpr uint32_t kModeBits[] = {0, xcloud::kModeSingle, xcloud::kModeOnlineMulti, xcloud::kModeOnlineCoop,
                                                 xcloud::kModeLocal};
        if (filterMode_ && !(t.modes & kModeBits[filterMode_])) continue;
        if (filterLanguage_ == 1 && !(t.languages & (xcloud::kLangInterface | xcloud::kLangSubtitles))) continue;
        if (filterLanguage_ == 2 && !(t.languages & xcloud::kLangAudio)) continue;
        if (!filterGenre_.empty() && std::find(t.categories.begin(), t.categories.end(), filterGenre_) == t.categories.end())
            continue;
        if (filterPublisher_ && !matchesPublisher(t.publisher, filterPublisher_))
            continue;
        auto price = prices_.find(t.productId);
        bool priced = price != prices_.end();
        if (tab_ == Tab::Library) {
            if (filterWishlist_ && !wishlist_.count(t.productId)) continue;
            // Free-to-play: free in the store and no regular price. A paid
            // game given away (price 0 now, regular price above 0) is a
            // sale, not free-to-play.
            bool zeroNow = t.purchasable && priced && price->second.list < 0.005;
            bool freeToPlay = (t.freeInStore || (zeroNow && price->second.msrp < 0.005)) && !unclaimable_.count(t.productId);
            if (filterFree_) {
                if (!freeToPlay && !zeroNow) continue;  // Free: both
                if (t.playable || isAcquired(t)) continue;  // Do not show games the user already owns on their account!
            }
            // Free-to-play games have their own filter: the price orders
            // leave them out, unless Free is on too. Giveaways stay (first).
            if (filterCheapest_ && freeToPlay && !filterFree_) continue;
            // Price filters look at games to buy only.
            if (filterCheapest_ && !t.purchasable) continue;
        }
        std::string name = fold(t.name);
        int score = 0;
        if (!words.empty()) {
            bool all = true;
            for (const auto& w : words)
                if (!matchSearchWord(name, w)) all = false;
            if (!all && withoutSpaces(name).find(compact) == std::string::npos) continue;
            // Starts with the query or acronym, then a word starting with it, then the rest.
            if (matchSearchWord(name, words[0])) {
                score = name.rfind(words[0], 0) == 0 ? 0 : name.find(" " + words[0]) != std::string::npos ? 1 : 2;
                if (words[0] == "cod" && name.find("call of duty") != std::string::npos) score = 0;
                else if ((words[0] == "cod4" || words[0] == "mw4" || words[0] == "codmw4" ||
                          words[0] == "mwiv" || words[0] == "codmwiv") &&
                         (name.find("modern warfare 4") != std::string::npos || name.find("call of duty 4") != std::string::npos)) score = 0;
                else if (words[0] == "codmw" && name.find("modern warfare") != std::string::npos) score = 0;
            } else {
                score = 2;
            }
        }
        if (!t.playable) score += 3;  // in "Your games": owned before games to buy
        // Lowest price: by the price now (with any discount, not the regular
        // one), lowest first; unknown prices last; the bigger discount first
        // among equal prices.
        double order = 0, discount = 0;
        if (filterCheapest_) order = priced ? price->second.list : 1e12;
        if (filterCheapest_ && priced && price->second.msrp > 0) discount = 1.0 - price->second.list / price->second.msrp;
        hits.push_back({score, order, discount, &t});
    }
    bool byOrder = tab_ == Tab::Library && filterCheapest_;
    std::stable_sort(hits.begin(), hits.end(), [&](const Hit& a, const Hit& b) {
        if (byOrder && a.order != b.order) return a.order < b.order;
        if (byOrder && a.discount != b.discount) return a.discount > b.discount;
        if (a.score != b.score) return a.score < b.score;
        return fold(a.tile->name) < fold(b.tile->name);
    });
    size_t max = words.empty() ? kMaxFilterResults : kMaxResults;
    for (size_t i = 0; i < hits.size() && i < max; ++i) results_.push_back(*hits[i].tile);
    if (keepPosition && !results_.empty()) {
        int newFocus = -1;
        if (!focusedProductId.empty() || !focusedTitleId.empty()) {
            for (size_t i = 0; i < results_.size(); ++i) {
                if ((!focusedProductId.empty() && results_[i].productId == focusedProductId) ||
                    (!focusedTitleId.empty() && results_[i].titleId == focusedTitleId)) {
                    newFocus = static_cast<int>(i);
                    break;
                }
            }
        }
        if (newFocus >= 0) {
            resultFocus_ = newFocus;
        } else {
            resultFocus_ = std::min(oldFocus, static_cast<int>(results_.size()) - 1);
        }
        resultScroll_.target = gridTarget(resultFocus_, kResultCols);
        resultScroll_.value = oldScrollVal;
    } else {
        resultFocus_ = 0;
        resultScroll_ = Anim{};
    }
}

void AppUi::setSearchQuery(const std::string& query) {
    std::lock_guard<std::mutex> lock(mutex_);
    query_ = query;
    if (screen_ == Screen::Home || screen_ == Screen::Details || screen_ == Screen::Settings || screen_ == Screen::Error) {
        screen_ = Screen::Home;
        searching_ = true;
        searchOnKeys_ = true;
        filterList_ = -1;
        runSearch();
        dirty_ = true;
    }
}

std::string AppUi::searchQuery() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return query_;
}

void AppUi::setSearchTab(Tab t) {
    setSearchState(nullptr, &t, nullptr);
}

Tab AppUi::searchTab() const {
    return tab();
}

void AppUi::setSearchState(const std::string* query, const Tab* tab,
                           const std::vector<std::pair<Filter, int>>* filters) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (query) {
        query_ = *query;
    }
    if (tab) {
        tab_ = *tab;
    }
    if (filters) {
        for (const auto& [f, val] : *filters) {
            applyFilterLocked(f, val);
        }
    }
    if (screen_ == Screen::Home || screen_ == Screen::Details || screen_ == Screen::Settings || screen_ == Screen::Error) {
        screen_ = Screen::Home;
        searching_ = true;
        searchOnKeys_ = true;
        filterList_ = -1;
        runSearch();
        dirty_ = true;
    }
}

int AppUi::filterSelectedPublic(Filter f) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (f == Filter::Free) return filterFree_ ? 1 : 0;
    if (f == Filter::Cheapest) return filterCheapest_ ? 1 : 0;
    if (f == Filter::MouseKeyboard) return filterMouseKeyboard_ ? 1 : 0;
    if (f == Filter::Wishlist) return filterWishlist_ ? 1 : 0;
    return filterSelected(f);
}

bool AppUi::filterOnPublic(Filter f) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return filterOn(f);
}

bool AppUi::anyFilter() const {
    // Caller holds mutex_. Free and Lowest price only count in "Your games";
    // Plan only in Game Pass.
    bool library = tab_ == Tab::Library && (filterFree_ || filterCheapest_ || filterWishlist_);
    bool gamePass = tab_ == Tab::GamePass && filterPlan_ > 0;
    return library || gamePass || filterConsole_ || filterMouseKeyboard_ || filterMode_ || !filterGenre_.empty() ||
           filterPublisher_ || filterLanguage_;
}

std::vector<AppUi::Filter> AppUi::filterRow(int row) const {
    // Caller holds mutex_. Prices only matter in "Your games" (Game Pass
    // games aren't bought).
    if (tab_ == Tab::GamePass) {
        if (row == 0) return {Filter::Plan, Filter::Console, Filter::MouseKeyboard};
        if (row == 1) return {Filter::Mode, Filter::Genre, Filter::Publisher};
        if (row == 2) return {Filter::Language};
        return {};
    }
    if (row == 0) return {Filter::Wishlist, Filter::Free, Filter::Cheapest};
    if (row == 1) return {Filter::Mode, Filter::Genre, Filter::Publisher};
    if (row == 2) return {Filter::Console, Filter::Language, Filter::MouseKeyboard};
    return {};
}

std::vector<std::string> AppUi::poolGenres() const {
    // Caller holds mutex_.
    std::map<std::string, int> count;
    for (const auto& t : tab_ == Tab::GamePass ? gamePassPool_ : libraryPool_)
        for (const auto& c : t.categories) ++count[c];
    std::vector<std::pair<int, std::string>> order;
    for (const auto& [g, n] : count) order.push_back({-n, g});
    std::sort(order.begin(), order.end());
    std::vector<std::string> out;
    for (const auto& [n, g] : order) out.push_back(g);
    return out;
}

std::vector<std::string> AppUi::filterOptions(Filter f) const {
    // Caller holds mutex_. The first choice is always "all".
    switch (f) {
    case Filter::Plan: return {tr(Str::PlanMine), "Ultimate", "Premium", "Essential", tr(Str::PlanAll)};
    case Filter::Console: return {tr(Str::FilterAllConsoles), "SERIES X|S", "XBOX ONE", "XBOX 360"};
    case Filter::Mode:
        return {tr(Str::ModeAll), tr(Str::ModeSingle), tr(Str::ModeOnlineMulti), tr(Str::ModeOnlineCoop), tr(Str::ModeLocal)};
    case Filter::Genre: {
        std::vector<std::string> out = {tr(Str::GenreAll)};
        for (auto& g : poolGenres()) out.push_back(std::move(g));
        return out;
    }
    case Filter::Publisher:
        return {
            tr(Str::PublisherAll),
            "Xbox Game Studios / Bethesda",
            "Electronic Arts",
            "Ubisoft",
            "Square Enix",
            "Warner Bros. Games",
            "2K / Take-Two",
            "Capcom",
            "SEGA / Atlus",
            "Bandai Namco",
            "THQ Nordic / Plaion",
            "Focus Entertainment",
            "Devolver Digital",
            "Annapurna Interactive",
            "Team17",
            tr(Str::PublisherIndies)
        };
    case Filter::Language:
        return {tr(Str::LanguageAll), trf(Str::LangSubtitles, tr(Str::LanguageNoun)), trf(Str::LangAudio, tr(Str::LanguageNoun))};
    default: return {};
    }
}

int AppUi::filterSelected(Filter f) const {
    // Caller holds mutex_.
    if (f == Filter::Plan) return filterPlan_;
    if (f == Filter::Console) return filterConsole_;
    if (f == Filter::Mode) return filterMode_;
    if (f == Filter::Publisher) return filterPublisher_;
    if (f == Filter::Language) return filterLanguage_;
    if (f == Filter::Genre) {
        auto genres = poolGenres();
        for (size_t i = 0; i < genres.size(); ++i)
            if (genres[i] == filterGenre_) return static_cast<int>(i) + 1;
    }
    return 0;
}

void AppUi::chooseFilter(Filter f, int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    chooseFilterLocked(f, index);
}

void AppUi::applyFilterLocked(Filter f, int index) {
    // Caller holds mutex_.
    if (f == Filter::Plan) filterPlan_ = index;
    if (f == Filter::Console) filterConsole_ = index;
    if (f == Filter::Mode) filterMode_ = index;
    if (f == Filter::Publisher) filterPublisher_ = index;
    if (f == Filter::Language) filterLanguage_ = index;
    if (f == Filter::Free) filterFree_ = index != 0;
    if (f == Filter::Cheapest) filterCheapest_ = index != 0;
    if (f == Filter::MouseKeyboard) filterMouseKeyboard_ = index != 0;
    if (f == Filter::Wishlist) filterWishlist_ = index != 0;
    if (f == Filter::Genre) {
        auto genres = poolGenres();
        filterGenre_ = index > 0 && index <= static_cast<int>(genres.size()) ? genres[static_cast<size_t>(index - 1)] : "";
    }
}

void AppUi::chooseFilterLocked(Filter f, int index) {
    // Caller holds mutex_.
    applyFilterLocked(f, index);
    filterList_ = -1;
    runSearch();
    dirty_ = true;
}

std::string AppUi::filterText(Filter f) const {
    // Caller holds mutex_. A list shows its name until a choice is made.
    if (f == Filter::Free) return tr(Str::FilterFree);
    if (f == Filter::MouseKeyboard) return tr(Str::FilterMouseKeyboard);
    if (f == Filter::Cheapest) return tr(Str::FilterCheapest);
    if (f == Filter::Wishlist) return tr(Str::Wishlist);
    int sel = filterSelected(f);
    if (sel > 0 || f == Filter::Console) {
        auto options = filterOptions(f);
        if (sel < static_cast<int>(options.size())) return options[static_cast<size_t>(sel)];
    }
    return tr(f == Filter::Plan      ? Str::FilterPlan
              : f == Filter::Mode      ? Str::FilterMode
              : f == Filter::Genre     ? Str::FilterGenre
              : f == Filter::Publisher ? Str::FilterPublisher
                                       : Str::FilterLanguage);
}

bool AppUi::filterOn(Filter f) const {
    // Caller holds mutex_.
    if (f == Filter::Free) return filterFree_;
    if (f == Filter::Cheapest) return filterCheapest_;
    if (f == Filter::MouseKeyboard) return filterMouseKeyboard_;
    if (f == Filter::Wishlist) return filterWishlist_;
    return filterSelected(f) > 0;
}

void AppUi::pressFilter(Filter f) {
    // Caller holds mutex_. A list opens its drop-down at the current choice.
    if (isList(f)) {
        filterList_ = static_cast<int>(f);
        filterListIndex_ = filterSelected(f);
        filterListTop_ = 0;
    } else {
        if (f == Filter::Free) filterFree_ ^= true;
        else if (f == Filter::MouseKeyboard) filterMouseKeyboard_ ^= true;
        else if (f == Filter::Cheapest) filterCheapest_ ^= true;
        else if (f == Filter::Wishlist) filterWishlist_ ^= true;
        runSearch();
    }
    dirty_ = true;
}

bool AppUi::wantsTextInput() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return screen_ == Screen::Home && searching_ && searchOnKeys_ && filterList_ < 0 && keyRow_ <= kKeyRows;
}

void AppUi::typeText(std::string_view utf8) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!(screen_ == Screen::Home && searching_ && searchOnKeys_)) return;
    bool edited = false;
    for (char ch : utf8) {
        if (ch == '\b') {
            // The last character, all of its UTF-8 bytes.
            while (!query_.empty() && (static_cast<unsigned char>(query_.back()) & 0xC0) == 0x80) query_.pop_back();
            if (!query_.empty()) query_.pop_back(), edited = true;
        } else if (ch == '\n') {
            if (edited) runSearch(), edited = false;
            if (!results_.empty()) {
                searchOnKeys_ = false;
                resultFocus_ = 0;
                dirty_ = true;
            }
        } else if (static_cast<unsigned char>(ch) >= 0x20 && (query_.size() < 64 || (ch & 0xC0) == 0x80)) {
            if (ch == ' ' && (query_.empty() || query_.back() == ' ')) continue;  // as the Space key
            query_ += ch;
            edited = true;
        }
    }
    if (edited) {
        runSearch();
        dirty_ = true;
    }
}

std::pair<int, int> AppUi::searchKeyFocus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {keyRow_, keyCol_};
}

int AppUi::searchResultFocus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return resultFocus_;
}

bool AppUi::isSearchOnKeys() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return searchOnKeys_;
}

std::vector<int> AppUi::keyCenters(int row) const {
    // Caller holds mutex_.
    std::vector<int> out;
    if (row < kKeyRows) {
        for (int k = 0; k < kKeyCols; ++k) out.push_back(kMargin + k * (kKeyW + kKeyGap) + kKeyW / 2);
    } else if (row == kKeyRows) {
        for (int k = 0; k < 3; ++k)
            out.push_back(kMargin + kWideCols[k] * (kKeyW + kKeyGap) + (kWideSpan[k] * kKeyW + (kWideSpan[k] - 1) * kKeyGap) / 2);
    } else {
        int r = row - kKeyRows - 1;
        auto chips = filterRow(r);
        int n = static_cast<int>(chips.size());
        constexpr int kCols = 3;
        int w = (kKeysW - kChipGap * (kCols - 1)) / kCols;
        for (int k = 0; k < n; ++k) out.push_back(kMargin + k * (w + kChipGap) + w / 2);
    }
    return out;
}

void AppUi::handleSearchKeys(const NavInput& in) {
    // Caller holds mutex_. keyRow_ == kKeyRows is the row of wide keys, where
    // keyCol_ is 0..2 (Space, Delete, Clear); kKeyRows + 1, + 2 and + 3 the three
    // rows of filters in the 3x3 grid.
    if (filterList_ >= 0) {
        // A filter's drop-down: up / down, Cross picks, Circle closes it unchanged.
        Filter f = static_cast<Filter>(filterList_);
        int n = static_cast<int>(filterOptions(f).size());
        if (in.down && filterListIndex_ + 1 < n) ++filterListIndex_;
        if (in.up && filterListIndex_ > 0) --filterListIndex_;
        if (in.accept) chooseFilterLocked(f, filterListIndex_);
        if (in.accept || in.back) filterList_ = -1;
        dirty_ = true;
        return;
    }
    bool wide = keyRow_ == kKeyRows, filters = keyRow_ > kKeyRows;
    int filterCount = filters ? static_cast<int>(filterRow(keyRow_ - kKeyRows - 1).size()) : 0;
    bool canDown = (keyRow_ < kKeyRows + 3) && (!filters || !filterRow(keyRow_ - kKeyRows).empty());
    if ((in.up && keyRow_ > 0) || (in.down && canDown)) {
        // To the item on the next row whose middle is nearest this one's
        // (the rows have 6, 3 and a few items of different widths).
        auto from = keyCenters(keyRow_);
        int x = from[static_cast<size_t>(std::clamp(keyCol_, 0, static_cast<int>(from.size()) - 1))];
        keyRow_ += in.up ? -1 : 1;
        auto to = keyCenters(keyRow_);
        int best = 0;
        for (int k = 1; k < static_cast<int>(to.size()); ++k)
            if (std::abs(to[static_cast<size_t>(k)] - x) < std::abs(to[static_cast<size_t>(best)] - x)) best = k;
        keyCol_ = std::clamp(best, 0, std::max(0, static_cast<int>(to.size()) - 1));
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
            pressFilter(filterRow(keyRow_ - kKeyRows - 1)[static_cast<size_t>(keyCol_)]);
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

}  // namespace xc::ui
