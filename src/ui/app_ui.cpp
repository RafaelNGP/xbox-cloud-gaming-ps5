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

enum Icon { kIconCross, kIconCircle, kIconOptions, kIconTriangle, kIconTouchpad };

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
    dirty_ = true;
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

UiEvent AppUi::handle(const NavInput& in) {
    std::lock_guard<std::mutex> lock(mutex_);
    UiEvent ev;
    switch (screen_) {
        case Screen::Home: {
            if (rows_.empty()) {
                if (in.options) {
                    screen_ = Screen::Settings;
                    settingsRow_ = 0;
                    dirty_ = true;
                }
                break;
            }
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
            if (in.accept && focusedTile()) {
                screen_ = Screen::Details;
                dirty_ = true;
            }
            if (in.options) {
                screen_ = Screen::Settings;
                settingsRow_ = 0;
                dirty_ = true;
            }
            break;
        }
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
            if (in.accept && focusedTile()) {
                ev.action = Action::Play;
                ev.game = *focusedTile();
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

void AppUi::drawHome(Canvas& c, uint64_t nowMs) {
    const GameTile* focus = focusedTile();
    c.clear(kBg);
    drawHero(c, nowMs, focus ? focus->heroUrl : std::string(), false);
    drawTopBar(c);

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
            if (row.gamePassBadges) {
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
    const GameTile* g = focusedTile();
    c.clear(kBg);
    if (!g) return;
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
