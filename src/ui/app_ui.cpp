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

enum Icon { kIconCross, kIconCircle, kIconOptions, kIconTriangle, kIconTouchpad, kIconSquare };

// Grids ("Your games", search results): cards with the name below.
constexpr int kGridPitchY = 300;
constexpr int kLibraryCols = 6;
constexpr int kResultCols = 4;
constexpr int kGridTop = 236;       // y of the first card row
constexpr int kResultsX = 760;
constexpr size_t kMaxResults = 60;

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

std::string withoutSpaces(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    return s;
}

constexpr uint64_t kSignOutHoldMs = 5000;

}  // namespace

std::string prettyRegion(const std::string& name) {
    static const char* kWords[] = {"SOUTHEAST", "NORTHEAST", "CENTRAL", "AUSTRALIA", "GERMANY", "EUROPE", "BRAZIL",
                                   "CANADA", "FRANCE", "MEXICO", "SWEDEN", "JAPAN",   "KOREA",  "INDIA", "NORTH",
                                   "SOUTH",  "EAST",   "WEST",   "ASIA",   "UK",      "US"};
    std::string out;
    size_t i = 0;
    while (i < name.size()) {
        bool matched = false;
        for (const char* w : kWords) {
            size_t n = std::char_traits<char>::length(w);
            if (name.compare(i, n, w) == 0) {
                std::string word = n <= 2 ? std::string(w) : std::string(1, w[0]);
                if (n > 2)
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
    // Remember, per row title, which game had the focus and the scroll.
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

void AppUi::setOwned(std::vector<GameTile> tiles, bool known) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string focused = gridFocus_ < static_cast<int>(owned_.size()) ? owned_[static_cast<size_t>(gridFocus_)].productId : "";
    owned_ = std::move(tiles);
    ownedKnown_ = known;
    gridFocus_ = 0;
    for (size_t i = 0; i < owned_.size(); ++i)
        if (owned_[i].productId == focused) gridFocus_ = static_cast<int>(i);
    refreshDetail();
    dirty_ = true;
}

void AppUi::setSearchPool(std::vector<GameTile> pool) {
    std::lock_guard<std::mutex> lock(mutex_);
    pool_ = std::move(pool);
    runSearch();
    refreshDetail();
    dirty_ = true;
}

Tab AppUi::tab() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tab_;
}

void AppUi::openDetails(const GameTile& tile) {
    // Caller holds mutex_.
    detail_ = tile;
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
    for (const auto& t : owned_) take(t);
    for (const auto& t : pool_) take(t);
}

void AppUi::runSearch() {
    // Caller holds mutex_.
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
    if (words.empty()) return;
    std::string compact = withoutSpaces(q);
    struct Hit {
        int score;
        const GameTile* tile;
    };
    std::vector<Hit> hits;
    for (const auto& t : pool_) {
        std::string name = fold(t.name);
        bool all = true;
        for (const auto& w : words)
            if (name.find(w) == std::string::npos) all = false;
        if (!all && withoutSpaces(name).find(compact) == std::string::npos) continue;
        // Starts with the query, then a word starting with it, then the rest.
        int score = name.rfind(words[0], 0) == 0 ? 0 : name.find(" " + words[0]) != std::string::npos ? 1 : 2;
        if (!t.playable) score += 3;
        hits.push_back({score, &t});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.score != b.score) return a.score < b.score;
        return a.tile->name < b.tile->name;
    });
    for (size_t i = 0; i < hits.size() && i < kMaxResults; ++i) results_.push_back(*hits[i].tile);
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

void AppUi::changeSetting(int delta) {
    // Caller holds mutex_.
    if (settingsRow_ == 0) {
        int n = static_cast<int>(Language::Count);
        settings_.language = (settings_.language + delta + n) % n;
        setLanguage(static_cast<Language>(settings_.language));  // the UI switches right away
    } else if (settingsRow_ == 1) {
        settings_.resolution = (settings_.resolution + delta + 3) % 3;
    } else {
        // Index 0 is automatic, then the regions in the login's order.
        int n = static_cast<int>(regions_.size()) + 1;
        int cur = 0;
        for (size_t i = 0; i < regions_.size(); ++i)
            if (regions_[i] == settings_.region) cur = static_cast<int>(i) + 1;
        cur = (cur + delta + n) % n;
        settings_.region = cur == 0 ? std::string() : regions_[static_cast<size_t>(cur - 1)];
    }
    dirty_ = true;
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

void AppUi::handleHome(const NavInput& in, UiEvent& ev) {
    // Caller holds mutex_.
    if (in.l1 || in.r1) {
        int t = (static_cast<int>(tab_) + (in.r1 ? 1 : 2)) % 3;
        tab_ = static_cast<Tab>(t);
        if (tab_ == Tab::Search) searchOnKeys_ = true;
        dirty_ = true;
        return;
    }
    if (in.options) {
        screen_ = Screen::Settings;
        settingsRow_ = 0;
        dirty_ = true;
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
            break;
        }
        case Tab::Library:
            if (owned_.empty()) break;
            if (moveInGrid(gridFocus_, static_cast<int>(owned_.size()), kLibraryCols, in)) {
                gridScroll_.target = gridTarget(gridFocus_, kLibraryCols);
                dirty_ = true;
            }
            if (in.accept) openDetails(owned_[static_cast<size_t>(gridFocus_)]);
            break;
        case Tab::Search:
            if (searchOnKeys_) {
                handleSearchKeys(in);
                break;
            }
            if (in.back) {  // back from the results to the keys
                searchOnKeys_ = true;
                dirty_ = true;
                break;
            }
            if (in.left && resultFocus_ % kResultCols == 0) {
                searchOnKeys_ = true;
                dirty_ = true;
                break;
            }
            if (in.square && !query_.empty()) {  // delete without going back to the keys
                query_.pop_back();
                runSearch();
                searchOnKeys_ = results_.empty();
                dirty_ = true;
                break;
            }
            if (moveInGrid(resultFocus_, static_cast<int>(results_.size()), kResultCols, in)) {
                resultScroll_.target = gridTarget(resultFocus_, kResultCols);
                dirty_ = true;
            }
            if (in.accept && resultFocus_ < static_cast<int>(results_.size()))
                openDetails(results_[static_cast<size_t>(resultFocus_)]);
            break;
    }
}

void AppUi::handleSearchKeys(const NavInput& in) {
    // Caller holds mutex_. keyRow_ == kKeyRows is the row of wide keys, where
    // keyCol_ is 0..2 (Space, Delete, Clear).
    bool wide = keyRow_ == kKeyRows;
    if (in.up && keyRow_ > 0) {
        if (wide) keyCol_ = kWideCols[keyCol_];
        --keyRow_;
    } else if (in.down && keyRow_ < kKeyRows) {
        ++keyRow_;
        if (keyRow_ == kKeyRows) keyCol_ = keyCol_ < 3 ? 0 : keyCol_ < 5 ? 1 : 2;
    } else if (in.left && keyCol_ > 0) {
        --keyCol_;
    } else if (in.right) {
        int last = wide ? 2 : kKeyCols - 1;
        if (keyCol_ < last) {
            ++keyCol_;
        } else if (!results_.empty()) {  // on to the results
            searchOnKeys_ = false;
            resultFocus_ = 0;
        }
    }
    bool edited = false;
    if (in.accept) {
        if (!wide) {
            query_ += kKeys[keyRow_ * kKeyCols + keyCol_];
        } else if (keyCol_ == 0) {
            if (!query_.empty() && query_.back() != ' ') query_ += ' ';
        } else if (keyCol_ == 1) {
            if (!query_.empty()) query_.pop_back();
        } else {
            query_.clear();
        }
        edited = true;
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
            if (in.down && settingsRow_ < 2) ++settingsRow_, dirty_ = true;
            if (in.up && settingsRow_ > 0) --settingsRow_, dirty_ = true;
            if (in.right || in.accept) changeSetting(+1);
            if (in.left) changeSetting(-1);
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
        if (it->first == kIconTouchpad) {
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

void AppUi::drawHero(Canvas& c, uint64_t nowMs, const std::string& url, bool details) {
    if (url != heroUrl_) {
        prevHeroUrl_ = heroUrl_;
        heroUrl_ = url;
        heroSince_ = nowMs;
    }
    auto current = images_.get(heroUrl_, kW, kH);
    auto previous = prevHeroUrl_.empty() ? nullptr : images_.get(prevHeroUrl_, kW, kH);
    uint64_t age = nowMs - heroSince_;
    if (previous && (!current || age < kHeroFadeMs)) c.drawImage(*previous, 0, 0);
    if (current) {
        uint8_t alpha = age >= kHeroFadeMs || !previous ? 255 : static_cast<uint8_t>(age * 255 / kHeroFadeMs);
        c.drawImage(*current, 0, 0, alpha);
        if (alpha < 255) animating_ = true;
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
    const char* labels[3] = {tr(Str::TabGamePass), tr(Str::YourGames), tr(Str::TabSearch)};
    constexpr int kPx = 22, kPad = 26, kGap = 12, kH = 44, kY = 50;
    int widths[3], total = 0;
    for (int i = 0; i < 3; ++i) {
        widths[i] = fonts_.semibold.measure(labels[i], kPx) + 2 * kPad;
        total += widths[i] + (i ? kGap : 0);
    }
    int x = (kW - total) / 2;
    auto shoulder = [&](const char* name, int cx) {
        Rect r{cx - 22, kY + 8, 44, 28};
        c.fillRect(r, rgba(255, 255, 255, 40), 8);
        fonts_.bold.draw(c, name, cx - fonts_.bold.measure(name, 16) / 2, kY + 13, 16, kGray);
    };
    shoulder("L1", x - 40);
    for (int i = 0; i < 3; ++i) {
        bool on = static_cast<int>(tab_) == i;
        Rect r{x, kY, widths[i], kH};
        if (on) c.fillRect(r, kWhite, kH / 2);
        fonts_.semibold.draw(c, labels[i], x + kPad, kY + 10, kPx, on ? kBg : kGray);
        x += widths[i] + kGap;
    }
    shoulder("R1", x - kGap + 40);
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
        const GameTile& t = tiles[i];
        if (auto img = images_.get(t.tileUrl, kCard, kCard)) {
            c.drawImage(*img, x, y, 255, 10);
        } else {
            c.fillRect({x, y, kCard, kCard}, kPlaceholder, 10);
            auto lines = fonts_.semibold.wrap(t.name, 22, kCard - 32, 3);
            for (size_t k = 0; k < lines.size(); ++k)
                fonts_.semibold.draw(c, lines[k], x + 16, y + 16 + static_cast<int>(k) * 30, 22, kGray);
        }
        if (!t.playable) {
            c.fillRect({x, y, kCard, kCard}, rgba(0, 0, 0, 150), 10);
            drawLock(c, x + kCard - 34, y + 30);
        }
        bool focused = static_cast<int>(i) == focus;
        auto name = fonts_.semibold.wrap(t.name, 20, kCard, 1);
        if (!name.empty()) fonts_.semibold.draw(c, name[0], x, y + kCard + 12, 20, focused ? kWhite : kGray);
        if (focused) c.strokeRect({x - 7, y - 7, kCard + 14, kCard + 14}, kWhite, 4, 16);
    }
    // Rows scrolled up (cards and names) disappear under the header.
    c.fillRect({0, 0, kW, clipTop}, kBg);
    c.gradientV({0, clipTop, kW, 12}, kBg, withAlpha(kBg, 0));
}

void AppUi::drawLibrary(Canvas& c, uint64_t nowMs) {
    drawBackground(c);
    constexpr int kGridW = kLibraryCols * kCardPitch - kCardGap;
    drawGrid(c, owned_, (kW - kGridW) / 2, kGridTop, kLibraryCols, gridScroll_.value, gridFocus_, kGridTop - 20);
    drawTopBar(c);
    drawTabs(c);
    const int left = (kW - (kLibraryCols * kCardPitch - kCardGap)) / 2;
    fonts_.bold.draw(c, tr(Str::YourGames), left, 130, 40, kWhite);
    if (!owned_.empty()) {
        std::string count = trf(Str::GamesCount, std::to_string(owned_.size()));
        fonts_.semibold.draw(c, count, left + fonts_.bold.measure(tr(Str::YourGames), 40) + 24, 144, 24, kDim);
    } else if (!ownedKnown_) {
        drawSpinner(c, kW / 2.0f, 470, 26, nowMs);
        drawCentered(c, fonts_.semibold, tr(Str::LoadingGames), 530, 28, kGray);
        animating_ = true;
    } else {
        drawCentered(c, fonts_.semibold, tr(Str::NoGames), 500, 30, kGray);
    }
    c.gradientV({0, kH - 190, kW, 110}, withAlpha(kBg, 0), withAlpha(kBg, 245));
    c.fillRect({0, kH - 80, kW, 80}, withAlpha(kBg, 245));
    drawHints(c, {{kIconCross, tr(Str::Select)}, {kIconOptions, tr(Str::Settings)}, {kIconTouchpad, tr(Str::HoldSignOut)}});
    drawToast(c, nowMs);
}

void AppUi::drawSearch(Canvas& c, uint64_t nowMs) {
    drawBackground(c);
    // Results on the right.
    drawGrid(c, results_, kResultsX, kGridTop, kResultCols, resultScroll_.value, searchOnKeys_ ? -1 : resultFocus_,
             kGridTop - 20);
    drawTopBar(c);
    drawTabs(c);
    if (query_.empty()) {
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
        fonts_.regular.draw(c, tr(Str::TabSearch), tx, box.y + 20, 30, kDim);
    } else {
        fonts_.semibold.draw(c, query_, tx, box.y + 20, 30, kWhite);
        tx += fonts_.semibold.measure(query_, 30) + 4;
    }
    if (searchOnKeys_ && (nowMs / 530) % 2 == 0) c.fillRect({tx, box.y + 18, 3, 40}, kWhite);
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
            fonts_.semibold.draw(c, label, key.x + (key.w - w) / 2, key.y + (kKeyH - px) / 2 - 4, px,
                                 focused ? kBg : kWhite);
        }
    }
    c.gradientV({0, kH - 190, kW, 110}, withAlpha(kBg, 0), withAlpha(kBg, 245));
    c.fillRect({0, kH - 80, kW, 80}, withAlpha(kBg, 245));
    if (searchOnKeys_)
        drawHints(c, {{kIconCross, tr(Str::KeyType)}, {kIconSquare, tr(Str::KeyDelete)}, {kIconOptions, tr(Str::Settings)}});
    else
        drawHints(c, {{kIconCross, tr(Str::Select)}, {kIconSquare, tr(Str::KeyDelete)}, {kIconCircle, tr(Str::Back)}});
    drawToast(c, nowMs);
}

void AppUi::drawHome(Canvas& c, uint64_t nowMs) {
    if (tab_ == Tab::Library) return drawLibrary(c, nowMs);
    if (tab_ == Tab::Search) return drawSearch(c, nowMs);
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
            const GameTile& t = row.tiles[col];
            bool focused = static_cast<int>(r) == focusRow_ && static_cast<int>(col) == focusCol_[r];
            if (auto img = images_.get(t.tileUrl, kCard, kCard)) {
                c.drawImage(*img, x, cardY, 255, 10);
            } else {
                c.fillRect({x, cardY, kCard, kCard}, kPlaceholder, 10);
                auto lines = fonts_.semibold.wrap(t.name, 22, kCard - 32, 3);
                for (size_t i = 0; i < lines.size(); ++i)
                    fonts_.semibold.draw(c, lines[i], x + 16, cardY + 16 + static_cast<int>(i) * 30, 22, kGray);
            }
            if (!t.playable) {  // dimmed, with a lock: can't be streamed on this account
                c.fillRect({x, cardY, kCard, kCard}, rgba(0, 0, 0, 150), 10);
                drawLock(c, x + kCard - 34, cardY + 30);
            } else if (row.gamePassBadges) {
                Rect badge{x + 10, cardY + kCard - 34, 96, 24};
                c.fillRect(badge, rgba(0, 0, 0, 210), 4);
                fonts_.bold.draw(c, "GAME PASS", badge.x + 9, badge.y + 4, 13, kWhite);
            }
            if (focused) c.strokeRect({x - 7, cardY - 7, kCard + 14, kCard + 14}, kWhite, 4, 16);
        }
    }
    // Fade the rows out under the button hints.
    c.gradientV({0, kH - 190, kW, 110}, withAlpha(kBg, 0), withAlpha(kBg, 245));
    c.fillRect({0, kH - 80, kW, 80}, withAlpha(kBg, 245));
    drawHints(c, {{kIconCross, tr(Str::Select)}, {kIconOptions, tr(Str::Settings)}, {kIconTouchpad, tr(Str::HoldSignOut)}});
    drawToast(c, nowMs);
}

void AppUi::drawDetails(Canvas& c, uint64_t nowMs) {
    const GameTile* g = &detail_;
    c.clear(kBg);
    if (g->productId.empty()) return;
    drawHero(c, nowMs, g->heroUrl, true);
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
    if (!g->playable) {
        // Bought separately or outside the subscription: no Play button.
        Rect note{kMargin, std::max(y + 48, 620), 560, 76};
        c.fillRect(note, rgba(255, 255, 255, 40), 38);
        drawLock(c, note.x + 52, note.y + note.h / 2);
        fonts_.semibold.draw(c, tr(Str::NotPlayable), note.x + 90, note.y + 22, 28, kGray);
        drawHints(c, {{kIconCircle, tr(Str::Back)}});
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
    drawHints(c, {{kIconCross, tr(Str::Play)}, {kIconCircle, tr(Str::Back)}});
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
    struct Row {
        const char* label;
        std::string value;
    };
    std::string region = settings_.region.empty()
                             ? trf(Str::RegionAuto, defaultRegion_.empty() ? "-" : prettyRegion(defaultRegion_))
                             : prettyRegion(settings_.region);
    const Row rows[] = {{tr(Str::Language), languageName(static_cast<Language>(settings_.language))},
                        {tr(Str::Resolution), tr(settings_.resolution == 1   ? Str::Res720
                                                : settings_.resolution == 2 ? Str::Res1440
                                                                            : Str::Res1080)},
                        {tr(Str::Region), region}};
    constexpr int kRowW = 1200, kRowH = 92;
    int y = 290;
    for (int i = 0; i < 3; ++i) {
        Rect r{kMargin, y, kRowW, kRowH};
        bool focused = i == settingsRow_;
        c.fillRect(r, focused ? rgba(255, 255, 255, 36) : rgba(255, 255, 255, 14), 16);
        if (focused) c.strokeRect({r.x - 5, r.y - 5, r.w + 10, r.h + 10}, kWhite, 3, 20);
        fonts_.semibold.draw(c, rows[i].label, r.x + 36, r.y + 28, 30, kWhite);
        std::string value = focused ? "\xE2\x80\xB9   " + rows[i].value + "   \xE2\x80\xBA" : rows[i].value;
        int w = fonts_.regular.measure(value, 30);
        fonts_.regular.draw(c, value, r.x + r.w - 36 - w, r.y + 28, 30, focused ? kWhite : kGray);
        y += kRowH + 22;
    }
    for (const auto& line : fonts_.regular.wrap(tr(Str::SettingsNote), 24, kRowW, 2)) {
        fonts_.regular.draw(c, line, kMargin, y + 20, 24, kDim);
        y += 34;
    }
    drawHints(c, {{kIconCross, tr(Str::Change)}, {kIconCircle, tr(Str::Back)}});
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
