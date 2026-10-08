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
#include "ui/pad_icons.h"

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
    // Free in the store (free-to-play): the "Free" search filter.
    bool freeInStore = false;
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
    bool l1 = false, r1 = false, square = false, triangle = false, r3 = false;
    bool l2 = false, r2 = false;  // pressed (triggers past halfway)
    bool touchpad = false;  // held right now (sign out needs a 5 s hold)
    uint64_t nowMs = 0;
};

// What the Settings screen edits.
struct SettingsChoice {
    int language = 0;      // ui::Language
    int resolution = 0;    // 0 = 1080p, 1 = 720p, 2 = 1440p (experimental)
    std::string region;    // gssv region name; empty = automatic
    int deadzone = 3;      // index into kDeadzonePercent (15 %)
    bool triggerRumble = true;
    bool circleConfirms = false;  // Circle is Xbox A (and Cross is B)
};

// The stick dead zones Settings offers, in percent of the travel.
constexpr int kDeadzonePercent[] = {0, 5, 10, 15, 20, 25};
constexpr int kSettingRows = 6;  // language, resolution, region, dead zone, triggers, confirm

enum class Action { None, Play, SignOut, Retry, CancelLaunch, SettingsChanged, PrefsChanged };

// How "Your games" is sorted (R3).
enum class LibrarySort { Recent, AZ, Console, Count };

// A store price, as shown and as a number (for sorting and the filters).
struct PriceInfo {
    std::string now, was;  // formatted; `was` empty unless on sale
    double list = 0, msrp = 0;
};

struct UiEvent {
    Action action = Action::None;
    GameTile game;
    SettingsChoice settings;  // SettingsChanged
    // PrefsChanged: hidden games (product ids) and the "Your games" order.
    std::vector<std::string> hidden;
    LibrarySort librarySort = LibrarySort::Recent;
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
    // The lists' background refresh: shown as a bar in "Your games" and the
    // search; `total` 0 = running, no measure yet.
    void setLoading(bool active, size_t done, size_t total);
    // What each tab's search looks through.
    void setSearchPools(std::vector<GameTile> gamePass, std::vector<GameTile> library);
    // Measured round trip to each region, ms (shown in Settings).
    void setRegionLatency(std::map<std::string, int> ms);
    // Store prices, formatted: productId -> {now, regular (empty unless on sale)}.
    void setPrices(const std::map<std::string, PriceInfo>& prices);
    // Saved preferences: hidden games and the "Your games" order.
    void setPrefs(const std::vector<std::string>& hidden, LibrarySort sort);
    // Games to buy on screen (or about to be) whose price hasn't been asked
    // for yet; with `background`, any other game to buy once those are done
    // (never while streaming). Marks them asked.
    std::vector<std::string> pricesWanted(size_t max, bool background = false);
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
    // The confirm button in force (the hints show it); Settings may be
    // showing another choice not saved yet.
    void setCircleConfirms(bool on);
    // The controllers in use (bottom left of the home screen).
    void setPads(const PadSlots& pads);
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
    void drawPads(Canvas& c);
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
        int headerY[3] = {0, 0, 0};     // section headers, relative (0: yours, 1: to buy, 2: hidden)
    };
    LibraryLayout libraryLayout() const;
    const GameTile* libraryTile(int index) const;
    // Applies hiding and sorting to the received lists (caller holds mutex_).
    // `keepPosition`: the cursor stays where it is on screen (same section
    // and place, same row and column) instead of following its game.
    void rebuild(bool keepPosition = false);
    void setRowsLocked(std::vector<GameRow> rows);
    // Hides / shows a game; fills `ev` so the app saves it.
    void toggleHidden(const GameTile& tile, UiEvent& ev);
    void prefsEvent(UiEvent& ev) const;
    // The filter buttons of the current tab's search.
    std::vector<std::string> filterLabels() const;
    void pressFilter(int index);
    bool anyFilter() const { return filterFree_ || filterCheapest_ || filterSale_ || filterConsole_; }
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
    std::vector<GameTile> owned_, purchasable_, hiddenTiles_;  // as shown
    // As received, before hiding and sorting (rebuild() derives the above).
    std::vector<GameRow> allRows_;
    std::vector<GameTile> allOwned_, allPurchasable_;
    std::set<std::string> hidden_;
    bool loading_ = false;
    size_t loadingDone_ = 0, loadingTotal_ = 0;
    void drawLoadingBar(Canvas& c, int x, int y, int w, uint64_t nowMs);
    LibrarySort librarySort_ = LibrarySort::Recent;
    // Search filters (the row of buttons under the keys).
    bool filterFree_ = false, filterCheapest_ = false, filterSale_ = false;
    int filterConsole_ = 0;  // 0 all, 1 Series X|S, 2 Xbox One, 3 Xbox 360
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
    bool circleConfirms_ = false;
    PadSlots pads_{};
    std::vector<std::string> regions_;
    std::string defaultRegion_;
    std::map<std::string, int> regionMs_;
    std::map<std::string, PriceInfo> prices_;
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
