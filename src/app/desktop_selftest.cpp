// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// 'psbox --selftest <dir>' (desktop_selftest.h): the desktop app's own
// parts, each through the same calls the app makes, checked and saved as
// screenshots read back from the renderer. No account, no network: the data
// folder is <dir> unless XCLOUD_DATA_DIR says otherwise.
#include "app/desktop_selftest.h"

#include "app/app_core.h"
#include "app/shell.h"
#include "display/display.h"
#include "display/display_sdl.h"
#include "input/controller.h"
#include "input/input_ext.h"
#include "input/pad_map.h"
#include "media/audio_in.h"
#include "media/audio_out.h"
#include "platform/platform.h"
#include "ui/app_ui.h"
#include "ui/canvas.h"
#include "ui/font.h"
#include "ui/image_cache.h"
#include "ui/pad_glyphs.h"
#include "ui/stream_menu.h"
#include "util/log.h"

#include <SDL3/SDL.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <filesystem>
#include <vector>

namespace xc::app {

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("selftest: %-52s %s\n", what, ok ? "ok" : "FAILED");
    std::fflush(stdout);
    if (!ok) ++g_failures;
}

struct Shot {
    std::vector<uint8_t> rgb;
    int w = 0, h = 0;
    const uint8_t* at(int x, int y) const { return &rgb[(static_cast<size_t>(y) * w + x) * 3]; }
};

// What the window shows now, read back and saved as <dir>/<name>.
bool screenshot(const std::string& dir, const char* name, Shot& shot) {
    display::pump();
    if (!display::readBackRgb(shot.rgb, shot.w, shot.h)) return false;
    std::string path = dir + "/" + name;
    return stbi_write_png(path.c_str(), shot.w, shot.h, 3, shot.rgb.data(), shot.w * 3) != 0;
}

// Not one flat colour (a blank or failed read back).
bool varied(const Shot& s) {
    if (s.rgb.empty()) return false;
    const uint8_t* first = s.rgb.data();
    size_t differing = 0;
    for (size_t i = 0; i + 2 < s.rgb.size(); i += 3 * 97)
        if (std::abs(s.rgb[i] - first[0]) + std::abs(s.rgb[i + 1] - first[1]) + std::abs(s.rgb[i + 2] - first[2]) > 24)
            ++differing;
    return differing > 100;
}

void drawCanvas(const ui::Canvas& c) {
    std::lock_guard<std::mutex> lock(display::frameMutex());
    display::drawRgba(c.data());
    display::present();
}

// SMPTE-like bars: white, yellow, cyan, green, magenta, red, blue, black.
constexpr uint8_t kBars[8][3] = {{235, 235, 235}, {235, 235, 16}, {16, 235, 235}, {16, 235, 16},
                                 {235, 16, 235},  {235, 16, 16},  {16, 16, 235},  {16, 16, 16}};

struct Yuv {
    int w = static_cast<int>(display::kWidth), h = static_cast<int>(display::kHeight);
    std::vector<uint8_t> y, u, v;
};

// The bars as a BT.709 limited-range 4:2:0 frame (what the decoder hands over).
Yuv colourBars() {
    Yuv f;
    f.y.resize(static_cast<size_t>(f.w) * f.h);
    f.u.resize(static_cast<size_t>(f.w / 2) * (f.h / 2));
    f.v.resize(f.u.size());
    uint8_t ys[8], us[8], vs[8];
    for (int b = 0; b < 8; ++b) {
        // The limited-range RGB above back to 0..1, then BT.709.
        double r = (kBars[b][0] - 16) / 219.0, g = (kBars[b][1] - 16) / 219.0, bl = (kBars[b][2] - 16) / 219.0;
        double yf = 0.2126 * r + 0.7152 * g + 0.0722 * bl;
        ys[b] = static_cast<uint8_t>(16 + 219 * yf + 0.5);
        us[b] = static_cast<uint8_t>(128 + 224 * (bl - yf) / 1.8556 + 0.5);
        vs[b] = static_cast<uint8_t>(128 + 224 * (r - yf) / 1.5748 + 0.5);
    }
    for (int yy = 0; yy < f.h; ++yy)
        for (int x = 0; x < f.w; ++x) f.y[static_cast<size_t>(yy) * f.w + x] = ys[x * 8 / f.w];
    for (int yy = 0; yy < f.h / 2; ++yy)
        for (int x = 0; x < f.w / 2; ++x) {
            f.u[static_cast<size_t>(yy) * (f.w / 2) + x] = us[x * 16 / f.w];
            f.v[static_cast<size_t>(yy) * (f.w / 2) + x] = vs[x * 16 / f.w];
        }
    return f;
}

void drawVideo(const Yuv& f) {
    std::lock_guard<std::mutex> lock(display::frameMutex());
    display::drawYuv420(f.y.data(), f.u.data(), f.v.data(), f.w, f.w / 2, f.w / 2, f.w, f.h, false);
    display::present();
}

// The bar's colour, full range (what the screen shows of the limited-range bars).
bool barMatches(const Shot& s, int bar, int y) {
    int x = bar * s.w / 8 + s.w / 16;
    const uint8_t* p = s.at(x, y * s.h / static_cast<int>(display::kHeight));
    for (int c = 0; c < 3; ++c) {
        int want = std::clamp((kBars[bar][c] - 16) * 255 / 219, 0, 255);
        if (std::abs(p[c] - want) > 24) return false;
    }
    return true;
}

}  // namespace

int desktopSelftest(const std::string& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (!std::getenv("XCLOUD_DATA_DIR")) SDL_setenv_unsafe("XCLOUD_DATA_DIR", dir.c_str(), 1);
    platform::init();
    log::setFile((dir + "/selftest.log").c_str());
    XC_LOGI("=== psbox self-test ===");

    // SDL and the window, as the app brings them up.
    shell::beforeSettings();
    Settings windowed;
    windowed.desktopFullscreen = false;  // a window of a known size (unless --fullscreen)
    shell::beforeDisplay(windowed);
    bool display = display::init(true);
    check(display, "window and renderer");
    if (!display) return 1;
    std::printf("selftest: SDL video driver %s, renderer %s\n", SDL_GetCurrentVideoDriver(), display::sdlRendererName());

    // Input: the window's hooks, the pads plugged in (none on a test machine).
    check(shell::afterDisplay(true), "input hooks on the window");
    input::init();
    shell::pumpEvents();
    int pads = 0;
    SDL_free(SDL_GetGamepads(&pads));
    input::PadCaps pc = input::padCaps(0);
    std::printf("selftest: %d gamepad(s); slot 0 kind %d\n", pads, static_cast<int>(pc.kind));
    check(pads > 0 || pc.kind == input::PadKind::None, "no gamepad -> slot 0 empty");
    input::kbmSetCaptured(true);
    input::kbmSetCaptured(false);

    // The screens, with this machine's capabilities.
    ui::Fonts fonts;
    check(fonts.load(platform::assetDir() + "/fonts"), "fonts (assets beside the executable)");
    ui::ImageCache images([] {}, 16u << 20);
    ui::AppUi ui(fonts, images);
    const ui::UiCaps caps = uiCaps();
    ui.setCaps(caps);
    ui.setSettings(ui::SettingsChoice{});
    ui.setUpdateState(XC_APP_VERSION, {}, false);
    const display::DisplayCaps dc = display::caps();
    check(!caps.console && caps.openUrl && !caps.selfUpdate && !caps.highRefresh &&
              caps.upscalers == dc.upscalers && caps.sharpen == dc.sharpen && caps.deband == dc.deband,
          "desktop capabilities");
    ui::Canvas canvas(static_cast<int>(display::kWidth), static_cast<int>(display::kHeight));

    Shot shot;
    ui.showSignIn("PSBX-TEST", "https://www.microsoft.com/link");
    ui.render(canvas, platform::nowMs());
    drawCanvas(canvas);
    check(screenshot(dir, "1-signin.png", shot) && varied(shot), "sign-in screen (1-signin.png)");

    ui.showHome();
    ui::NavInput nav;
    nav.options = true;
    nav.nowMs = platform::nowMs();
    ui.handle(nav);
    check(ui.screen() == ui::Screen::Settings, "Settings opens from the home screen");
    auto rows = ui::visibleSettingsRows(caps);
    bool hidden = std::none_of(rows.begin(), rows.end(), [&](const ui::SettingsRowPos& r) {
        return (r.row == 6 && !caps.lightBar) || (r.row == 9 && !caps.highRefresh) ||
               (r.row == 4 && !caps.triggerRumble && !caps.adaptiveTriggers) ||
               (r.row == 15 && !caps.gyro);
    });
    std::printf("selftest: Settings shows %zu of %d rows\n", rows.size(), ui::kSettingRows);
    check(hidden && rows.size() < static_cast<size_t>(ui::kSettingRows), "Settings hides light bar / 120 Hz rows");
    check(caps.desktopWindow && std::any_of(rows.begin(), rows.end(), [](const ui::SettingsRowPos& r) { return r.row == 13; }),
          "Settings has the Display mode row");
    ui.render(canvas, platform::nowMs());
    drawCanvas(canvas);
    check(screenshot(dir, "2-settings.png", shot) && varied(shot), "Settings screen (2-settings.png)");
    // The PS5's Settings, every row shown (the console's own capabilities).
    ui.setCaps(ui::UiCaps{});
    ui.render(canvas, platform::nowMs());
    drawCanvas(canvas);
    check(screenshot(dir, "2e-settings-all-rows.png", shot) && varied(shot), "Settings with every row (2e-settings-all-rows.png)");
    ui.setCaps(caps);
    // The same with an Xbox pad's glyphs (A / B, Menu, View...).
    {
        const std::vector<uint8_t> ps = shot.rgb;
        ui::g_glyphStyle = ui::glyphStyleFor(input::PadKind::Xbox);
        ui.invalidate();
        ui.render(canvas, platform::nowMs());
        drawCanvas(canvas);
        check(screenshot(dir, "2b-settings-xbox.png", shot) && varied(shot) && shot.rgb != ps,
              "Settings with Xbox glyphs (2b-settings-xbox.png)");
        ui::NavInput back;
        back.back = true;
        back.nowMs = platform::nowMs();
        ui.handle(back);  // the home screen: LB / RB, Y, Menu, View
        ui.render(canvas, platform::nowMs());
        drawCanvas(canvas);
        check(ui.screen() == ui::Screen::Home && screenshot(dir, "2c-home-xbox.png", shot) && varied(shot),
              "home screen with Xbox glyphs (2c-home-xbox.png)");
        ui::g_glyphStyle = ui::GlyphStyle::PlayStation;
        ui.invalidate();
    }
    // Settings > Controller buttons: the map as it is, a row cleared (Circle
    // then stays, naming it), a press captured, and a complete map kept on
    // the way out (A and B swapped).
    {
        auto step = [&](auto set) {
            ui::NavInput in;
            set(in);
            in.nowMs = platform::nowMs();
            ui.handle(in);
            ui.render(canvas, platform::nowMs());
            drawCanvas(canvas);
        };
        auto bit = [](input::PadFn f) { return 1u << static_cast<int>(f); };
        step([](ui::NavInput& in) { in.options = true; });
        const int downs = [&] {
            auto shown = ui::visibleSettingsRows(caps);
            for (size_t i = 0; i < shown.size(); ++i)
                if (shown[i].row == 14) return static_cast<int>(i);
            return -1;
        }();
        check(downs > 0, "Settings has the Controller buttons row");
        for (int i = 0; i < downs; ++i) step([](ui::NavInput& in) { in.down = true; });
        step([](ui::NavInput& in) { in.accept = true; });
        check(screenshot(dir, "5-padmap.png", shot) && varied(shot), "Controller buttons screen (5-padmap.png)");
        step([](ui::NavInput& in) { in.square = true; });  // A cleared
        step([](ui::NavInput& in) { in.back = true; });    // refused: A has no button
        check(ui.screen() == ui::Screen::Settings && ui.settings().padMap == input::defaultPadMap() &&
                  screenshot(dir, "5b-padmap-incomplete.png", shot) && varied(shot),
              "an incomplete map stays on its screen, unsaved (5b-padmap-incomplete.png)");
        step([](ui::NavInput& in) { in.accept = true; });  // A: waiting for a press
        check(screenshot(dir, "5c-padmap-capture.png", shot) && varied(shot), "waiting for a press (5c-padmap-capture.png)");
        step([&](ui::NavInput& in) { in.padPressed = bit(input::PadFn::B); });  // Circle -> A (B now has none)
        step([](ui::NavInput& in) { in.down = true; });
        step([](ui::NavInput& in) { in.accept = true; });
        step([&](ui::NavInput& in) { in.padPressed = bit(input::PadFn::A); });  // Cross -> B
        check(screenshot(dir, "5d-padmap-swapped.png", shot) && varied(shot), "A and B swapped (5d-padmap-swapped.png)");
        step([](ui::NavInput& in) { in.back = true; });  // complete: kept
        input::PadMap swapped = input::defaultPadMap();
        input::assignPadSource(swapped, input::PadFn::A, static_cast<uint8_t>(input::PadFn::B));
        check(ui.settings().padMap == swapped, "a complete map is kept on the way out");
        // Back to the default for the rest (Reset to default, then out).
        step([](ui::NavInput& in) { in.accept = true; });
        for (int i = 0; i < 8; ++i) step([](ui::NavInput& in) { in.down = true; });
        step([](ui::NavInput& in) { in.accept = true; });
        step([](ui::NavInput& in) { in.back = true; });
        check(ui.settings().padMap == input::defaultPadMap(), "Reset to default");
        step([](ui::NavInput& in) { in.back = true; });  // the home screen
        // A game's settings with every row (the PS5's), its Controller
        // buttons row opening the screen on the game's map; Discard leaves.
        ui.setCaps(ui::UiCaps{});
        ui::GameTile game;
        game.productId = "SELFTEST";
        game.titleId = "selftest";
        game.name = "Self-test game";
        ui.showDetails(game);
        step([](ui::NavInput& in) { in.options = true; });
        check(screenshot(dir, "5e-game-settings.png", shot) && varied(shot), "a game's settings (5e-game-settings.png)");
        const auto gameRows = ui::visibleGameSettingsRows(ui::UiCaps{}, false);
        for (size_t i = 0; i < gameRows.size() && gameRows[i].row != 11; ++i) step([](ui::NavInput& in) { in.down = true; });
        step([](ui::NavInput& in) { in.accept = true; });
        step([](ui::NavInput& in) { in.square = true; });
        check(screenshot(dir, "5f-game-padmap.png", shot) && varied(shot), "the game's controller buttons (5f-game-padmap.png)");
        step([](ui::NavInput& in) { in.up = true; });     // Reset / Discard
        step([](ui::NavInput& in) { in.right = true; });  // Discard changes
        step([](ui::NavInput& in) { in.accept = true; });
        check(screenshot(dir, "5g-game-settings-after.png", shot) && varied(shot), "discarded: still Default (5g-game-settings-after.png)");
        step([](ui::NavInput& in) { in.back = true; });
        step([](ui::NavInput& in) { in.back = true; });
        ui.setCaps(caps);
        ui.showHome();
    }
    // The search typed on the keyboard: the shell's text input, the text
    // through AppUi::typeText().
    {
        std::vector<ui::GameTile> pool(3);
        pool[0].productId = "1", pool[0].name = "Halo Infinite";
        pool[1].productId = "2", pool[1].name = "Forza Horizon 5";
        pool[2].productId = "3", pool[2].name = "Halo: The Master Chief Collection";
        ui.setSearchPools(pool, {});
        ui::NavInput tri;
        tri.triangle = true;
        tri.nowMs = platform::nowMs();
        ui.handle(tri);
        bool wants = ui.wantsTextInput();
        bool on = shell::setTextInput(wants);
        ui.typeText("halox\b");  // a typo, then Backspace
        ui.render(canvas, platform::nowMs());
        drawCanvas(canvas);
        auto hits = ui.searchResults(8);
        check(wants && on && hits.size() == 2 && screenshot(dir, "2d-search-typed.png", shot) && varied(shot),
              "search typed with the keyboard (2d-search-typed.png)");
        ui.typeText("\n");  // Enter: on to the results
        check(!ui.wantsTextInput(), "Enter moves to the results");
        shell::setTextInput(false);
        ui::NavInput back;
        back.back = true;
        back.nowMs = platform::nowMs();
        ui.handle(back);
        ui.handle(back);
    }

    // A video frame through the mailbox, then under the game menu.
    Yuv bars = colourBars();
    drawVideo(bars);
    bool barsOk = screenshot(dir, "3-video.png", shot);
    for (int b = 0; b < 8 && barsOk; ++b) barsOk = barMatches(shot, b, 540);
    check(barsOk, "colour bars through drawYuv420 + pump (3-video.png)");
    check(display::lastPresentId() > 0, "present ids counted");
    if (dc.upscalers) {
        display::setSharpness(176);
        display::setDeband(2);
        display::setUpscaler(1);
        display::setRestore(true);
        drawVideo(bars);
        bool gpuOk = screenshot(dir, "3c-video-upscaled.png", shot);
        for (int b = 0; b < 8 && gpuOk; ++b) gpuOk = barMatches(shot, b, 540);
        check(gpuOk, "GPU filters: Anime4K + Restore + RCAS + deband (3c-video-upscaled.png)");
        display::setSharpness(0);
        display::setDeband(1);
        display::setUpscaler(0);
        display::setRestore(false);
    }
    // A read back asked from another thread (the video thread's
    // screenshots), done by the main loop's pump().
    {
        Shot t;
        std::atomic<bool> finished{false};
        bool ok = false;
        std::thread reader([&] {
            ok = display::readBackRgb(t.rgb, t.w, t.h);
            finished = true;
        });
        for (int i = 0; i < 200 && !finished; ++i) {
            display::idleWait(5);
            display::pump();
        }
        reader.join();
        for (int b = 0; b < 8 && ok; ++b) ok = barMatches(t, b, 540);
        std::string path = dir + "/3b-readback-thread.png";
        ok = ok && stbi_write_png(path.c_str(), t.w, t.h, 3, t.rgb.data(), t.w * 3) != 0;
        check(ok, "read back off the main thread (3b-readback-thread.png)");
    }

    ui::StreamMenu menu(fonts);
    menu.setCapabilities(caps);
    menu.open(0, false);
    ui::StreamInfo info;
    info.region = "selftest";
    info.width = bars.w, info.height = bars.h;
    ui::Canvas mc = menu.renderMenu(info);
    display::setOverlay(mc.data(), ui::StreamMenu::kMenuX, ui::StreamMenu::kMenuY, mc.width(), mc.height(), 235);
    drawVideo(bars);
    bool overlayOk = screenshot(dir, "4-overlay.png", shot);
    // Beside the menu the bars are untouched; under it they are covered.
    for (int b = 3; b < 8 && overlayOk; ++b) overlayOk = barMatches(shot, b, 540);
    overlayOk = overlayOk && !barMatches(shot, 1, 540);
    check(overlayOk, "game menu over the video (4-overlay.png)");
    display::setOverlay(nullptr, 0, 0, 0, 0, 0);

    // Sound out: 200 ms of a tone into the device, then closed.
    bool out = media::audioStart();
    if (out) {
        std::vector<float> tone(480 * 2);
        for (int chunk = 0, t = 0; chunk < 20; ++chunk) {
            for (size_t i = 0; i < 480; ++i, ++t) tone[i * 2] = tone[i * 2 + 1] = 0.1f * ((t / 55) % 2 ? 1.0f : -1.0f);
            media::audioPush(tone.data(), 480);
            platform::sleepMs(10);
        }
        media::audioStop();
    }
    check(out, "sound out: start, 200 ms, stop");
    // Sound in: the microphone opened and closed.
    {
        media::AudioIn mic;
        std::atomic<int> frames{0};
        bool in = mic.open([&](const int16_t*, size_t) { ++frames; });
        if (in) {
            platform::sleepMs(200);
            mic.close();
        }
        std::printf("selftest: microphone frames in 200 ms: %d\n", frames.load());
        check(in, "sound in: open, close");
    }

    shell::beforeShutdown();
    input::shutdown();
    display::shutdown();
    if (dc.upscalers) {
        bool fb = display::init(false);
        bool fbOk = fb && !display::caps().upscalers;
        if (fbOk) {
            drawVideo(bars);
            fbOk = screenshot(dir, "3d-video-fallback.png", shot);
            for (int b = 0; b < 8 && fbOk; ++b) fbOk = barMatches(shot, b, 540);
        }
        display::shutdown();
        check(fbOk, "SDL_Renderer fallback (3d-video-fallback.png)");
    }
    std::printf("selftest: %s (%d failed)\n", g_failures ? "FAILED" : "all passed", g_failures);
    XC_LOGI("self-test: %d failed", g_failures);
    log::setFile(nullptr);
    return g_failures ? 1 : 0;
}

}  // namespace xc::app
