// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The menus, styled after xbox.com/play: a hero background of the focused
// game, rows of box art, a details page with Play, plus the sign-in, loading
// and error screens. Pure UI: it renders into a Canvas and turns pad input
// into actions; the app's worker thread feeds it data.
//
// Model setters are thread-safe; handle() and render() run on the UI thread.
#pragma once

#include "ui/canvas.h"
#include "ui/font.h"
#include "ui/image_cache.h"

#include <cstdint>
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <vector>

namespace xc::ui {

struct GameTile {
    std::string productId;
    std::string titleId;  // xCloud title id used to start the session
    std::string name;
    std::string publisher;
    std::string description;
    std::string tileUrl;
    std::string heroUrl;
    std::vector<std::string> categories;
    // False once the account's titles are known and this one isn't among
    // them (bought separately, or not in the subscription).
    bool playable = true;
    // Not playable, but streams in the cloud once bought.
    bool purchasable = false;
    // Console it was made for: "360", "ONE", "XS" (xcloud/titlehub.h); empty
    // while unknown.
    std::string platform;
};

struct GameRow {
    std::string title;
    std::vector<GameTile> tiles;
    bool gamePassBadges = true;
};

enum class Screen { Splash, SignIn, Home, Details, Launching, Streaming, Error, Settings };

// The home screen's tabs (L1 / R1). Triangle searches the current one.
enum class Tab { GamePass, Library };

struct NavInput {
    bool up = false, down = false, left = false, right = false;
    bool accept = false, back = false, options = false;
    bool l1 = false, r1 = false, square = false, triangle = false;
    bool touchpad = false;  // held right now (sign out needs a 5 s hold)
    uint64_t nowMs = 0;
};

// What the Settings screen edits.
struct SettingsChoice {
    int language = 0;      // ui::Language
    int resolution = 0;    // 0 = 1080p, 1 = 720p, 2 = 1440p (experimental)
    std::string region;    // gssv region name; empty = automatic
};

enum class Action { None, Play, SignOut, Retry, CancelLaunch, SettingsChanged };

struct UiEvent {
    Action action = Action::None;
    GameTile game;
    SettingsChoice settings;  // SettingsChanged
};

// "SOUTHCENTRALUS" -> "South Central US".
std::string prettyRegion(const std::string& name);

class AppUi {
public:
    AppUi(const Fonts& fonts, ImageCache& images);

    // --- Model -------------------------------------------------------------
    void showSplash(const std::string& status);
    void showSignIn(const std::string& code, const std::string& url);
    void setProfile(const std::string& gamertag, const std::string& gamerpicUrl);
    // Replaces the rows, keeping the focus on the same game when possible.
    void setRows(std::vector<GameRow> rows);
    // "Your games": the account's games, then those to buy; `known` false
    // while the account's games load.
    void setOwned(std::vector<GameTile> owned, std::vector<GameTile> purchasable, bool known);
    // What each tab's search looks through.
    void setSearchPools(std::vector<GameTile> gamePass, std::vector<GameTile> library);
    // Measured round trip to each region, ms (shown in Settings).
    void setRegionLatency(std::map<std::string, int> ms);
    // Store prices, formatted: productId -> {now, regular (empty unless on sale)}.
    void setPrices(const std::map<std::string, std::pair<std::string, std::string>>& prices);
    // Games to buy on screen (or about to be) whose price hasn't been asked
    // for yet; marks them asked.
    std::vector<std::string> pricesWanted(size_t max);
    // The game whose page is open, if its description or hero art hasn't
    // arrived; asked again every 5 s while missing (empty otherwise).
    std::string detailWanted(uint64_t nowMs);
    // Fills in the open page when it shows `productId`, and keeps the details
    // for the next time that game's page opens.
    void setDetailInfo(const std::string& productId, const std::string& description, const std::string& publisher,
                       const std::vector<std::string>& categories, const std::string& heroUrl);
    // Opens a game's page (autoplay tests).
    void showDetails(const GameTile& tile);
    // The `index`-th game to buy, if loaded (autoplay tests).
    bool purchasableAt(size_t index, GameTile& out) const;
    void showHome(const std::string& toast = {});
    void showLaunching(const GameTile& game, const std::string& status);
    void setLaunchStatus(const std::string& status);
    void showStreaming();
    void showError(const std::string& message);
    void setSettings(const SettingsChoice& choice);
    // Regions offered by the account's xCloud login; `defaultRegion` is the
    // one "Automatic" picks.
    void setRegions(std::vector<std::string> regions, const std::string& defaultRegion);
    Screen screen() const;
    Tab tab() const;
    void invalidate();

    // --- UI thread -----------------------------------------------------------
    UiEvent handle(const NavInput& in);
    bool needsRedraw(uint64_t nowMs) const;
    void render(Canvas& c, uint64_t nowMs);

private:
    struct Anim {
        float value = 0, target = 0;
        bool step(float dt);  // true while moving
    };

    const GameTile* focusedTile() const;
    void retarget();
    void drawBackground(Canvas& c);
    // The game's backdrop; false (and nothing drawn) until its image is
    // there: never another game's art in the meantime.
    bool drawHero(Canvas& c, uint64_t nowMs, const std::string& url, bool details);
    void drawTopBar(Canvas& c);
    void drawLogo(Canvas& c, float cx, float cy, float r);
    void drawHints(Canvas& c, const std::vector<std::pair<int, const char*>>& hints);
    void drawSpinner(Canvas& c, float cx, float cy, float r, uint64_t nowMs);
    void drawToast(Canvas& c, uint64_t nowMs);
    void drawSplash(Canvas& c, uint64_t nowMs);
    void drawSignIn(Canvas& c, uint64_t nowMs);
    void drawHome(Canvas& c, uint64_t nowMs);
    void drawTabs(Canvas& c);
    void drawLibrary(Canvas& c, uint64_t nowMs);
    void drawSearch(Canvas& c, uint64_t nowMs);
    void drawCard(Canvas& c, const GameTile& t, int x, int y, bool focused, bool gamePassBadge);
    // A grid of cards with names below; `focus` < 0: none highlighted.
    void drawGrid(Canvas& c, const std::vector<GameTile>& tiles, int x0, int y0, int cols, float scroll, int focus,
                  int clipTop);
    // "Your games" as one grid of two sections: the tile index of `focus`,
    // its row, and each section's first row and header position.
    struct LibraryLayout {
        int cols = 6;
        std::vector<int> rowOf, colOf;  // per tile index (owned, then purchasable)
        std::vector<int> rowY;          // per row, y relative to the grid top
        std::vector<int> rowFirst;      // first tile index of each row
        int headerY[2] = {0, 0};        // section headers, relative
    };
    LibraryLayout libraryLayout() const;
    const GameTile* libraryTile(int index) const;
    void handleHome(const NavInput& in, UiEvent& ev);
    void handleSearchKeys(const NavInput& in);
    void runSearch();
    void openDetails(const GameTile& tile);
    void refreshDetail();
    void drawDetails(Canvas& c, uint64_t nowMs);
    void drawLaunching(Canvas& c, uint64_t nowMs);
    void drawError(Canvas& c);
    void drawSettings(Canvas& c);
    void changeSetting(int delta);
    // Settings drop-downs: the choices of a row, the chosen one, choosing.
    std::vector<std::string> settingOptions(int row) const;
    int settingSelected(int row) const;
    void applySetting(int row, int index);
    void drawCentered(Canvas& c, const Font& f, const std::string& text, int y, int px, Color color);

    const Fonts& fonts_;
    ImageCache& images_;

    mutable std::mutex mutex_;
    Screen screen_ = Screen::Splash;
    bool dirty_ = true;
    std::string status_;
    std::string code_, codeUrl_;
    std::string gamertag_, gamerpicUrl_;
    std::vector<GameRow> rows_;
    int focusRow_ = 0;
    std::vector<int> focusCol_;
    std::vector<Anim> rowScroll_;
    Anim rowY_;
    Tab tab_ = Tab::GamePass;
    std::vector<GameTile> owned_, purchasable_;
    bool ownedKnown_ = false;
    int gridFocus_ = 0;
    Anim gridScroll_;
    bool searching_ = false;  // the search of `tab_` is open
    std::vector<GameTile> gamePassPool_, libraryPool_, results_;
    std::string query_;
    bool searchOnKeys_ = true;
    int keyRow_ = 0, keyCol_ = 0;
    int resultFocus_ = 0;
    Anim resultScroll_;
    GameTile detail_;  // the game the details page shows
    GameTile launching_;
    std::string error_;
    SettingsChoice settings_;
    std::vector<std::string> regions_;
    std::string defaultRegion_;
    std::map<std::string, int> regionMs_;
    std::map<std::string, std::pair<std::string, std::string>> prices_;
    std::set<std::string> pricesAsked_;
    std::string detailAskedFor_;
    uint64_t detailAskedAt_ = 0;
    struct DetailInfo {
        std::string description, publisher, heroUrl;
        std::vector<std::string> categories;
    };
    std::map<std::string, DetailInfo> detailInfo_;  // fetched on demand, by product id
    int settingsRow_ = 0;
    bool dropdownOpen_ = false;  // the list of the focused settings row
    int dropdownIndex_ = 0, dropdownTop_ = 0;
    uint64_t signOutHoldStart_ = 0;  // TOUCHPAD hold in progress
    bool signOutLatched_ = false;    // fired; wait for release
    void drawSignOutHold(Canvas& c, uint64_t nowMs);
    std::string toast_;
    uint64_t toastUntil_ = 0;
    std::string heroUrl_, prevHeroUrl_;
    uint64_t heroSince_ = 0;    // when heroUrl_ became the wanted one
    uint64_t heroShownAt_ = 0;  // when its image first drew (0: not yet)
    uint64_t lastRender_ = 0;
    bool animating_ = false;
};

}  // namespace xc::ui
