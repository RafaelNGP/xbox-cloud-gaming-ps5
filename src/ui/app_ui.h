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
};

struct GameRow {
    std::string title;
    std::vector<GameTile> tiles;
    bool gamePassBadges = true;
};

enum class Screen { Splash, SignIn, Home, Details, Launching, Streaming, Error, Settings };

struct NavInput {
    bool up = false, down = false, left = false, right = false;
    bool accept = false, back = false, options = false;
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
    void drawHero(Canvas& c, uint64_t nowMs, const std::string& url, bool details);
    void drawTopBar(Canvas& c);
    void drawLogo(Canvas& c, float cx, float cy, float r);
    void drawHints(Canvas& c, const std::vector<std::pair<int, const char*>>& hints);
    void drawSpinner(Canvas& c, float cx, float cy, float r, uint64_t nowMs);
    void drawToast(Canvas& c, uint64_t nowMs);
    void drawSplash(Canvas& c, uint64_t nowMs);
    void drawSignIn(Canvas& c, uint64_t nowMs);
    void drawHome(Canvas& c, uint64_t nowMs);
    void drawDetails(Canvas& c, uint64_t nowMs);
    void drawLaunching(Canvas& c, uint64_t nowMs);
    void drawError(Canvas& c);
    void drawSettings(Canvas& c);
    void changeSetting(int delta);
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
    GameTile launching_;
    std::string error_;
    SettingsChoice settings_;
    std::vector<std::string> regions_;
    std::string defaultRegion_;
    int settingsRow_ = 0;
    uint64_t signOutHoldStart_ = 0;  // TOUCHPAD hold in progress
    bool signOutLatched_ = false;    // fired; wait for release
    void drawSignOutHold(Canvas& c, uint64_t nowMs);
    std::string toast_;
    uint64_t toastUntil_ = 0;
    std::string heroUrl_, prevHeroUrl_;
    uint64_t heroSince_ = 0;
    uint64_t lastRender_ = 0;
    bool animating_ = false;
};

}  // namespace xc::ui
