// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Unattended test mode (tools/ps5/autotest.sh).
// <dataDir>/autoplay.txt holds "<titleId> <seconds> [option]". The app signs
// in, plays that title for that long (pressing A at 15 s and 20 s), saves
// decoded frames and logs "AUTOPLAY END". Options, comma-separated: nosimd,
// dump, repeat, idle (no A presses), threads=N (H.264 decoder threads),
// rumbletest (rumbles the pad for 1.5 s at start), triggertest (the triggers
// for 3 s), vibetest (each motor and trigger alone, announced, no game),
// droptest (the connection dropped at 20 s: the stream reconnects), detailtest (opens a game to
// buy far down the list instead of playing, saves detail.ppm), imetest (opens
// the system keyboard on the home screen), menutest (in the game: the menu,
// 720p, back to 1080p), res=720p|1080p|1080p-hq|1440p, sharp=0..3 and
// deband=0..3 and ai (Anime4K) instead of the settings.
// The title "BENCH" decodes <dataDir>/sample.h264 instead; "XHOME" plays
// the account's first own console; "UPDATE" stays on the home screen (the
// update test: updatefeed=<url> of a release, testca to trust
// <dataDir>/test-ca.pem, updatetest to accept the pop-up, norestart).
#pragma once

#include "input/controller.h"
#include "ui/app_ui.h"
#include "ui/canvas.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace xc::app {

struct Autoplay {
    std::string title;  // empty: not testing
    int seconds = 0;
    bool dump = false;
    int runs = 1;
    bool idle = false;
    bool consolesTab = false;    // "consolestab": open My consoles, save consoles.ppm
    bool consolesEmpty = false;  // with "consolestab": as if none were found
    bool settingsTest = false;   // "settingstest": open the resolution list, save settings.ppm
    bool badCa = false;          // "badca": a missing CA bundle, to see the TLS setup error
    bool consoles = false;       // "consoles": log the account's own consoles (xhome)
    bool pad = false;            // "pad": the physical pad stays in use, its buttons logged
    bool detailTest = false;     // open a game to buy far down the list, save its page
    bool libraryTest = false;    // open "Your games", save it at 4 s and 25 s
    bool vkTest = false;         // the GPU presenting instead of the CPU display
    bool cpuDisplay = false;     // the CPU display even where the GPU comes up
    int upscaler = -1;           // ai: Anime4K instead of the setting
    bool restore = false;        // restore: Anime4K Restore before the upscale
    bool dropTest = false;       // droptest: the connection dropped at 20 s
    bool hwDecode = false;       // hwdecode: the hardware decoder alongside, logged
    bool vibeTest = false;       // each motor alone, with a notification
    bool imeTest = false;        // open the system keyboard on the home screen
    bool menuTest = false;       // in the game: open the menu, switch to 720p
    int decodeThreads = 1;
    int deband = -1;             // deband=0..3: instead of the setting
    int sharpness = -1;          // sharp=0..3: instead of the setting
    std::string resolution;      // res=720p|1080p|1440p: instead of the setting
    std::string updateFeed;      // updatefeed=<url>: the release JSON instead of GitHub's
    bool testCa = false;         // testca: also trust <dataDir>/test-ca.pem (the feed's server)
    bool updateTest = false;     // updatetest: accept the update pop-up, save update.ppm / updating.ppm
    bool updateSkip = false;     // updateskip: "Not now" on the update pop-up
    bool settingsUpdate = false; // settingsupdate: Settings > Updates, Cross
    bool quickTest = false;      // quicktest: Hogwarts Legacy's badges, MOBA games, the lock on `lockTitle`
    std::string lockTitle;       // locktitle=<xCloud title id>: logged playable or locked at once
    bool searchTest = false;     // searchtest: lowest price, Game Pass co-op + dubbed, the genres' list
                                 // (cheapest.ppm, filters.ppm, genres.ppm)
    bool tuneTest = false;       // tunetest: the dead zone's stick tester and the trigger tester (sticks.ppm, triggers.ppm)
    bool confirmTest = false;    // confirmtest: physical Cross / Circle presses through Settings > Confirm button
    bool menuShot = false;       // menushot: the game menu open over the 10 s snapshot (screen.ppm)
    bool pickerTest = false;     // pickertest: the confirm button's list (confirm.ppm), the light
                                 // bar's colour picker moved (picker.ppm), chosen (settings.ppm)
    bool noRestart = false;      // norestart: after an update, close instead of restarting
};
extern Autoplay g_autoplay;
extern std::atomic<bool> g_syntheticA;  // A held down for the game

void loadAutoplay();
// A test stream ended: the next run, or "AUTOPLAY END".
void autoplayFinished(const std::string& result);
void runDecodeBench();

// Main thread, each pass. The pad as read: kept from the tests (or logged).
void autoplayPad(input::ControllerState& pad);
// Before the screen gets `nav`: the scripted presses (menutest, settingstest).
void autoplayNav(ui::NavInput& nav, bool& menuCombo, uint64_t now);
// Off the stream, after the UI handled its input: the snapshots and the
// tests on the home screen.
void autoplayScreens(const ui::Canvas& canvas, uint64_t now);

}  // namespace xc::app
