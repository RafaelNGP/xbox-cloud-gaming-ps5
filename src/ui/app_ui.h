// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The menus, styled after xbox.com/play: a hero background of the focused
// game, rows of box art, a details page with Play, plus the sign-in, loading
// and error screens. Pure UI: it renders into a Canvas and turns pad input
// into actions; the app's worker thread feeds it data.
//
// Model setters are thread-safe; handle() and render() run on the UI thread.
#pragma once

#include "app/settings.h"
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

struct FriendPlaying {
    std::string gamertag;
    std::string gamerpicUrl;
};

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
    // The cloud takes native mouse and keyboard in it (xCloud's "MKB").
    bool mouseKeyboard = false;
    // Console it was made for: "360", "ONE", "XS" (xcloud/titlehub.h); empty
    // while unknown.
    std::string platform;
    // Not a game: the user's own Xbox (Remote Play); titleId is its serverId.
    bool homeConsole = false;
    // Once the store's full details are in (`detailed`): how it can be
    // played (xcloud::kMode* bits) and what is translated into the app's
    // language (xcloud::kLang* bits).
    bool detailed = false;
    uint32_t modes = 0;
    uint8_t languages = 0;
    // Active friends playing this game now
    std::vector<FriendPlaying> friends;
};

// One of the user's own consoles, as "My consoles" shows it.
struct ConsoleTile {
    std::string serverId, name;
    std::string type;   // "XboxSeriesX", "XboxOne"...
    std::string power;  // "On", "ConnectedStandby", "Off"...
};

struct GameRow {
    std::string title;
    std::vector<GameTile> tiles;
    bool gamePassBadges = true;
    bool isGrid = false;
};

enum class Screen { Splash, SignIn, Home, Details, Launching, Streaming, Error, Settings, Updating };

// The home screen's tabs (L1 / R1). Triangle searches the current one.
enum class Tab { GamePass, Library, Consoles };

struct NavInput {
    bool up = false, down = false, left = false, right = false;
    bool accept = false, back = false, options = false;
    bool l1 = false, r1 = false, square = false, triangle = false, r3 = false;
    bool l2 = false, r2 = false;  // pressed (triggers past halfway)
    bool touchpad = false;  // held right now (sign out needs a 5 s hold)
    float stickX = 0, stickY = 0;  // the left stick, -1..1 (the colour picker)
    // For the Settings testers: the D-pad alone (with repeat), the sticks
    // before the dead zone, and how far the triggers are pressed (0..1).
    bool dpadLeft = false, dpadRight = false;
    float rawLX = 0, rawLY = 0, rawRX = 0, rawRY = 0;
    float l2Analog = 0, r2Analog = 0;
    // Smooth scrolling (the right stick, or the touchpad with inertia):
    // rows down (+) / up (-) and cards right (+) / left (-) to move now.
    int scrollRows = 0, scrollCards = 0;
    uint64_t nowMs = 0;
};

// What the Settings screen edits.
struct SettingsChoice {
    int language = 0;      // ui::Language
    int resolution = 0;    // 0 = 1080p, 1 = 720p, 2 = 1440p (experimental)
    std::string region;    // gssv region name; empty = automatic
    std::string loginCountry;  // "Sign in as another country" (kLoginCountries); empty = off
    int deadzone[2] = {15, 15};  // left, right stick: percent of the travel, 0..kMaxDeadzone
    int triggerStrength = 2;     // 0 off .. 4 max (input/tuning.h)
    int triggerHz = 1;           // index into input::kTriggerHz
    int triggerResistance = 0;   // 0 off .. 3 strong
    bool triggerPulses = false;  // force pulses instead of the motor vibrating
    bool circleConfirms = false;  // Circle is Xbox A (and Cross is B)
    int lightBarMode = 0;         // 0 the game's colour, 1 lightBarColour, 2 off
    Color lightBarColour = rgba(0, 112, 220);
    int sharpness = 0;
    int deband = 3;
    int upscaler = 2;
    bool highRefresh = false;  // 120 Hz output while a game streams
    int maxBitrateMbps = 0;    // 0 automatic, else 20 / 15 / 10 / 5
    int gameVolume = 100;      // percent: 25 / 50 / 75 / 100 / 150
    input::KeyMap keyMap = input::defaultKeyMap();  // the keyboard as a player's controller
};

// What this system and its pads can do, filled in by the app from the pads'
// capabilities (input/input_ext.h), display::caps() and shell::caps(): the
// screens hide the options that can't work here and word the rest for the
// platform. The defaults are the PS5's, where every option shows.
struct UiCaps {
    bool lightBar = true;          // a pad with a light bar
    bool triggerRumble = true;     // vibration in the triggers (DualSense, Xbox impulse triggers)
    bool adaptiveTriggers = true;  // the DualSense's resistance and pulses
    bool gyro = true;              // gyro aiming
    bool touchpad = true;          // the touchpad's gestures (else the buttons and keys that do the same)
    bool upscalers = true;         // FSR / Anime4K
    bool sharpen = true;
    bool deband = true;            // block smoothing
    bool highRefresh = true;       // a 120 Hz output setting
    bool console = true;           // "this console" and the PS5 menus' paths (else "this computer")
    bool openUrl = false;          // a link on screen opens in the browser (Cross / Enter)
    bool selfUpdate = true;        // Updates installs the new version (else Cross opens its page)
    bool operator==(const UiCaps&) const = default;
};

// A row of Settings (or of a game's settings) as shown: its id, and whether
// a group of alike rows starts there (a gap above it).
struct SettingsRowPos {
    int row = 0;
    bool groupStart = false;
};
// Settings' rows on screen with `caps`, in order: those it rules out are
// hidden (their setting stays as it is in settings.json).
std::vector<SettingsRowPos> visibleSettingsRows(const UiCaps& caps);
// The same for a game's settings (row ids as AppUi draws them); "Reset to
// default" only with a profile of its own (`custom`).
std::vector<SettingsRowPos> visibleGameSettingsRows(const UiCaps& caps, bool custom);

// The largest stick dead zone Settings offers, in percent of the travel.
constexpr int kMaxDeadzone = 30;
constexpr int kSettingRows = 13;  // language, resolution, region, dead zone, triggers, confirm, light bar, updates,
                                  // sign-in country, 120 Hz output, bitrate cap, game volume, keyboard keys

// The countries "Sign in as another country" offers (xcloud::loginCountryAddress).
constexpr const char* kLoginCountries[] = {"br", "jp", "kr", "pl", "us"};
// "br" -> "Brazil", in the UI's language ("" for an unknown code).
std::string loginCountryName(const std::string& country);

// ConsolesShown: "My consoles" opened (its list is refreshed). UpdateNow
// (with the settings, saved first when it came from Settings) / UpdateLater:
// the answer to the update pop-up, or Updates in Settings.
enum class Action {
    None, Play, SignOut, Retry, CancelLaunch, SettingsChanged, PrefsChanged, ConsolesShown, UpdateNow, UpdateLater,
    GameSettingsChanged,
    Report,  // "Report a problem" (Triangle on an error or in Settings): reportWhat is the error shown
    LoginCountry,  // the sign-in country changed (settings): save them and sign in again
    OpenUrl  // a link on screen to open in the browser (UiCaps::openUrl): `url`
};

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
    app::GameProfile gameProfile;  // GameSettingsChanged
    bool hasGameProfile = false;
    // PrefsChanged: hidden games (product ids) and the "Your games" order.
    std::vector<std::string> hidden;
    LibrarySort librarySort = LibrarySort::Recent;
    std::string reportWhat;  // Report: the error on screen (empty from Settings)
    std::string url;         // OpenUrl
};

// "SOUTHCENTRALUS" -> "South Central US".
std::string prettyRegion(const std::string& name);

class AppUi {
public:
    AppUi(const Fonts& fonts, ImageCache& images);

    const Fonts& fonts() const { return fonts_; }

    // --- Model -------------------------------------------------------------
    void showSplash(const std::string& status);
    void showSignIn(const std::string& code, const std::string& url);
    // The signed-in account, top right; `gamerscore` (digits) under the name.
    void setProfile(const std::string& gamertag, const std::string& gamerpicUrl, const std::string& gamerscore = {});
    // The account's Game Pass plan under the name ("Ultimate", "Premium",
    // "Essential"...; "GamePass" for a member whose plan isn't known); empty
    // for none (nothing shown).
    void setSubscription(const std::string& plan);
    // From the title list: a Game Pass member or not; only when the plan
    // itself isn't known ("GamePass" or nothing).
    void setMembership(bool member);
    std::string gamertag() const;  // for keeping it out of reports
    std::vector<std::string> consoleNames() const;  // the same: the user's names for their consoles
    // "Report a problem": a QR code of `url` (the GitHub bug form, filled
    // in) over whatever is on screen, with `heading`; Circle closes it.
    void showReport(const std::string& url, const std::string& heading, const std::string& detail = {});
    bool reportOpen() const;
    // Replaces the rows, keeping the focus on the same game when possible.
    void setRows(std::vector<GameRow> rows);
    // "Your games": the account's games, then those to buy; `known` false
    // while the account's games load.
    void setOwned(std::vector<GameTile> owned, std::vector<GameTile> purchasable, bool known);
    // "My consoles"; `known` false while they load.
    void setConsoles(std::vector<ConsoleTile> consoles, bool known);
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
    // Their request failed (offline...): asked for again later.
    void pricesFailed(const std::vector<std::string>& productIds);
    // The game whose page is open, if its description or hero art hasn't
    // arrived; asked again every 5 s while missing (empty otherwise).
    std::string detailWanted(uint64_t nowMs);
    // The open page's game (xCloud title id) whose queue estimate is due
    // (one a minute per game); empty when none.
    std::string queueWanted(uint64_t nowMs);
    // Its estimate in seconds (-1: unknown, not shown).
    void setQueueTime(const std::string& titleId, int seconds);
    // Fills in the open page when it shows `productId`, and keeps the details
    // for the next time that game's page opens.
    void setDetailInfo(const std::string& productId, const std::string& description, const std::string& publisher,
                       const std::vector<std::string>& categories, const std::string& heroUrl);
    // Opens a game's page (autoplay tests).
    void showDetails(const GameTile& tile);
    // The `index`-th game to buy, if loaded (autoplay tests).
    bool purchasableAt(size_t index, GameTile& out) const;
    // Any loaded game on the home screen or catalog.
    bool firstTile(GameTile& out) const;
    // The game with this xCloud title id on the home screen or in Your games.
    bool findTile(const std::string& titleId, GameTile& out) const;
    void showHome(const std::string& toast = {});
    void showLaunching(const GameTile& game, const std::string& status);
    void setLaunchStatus(const std::string& status);
    void showStreaming();
    void showError(const std::string& message);
    // A game that could not start but may on another try (a free game got
    // on the phone meanwhile): Cross plays it again, Circle opens its page.
    void showPlayError(const std::string& message, const GameTile& game);
    // The sign-in refused by xCloud: an error that also offers to sign in as
    // another country (Square), and Settings (Options).
    void showRegionRefused(const std::string& message);
    void setSettings(const SettingsChoice& choice);
    SettingsChoice settings() const;  // what Settings holds now (the autoplay tests put it back)
    // Per-game settings
    void setPerGameSettings(const std::map<std::string, app::GameProfile>& perGame);
    // The general gyro aiming (set from the in-game menu): what a game
    // without its own profile starts its game settings with.
    void setGyroDefaults(int mode, int level);
    bool gameSettingsOpen() const { return gameSettingsOpen_; }
    void openGameSettings();
    void closeGameSettings();
    // Whether 1440p is offered (a stream delivered it); false hides it.
    void setAllow1440(bool on);
    // The confirm button in force (the hints show it); Settings may be
    // showing another choice not saved yet.
    void setCircleConfirms(bool on);
    // The confirm button in force now: Settings changes it as soon as it is
    // chosen there, before it is saved (the app makes the pad follow).
    bool circleConfirms() const;
    // The controllers in use (bottom left of the home screen).
    void setPads(const PadSlots& pads);
    // The colour of the game in focus (or being played), from its art, for
    // the DualSense light bar; false while there is none to show.
    bool accentColor(Color& out);
    // While Settings is open: the light bar being chosen there (the colour
    // picker's colour as it moves), so the pad shows it at once.
    bool settingsLightBar(int& mode, Color& colour) const;
    // The same for the trigger vibration (strength, frequency index), and,
    // while its tester is open, how far each trigger is pressed: the app
    // makes them vibrate as a game asking that much would.
    bool settingsTriggerFeel(int& strength, int& hzIndex, int& resistance, bool& pulses) const;
    bool triggerTest(float& l2, float& r2) const;
    // Regions offered by the account's xCloud login; `defaultRegion` is the
    // one "Automatic" picks.
    void setRegions(std::vector<std::string> regions, const std::string& defaultRegion);
    // Updates: this version, and the newer one (empty: up to date);
    // `checking` while it is being asked (Settings shows it).
    void setUpdateState(const std::string& current, const std::string& available, bool checking);
    // The pop-up offering the newer version, over the home screen.
    void offerUpdate();
    bool updateOffered() const;
    // The screen while it updates: `status` and, 0..1, how far (-1: a spinner).
    void showUpdating(const std::string& version);
    void setUpdateStatus(const std::string& status, float fraction);
    Screen screen() const;
    Tab tab() const;
    // Switches the home screen's tab (autoplay tests).
    void showTab(Tab t);
    // Autoplay tests: a tab's search with these filters (`mode` and
    // `language` as their lists' index, `genre` "" for all), optionally with
    // `openList` (0 mode, 1 genre, 2 language) open; and the first results.
    void showFilteredSearch(Tab tab, bool cheapest, int mode, const std::string& genre, int language, int openList = -1);
    // The search keyboard's focus (row, column), for the tests.
    std::pair<int, int> searchKeyFocus() const;
    std::vector<std::pair<std::string, PriceInfo>> searchResults(size_t max) const;
    enum class Filter { Free, Cheapest, Console, Mode, Genre, Publisher, Language, MouseKeyboard };
    void chooseFilter(Filter f, int index);
    // Autoplay tests: where the focus is ("Game Pass row 2 card 6", ...).
    std::string focusDescription() const;
    void invalidate();
    // The capabilities in force (UiCaps); the focus leaves a row that goes
    // away. Any thread.
    void setCaps(const UiCaps& caps);
    UiCaps caps() const;

    // --- UI thread -----------------------------------------------------------
    UiEvent handle(const NavInput& in);
    // The keyboard keys' screen waits for a key: the main loop gives it the
    // next key or mouse button pressed (input::kMouseLeft..) instead of
    // moving through the screens with it.
    bool capturingKey() const;
    void captureKey(uint8_t code);
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
    void drawGamerscoreBadge(Canvas& c, float cx, float cy, float r);
    void drawHints(Canvas& c, const std::vector<std::pair<int, const char*>>& hints);
    void drawSpinner(Canvas& c, float cx, float cy, float r, uint64_t nowMs);
    void drawToast(Canvas& c, uint64_t nowMs);
    void drawPads(Canvas& c);
    void drawSplash(Canvas& c, uint64_t nowMs);
    void drawSignIn(Canvas& c, uint64_t nowMs);
    void drawHome(Canvas& c, uint64_t nowMs);
    void drawTabs(Canvas& c);
    void drawConsoles(Canvas& c, uint64_t nowMs);
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
    // The search's filters, in two rows: Free, Lowest price (in "Your games"
    // only) and Console; then Mode, Genre, Publisher and Language. The lists open a
    // drop-down; Free and Lowest price turn on and off.
    std::vector<Filter> filterRow(int row) const;
    bool isList(Filter f) const { return f != Filter::Free && f != Filter::Cheapest && f != Filter::MouseKeyboard; }
    std::string filterText(Filter f) const;  // the chip's label: its choice when one is made
    bool filterOn(Filter f) const;
    std::vector<std::string> filterOptions(Filter f) const;
    int filterSelected(Filter f) const;
    void chooseFilterLocked(Filter f, int index);
    void pressFilter(Filter f);
    std::vector<std::string> poolGenres() const;  // the genres of the tab's games, most common first
    bool anyFilter() const;
    void handleHome(const NavInput& in, UiEvent& ev);
    void handleSearchKeys(const NavInput& in);
    // The x of the middle of each item on a row of the search keyboard
    // (letters, wide keys, the two rows of filters), as drawn: up / down
    // go to the nearest one on screen.
    std::vector<int> keyCenters(int row) const;
    void runSearch();
    void openDetails(const GameTile& tile);
    void refreshDetail();
    void drawDetails(Canvas& c, uint64_t nowMs);
    void drawLaunching(Canvas& c, uint64_t nowMs);
    void drawError(Canvas& c);
    void drawSettings(Canvas& c);
    void drawUpdatePrompt(Canvas& c);
    void drawReport(Canvas& c);
    void drawCountryPicker(Canvas& c);
    // The light bar's colour picker (Settings > Light bar > Custom colour).
    void openColourPicker(int previousMode);
    void handleColourPicker(const NavInput& in);
    Color pickerColour() const;
    void drawColourPicker(Canvas& c);
    // The testers that Dead zone and Trigger vibration open in Settings.
    enum class Tester { None, Sticks, Triggers, Keys };
    void openTester(Tester t);
    void handleTester(const NavInput& in);
    void drawStickTester(Canvas& c);
    void drawTriggerTester(Canvas& c);
    void handleKeysTester(const NavInput& in);
    void drawKeysTester(Canvas& c);
    // A modal panel over Settings with its title; returns its rectangle.
    Rect drawModal(Canvas& c, int w, int h, const std::string& title);
    void drawUpdating(Canvas& c, uint64_t nowMs);
    void changeSetting(int delta);
    // Settings drop-downs: the choices of a row, the chosen one, choosing.
    std::vector<std::string> settingOptions(int row) const;
    int settingSelected(int row) const;
    void applySetting(int row, int index);
    void drawCentered(Canvas& c, const Font& f, const std::string& text, int y, int px, Color color);
    // The sign-in country's "off" choice, worded for the platform (caller holds mutex_).
    const char* loginCountryOffText() const;
    // The trigger tester's rows: intensity and frequency, then resistance
    // and style with adaptive triggers.
    int triggerTesterRows() const { return caps_.adaptiveTriggers ? 4 : 2; }

    const Fonts& fonts_;
    ImageCache& images_;

    mutable std::mutex mutex_;
    UiCaps caps_;
    uint64_t toastsDrawn_ = 0;  // platform::toasts()'s version last drawn
    Screen screen_ = Screen::Splash;
    bool dirty_ = true;
    std::string status_;
    std::string code_, codeUrl_;
    std::string gamertag_, gamerpicUrl_, gamerscore_, subscription_;
    std::vector<GameRow> rows_;
    int focusRow_ = 0;
    std::vector<int> focusCol_;
    std::vector<Anim> rowScroll_;
    Anim rowY_;
    Tab tab_ = Tab::GamePass;
    std::vector<ConsoleTile> consoles_;
    bool allow1440_ = false;
    bool consolesKnown_ = false;
    int consoleFocus_ = 0;
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
    bool filterFree_ = false, filterCheapest_ = false, filterMouseKeyboard_ = false;
    int filterConsole_ = 0;   // 0 all, 1 Series X|S, 2 Xbox One, 3 Xbox 360
    int filterMode_ = 0;      // 0 all, single, online multiplayer, online co-op, local
    std::string filterGenre_;  // empty: all
    int filterLanguage_ = 0;  // 0 any, 1 subtitles (or menus) in the app's language, 2 audio
    int filterPublisher_ = 0; // 0 all, 1 Xbox/Bethesda, 2 EA, 3 Ubisoft, etc.
    // The filter whose drop-down is open (-1: none), its focus and first row.
    int filterList_ = -1, filterListIndex_ = 0, filterListTop_ = 0;
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
    GameTile errorGame_;  // showPlayError()'s game; empty for other errors
    SettingsChoice settings_;
    bool circleConfirms_ = false;
    PadSlots pads_{};
    std::map<std::string, Color> accents_;  // tile art url -> its colour
    std::vector<std::string> regions_;
    std::string defaultRegion_;
    std::map<std::string, int> regionMs_;
    std::map<std::string, PriceInfo> prices_;
    std::set<std::string> pricesAsked_;
    std::string detailAskedFor_;
    uint64_t detailAskedAt_ = 0;
    std::string queueAskedFor_, queueTitle_;
    uint64_t queueAskedAt_ = 0;
    int queueSeconds_ = -1;  // for queueTitle_
    struct DetailInfo {
        std::string description, publisher, heroUrl;
        std::vector<std::string> categories;
    };
    std::map<std::string, DetailInfo> detailInfo_;  // fetched on demand, by product id
    std::string updateCurrent_, updateAvailable_;
    bool updateChecking_ = false;
    bool reportOpen_ = false;  // the report's QR code is up
    bool regionRefused_ = false;  // the error is showRegionRefused()'s
    bool countryPickerOpen_ = false;
    int countryIndex_ = 0;  // 0 off, then kLoginCountries
    bool settingsFromError_ = false;  // Circle goes back to the error
    std::string settingsCountry_;  // the sign-in country when Settings opened
    std::string reportUrl_, reportHeading_, reportDetail_;
    bool updatePrompt_ = false;  // the pop-up is up (on the home screen)
    int promptFocus_ = 0;        // 0 Update now, 1 Not now
    std::string updatingTo_, updateStatus_;
    float updateFraction_ = -1;
    Tester tester_ = Tester::None;
    SettingsChoice testerBackup_;  // what Circle puts back
    int testerRow_ = 0;            // the trigger tester's row: intensity, frequency, resistance, style
    // The keyboard keys' screen: the row (kKeysPerColumn: restore defaults),
    // the key across both columns (0, 1 the left column's two, 2, 3 the
    // right's), waiting for a key since (ms; 0 not), a key refused and until when.
    int keysRow_ = 0, keysCol_ = 0;
    uint64_t captureSince_ = 0;
    uint8_t keyRefused_ = 0;
    uint64_t keyRefusedUntil_ = 0;
    int testerStick_ = 0;          // the stick tester: the one being adjusted (L1 / R1)
    float testSticks_[4] = {0, 0, 0, 0}, testL2_ = 0, testR2_ = 0;
    bool pickerOpen_ = false;
    float pickU_ = 0, pickV_ = 0;  // on the wheel: angle = hue, distance = saturation
    float pickValue_ = 1;          // brightness
    int pickerPrevMode_ = 0;       // what Circle goes back to
    uint64_t pickerLastMs_ = 0;
    std::vector<uint32_t> wheel_;  // the wheel's pixels at wheelValue_
    float wheelValue_ = -1;
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
    std::map<std::string, app::GameProfile> perGameSettings_;
    bool gameSettingsOpen_ = false;
    int gameSettingsRow_ = 0;
    app::GameProfile activeGameProfile_;
    int gyroMode_ = 0, gyroLevel_ = 5;  // setGyroDefaults
    bool activeGameHasCustom_ = false;
    void handleGameSettings(const NavInput& in, UiEvent& ev);
    void drawGameSettings(Canvas& c);
};

}  // namespace xc::ui
