// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include "ui/app_ui.h"
#include "ui/input_mode.h"
#include "ui/pad_glyphs.h"
#include "ui/pad_icons.h"

#include "input/tuning.h"
#include "xcloud/catalog.h"
#include "ui/accent_color.h"

#include "platform/platform.h"
#include "platform/toasts.h"
#include "ui/brand.h"
#include "ui/strings.h"
#include "ui/toast_view.h"
#include "app/update_check.h"

#include "qrcodegen.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace xc::ui {

// A padlock centred on (cx, cy), about 24x30 px: a game the account can't play.
inline void drawLock(Canvas& c, int cx, int cy) {
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

enum Icon { kIconCross, kIconCircle, kIconOptions, kIconTriangle, kIconTouchpad, kIconSquare, kIconR3, kIconL3, kIconL2R2, kIconDpadLR, kIconL1R1 };

// The resolution list, lowest first: SettingsChoice::resolution value per
// position (0 = 1080p, 1 = 720p, 2 = 1440p, as saved).
constexpr int kResolutionOrder[3] = {1, 0, 2};
// Settings' rows on screen, alike ones together (the row ids stay what
// settingsRow_ and the helpers use): language, sign-in country, and sign-in check; the
// stream (resolution, a desktop's display mode, 120 Hz, bitrate, region, network test);
// the controller, keyboard and queue-ready alert; support and updates last. A group starts at each
// kSettingsGroupStart.
constexpr int kSettingsCount = 23;
constexpr int kSettingsOrder[kSettingsCount] = {0, 2, 8, 17, 18, 1, 20, 13, 9, 10, 14, 12, 3, 15, 4, 21, 5, 6, 16, 22, 19, 7, 23};
constexpr bool kSettingsGroupStart[kSettingsCount] = {false, false, false, false, false, true, false, false, false, false,
                                                      true, false, false, false, false, false, false, false, false, false, true, false, true};
// The game settings' rows on screen (ids: 0 profile, 1 upscaler, 2
// sharpness, 3 block smoothing, 4 resolution, 5 dead zone, 6 triggers, 7
// confirm, 8 120 Hz, 9 keyboard keys, 10 back to the default, 11 controller
// buttons, 12 gyro aiming, 13 touchpad as mouse, 14 game language, 15 video decoder, 16 audio-to-haptics).
constexpr int kGameRowCount = 17;
constexpr int kGameRowOrder[kGameRowCount] = {0, 4, 8, 15, 14, 1, 2, 3, 5, 6, 16, 7, 12, 11, 13, 9, 10};
constexpr bool kGameGroupStart[kGameRowCount] = {false, true, false, false, false, true, false, false, true, false, false, false, false, false, false, false, true};
// The keyboard keys' screen: two columns of inputs, then "Reset to default".
constexpr int kKeysPerColumn = input::kKeyActions / 2;
// Updates on a desktop: the newest release's page (the app doesn't install it there).
constexpr const char* kReleasesPage = "https://github.com/RafaelNGP/xbox-cloud-gaming-ps5/releases/latest";

// The rows of `order` that `shown` keeps. A group starts on screen where the
// row above belongs to another group (a hidden group leaves no gap).
template <size_t N, class Shown>
std::vector<SettingsRowPos> filterRows(const int (&order)[N], const bool (&groupStart)[N], Shown shown) {
    std::vector<SettingsRowPos> out;
    int group = 0, lastGroup = -1;
    for (size_t pos = 0; pos < N; ++pos) {
        if (groupStart[pos]) ++group;
        if (!shown(order[pos])) continue;
        out.push_back({order[pos], !out.empty() && group != lastGroup});
        lastGroup = group;
    }
    return out;
}

// Where `row` is in `rows` (0 when it isn't there).
inline int posOf(const std::vector<SettingsRowPos>& rows, int row) {
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].row == row) return static_cast<int>(i);
    return 0;
}

// Grids ("Your games", search results): cards with the name below.
constexpr int kGridPitchY = 300;
constexpr int kLibraryCols = 6;
constexpr int kResultCols = 4;
// Keeps the focused grid row on screen: scrolls once it passes the second row.
inline float gridTarget(int focus, int cols) { return static_cast<float>(std::max(0, focus / cols - 1) * kGridPitchY); }
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
constexpr int kKeysW = kKeyCols * (kKeyW + kKeyGap) - kKeyGap;
constexpr int kChipGap = 10;
inline int chipWidth(int count) { return (kKeysW - kChipGap * (count - 1)) / std::max(1, count); }

// Lower case, accents dropped (Latin-1 range), anything else a space: what
// the search compares.
inline std::string fold(const std::string& s) {
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
inline std::string gamesCount(size_t n) { return n == 1 ? std::string(tr(Str::OneGame)) : trf(Str::GamesCount, std::to_string(n)); }

inline std::string withoutSpaces(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    return s;
}

inline bool matchesPublisher(const std::string& rawPublisher, int filter) {
    if (filter <= 0) return true;
    std::string p = fold(rawPublisher);

    auto isXbox = [&]() {
        return p.find("xbox game studios") != std::string::npos ||
               p.find("microsoft") != std::string::npos ||
               p.find("bethesda") != std::string::npos ||
               p.find("mojang") != std::string::npos ||
               p.find("activision") != std::string::npos ||
               p.find("blizzard") != std::string::npos ||
               p.find("zenimax") != std::string::npos ||
               p.find("id software") != std::string::npos ||
               p.find("obsidian") != std::string::npos ||
               p.find("ninja theory") != std::string::npos ||
               p.find("double fine") != std::string::npos;
    };
    auto isEA = [&]() {
        return p.find("electronic arts") != std::string::npos ||
               p.find("ea swiss") != std::string::npos ||
               p.find("ea sports") != std::string::npos ||
               p == "ea" || p.find("ea ") == 0 ||
               p.find(" ea ") != std::string::npos;
    };
    auto isUbisoft = [&]() { return p.find("ubisoft") != std::string::npos; };
    auto isSquareEnix = [&]() { return p.find("square enix") != std::string::npos; };
    auto isWarner = [&]() { return p.find("warner") != std::string::npos || p.find("wb games") != std::string::npos; };
    auto is2K = [&]() {
        return p.find("2k") != std::string::npos ||
               p.find("take-two") != std::string::npos ||
               p.find("take two") != std::string::npos ||
               p.find("rockstar") != std::string::npos ||
               p.find("private division") != std::string::npos;
    };
    auto isCapcom = [&]() { return p.find("capcom") != std::string::npos; };
    auto isSega = [&]() { return p.find("sega") != std::string::npos || p.find("atlus") != std::string::npos; };
    auto isBandai = [&]() {
        return p.find("bandai namco") != std::string::npos ||
               p.find("namco bandai") != std::string::npos ||
               p.find("bandai") != std::string::npos;
    };
    auto isTHQ = [&]() {
        return p.find("thq nordic") != std::string::npos ||
               p.find("plaion") != std::string::npos ||
               p.find("deep silver") != std::string::npos ||
               p.find("koch media") != std::string::npos ||
               p.find("handygames") != std::string::npos ||
               p.find("embracer") != std::string::npos;
    };
    auto isFocus = [&]() { return p.find("focus entertainment") != std::string::npos || p.find("focus home") != std::string::npos || p == "focus"; };
    auto isDevolver = [&]() { return p.find("devolver") != std::string::npos; };
    auto isAnnapurna = [&]() { return p.find("annapurna") != std::string::npos; };
    auto isTeam17 = [&]() { return p.find("team17") != std::string::npos || p.find("team 17") != std::string::npos; };

    switch (filter) {
    case 1: return isXbox();
    case 2: return isEA();
    case 3: return isUbisoft();
    case 4: return isSquareEnix();
    case 5: return isWarner();
    case 6: return is2K();
    case 7: return isCapcom();
    case 8: return isSega();
    case 9: return isBandai();
    case 10: return isTHQ();
    case 11: return isFocus();
    case 12: return isDevolver();
    case 13: return isAnnapurna();
    case 14: return isTeam17();
    case 15: // Indies & Outras
        return !isXbox() && !isEA() && !isUbisoft() && !isSquareEnix() &&
               !isWarner() && !is2K() && !isCapcom() && !isSega() &&
               !isBandai() && !isTHQ() && !isFocus() && !isDevolver() &&
               !isAnnapurna() && !isTeam17();
    default: return true;
    }
}

constexpr uint64_t kSignOutHoldMs = 5000;

inline std::string loginCountryName(const std::string& country) {
    static const std::pair<const char*, Str> kNames[] = {
        {"br", Str::CountryBR}, {"jp", Str::CountryJP}, {"kr", Str::CountryKR}, {"pl", Str::CountryPL}, {"us", Str::CountryUS}};
    for (const auto& [code, name] : kNames)
        if (country == code) return tr(name);
    return {};
}

inline int consoleRank(const std::string& platform) {
    return platform == "XS" ? 0 : platform == "ONE" ? 1 : platform == "360" ? 2 : 3;
}

// Hue 0..1, saturation and value 0..1.
inline Color hsvColour(float h, float s, float v) {
    h = (h - std::floor(h)) * 6.0f;
    int i = static_cast<int>(h) % 6;
    float f = h - std::floor(h), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float r = i == 0 || i == 5 ? v : i == 1 ? q : i == 2 || i == 3 ? p : t;
    float g = i == 0 ? t : i == 1 || i == 2 ? v : i == 3 ? q : p;
    float b = i == 2 ? t : i == 3 || i == 4 ? v : i == 5 ? q : p;
    auto byte = [](float x) { return static_cast<uint8_t>(std::lround(std::clamp(x, 0.0f, 1.0f) * 255)); };
    return rgba(byte(r), byte(g), byte(b));
}

inline void colourHsv(Color c, float& h, float& s, float& v) {
    float r = (c & 0xFF) / 255.0f, g = (c >> 8 & 0xFF) / 255.0f, b = (c >> 16 & 0xFF) / 255.0f;
    float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    v = mx;
    s = mx > 0 ? d / mx : 0;
    if (d <= 0) h = 0;
    else if (mx == r) h = (g - b) / d / 6.0f;
    else if (mx == g) h = ((b - r) / d + 2) / 6.0f;
    else h = ((r - g) / d + 4) / 6.0f;
    if (h < 0) h += 1;
}

constexpr int kWheelR = 180;
constexpr float kMinValue = 0.2f;  // darker turns the light bar all but off

// Settings' bitrate caps (Mbps, after Automatic) and game volumes (percent).
constexpr int kBitrateCaps[] = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50};
constexpr int kVolumes[] = {25, 50, 75, 100, 150};

// Moves `focus` in a grid of `count` items, `cols` wide. Returns true if it moved.
inline bool moveInGrid(int& focus, int count, int cols, const NavInput& in) {
    int before = focus;
    if (in.right && focus + 1 < count && (focus + 1) % cols != 0) ++focus;
    if (in.left && focus % cols != 0) --focus;
    if (in.down && focus + cols < count) focus += cols;
    else if (in.down && focus / cols < (count - 1) / cols) focus = count - 1;  // onto a short last row
    if (in.up && focus >= cols) focus -= cols;
    return focus != before;
}

inline PadButton faceOf(int icon) {
    return icon == kIconCross ? PadButton::Cross
           : icon == kIconCircle ? PadButton::Circle
           : icon == kIconSquare ? PadButton::Square
                                 : PadButton::Triangle;
}
inline void drawFaceButton(Canvas& c, const Font& letters, int icon, float cx, float cy, float r) {
    ui::drawFaceButton(c, letters, g_glyphStyle, faceOf(icon), cx, cy, r);
}

// A QR code of `url` on a white card whose top-right corner is (right, top),
// about `target` px of code; the card's rect (empty when it can't be encoded).
inline Rect drawQr(Canvas& c, const std::string& url, int right, int top, int target,
            qrcodegen_Ecc ecc = qrcodegen_Ecc_MEDIUM) {
    if (url.empty()) return {};
    struct CachedQr {
        std::string url;
        qrcodegen_Ecc ecc = qrcodegen_Ecc_MEDIUM;
        uint8_t qr[qrcodegen_BUFFER_LEN_MAX]{};
        bool valid = false;
        bool attempted = false;
        uint64_t lastUsed = 0;
    };
    constexpr size_t kCacheSlots = 4;
    static CachedQr s_cache[kCacheSlots];
    static uint64_t s_usageCounter = 0;

    CachedQr* entry = nullptr;
    for (size_t i = 0; i < kCacheSlots; ++i) {
        if (s_cache[i].attempted && s_cache[i].url == url && s_cache[i].ecc == ecc) {
            entry = &s_cache[i];
            break;
        }
    }
    if (!entry) {
        size_t lruIndex = 0;
        uint64_t minUsed = s_cache[0].lastUsed;
        for (size_t i = 1; i < kCacheSlots; ++i) {
            if (!s_cache[i].attempted) {
                lruIndex = i;
                break;
            }
            if (s_cache[i].lastUsed < minUsed) {
                minUsed = s_cache[i].lastUsed;
                lruIndex = i;
            }
        }
        entry = &s_cache[lruIndex];
        uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
        entry->valid = qrcodegen_encodeText(url.c_str(), tmp, entry->qr, ecc,
                                            qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                                            qrcodegen_Mask_AUTO, true);
        entry->attempted = true;
        entry->url = url;
        entry->ecc = ecc;
    }
    entry->lastUsed = ++s_usageCounter;
    if (!entry->valid) return {};
    int n = qrcodegen_getSize(entry->qr);
    int module = target / (n + 2), size = module * (n + 2);
    Rect box{right - size - 40, top, size + 40, size + 40};
    c.fillRect(box, kWhite, 20);
    int ox = box.x + 20 + module, oy = box.y + 20 + module;
    for (int yy = 0; yy < n; ++yy)
        for (int xx = 0; xx < n; ++xx)
            if (qrcodegen_getModule(entry->qr, xx, yy)) c.fillRect({ox + xx * module, oy + yy * module, module, module}, kBg);
    return box;
}

inline const char* platformLabel(const std::string& code) {
    if (code == "360") return "XBOX 360";
    if (code == "ONE") return "XBOX ONE";
    if (code == "XS") return "SERIES X|S";
    return nullptr;
}

inline const char* planBadgeLabel(uint8_t plan) {
    if (plan == xcloud::kPlanEssential) return "ESSENTIAL";
    if (plan == xcloud::kPlanPremium) return "PREMIUM";
    if (plan == xcloud::kPlanUltimate) return "ULTIMATE";
    return nullptr;
}

inline const char* planDisplayName(uint8_t plan) {
    if (plan == xcloud::kPlanEssential) return "Essential";
    if (plan == xcloud::kPlanPremium) return "Premium";
    if (plan == xcloud::kPlanUltimate) return "Ultimate";
    return nullptr;
}

// A shopping bag centred on (cx, cy), about 22x24 px: a game to buy.
// A shopping bag (body + handle) centred on (cx, cy); `size` scales it
// (1 = 22 x 23 px).
inline void drawBag(Canvas& c, int cx, int cy, Color color, float size = 1.0f) {
    float w = 22 * size, h = 17 * size, r = 6 * size;
    float bodyTop = cy - (h + r) / 2 + r;
    c.fillRect({static_cast<int>(std::lround(cx - w / 2)), static_cast<int>(std::lround(bodyTop)),
                static_cast<int>(std::lround(w)), static_cast<int>(std::lround(h))},
               color, std::max(1, static_cast<int>(3 * size)));
    c.strokeArc(static_cast<float>(cx), bodyTop, r, std::max(1.5f, 2.5f * size), 3.1416f, 3.1416f, color);
}

// A 5-pointed star centred on (cx, cy) with outer radius r.
inline void drawStar(Canvas& c, float cx, float cy, float r, Color color) {
    constexpr float kPi = 3.14159265358979323846f;
    float innerR = r * 0.42f;
    float px[10], py[10];
    for (int i = 0; i < 10; ++i) {
        float angle = -kPi / 2.0f + i * (kPi / 5.0f);
        float radius = (i % 2 == 0) ? r : innerR;
        px[i] = cx + radius * std::cos(angle);
        py[i] = cy + radius * std::sin(angle);
    }
    for (int i = 0; i < 10; ++i) {
        int next = (i + 1) % 10;
        c.line(px[i], py[i], px[next], py[next], 1.2f, color);
        c.line(cx, cy, px[i], py[i], 1.5f, color);
    }
    c.fillCircle(cx, cy, innerR * 0.95f, color);
    for (int i = 0; i < 5; ++i) {
        int pt = i * 2;
        c.line(cx, cy, px[pt], py[pt], 2.0f, color);
    }
}

}  // namespace xc::ui
