// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Offline unit tests for the portable core (no network).
#include "app/auto_deband.h"
#include "app/settings.h"
#include "app/update_check.h"
#include "app/updater.h"
#include "app/crash_guard.h"
#include "app/report.h"
#include "display/display.h"
#include "media/audio_encoder.h"
#include "media/audio_in.h"
#include "media/audio_in_common.h"
#include "media/audio_out.h"
#include "media/audio_queue.h"
#include "media/decoder.h"
#include "media/hw_decoder.h"
#include "input/controller.h"
#include "input/gyro.h"
#include "input/tuning.h"
#include "net/http.h"
#include "platform/platform.h"
#include "platform/toasts.h"
#include "stream/input_packet.h"
#include "stream/stream_session.h"
#include "ui/app_ui.h"
#include "ui/accent_color.h"
#include "ui/pad_glyphs.h"
#include "ui/stream_menu.h"
#include "ui/strings.h"
#include "util/json.h"
#include "auth/auth_manager.h"
#include "stream/h264_receiver.h"
#include "input/kbm.h"
#include "input/keyboard_nav.h"
#include "input/pad_map.h"
#include "input/sdl_mapping.h"
#include "platform/platform.h"
#include "auth/msa.h"
#include "ui/image_cache.h"
#include "xcloud/catalog.h"
#include "xcloud/gssv.h"
#include "xcloud/titlehub.h"
#include "xcloud/prices.h"
#include "xcloud/regions.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <vector>
#include <string>

using namespace xc;

static int g_failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

static void testJson() {
    auto v = json::parse(R"({"a":1,"b":[true,null,"x\u00e9\n"],"c":{"d":-2.5e1},"e":"\ud83c\udfae"})");
    CHECK(v.has_value());
    CHECK((*v)["a"].asInt() == 1);
    CHECK((*v)["b"].size() == 3);
    CHECK((*v)["b"][0].asBool());
    CHECK((*v)["b"][1].isNull());
    CHECK((*v)["b"][2].str() == "x\xc3\xa9\n");
    CHECK((*v)["c"]["d"].asNumber() == -25.0);
    CHECK((*v)["e"].str() == "\xf0\x9f\x8e\xae");
    CHECK((*v)["missing"]["deep"].str("def") == "def");

    // exchangeResponse-style nested JSON string round trip.
    json::Value inner = json::Value::object();
    inner.set("sdp", "v=0\r\no=- 1 2 IN IP4 0.0.0.0\r\n");
    json::Value outer = json::Value::object();
    outer.set("exchangeResponse", inner.dump());
    auto back = json::parse(outer.dump());
    CHECK(back.has_value());
    auto innerBack = json::parse((*back)["exchangeResponse"].str());
    CHECK(innerBack && (*innerBack)["sdp"].str() == "v=0\r\no=- 1 2 IN IP4 0.0.0.0\r\n");

    CHECK(!json::parse("{\"a\":}").has_value());
    CHECK(!json::parse("[1,2").has_value());
    CHECK(!json::parse("{} x").has_value());
    CHECK(json::Value(int64_t(2148916233)).dump() == "2148916233");

    // Copy-on-write: mutating a copy must not alter the original.
    json::Value a = json::Value::array();
    a.push(1);
    json::Value b = a;
    b.push(2);
    CHECK(a.size() == 1 && b.size() == 2);
}

static void testUrl() {
    net::Url u;
    CHECK(net::Url::parse("https://xgpuweb.gssv-play-prod.xboxlive.com/v2/login/user", u));
    CHECK(u.host == "xgpuweb.gssv-play-prod.xboxlive.com" && u.port == 443 && u.path == "/v2/login/user");
    CHECK(net::Url::parse("https://example.com:8443", u));
    CHECK(u.port == 8443 && u.path == "/");
    CHECK(!net::Url::parse("ftp://x", u));
    CHECK(net::urlEncode("a b&c=d/é") == "a%20b%26c%3Dd%2F%C3%A9");
    CHECK(net::formEncode({{"grant_type", "refresh_token"}, {"scope", "service::x::MBI_SSL"}}) ==
          "grant_type=refresh_token&scope=service%3A%3Ax%3A%3AMBI_SSL");
}

static void testInputPacket() {
    using namespace stream;
    auto meta = clientMetadataReport(0, 0.0, 1);
    CHECK(meta.size() == 15 && meta[0] == 8 && meta[1] == 0 && meta[14] == 1);

    GamepadFrame f;
    f.buttons = kA | kDPadUp;
    f.leftX = 1.0f;
    f.leftY = -1.0f;
    f.rightTrigger = 1.0f;
    auto p = gamepadReport(7, 1.5, f);
    CHECK(p.size() == 38);
    CHECK(p[0] == 2 && p[2] == 7 && p[3] == 0);
    CHECK(p[13] == 0x3f && p[12] == 0xf8);  // 1.5 as little-endian float64
    CHECK(p[14] == 1 && p[15] == 0);        // one frame, gamepad 0
    CHECK(p[16] == 0x10 && p[17] == 0x01);  // A | DPadUp
    CHECK(p[18] == 0xff && p[19] == 0x7f);  // left X = 32767
    CHECK(p[20] == 0x01 && p[21] == 0x80);  // left Y = -32767
    CHECK(p[28] == 0xff && p[29] == 0xff);  // right trigger
    CHECK(gamepadPhysicality(f) == 0x000e1001);
    CHECK(p[30] == 0x01 && p[31] == 0x10 && p[32] == 0x0e && p[33] == 0x00);  // physicality LE
    CHECK(p[34] == 0 && p[35] == 0 && p[36] == 0 && p[37] == 0);              // virtual physicality LE = 0

    GamepadFrame idle;
    CHECK(gamepadPhysicality(idle) == 0);
    auto pIdle = gamepadReport(8, 2.0, idle);
    CHECK(pIdle[30] == 0 && pIdle[31] == 0 && pIdle[32] == 0 && pIdle[33] == 0);
    CHECK(pIdle[34] == 0 && pIdle[35] == 0 && pIdle[36] == 0 && pIdle[37] == 0);

    FrameMetadata fm;
    fm.serverDataKey = 0x01020304;
    fm.firstPacketArrivalMs = 100.0;
    fm.submittedMs = 100.05;
    fm.decodedMs = 102.5;
    fm.renderedMs = 110.0;
    auto md = metadataReport(9, 111.0, {fm});
    CHECK(md.size() == 14 + 1 + 28);
    CHECK(md[0] == 1 && md[1] == 0 && md[2] == 9 && md[14] == 1);
    CHECK(md[15] == 0x04 && md[18] == 0x01);                  // RTP timestamp, little-endian
    CHECK(md[19] == 0xE8 && md[20] == 0x03);                  // 100 ms = 1000 tenths
    CHECK(md[27] == 0x01 && md[28] == 0x04);                  // 102.5 ms = 1025
    CHECK(md[39] == 0x56 && md[40] == 0x04);                  // report time 111 ms = 1110

    uint8_t vib[13] = {128, 0, 0, 0, 50, 25, 0, 0, 0x10, 0x00, 0, 0, 1};
    Vibration v;
    CHECK(parseVibration(vib, sizeof vib, v) && v.leftMotor == 50 && v.rightMotor == 25 && v.durationMs == 16);
    uint8_t sm[10] = {16, 0, 0x38, 0x04, 0, 0, 0x80, 0x07, 0, 0};
    uint32_t w = 0, h = 0;
    CHECK(parseServerMetadata(sm, sizeof sm, w, h) && w == 1920 && h == 1080);
}

static void testTeredo() {
    std::string ip;
    int port = 0;
    // RFC 4380 example: server 65.54.227.120, client 192.0.2.45:40000.
    CHECK(stream::decodeTeredo("2001:0:4136:e378:8000:63bf:3fff:fdd2", ip, port));
    CHECK(ip == "192.0.2.45" && port == 40000);
    CHECK(!stream::decodeTeredo("2a01::1", ip, port));
    CHECK(!stream::decodeTeredo("10.0.0.1", ip, port));
}

static void testLoginCountry() {
    // Every country the UI offers has an address and a name; off and
    // anything else sends none.
    for (const char* country : ui::kLoginCountries) {
        const char* address = xcloud::loginCountryAddress(country);
        CHECK(address && std::string(address).find('.') != std::string::npos);
        CHECK(!ui::loginCountryName(country).empty());
    }
    CHECK(!xcloud::loginCountryAddress("") && !xcloud::loginCountryAddress("xx") && !xcloud::loginCountryAddress("BR"));
    CHECK(ui::loginCountryName("xx").empty());
    // Saved, and off when the file has none.
    std::string path = "/tmp/xcloud-tests-country.json";
    app::Settings s;
    s.loginCountry = "jp";
    CHECK(s.save(path));
    app::Settings back;
    CHECK(back.load(path) && back.loginCountry == "jp");
    xc::platform::writeFileAtomic(path, "{}");
    app::Settings none;
    CHECK(none.load(path) && none.loginCountry.empty());
    std::remove(path.c_str());
    // No login yet, or a new sign-in: not refused.
    xcloud::GssvClient gssv;
    CHECK(!gssv.loginRefused() && gssv.loginStatus() == 0);
}

static void testStrings() {
    using namespace ui;
    auto placeholders = [](const std::string& t) {
        int n = 0;
        for (size_t at = t.find("%s"); at != std::string::npos; at = t.find("%s", at + 2)) ++n;
        return n;
    };
    for (int l = 0; l < static_cast<int>(Language::Count); ++l) {
        setLanguage(Language::English);
        std::vector<std::string> english;
        for (int i = 0; i < static_cast<int>(Str::Count); ++i) english.push_back(tr(static_cast<Str>(i)));
        setLanguage(static_cast<Language>(l));
        CHECK(languageFromCode(languageCode(static_cast<Language>(l))) == static_cast<Language>(l));
        for (int i = 0; i < static_cast<int>(Str::Count); ++i) {
            std::string t = tr(static_cast<Str>(i));
            CHECK(!t.empty());
            CHECK(placeholders(t) == placeholders(english[static_cast<size_t>(i)]));
        }
    }
    setLanguage(Language::English);
    CHECK(languageFromCode("xx") == Language::English);
    CHECK(prettyRegion("SOUTHCENTRALUS") == "South Central US");
    CHECK(prettyRegion("EASTUS2") == "East US 2");
    CHECK(prettyRegion("BRAZILSOUTH") == "Brazil South");
    CHECK(prettyRegion("CHILECENTRAL") == "Chile Central");
    CHECK(prettyRegion("MEXICOCENTRAL") == "Mexico Central");
    CHECK(prettyRegion("SWEDENCENTRAL") == "Sweden Central");
    CHECK(prettyRegion("UAENORTH") == "UAE North");
}

static void testRegions() {
    using xc::xcloud::regionsByExpectedRtt;
    std::vector<std::string> all = {"BRAZILSOUTH", "EASTUS", "WESTEUROPE", "CHILECENTRAL", "JAPANEAST", "UNKNOWN"};
    // Nothing measured: by distance, the unknown region last.
    auto order = regionsByExpectedRtt("BRAZILSOUTH", all, {});
    CHECK(order.size() == 5);
    CHECK(order.front() == "CHILECENTRAL");
    CHECK(order[1] == "EASTUS");
    CHECK(order.back() == "UNKNOWN");
    // A measured round trip beats an estimate.
    order = regionsByExpectedRtt("BRAZILSOUTH", all, {{"BRAZILSOUTH", 15}, {"EASTUS", 40}});
    CHECK(order.front() == "EASTUS");
    double km = xc::xcloud::regionDistanceKm("brazilsouth", "CHILECENTRAL");
    CHECK(km > 2500 && km < 3000);
}

static void testPrices() {
    using xc::xcloud::formatPrice;
    CHECK(formatPrice(78.82, "BRL") == "R$ 78,82");
    CHECK(formatPrice(1299.9, "BRL") == "R$ 1.299,90");
    CHECK(formatPrice(59.99, "USD") == "$59.99");
    CHECK(formatPrice(69.99, "EUR") == "69,99 \xE2\x82\xAC");
    CHECK(formatPrice(4, "XYZ") == "XYZ 4.00");
}

static void testAccentColor() {
    using namespace xc::ui;
    Image img;
    img.w = img.h = 64;
    img.px.assign(64 * 64, rgba(60, 60, 60));                            // grey background
    for (int i = 0; i < 64 * 24; ++i) img.px[i] = rgba(200, 40, 30);    // a red third
    for (int i = 64 * 60; i < 64 * 64; ++i) img.px[i] = rgba(30, 60, 200);  // a little blue
    Color c = 0;
    CHECK(dominantColor(img, c));
    CHECK((c & 0xFF) == 255 && ((c >> 8) & 0xFF) < 90 && ((c >> 16) & 0xFF) < 90);  // red, at full
    Image grey;
    grey.w = grey.h = 16;
    grey.px.assign(16 * 16, rgba(128, 128, 128));
    CHECK(!dominantColor(grey, c));
}

static void testVersions() {
    using xc::app::isNewerVersion;
    CHECK(isNewerVersion("v0.4.0", "0.3.0"));
    CHECK(isNewerVersion("v0.3.1", "0.3.0"));
    CHECK(isNewerVersion("1.0", "0.9.9"));
    CHECK(!isNewerVersion("v0.3.0", "0.3.0"));
    CHECK(!isNewerVersion("v0.2.9", "0.3.0"));
    CHECK(!isNewerVersion("nightly", "0.3.0"));
    CHECK(!isNewerVersion("", "0.3.0"));
}

static void testSettingsMigration() {
    // A file from before v0.8.0 at the old defaults gets the new ones; other
    // choices, and files already saved by v0.8.0, stay as they are.
    std::string path = "/tmp/xcloud-tests-settings.json";
    auto loadFrom = [&](const std::string& text) {
        xc::platform::writeFileAtomic(path, text);
        xc::app::Settings s;
        CHECK(s.load(path));
        return s;
    };
    auto old = loadFrom(R"({"deband":1,"upscaler":0})");
    CHECK(old.deband == 3 && old.upscaler == 2);
    auto chosen = loadFrom(R"({"deband":2,"upscaler":1})");
    CHECK(chosen.deband == 2 && chosen.upscaler == 1);
    auto current = loadFrom(R"({"deband":1,"upscaler":0,"settingsVersion":2})");
    CHECK(current.deband == 1 && current.upscaler == 0);
    // Saved again, the migration doesn't repeat.
    CHECK(old.save(path));
    xc::app::Settings again;
    CHECK(again.load(path) && again.deband == 3 && again.upscaler == 2);
    // The triggers' on/off before their strength could be chosen.
    CHECK(loadFrom(R"({"triggerRumble":false})").triggerStrength == 0 && loadFrom("{}").triggerStrength == 2);
    // One dead zone for both sticks, until each could have its own.
    auto one = loadFrom(R"({"deadzone":22})");
    CHECK(one.deadzoneLeft == 22 && one.deadzoneRight == 22);
    auto two = loadFrom(R"({"deadzone":22,"deadzoneLeft":8,"deadzoneRight":30})");
    CHECK(two.deadzoneLeft == 8 && two.deadzoneRight == 30);
    // The light bar's on/off before its colour could be chosen.
    CHECK(loadFrom(R"({"lightBar":false})").lightBarMode == 2 && loadFrom(R"({"lightBar":true})").lightBarMode == 0);
    xc::app::Settings custom;
    custom.lightBarMode = 1;
    custom.lightBarColour = xc::ui::rgba(0x12, 0x34, 0x56);
    CHECK(custom.save(path));
    std::string text;
    CHECK(xc::platform::readFile(path, text) && text.find("\"#123456\"") != std::string::npos);
    xc::app::Settings back;
    CHECK(back.load(path) && back.lightBarMode == 1 && back.lightBarColour == xc::ui::rgba(0x12, 0x34, 0x56));
    std::remove(path.c_str());
}

// The microphone's gain is stored as a choice (0..2), not as the factor.
static void testMicGain() {
    app::Settings s;
    CHECK(s.micGain == 1 && s.micGainFactor() == 1.0f);
    s.micGain = 0;
    CHECK(s.micGainFactor() == 0.5f);
    s.micGain = 2;
    CHECK(s.micGainFactor() == 1.5f);
    s.micGain = 7;  // out of range (a hand-edited file): the nearest choice
    CHECK(s.micGainFactor() == 1.5f);
}

static void testPerGameSettings() {
    std::string path = "/tmp/xcloud-tests-pergame.json";
    xc::app::Settings s;
    s.sharpness = 1;
    s.deband = 2;
    s.upscaler = 0;
    s.deadzoneLeft = 12;
    s.deadzoneRight = 18;
    s.triggerStrength = 3;
    s.circleConfirms = false;

    // Default profile reflects global settings.
    auto def = s.defaultProfile();
    CHECK(def.sharpness == 1 && def.deband == 2 && def.upscaler == 0);
    CHECK(def.deadzoneLeft == 12 && def.deadzoneRight == 18 && def.triggerStrength == 3);
    CHECK(!def.circleConfirms);
    CHECK(!def.highRefresh);  // off unless chosen
    CHECK(!def.touchpadMouse);

    // Unknown games return default profile with hasCustom = false.
    bool hasCustom = true;
    auto prof1 = s.profileForGame("GAME1", "", &hasCustom);
    CHECK(!hasCustom);
    CHECK(prof1.sharpness == 1 && prof1.deadzoneLeft == 12);

    // Add per-game profile for GAME1.
    xc::app::GameProfile p1;
    p1.sharpness = 3;
    p1.deband = 0;
    p1.upscaler = 3;
    p1.deadzoneLeft = 5;
    p1.deadzoneRight = 8;
    p1.triggerStrength = 1;
    p1.circleConfirms = true;
    p1.highRefresh = true;
    p1.touchpadMouse = true;
    p1.locale = "pt-BR";
    s.favorites = {"GAME1", "GAME3"};
    s.perGame["GAME1"] = p1;

    // Now GAME1 returns custom profile, while GAME2 still returns defaults.
    auto prof1Custom = s.profileForGame("GAME1", "", &hasCustom);
    CHECK(hasCustom);
    CHECK(prof1Custom.sharpness == 3 && prof1Custom.upscaler == 3 && prof1Custom.circleConfirms);
    CHECK(prof1Custom.deadzoneLeft == 5 && prof1Custom.triggerStrength == 1 && prof1Custom.touchpadMouse);
    CHECK(prof1Custom.locale == "pt-BR");

    hasCustom = true;
    auto prof2 = s.profileForGame("GAME2", "", &hasCustom);
    CHECK(!hasCustom);
    CHECK(prof2.sharpness == 1 && !prof2.circleConfirms && !prof2.touchpadMouse && prof2.locale.empty());

    // Title ID fallback when productId is empty.
    s.perGame["CONSOLE_SERVER_1"] = p1;
    auto consoleProf = s.profileForGame("", "CONSOLE_SERVER_1", &hasCustom);
    CHECK(hasCustom);
    CHECK(consoleProf.sharpness == 3 && consoleProf.circleConfirms && consoleProf.locale == "pt-BR");

    // Save to disk and reload.
    CHECK(s.desktopFullscreen);  // a desktop window starts fullscreen unless chosen otherwise
    s.desktopFullscreen = false;
    CHECK(s.save(path));
    xc::app::Settings reloaded;
    CHECK(reloaded.load(path));
    CHECK(!reloaded.desktopFullscreen);
    CHECK(reloaded.favorites.size() == 2 && reloaded.favorites[0] == "GAME1" && reloaded.favorites[1] == "GAME3");
    CHECK(reloaded.perGame.size() == 2);
    auto loadedP1 = reloaded.profileForGame("GAME1", "", &hasCustom);
    CHECK(hasCustom);
    CHECK(loadedP1.sharpness == 3 && loadedP1.upscaler == 3 && loadedP1.circleConfirms);
    CHECK(loadedP1.highRefresh && loadedP1.touchpadMouse && loadedP1.locale == "pt-BR");
    CHECK(loadedP1.deadzoneLeft == 5 && loadedP1.deadzoneRight == 8 && loadedP1.triggerStrength == 1);

    // Remove game profile restores default.
    reloaded.perGame.erase("GAME1");
    auto afterErase = reloaded.profileForGame("GAME1", "", &hasCustom);
    CHECK(!hasCustom);
    CHECK(afterErase.sharpness == reloaded.sharpness && afterErase.locale.empty());

    std::remove(path.c_str());
}

static void testPlaytimeLimit() {
    int64_t rem = -1, max = -1;

    // 1. Valid JSON format
    std::string jsonStr = R"({"remainingGameplayTimeInSeconds": 36000, "maxGameplayTimeInSeconds": 54000})";
    CHECK(auth::parsePlaytimeLimit(jsonStr, rem, max));
    CHECK(rem == 36000 && max == 54000);

    // 2. Nested JSON format
    std::string nestedJson = R"({"user": {"limits": {"remainingGameplayTimeInSeconds": 7200, "maxGameplayTimeInSeconds": 18000}}})";
    CHECK(auth::parsePlaytimeLimit(nestedJson, rem, max));
    CHECK(rem == 7200 && max == 18000);

    // 3. String-encoded numbers in JSON
    std::string strJson = R"({"remainingGameplayTimeInSeconds": "18000", "maxGameplayTimeInSeconds": "36000"})";
    CHECK(auth::parsePlaytimeLimit(strJson, rem, max));
    CHECK(rem == 18000 && max == 36000);

    // 4. Protobuf wire format (tag 1 varint 18000, tag 2 varint 54000)
    std::string proto = std::string("\x08\xD0\x8C\x01\x10\xF0\xA5\x03", 8);
    CHECK(auth::parsePlaytimeLimit(proto, rem, max));
    CHECK(rem == 18000 && max == 54000);

    // 5. Empty and invalid inputs
    CHECK(!auth::parsePlaytimeLimit("", rem, max));
    CHECK(!auth::parsePlaytimeLimit("not a valid payload", rem, max));

    // 6. Zero playtime remains purely informational
    std::string zeroJson = R"({"remainingGameplayTimeInSeconds": 0, "maxGameplayTimeInSeconds": 18000})";
    CHECK(auth::parsePlaytimeLimit(zeroJson, rem, max));
    CHECK(rem == 0 && max == 18000);

    // 7. Client-side launch is never blocked by zero or low remaining playtime
    ui::Fonts fonts;
    ui::ImageCache images([] {});
    ui::AppUi app(fonts, images);
    ui::GameTile tile;
    tile.titleId = "TEST_TITLE_1";
    tile.productId = "TEST_PROD_1";
    tile.playable = true;
    ui::GameRow row;
    row.title = "Popular";
    row.tiles = {tile};
    app.setRows({row});
    app.setPlaytime(0, 18000);  // 0 seconds remaining
    app.showHome();
    ui::NavInput in;
    in.accept = true;
    app.handle(in);                   // Home -> Details
    ui::UiEvent ev = app.handle(in);  // Details -> Play
    CHECK(ev.action == ui::Action::Play);
    CHECK(ev.game.titleId == "TEST_TITLE_1");
}

static void testStreamMenu() {
    using xc::ui::MenuAction;
    xc::ui::Fonts fonts;  // not loaded: handle() never draws
    xc::ui::StreamMenu menu(fonts);
    auto press = [&](void (*set)(xc::ui::NavInput&)) {
        xc::ui::NavInput n;
        set(n);
        return menu.handle(n);
    };
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::None);  // closed
    menu.open(0, false);
    CHECK(menu.isOpen());
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::XboxButton);  // first; it closes
    CHECK(!menu.isOpen());
    menu.open(0, false);
    press([](xc::ui::NavInput& n) { n.down = true; });
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::ProfileToggle);
    CHECK(menu.hasCustomProfile());
    press([](xc::ui::NavInput& n) { n.down = true; });
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::Stats);
    CHECK(menu.statsOn());
    press([](xc::ui::NavInput& n) { n.down = true; });
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::Upscaler);  // FSR -> FSR + clean-up
    CHECK(menu.upscaler() == 2);
    press([](xc::ui::NavInput& n) { n.right = true; });  // -> AI
    CHECK(menu.upscaler() == 1);
    press([](xc::ui::NavInput& n) { n.right = true; });  // -> AI + clean-up
    CHECK(menu.upscaler() == 3);
    press([](xc::ui::NavInput& n) { n.right = true; });  // wraps to FSR
    CHECK(menu.upscaler() == 0);
    press([](xc::ui::NavInput& n) { n.down = true; });
    CHECK(press([](xc::ui::NavInput& n) { n.left = true; }) == MenuAction::Sharpness);  // off -> high
    CHECK(menu.sharpness() == 3);
    press([](xc::ui::NavInput& n) { n.down = true; });
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::Deband);  // low -> high
    CHECK(menu.deband() == 2);
    press([](xc::ui::NavInput& n) { n.right = true; });  // -> auto
    CHECK(menu.deband() == 3);
    press([](xc::ui::NavInput& n) { n.down = true; });
    press([](xc::ui::NavInput& n) { n.left = true; });  // 1080p -> 720p
    CHECK(menu.resolution() == 1);
    press([](xc::ui::NavInput& n) { n.left = true; });  // wraps to 1440p
    CHECK(menu.resolution() == 2);
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::Resolution);
    press([](xc::ui::NavInput& n) { n.down = true; });
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::Triggers);
    press([](xc::ui::NavInput& n) { n.down = true; });
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::Deadzone);
    press([](xc::ui::NavInput& n) { n.down = true; });
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::ConfirmButton);
    press([](xc::ui::NavInput& n) { n.down = true; });  // gyro aiming: off -> always -> while aiming
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::Gyro && menu.gyroMode() == 1);
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::Gyro && menu.gyroMode() == 2);
    press([](xc::ui::NavInput& n) { n.down = true; });  // its intensity, 1..10
    CHECK(press([](xc::ui::NavInput& n) { n.left = true; }) == MenuAction::Gyro && menu.gyroLevel() == 4);
    press([](xc::ui::NavInput& n) { n.down = true; });  // "Leave game", last
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::Leave);
    CHECK(!menu.isOpen());
    menu.open(1, true);
    CHECK(press([](xc::ui::NavInput& n) { n.back = true; }) == MenuAction::Close);
    // Best (Auto) offered as resolution 2; cycling is 720p (1) -> 1080p (0) -> Best (2).
    menu.open(2, false, 0, 1, 0, false, false);
    CHECK(menu.resolution() == 2);
    for (int i = 0; i < 6; ++i) press([](xc::ui::NavInput& n) { n.down = true; });  // to the resolution
    press([](xc::ui::NavInput& n) { n.right = true; });
    CHECK(menu.resolution() == 1);  // Best (2) -> wraps to 720p (1)
    press([](xc::ui::NavInput& n) { n.right = true; });
    CHECK(menu.resolution() == 0);  // 720p -> 1080p
    press([](xc::ui::NavInput& n) { n.right = true; });
    CHECK(menu.resolution() == 2);  // 1080p -> Best (2)

    // Test stats indicator customization
    menu.open(0, true);
    press([](xc::ui::NavInput& n) { n.down = true; });  // -> Profile
    press([](xc::ui::NavInput& n) { n.down = true; });  // -> Stats
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::Stats);
    CHECK(menu.isCustomizingStats());
    // Indicator 0 (StatsFps) is on by default in StatsDefaultMask; toggle it off:
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::Stats);
    CHECK((menu.statsMask() & xc::ui::StatsFps) == 0);
    // Toggle it back on:
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::Stats);
    CHECK((menu.statsMask() & xc::ui::StatsFps) != 0);
    // Back returns to main menu:
    press([](xc::ui::NavInput& n) { n.back = true; });
    CHECK(!menu.isCustomizingStats());
    CHECK(menu.isOpen());
}

// A zip archive of stored (uncompressed) files, as the test needs one.
static std::string storedZip(const std::vector<std::pair<std::string, std::string>>& files) {
    auto le = [](std::string& s, uint32_t v, int bytes) {
        for (int i = 0; i < bytes; ++i) s += static_cast<char>((v >> (8 * i)) & 0xFF);
    };
    std::string out, dir;
    for (const auto& [name, data] : files) {
        uint32_t at = static_cast<uint32_t>(out.size()), n = static_cast<uint32_t>(data.size());
        le(out, 0x04034b50, 4), le(out, 20, 2), le(out, 0, 2), le(out, 0, 2), le(out, 0, 4), le(out, 0, 4);
        le(out, n, 4), le(out, n, 4), le(out, static_cast<uint32_t>(name.size()), 2), le(out, 0, 2);
        out += name + data;
        le(dir, 0x02014b50, 4), le(dir, 20, 2), le(dir, 20, 2), le(dir, 0, 2), le(dir, 0, 2), le(dir, 0, 4), le(dir, 0, 4);
        le(dir, n, 4), le(dir, n, 4), le(dir, static_cast<uint32_t>(name.size()), 2), le(dir, 0, 2), le(dir, 0, 2);
        le(dir, 0, 2), le(dir, 0, 2), le(dir, 0, 4), le(dir, at, 4);
        dir += name;
    }
    uint32_t dirAt = static_cast<uint32_t>(out.size());
    out += dir;
    le(out, 0x06054b50, 4), le(out, 0, 2), le(out, 0, 2), le(out, static_cast<uint32_t>(files.size()), 2);
    le(out, static_cast<uint32_t>(files.size()), 2), le(out, static_cast<uint32_t>(dir.size()), 4), le(out, dirAt, 4);
    le(out, 0, 2);
    return out;
}

static void testUpdater() {
    using namespace xc::app;
    CHECK(contentVersionOf("v0.8.1") == "00.801.000" && contentVersionOf("1.10.0").empty());
    CHECK(updatablePath("eboot.bin") && updatablePath("sce_sys/param.json") && updatablePath("assets/fonts/a.ttf"));
    CHECK(!updatablePath("../eboot.bin") && !updatablePath("/data/x") && !updatablePath("a//b") && !updatablePath("a/./b"));
    CHECK(!updatablePath("account.json") && !updatablePath("settings.json") && !updatablePath("imgcache/x"));
    CHECK(!updatablePath("update.old/eboot.bin") && !updatablePath("update.journal") && !updatablePath("frame.ppm"));

    // A signature by a throwaway key (made for this test only).
    const char* pub =
        "-----BEGIN PUBLIC KEY-----\n"
        "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAETaRKE8e1bXOEYcQDqc3yvrCF4reo\n"
        "nn1S6x6D3sFYl8eHLd7qogQNmh7Nseqx41gMcXh5TVqOjvMS+7mpqk0Wxg==\n"
        "-----END PUBLIC KEY-----\n";
    const char* hex =
        "3045022021a9fa17a6717284e6d5989fcffb6363d371f97ea00b4e9ecec4adaa11a01a050221008053381afe913f3071594281868a44"
        "cd57589796b296b1818b37a6e3c8880514";
    std::string sig;
    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) sig += static_cast<char>(std::stoi(std::string(hex + i, 2), nullptr, 16));
    std::string err;
    CHECK(verifySignature("PSBox test", sig, pub, err));
    CHECK(!verifySignature("PSBox tesT", sig, pub, err));
    CHECK(!verifySignature("PSBox test", sig, kReleaseKey, err));  // another key

    std::vector<ZipEntry> files;
    CHECK(unzip(storedZip({{"PPSA99810/eboot.bin", "elf"}, {"PPSA99810/sce_sys/param.json", "{}"}}), "PPSA99810/", files,
                err));
    CHECK(files.size() == 2 && files[0].path == "eboot.bin" && files[0].data == "elf" && files[1].path == "sce_sys/param.json");
    CHECK(!unzip(storedZip({{"PPSA99810/../evil", "x"}}), "PPSA99810/", files, err));
    CHECK(!unzip(storedZip({{"PPSA99810/account.json", "x"}}), "PPSA99810/", files, err));
    CHECK(!unzip(storedZip({{"other/eboot.bin", "x"}}), "PPSA99810/", files, err));
    CHECK(!unzip("not a zip", "PPSA99810/", files, err));

    // An update cut short while files moved: the old ones go back.
    std::string dir = "/tmp/xcloud-updater-test";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir + "/update.old/sce_sys").c_str());
    xc::platform::writeFileAtomic(dir + "/eboot.bin", "new");
    xc::platform::writeFileAtomic(dir + "/update.old/eboot.bin", "old");
    xc::platform::writeFileAtomic(dir + "/update.old/sce_sys/param.json", "old param");
    xc::platform::writeFileAtomic(dir + "/account.json", "mine");
    xc::platform::writeFileAtomic(dir + "/update.journal", "swapping\n");
    CHECK(recoverUpdate(dir));
    std::string text;
    CHECK(xc::platform::readFile(dir + "/eboot.bin", text) && text == "old");
    CHECK(xc::platform::readFile(dir + "/sce_sys/param.json", text) && text == "old param");
    CHECK(xc::platform::readFile(dir + "/account.json", text) && text == "mine");
    CHECK(!xc::platform::readFile(dir + "/update.journal", text) && !xc::platform::readFile(dir + "/update.old/eboot.bin", text));
    CHECK(!recoverUpdate(dir));  // nothing left to do
    std::system(("rm -rf " + dir).c_str());
}

static void testControllerTuning() {
    using xc::input::radialDeadzone;
    using xc::input::triggerAmplitude;
    auto near = [](float a, float b) { return std::abs(a - b) < 0.01f; };
    float x = 0.1f, y = 0.1f;
    radialDeadzone(x, y, 0.15f);  // |0.14| < 0.15: nothing
    CHECK(x == 0 && y == 0);
    x = 1, y = 0;
    radialDeadzone(x, y, 0.15f);  // the edge stays the edge
    CHECK(near(x, 1) && y == 0);
    x = 0.575f, y = 0;
    radialDeadzone(x, y, 0.15f);  // halfway out of the dead zone -> 0.5, from 0 (no jump)
    CHECK(near(x, 0.5f));
    x = 0.6f, y = 0.1f;  // a diagonal near the X axis keeps its Y (no snapping to the axis)
    radialDeadzone(x, y, 0.15f);
    CHECK(y > 0.05f && near(y / x, 0.1f / 0.6f));

    CHECK(triggerAmplitude(38) == 4 && triggerAmplitude(128) == 6);  // medium: as before
    CHECK(triggerAmplitude(38, 0) == 0 && triggerAmplitude(0, 4) == 0);
    CHECK(triggerAmplitude(38, 1) == 2 && triggerAmplitude(38, 3) == 6 && triggerAmplitude(38, 4) == 8);
    CHECK(triggerAmplitude(255, 3) == 8 && triggerAmplitude(1, 1) == 1);

    using xc::input::triggerCommand;
    auto vib = triggerCommand(38, 2, 1, 2, false, 0);  // vibration style: the motor, 60 Hz
    CHECK(vib.mode == 3 && vib.data[1] == 4 && vib.data[2] == 60);
    auto still = triggerCommand(0, 2, 1, 2, false, 0);  // no request: the medium resistance (from 3, strength 4)
    CHECK(still.mode == 1 && still.data[0] == 3 && still.data[1] == 4);
    CHECK(triggerCommand(0, 2, 1, 0, false, 0).mode == 0);  // no resistance: off
    // Pulses (20 Hz: 25 ms push, 25 ms release) keep the weight: base 3 + amplitude 4.
    auto push = triggerCommand(38, 2, 1, 2, true, 0), release = triggerCommand(38, 2, 1, 2, true, 30);
    CHECK(push.mode == 1 && push.data[1] == 7 && release.mode == 1 && release.data[1] == 3);
    CHECK(triggerCommand(38, 2, 1, 0, true, 30).mode == 0);  // no resistance: released is off
    CHECK(triggerCommand(38, 0, 1, 2, true, 0) == still);    // vibration off: only the weight
}

static void testReport() {
    using namespace xc::app::report;
    // Private data out.
    // Made-up values (the tokens put together here, so the source holds none).
    const std::string jwt = std::string("ey") + "JmYWtlIjoidG9rZW4ifQ" + "QUJDREVGR0hJSktMTU5PUA.abc";
    const std::string xbl = std::string("XBL3.0 x=") + "12345;" + "ey" + "Jfake";
    std::string s = sanitize("signed in as SomePlayer from 203.0.113.7 token " + jwt + " auth " + xbl +
                                 " mail me@example.com id 0123456789abcdef0123456789abcdef ok",
                             {"SomePlayer"});
    CHECK(s.find("SomePlayer") == std::string::npos && s.find("<name>") != std::string::npos);
    CHECK(s.find("203.0.113") == std::string::npos && s.find("<ip>") != std::string::npos);
    CHECK(s.find("ey" "J") == std::string::npos && s.find("12345") == std::string::npos && s.find("<token>") != std::string::npos);
    CHECK(s.find("example.com") == std::string::npos && s.find("<email>") != std::string::npos);
    CHECK(s.find("0123456789abcdef0123") == std::string::npos && s.find("<id>") != std::string::npos);
    CHECK(sanitize("v0.9.5 at 1080p, 60 fps, 9 ms", {}) == "v0.9.5 at 1080p, 60 fps, 9 ms");  // nothing private
    CHECK(sanitize("crash: SIGSEGV pc=eboot+0x1a2b3c addr=0x0", {}) == "crash: SIGSEGV pc=eboot+0x1a2b3c addr=0x0");

    // The excerpt: the session wanted, its warnings / errors and last lines.
    std::string log = "10:00 [I] === PSBox Cloud Gaming starting ===\n10:00 [E] old error\n"
                      "11:00 [I] === PSBox Cloud Gaming starting ===\n";
    for (int i = 0; i < 40; ++i) log += "11:01 [I] line " + std::to_string(i) + "\n";
    log += "11:02 [W] a warning\n11:03 [I] tail\n";
    std::string now = logExcerpt(log, false, 5), before = logExcerpt(log, true, 5);
    CHECK(now.find("old error") == std::string::npos && now.find("a warning") != std::string::npos);
    CHECK(now.find("line 39") != std::string::npos && now.find("line 3\n") == std::string::npos && now.find("...") != std::string::npos);
    CHECK(before.find("old error") != std::string::npos && before.find("line") == std::string::npos);
    CHECK(logExcerpt("no sessions\n", true).empty());

    // The link fits the QR code; the log keeps its end, and a crash report
    // keeps its offset and app version in the issue body.
    Info info{"Error: certificates", "TLS setup failed", "0.9.6", "13.00", "", "crash: SIGSEGV pc=eboot+0x1a2b3c addr=0x0"};
    for (int i = 0; i < 200; ++i) info.log += "12:00 [I] a long line of the log number " + std::to_string(i) + "\n";
    std::string url = issueUrl(info, 1800);
    CHECK(url.size() <= 1800 && url.find("template=bug_report.yml") != std::string::npos);
    CHECK(url.find("number+199") != std::string::npos && url.find("number+0%0A") == std::string::npos);
    CHECK(url.find("release=0.9.6") != std::string::npos && url.find("title=Error:+certificates") != std::string::npos);
    CHECK(url.find("pc%3Deboot%2B0x1a2b3c") != std::string::npos && url.find("(v0.9.6)") != std::string::npos);
}

[[gnu::noinline]] static void triggerNullWriteCrash() {
    volatile int* p = nullptr;
    asm volatile("" : "+r"(p));
    *p = 1;
}

static void testCrashGuard() {
    // A crash in a child process leaves the marker with its stage and faulting PC offset.
    std::string marker = "/tmp/xcloud-tests-crash.marker";
    std::remove(marker.c_str());
    pid_t pid = fork();
    if (pid == 0) {
        xc::app::crash::install(marker);
        xc::app::crash::setStage("streaming TEST");
        triggerNullWriteCrash();
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);  // still dies of it
    auto prev = xc::app::crash::takePrevious(marker);
    CHECK(prev.found && prev.kind == "signal 11 (SIGSEGV)" && prev.stage == "streaming TEST");
    CHECK(prev.crash.rfind("crash: SIGSEGV pc=eboot+0x", 0) == 0);
    CHECK(prev.crash.find("addr=0x0") != std::string::npos);
    // The offset points inside triggerNullWriteCrash().
    unsigned long long pcOff = std::strtoull(prev.crash.c_str() + sizeof("crash: SIGSEGV pc=eboot+0x") - 1, nullptr, 16);
    CHECK(pcOff > 0);
    CHECK(!xc::app::crash::takePrevious(marker).found);  // taken once

    // A signal raised inside a system library (std::raise) records that library's name and offset.
    pid = fork();
    if (pid == 0) {
        xc::app::crash::install(marker);
        xc::app::crash::setStage("home");
        std::raise(SIGABRT);
        _exit(0);
    }
    waitpid(pid, &status, 0);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    prev = xc::app::crash::takePrevious(marker);
    CHECK(prev.found && prev.kind == "signal 6 (SIGABRT)" && prev.stage == "home");
    CHECK(prev.crash.rfind("crash: SIGABRT pc=", 0) == 0 && prev.crash.find("+0x") != std::string::npos);
}

static void testBuiltinCa() {
    // A missing bundle (an incomplete copy of the app): the copy built in.
    CHECK(xc::net::initTls("/nonexistent/cacert.pem"));
    CHECK(xc::net::usingBuiltinCa());
    xc::net::shutdownTls();
}

static void testPrivacyAndRobustness() {
    // Error answers: their code and message only, never the rest.
    std::string sdp = R"({"status":"failed","code":"Rejected","sdp":"c=IN IP4 203.0.113.7","candidate":"a=candidate:1"})";
    std::string fields = xcloud::errorFields(sdp);
    CHECK(fields.find("Rejected") != std::string::npos && fields.find("failed") != std::string::npos);
    CHECK(fields.find("IP4") == std::string::npos && fields.find("candidate") == std::string::npos);
    CHECK(xcloud::errorFields(R"({"errorDetails":{"code":"NoEntitlement","message":"m"}})").find("NoEntitlement") !=
          std::string::npos);
    CHECK(xcloud::errorFields("<html>203.0.113.7</html>").empty() && xcloud::errorFields("").empty());

    // Numbers no int64_t holds: the default, not undefined behaviour.
    CHECK(json::parse("1e400")->asInt(7) == 7 && json::parse("-1e30")->asInt(7) == 7 && json::parse("42")->asInt() == 42);

    // Chunked bodies: well formed, and every kind of broken one refused.
    std::string body;
    CHECK(net::dechunk("4\r\nWiki\r\n5;x=1\r\npedia\r\n0\r\n\r\n", body) && body == "Wikipedia");
    CHECK(!net::dechunk("zz\r\nab\r\n0\r\n\r\n", body));
    CHECK(!net::dechunk("ffffffffffffffff\r\nab\r\n0\r\n\r\n", body));
    CHECK(!net::dechunk("2\r\nabXX0\r\n\r\n", body));
    CHECK(!net::dechunk("4\r\nWi", body));

    // Report titles aren't cut inside a character; a NUL is encoded.
    CHECK(app::report::shorten("Configura\xC3\xA7\xC3\xB5" "es", 11) == "Configura\xC3\xA7");
    CHECK(app::report::shorten("Configura\xC3\xA7\xC3\xB5" "es", 10) == "Configura");
    CHECK(app::report::shorten("short", 70) == "short");
    app::report::Info info{"t", std::string("a\0b", 3), "1", "13.00", ""};
    CHECK(app::report::issueUrl(info).find("a%00b") != std::string::npos);

    // Sign-in: the service busy or throttling keeps the code polling and
    // the stored sign-in; only a refused token drops it.
    int status = 0;
    std::string answer;
    int calls = 0;
    net::setTestTransport([&](const net::Request&) {
        ++calls;
        net::Response r;
        r.status = status;
        r.body = answer;
        return r;
    });
    auth::MsaClient msa;
    auth::DeviceCode dc;
    dc.deviceCode = "code";
    auth::MsaTokens tokens;
    std::string err;
    status = 503, answer = "<html>busy</html>";
    CHECK(msa.pollDeviceCode(dc, tokens, err) == auth::PollResult::Pending);
    status = 429;
    CHECK(msa.pollDeviceCode(dc, tokens, err) == auth::PollResult::SlowDown);
    status = 400, answer = R"({"error":"authorization_pending"})";
    CHECK(msa.pollDeviceCode(dc, tokens, err) == auth::PollResult::Pending);
    bool networkError = false;
    status = 429, answer = "";
    CHECK(!msa.refresh("r", tokens, err, {}, &networkError) && networkError);
    status = 400, answer = R"({"error":"invalid_grant"})";
    CHECK(!msa.refresh("r", tokens, err, {}, &networkError) && !networkError);

    // A title list whose server always says "more" with the same token ends.
    status = 200, answer = R"({"results":[{"titleId":"GAME1","details":{"productId":"P1"}}],"continuationToken":"same"})";
    calls = 0;
    xcloud::GssvClient gssv;
    std::vector<xcloud::Title> titles;
    CHECK(gssv.listTitles(titles, err) && calls == 2);
    status = 200, answer = R"({"results":[],"continuationToken":"other"})";
    calls = 0;
    CHECK(gssv.listTitles(titles, err) && calls == 1);

    // titlehub refusing every batch: an error after a few requests, not
    // ~200 of them and every id marked unknown.
    status = 400, answer = "{}";
    calls = 0;
    std::vector<std::string> ids;
    for (int i = 0; i < 100; ++i) ids.push_back(std::to_string(1000 + i));
    std::map<std::string, std::string> platforms;
    CHECK(!xcloud::fetchPlatforms("auth", ids, platforms, err) && calls < 60 && platforms.size() <= 8);
    net::setTestTransport({});

    // Prices whose request failed are asked for again.
    ui::Fonts fonts;
    ui::ImageCache images([] {});
    ui::AppUi app(fonts, images);
    std::vector<ui::GameTile> toBuy(3);
    for (int i = 0; i < 3; ++i) toBuy[i].productId = "BUY" + std::to_string(i), toBuy[i].purchasable = true, toBuy[i].playable = false;
    app.setOwned({}, toBuy, true);
    app.showHome();
    auto first = app.pricesWanted(20, true);
    CHECK(first.size() == 3 && app.pricesWanted(20, true).empty());
    app.pricesFailed(first);
    CHECK(app.pricesWanted(20, true).size() == 3);
    // The user's console names, for keeping them out of reports.
    ui::ConsoleTile console;
    console.serverId = "F4000000AAAA";
    console.name = "Living Room Xbox";
    app.setConsoles({console}, true);
    auto names = app.consoleNames();
    CHECK(names.size() == 1 && names[0] == "Living Room Xbox");
    CHECK(app::report::sanitize("starting Living Room Xbox", names).find("Living Room") == std::string::npos);

    // An update whose old files can't all go back keeps them, and its
    // journal, for the next start.
    std::string dir = "/tmp/xcloud-rollback-test";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir + "/update.old " + dir + "/eboot.bin/busy").c_str());
    xc::platform::writeFileAtomic(dir + "/update.old/eboot.bin", "old");
    xc::platform::writeFileAtomic(dir + "/update.journal", "swapping\n");
    app::recoverUpdate(dir);
    std::string text;
    CHECK(xc::platform::readFile(dir + "/update.old/eboot.bin", text) && text == "old");
    CHECK(xc::platform::readFile(dir + "/update.journal", text));
    std::system(("rm -rf " + dir).c_str());
    // The same file twice in a package: refused.
    std::vector<app::ZipEntry> files;
    CHECK(!app::unzip(storedZip({{"PPSA99810/eboot.bin", "a"}, {"PPSA99810/eboot.bin", "b"}}), "PPSA99810/", files, err));
}

static void testStoreLeftovers() {
    // ProsperoStore replaced the app folder and kept the old one: the
    // sign-in and settings come back from it, once.
    std::string dir = "/tmp/xcloud-store-test/app", prev = "/tmp/xcloud-store-test/previous";
    std::system("rm -rf /tmp/xcloud-store-test && mkdir -p /tmp/xcloud-store-test/app /tmp/xcloud-store-test/previous");
    xc::platform::writeFileAtomic(prev + "/account.json", "{\"a\":1}");
    xc::platform::writeFileAtomic(prev + "/settings.json", "{\"language\":\"pt-BR\"}");
    xc::platform::writeFileAtomic(prev + "/xcloud.log", "old log");
    std::string text;
    CHECK(app::importStoreLeftovers(dir, prev));
    CHECK(xc::platform::readFile(dir + "/account.json", text) && text == "{\"a\":1}");
    CHECK(xc::platform::readFile(dir + "/settings.json", text) && text.find("pt-BR") != std::string::npos);
    CHECK(!xc::platform::readFile(dir + "/xcloud.log", text));             // only the user's settings and sign-in
    CHECK(xc::platform::readFile(prev + "/account.json", text));           // the store's folder stays
    // Signed out later: the old account doesn't come back.
    std::remove((dir + "/account.json").c_str());
    CHECK(!app::importStoreLeftovers(dir, prev) && !xc::platform::readFile(dir + "/account.json", text));
    // Already signed in when the store's folder is found: never imported.
    std::system("rm -rf /tmp/xcloud-store-test/app && mkdir -p /tmp/xcloud-store-test/app");
    xc::platform::writeFileAtomic(dir + "/account.json", "{\"b\":2}");
    CHECK(!app::importStoreLeftovers(dir, prev));
    std::remove((dir + "/account.json").c_str());
    CHECK(!app::importStoreLeftovers(dir, prev));
    // No store folder: nothing.
    std::system("rm -rf /tmp/xcloud-store-test/app && mkdir -p /tmp/xcloud-store-test/app");
    CHECK(!app::importStoreLeftovers(dir, "/tmp/xcloud-store-test/none"));
    CHECK(!app::updatablePath("store_import.done"));
    std::system("rm -rf /tmp/xcloud-store-test");
}

static void testMicrosoftHost() {
    // The services whose refusal for the clock says the console's date is wrong.
    for (const char* h : {"login.live.com", "displaycatalog.mp.microsoft.com", "xhome.gssv-play-prod.xboxlive.com",
                          "store-images.s-microsoft.com", "www.xbox.com", "catalog.gamepass.com", "xboxlive.com"})
        CHECK(net::microsoftHost(h));
    for (const char* h : {"expired.badssl.com", "evilxboxlive.com", "microsoft.com.evil.example", "github.com", ""})
        CHECK(!net::microsoftHost(h));
}

static void testSubscriptionPlan() {
    // The store's entitlements: the highest active Game Pass plan.
    auto entry = [](const char* id, const char* status) {
        return std::string("\"") + id + "\":{\"productId\":\"" + id + "\",\"isOwned\":true,\"status\":\"" + status + "\"}";
    };
    std::string all = "{\"entitlements\":{" + entry("CFQ7TTC0K5DJ", "Active") + "," + entry("CFQ7TTC0KHS0", "Active") + "," +
                      entry("CFQ7TTC0P85B", "Active") + "}}";
    CHECK(auth::subscriptionPlan(all) == "Ultimate");
    CHECK(auth::subscriptionPlan("{\"entitlements\":{" + entry("CFQ7TTC0P85B", "Active") + "}}") == "Premium");
    CHECK(auth::subscriptionPlan("{\"entitlements\":{" + entry("CFQ7TTC0KHS0", "Expired") + "," + entry("CFQ7TTC0K5DJ", "Active") + "}}") ==
          "Essential");
    CHECK(auth::subscriptionPlan("{\"entitlements\":{\"cfq7ttc0kgq8\":{\"isSatisfyingEntitlement\":true}}}") == "PC");
    CHECK(auth::subscriptionPlan("{\"entitlements\":{}}").empty() && auth::subscriptionPlan("not json").empty());
    // The log's renewal line: no date, only days left.
    std::string renews = "{\"entitlements\":{\"CFQ7TTC0KHS0\":{\"status\":\"Active\",\"autoRenew\":true,\"endDate\":\"2026-11-14T00:00:00Z\"}}}";
    CHECK(auth::subscriptionRenewal(renews, 1791504000) == "renews automatically");
    std::string ends = "{\"entitlements\":{\"CFQ7TTC0KHS0\":{\"status\":\"Active\",\"autoRenew\":false,\"endDate\":\"2026-10-20T00:00:00.0000000Z\"}}}";
    CHECK(auth::subscriptionRenewal(ends, 1792108800) == "no auto-renew, ends in 4 day(s)");  // from 2026-10-16
    CHECK(auth::subscriptionRenewal("{\"entitlements\":{}}", 0).empty());

    // Catalog plan tiers from PassMetadataByPassProductId, and search Plan filter.
    using namespace xcloud;
    auto pass = [](const char* text) { return productPlanFromJson(text); };
    CHECK(pass(R"({"CFQ7TTC0K5DJ":{},"CFQ7TTC0P85B":{},"CFQ7TTC0KHS0":{}})") == kPlanEssential);
    CHECK(pass(R"({"CFQ7TTC0P85B":{},"CFQ7TTC0KHS0":{}})") == kPlanPremium);
    CHECK(pass(R"({"CFQ7TTC0KHS0":{},"CFQ7TTC0K5DH":{}})") == kPlanUltimate);
    CHECK(pass(R"({})") == kPlanUltimate);
    CHECK(planTier("Essential") == kPlanEssential && planTier("Premium") == kPlanPremium &&
          planTier("Ultimate") == kPlanUltimate && planTier("PC") == kPlanNone && planTier("") == kPlanNone);
    CHECK(planIncludes(kPlanEssential, kPlanEssential, false) && !planIncludes(kPlanEssential, kPlanPremium, false) &&
          !planIncludes(kPlanEssential, kPlanUltimate, false) && planIncludes(kPlanEssential, kPlanUltimate, true));
    CHECK(planIncludes(kPlanPremium, kPlanEssential, false) && planIncludes(kPlanPremium, kPlanPremium, false) &&
          !planIncludes(kPlanPremium, kPlanUltimate, false));
    CHECK(planIncludes(kPlanUltimate, kPlanEssential, false) && planIncludes(kPlanUltimate, kPlanPremium, false) &&
          planIncludes(kPlanUltimate, kPlanUltimate, false));

    ui::Fonts fonts;
    ui::ImageCache images([] {});
    ui::AppUi app(fonts, images);
    ui::GameTile ess, prem, ult, f2p;
    ess.productId = "ESS", ess.name = "Alpha", ess.plan = kPlanEssential, ess.playable = true;
    prem.productId = "PREM", prem.name = "Beta", prem.plan = kPlanPremium, prem.playable = true;
    ult.productId = "ULT", ult.name = "Gamma", ult.plan = kPlanUltimate, ult.playable = false;
    f2p.productId = "F2P", f2p.name = "Delta", f2p.plan = kPlanNone, f2p.freeInStore = true, f2p.playable = false;
    app.setSubscription("Premium");
    app.setSearchPools({ess, prem, ult, f2p}, {});
    app.showFilteredSearch(ui::Tab::GamePass, false, 0, "", 0, -1);
    app.chooseFilter(ui::AppUi::Filter::Plan, 3);  // Essential: Alpha + Delta (free-to-play)
    CHECK(app.searchResults(10).size() == 2);
    app.chooseFilter(ui::AppUi::Filter::Plan, 2);  // Premium: Alpha + Beta + Delta
    CHECK(app.searchResults(10).size() == 3);
    app.chooseFilter(ui::AppUi::Filter::Plan, 1);  // Ultimate: all 4
    CHECK(app.searchResults(10).size() == 4);
    app.chooseFilter(ui::AppUi::Filter::Plan, 4);  // All plans: all 4
    CHECK(app.searchResults(10).size() == 4);
    app.chooseFilter(ui::AppUi::Filter::Plan, 0);  // My plan (Premium), with query "a": Alpha, Beta, Delta (not Gamma)
    app.typeText("a");
    CHECK(app.searchResults(10).size() == 3);
}

static void testHostPacer() {
    // Five a second per Microsoft host; other hosts never wait; a 429 holds
    // the host for its Retry-After (30 s at most).
    net::HostPacer p;
    CHECK(p.reserve("xgpuweb.gssv-play-prod.xboxlive.com", 1000) == 0);
    CHECK(p.reserve("xgpuweb.gssv-play-prod.xboxlive.com", 1000) == 200);
    CHECK(p.reserve("xgpuweb.gssv-play-prod.xboxlive.com", 1100) == 300);
    CHECK(p.reserve("titlehub.xboxlive.com", 1100) == 0);  // its own slots
    CHECK(p.reserve("login.live.com", 5000) == 0 && p.reserve("login.live.com", 5000) == 200);
    for (int i = 0; i < 10; ++i) CHECK(p.reserve("store-images.s-microsoft.com", 1000) == 0);
    CHECK(!net::HostPacer::paced("evilxboxlive.com") && net::HostPacer::paced("a.xboxlive.com"));
    p.throttled("peoplehub.xboxlive.com", 7, 10000);
    CHECK(p.reserve("peoplehub.xboxlive.com", 10000) == 7000);
    p.throttled("peoplehub.xboxlive.com", 3600, 20000);
    CHECK(p.reserve("peoplehub.xboxlive.com", 20000) == 30000);
    p.throttled("store-images.s-microsoft.com", 60, 0);
    CHECK(p.reserve("store-images.s-microsoft.com", 0) == 0);
}

static void testAutoDeband() {
    using xc::app::debandForMbps;
    CHECK(debandForMbps(0.3, 1) == -1);  // starting or standing still: no measure
    CHECK(debandForMbps(3, 0) == 2 && debandForMbps(7, 0) == 1 && debandForMbps(12, 0) == 0);
    // A 1 Mbps margin each way: near a limit the level in use stays.
    CHECK(debandForMbps(5.5, 2) == 2 && debandForMbps(4.5, 1) == 1 && debandForMbps(10.5, 1) == 1);
    CHECK(debandForMbps(9.5, 0) == 0 && debandForMbps(6.5, 2) == 1);

    xc::app::AutoDeband a;
    CHECK(a.level() == 1);
    int changedAt = 0;
    for (int s = 1; s <= 10 && !changedAt; ++s)
        if (a.update(1.5)) changedAt = s;
    CHECK(changedAt == 8);  // the first 5 s ignored, then 3 s in a row
    CHECK(a.level() == 2);
    // A one-second spike doesn't count; 3 s of a high bitrate do.
    CHECK(!a.update(20) && !a.update(1.5) && !a.update(20) && !a.update(20));
    CHECK(a.update(20) && a.level() == 0);
    CHECK(!a.update(0.2) && a.level() == 0);  // no measure: kept
    a.reset();
    CHECK(a.level() == 1);
    for (int s = 1; s <= 5; ++s) CHECK(!a.update(1.0));  // a new stream: the first 5 s again
}

static void testAudioEncoder() {
    xc::media::AudioEncoder enc;
    CHECK(enc.init(24000));
    CHECK(xc::media::AudioEncoder::kFrameSize == 960);

    // 960 samples of 440 Hz tone
    std::vector<int16_t> pcm(960);
    for (size_t i = 0; i < pcm.size(); ++i) {
        pcm[i] = static_cast<int16_t>(10000.0 * std::sin(2.0 * M_PI * 440.0 * i / 48000.0));
    }
    for (int frame = 0; frame < 10; ++frame) {
        std::vector<uint8_t> opus;
        CHECK(enc.encode(pcm.data(), pcm.size(), opus));
        CHECK(!opus.empty());
        CHECK(opus.size() > 5 && opus.size() < 1000);
    }
}

// A burst of loss: a frame lost for good, then a frame with a hole of its
// own, then a complete key frame. The receiver must resume at that key frame
// (it used to stop at the frame with the hole: a freeze of seconds).
static void testH264Resync() {
    xc::stream::VideoReceiveStats stats;
    int keyframeAsks = 0;
    xc::stream::H264Receiver rx([&] { ++keyframeAsks; }, stats, 150);
    auto packet = [](uint16_t seq, uint32_t ts, bool marker, uint8_t nal) {
        std::vector<uint8_t> b = {0x80, static_cast<uint8_t>((marker ? 0x80 : 0) | 102),
                                  static_cast<uint8_t>(seq >> 8), static_cast<uint8_t>(seq),
                                  static_cast<uint8_t>(ts >> 24), static_cast<uint8_t>(ts >> 16),
                                  static_cast<uint8_t>(ts >> 8), static_cast<uint8_t>(ts), 0, 0, 0, 7,
                                  nal, 0xAA, 0xBB};
        return rtc::make_message(reinterpret_cast<const std::byte*>(b.data()),
                                 reinterpret_cast<const std::byte*>(b.data()) + b.size());
    };
    size_t keyframesOut = 0, framesOut = 0;
    auto feed = [&](rtc::message_vector in) {
        rx.incoming(in, [](rtc::message_ptr) { return true; });
        for (const auto& m : in) {
            ++framesOut;
            if (m->size() > 4 && (static_cast<uint8_t>((*m)[4]) & 0x1F) == 5) ++keyframesOut;
        }
    };
    // The end of a frame from before (a boundary to start at), a key frame, a P frame.
    feed({packet(0, 0, true, 0x41), packet(1, 1500, true, 0x65), packet(2, 3000, true, 0x41)});
    CHECK(framesOut == 2 && keyframesOut == 1);
    // seq 4 never comes; seq 7 never comes; 9-10 a key frame.
    feed({packet(3, 4500, false, 0x41), packet(5, 6000, true, 0x41), packet(6, 7500, false, 0x41),
          packet(8, 7500, true, 0x41), packet(9, 9000, false, 0x65), packet(10, 9000, true, 0x65)});
    CHECK(framesOut == 2);  // waiting for the retransmission
    xc::platform::sleepMs(200);
    feed({});
    CHECK(keyframesOut == 2);  // resumed at the key frame
    CHECK(framesOut == 3);
    CHECK(stats.framesDropped >= 1);
    CHECK(keyframeAsks >= 1);
}

// An empty audio packet (seen after loss) must not end the sound.
static void testAudioDecoderEmptyPacket() {
    xc::media::AudioEncoder enc;
    xc::media::AudioDecoder dec;
    CHECK(enc.init(24000));
    CHECK(dec.init());
    std::vector<int16_t> pcm(960);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<int16_t>(8000.0 * std::sin(i * 0.05));
    std::vector<uint8_t> opus;
    std::vector<float> out;
    CHECK(enc.encode(pcm.data(), pcm.size(), opus));
    CHECK(dec.decode(opus.data(), opus.size(), out));
    CHECK(!dec.decode(nullptr, 0, out));
    out.clear();
    CHECK(enc.encode(pcm.data(), pcm.size(), opus));
    CHECK(dec.decode(opus.data(), opus.size(), out));
    CHECK(!out.empty());
}

static void testAudioDecoderPlc() {
    xc::media::AudioEncoder enc;
    xc::media::AudioDecoder dec;
    CHECK(enc.init(24000));
    CHECK(dec.init());

    // 1. Concealment without history produces silence (zeros)
    std::vector<float> pcm;
    dec.conceal(pcm, 960);
    CHECK(pcm.size() == 960 * 2);
    for (float sample : pcm) {
        CHECK(sample == 0.0f);
    }

    // 2. Decode a known tone
    std::vector<int16_t> tone(960);
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = static_cast<int16_t>(10000.0 * std::sin(i * 0.1));
    std::vector<uint8_t> opus;
    CHECK(enc.encode(tone.data(), tone.size(), opus));
    pcm.clear();
    CHECK(dec.decode(opus.data(), opus.size(), pcm));
    CHECK(pcm.size() == 960 * 2);

    // 3. Concealment after valid frame extrapolates non-zero samples
    std::vector<float> concealed1;
    dec.conceal(concealed1, 960);
    CHECK(concealed1.size() == 960 * 2);
    bool nonZero = false;
    for (float sample : concealed1) {
        if (std::abs(sample) > 0.001f) nonZero = true;
    }
    CHECK(nonZero);

    // 4. Consecutive concealments decay amplitude
    std::vector<float> concealed2;
    dec.conceal(concealed2, 960);
    CHECK(concealed2.size() == 960 * 2);
    float max1 = 0.0f, max2 = 0.0f;
    for (float s : concealed1) max1 = std::max(max1, std::abs(s));
    for (float s : concealed2) max2 = std::max(max2, std::abs(s));
    CHECK(max2 < max1);

    // 5. Subsequent decode applies smooth ramp-in
    pcm.clear();
    CHECK(dec.decode(opus.data(), opus.size(), pcm));
    CHECK(pcm.size() == 960 * 2);
    CHECK(pcm[0] == 0.0f && pcm[1] == 0.0f);  // Ramped from 0 at sample 0

    // 6. Reset PLC clears history
    dec.resetPlc();
    pcm.clear();
    dec.conceal(pcm, 960);
    CHECK(pcm.size() == 960 * 2);
    for (float sample : pcm) {
        CHECK(sample == 0.0f);
    }
}

static void testAudioGapLossConcealment() {
    constexpr uint32_t kOpusFrameSamples = 960;
    auto detectLoss = [](uint32_t lastTs, uint32_t newTs) -> int {
        int32_t diff = static_cast<int32_t>(newTs - lastTs);
        if (diff <= 0) return -1; // duplicate or late out-of-order
        if (diff > static_cast<int32_t>(kOpusFrameSamples) && diff <= static_cast<int32_t>(kOpusFrameSamples * 6)) {
            return static_cast<int>((static_cast<uint32_t>(diff) + kOpusFrameSamples / 2) / kOpusFrameSamples - 1);
        }
        return 0; // normal step or huge stall
    };

    CHECK(detectLoss(1000, 1960) == 0);   // in-order 20ms step
    CHECK(detectLoss(1960, 1960) == -1);  // duplicate packet
    CHECK(detectLoss(2920, 1960) == -1);  // late packet from past
    CHECK(detectLoss(1000, 2920) == 1);   // 1 lost packet
    CHECK(detectLoss(1000, 3880) == 2);   // 2 lost packets
    CHECK(detectLoss(1000, 6760) == 5);   // 5 lost packets
    CHECK(detectLoss(1000, 100000) == 0); // huge stall: resync without concealing 100 frames
}

// Native mouse and keyboard reports: header, counts and fields.
static void testMouseKeyboardReports() {
    using namespace xc::stream;
    auto m = mouseReport(7, 1.0, {MouseFrame{-3, 5, 0, 1, 1}});
    CHECK(m.size() == 14 + 1 + 18);
    CHECK(m[0] == 32 && m[1] == 0 && m[2] == 7);  // type 32 (mouse), sequence 7
    CHECK(m[14] == 1);                              // one frame
    CHECK(m[15] == 0xFD && m[16] == 0xFF && m[17] == 0xFF && m[18] == 0xFF);  // dx -3, little-endian
    CHECK(m[19] == 5 && m[27] == 1 && m[31] == 1 && m[32] == 0);              // dy, wheel Y, buttons, relative
    auto k = keyboardReport(8, 1.0, {KeyFrame{0x41, true}, KeyFrame{0x41, false}});
    CHECK(k.size() == 14 + 1 + 6);
    CHECK(k[0] == 64 && k[14] == 2);
    CHECK(k[15] == 2 && k[16] == 1 && k[17] == 0x41);
    CHECK(k[18] == 2 && k[19] == 0 && k[20] == 0x41);
}

// USB keyboard: HID usages and modifier bits to Windows virtual keys.
static void testHidToVk() {
    using xc::input::hidToVk;
    CHECK(hidToVk(0x04) == 'A' && hidToVk(0x1D) == 'Z');
    CHECK(hidToVk(0x1E) == '1' && hidToVk(0x26) == '9' && hidToVk(0x27) == '0');
    CHECK(hidToVk(0x28) == 0x0D && hidToVk(0x29) == 0x1B && hidToVk(0x2C) == 0x20);
    CHECK(hidToVk(0x3A) == 0x70 && hidToVk(0x45) == 0x7B);  // F1, F12
    CHECK(hidToVk(0x4F) == 0x27 && hidToVk(0x52) == 0x26);  // right, up
    CHECK(hidToVk(0x59) == 0x61 && hidToVk(0x62) == 0x60);  // keypad 1, 0
    CHECK(hidToVk(0x00) == 0 && hidToVk(0xE0) == 0);
    CHECK(xc::input::modifierVk(0) == 0x11 && xc::input::modifierVk(1) == 0x10 && xc::input::modifierVk(2) == 0x12);
    CHECK(xc::input::modifierVk(5) == 0x10 && xc::input::modifierVk(7) == 0x5C);
}

// The app's screens by keyboard: keys held -> the controller's buttons.
static void testKeyboardNav() {
    xc::input::ControllerState pad;
    xc::input::keyboardToPad({0x0D, 'W', 0x22}, pad);  // enter, w, page down
    CHECK(pad.btnA && pad.dpadUp && !pad.dpadDown && pad.rightStickY == 1.0f);
    xc::input::ControllerState p2;
    p2.btnB = true;  // the controller's own press stays
    xc::input::keyboardToPad({0x25, 0x1B, 0x09, 'Q', 'E', 'F', 'H', 'Z'}, p2);
    CHECK(p2.dpadLeft && p2.btnB && p2.btnOptions && p2.btnL1 && p2.btnR1 && p2.btnY && p2.btnX && p2.triggerL2 == 1.0f);
    xc::input::ControllerState p3;
    xc::input::keyboardToPad({}, p3);
    CHECK(!p3.btnA && !p3.dpadUp && p3.rightStickY == 0.0f);
}

// The keyboard and mouse as a controller of their own.
static void testKeyboardAsGamepad() {
    xc::input::KbmInput in;
    in.keyboard = true;
    in.held = {0x20, 'D', 'W'};  // space, d, w (sorted)
    in.buttons = 1;              // left button
    in.dx = 24;                  // a fast move right
    xc::input::MouseStick stick;
    xc::input::ControllerState out;
    xc::input::keyboardAsGamepad(in, stick, out);
    CHECK(out.connected && out.btnA && out.leftStickX == 1.0f && out.leftStickY == -1.0f);
    CHECK(out.triggerR2 == 1.0f && out.triggerL2 == 0.0f);
    CHECK(out.rightStickX > 0.4f && out.rightStickY == 0.0f);
    in = {};
    in.keyboard = true;
    for (int i = 0; i < 20; ++i) xc::input::keyboardAsGamepad(in, stick, out);  // the mouse still: back to the centre
    CHECK(out.rightStickX == 0.0f && !out.btnA);
    // A wheel notch: Y pressed for a few passes, then released.
    in = {};
    in.keyboard = true;
    in.wheel = 1;
    xc::input::keyboardAsGamepad(in, stick, out);
    CHECK(out.btnY);
    in.wheel = 0;
    bool released = false;
    for (int i = 0; i < 12; ++i) {
        xc::input::keyboardAsGamepad(in, stick, out);
        released |= !out.btnY;
    }
    CHECK(released && !out.btnY);
    xc::input::ControllerState pad, kb;
    pad.btnB = true;
    pad.leftStickX = 0.3f;
    kb.btnA = true;
    kb.leftStickX = -1.0f;
    kb.triggerR2 = 1.0f;
    xc::input::mergePads(pad, kb);
    CHECK(pad.btnA && pad.btnB && pad.leftStickX == -1.0f && pad.triggerR2 == 1.0f);
}

// The DualSense's touchpad as a mouse in MKB games: cursor motion, taps,
// two-finger scroll and click-to-drag after moving.
static void testTouchpadAsMouse() {
    using namespace xc::input;
    TouchpadMouse tm;
    ControllerState pad;
    KbmInput io;

    // 1. One finger down, then sliding right by 0.25 and down by 0.5 -> 240 x 270 counts.
    pad.touching = true;
    pad.touchX = 0.25f;
    pad.touchY = 0.25f;
    CHECK(!touchpadAsMouse(pad, 100, tm, io));
    CHECK(io.dx == 0 && io.dy == 0 && io.buttons == 0 && !io.moved);

    io = {};
    pad.touchX = 0.5f;
    pad.touchY = 0.75f;
    CHECK(!touchpadAsMouse(pad, 116, tm, io));
    CHECK(io.dx == 240 && io.dy == 270 && io.buttons == 0 && io.moved);

    // Clicking the touchpad after moving holds the left mouse button for dragging.
    io = {};
    pad.btnTouchpad = true;
    CHECK(touchpadAsMouse(pad, 124, tm, io));
    CHECK(io.buttons == 1 && io.moved);

    // Releasing the finger lets go of the button.
    io = {};
    pad.touching = false;
    pad.btnTouchpad = false;
    CHECK(!touchpadAsMouse(pad, 140, tm, io));
    CHECK(io.buttons == 0 && io.moved);

    // 2. A light one-finger tap clicks the left button for 50 ms.
    io = {};
    pad.touching = true;
    pad.touchX = 0.5f;
    pad.touchY = 0.5f;
    CHECK(!touchpadAsMouse(pad, 200, tm, io));
    io = {};
    pad.touching = false;
    CHECK(!touchpadAsMouse(pad, 260, tm, io));
    CHECK(io.buttons == 1 && io.moved);
    io = {};
    CHECK(!touchpadAsMouse(pad, 290, tm, io));
    CHECK(io.buttons == 1 && !io.moved);
    io = {};
    CHECK(!touchpadAsMouse(pad, 315, tm, io));
    CHECK(io.buttons == 0 && io.moved);

    // 3. A light two-finger tap clicks the right button.
    io = {};
    pad.touching = true;
    pad.touchSecond = true;
    CHECK(!touchpadAsMouse(pad, 400, tm, io));
    io = {};
    pad.touching = false;
    pad.touchSecond = false;
    CHECK(!touchpadAsMouse(pad, 450, tm, io));
    CHECK(io.buttons == 2 && io.moved);
    io = {};
    CHECK(!touchpadAsMouse(pad, 510, tm, io));
    CHECK(io.buttons == 0 && io.moved);

    // 4. Two-finger vertical slide scrolls the wheel without moving the cursor.
    io = {};
    pad.touching = true;
    pad.touchSecond = true;
    pad.touchX = 0.5f;
    pad.touchY = 0.6f;
    CHECK(!touchpadAsMouse(pad, 600, tm, io));
    io = {};
    pad.touchY = 0.35f;  // up by 0.25 -> +2 wheel notches
    CHECK(!touchpadAsMouse(pad, 620, tm, io));
    CHECK(io.dx == 0 && io.dy == 0 && io.wheel == 2 && io.moved);
    pad.touching = false;
    pad.touchSecond = false;
    io = {};
    touchpadAsMouse(pad, 640, tm, io);

    // 5. A plain physical touchpad click without moving first stays View (not a mouse click).
    io = {};
    pad.touching = true;
    pad.btnTouchpad = true;
    CHECK(!touchpadAsMouse(pad, 700, tm, io));
    CHECK(io.buttons == 0 && !io.moved);
    pad.touching = false;
    pad.btnTouchpad = false;
    io = {};
    CHECK(!touchpadAsMouse(pad, 740, tm, io));
    CHECK(io.buttons == 0 && !io.moved);
}

// Keys chosen in Settings: one key moves off the input it was on.
static void testKeyMap() {
    using namespace xc::input;
    KeyMap map = defaultKeyMap();
    assignKey(map, KeyAction::A, 0, 'E');  // E was Y
    CHECK(map[static_cast<size_t>(KeyAction::A)][0] == 'E' && map[static_cast<size_t>(KeyAction::Y)][0] == 0);
    assignKey(map, KeyAction::Y, 1, kMouseBack);
    KbmInput in;
    in.keyboard = in.mouse = true;
    in.held = {'E'};
    in.buttons = 8;  // back
    MouseStick stick;
    ControllerState out;
    keyboardAsGamepad(in, stick, out, map);
    CHECK(out.btnA && out.btnY && !out.triggerR2);
    CHECK(keyName(0x20) == "Space" && keyName('W') == "W" && keyName(kMouseLeft) == "Mouse L" && keyName(0x7B) == "F12");
    CHECK(isReservedKey(0x7B) && isReservedKey('2') && !isReservedKey('5'));
    // Saved and read back, in the settings and a game's profile.
    xc::app::Settings st;
    st.keyMap = map;
    st.perGame["g"] = st.defaultProfile();
    st.perGame["g"].keyMap[0] = {'I', 0};
    std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/xc_keymap_test.json";
    CHECK(st.save(path));
    xc::app::Settings back;
    CHECK(back.load(path));
    CHECK(back.keyMap == map && back.perGame["g"].keyMap[0][0] == 'I');
    std::remove(path.c_str());
}

// The controller's buttons toward the game (input/pad_map.h).
static void testPadMap() {
    using namespace xc::input;
    auto at = [](const PadMap& m, PadFn f) { return m[static_cast<size_t>(f)]; };
    auto src = [](PadFn f) { return static_cast<uint8_t>(f); };
    const PadMap def = defaultPadMap();
    CHECK(padMapValid(def) && padMapMissing(def) == 0);
    // The default map passes everything through.
    ControllerState in;
    in.btnA = in.dpadLeft = in.btnTouchpad = true;
    in.triggerL2 = 0.3f;
    in.leftStickX = 0.7f;
    in.btnNexus = true;
    ControllerState same = applyPadMap(in, def);
    CHECK(same.btnA && same.dpadLeft && same.btnTouchpad && !same.btnB && same.triggerL2 == 0.3f &&
          same.leftStickX == 0.7f && same.btnNexus);
    // Assigning a source used elsewhere swaps the two: still complete.
    PadMap m = def;
    assignPadSource(m, PadFn::A, src(PadFn::B));  // A <- Circle, so B <- Cross
    CHECK(at(m, PadFn::A) == src(PadFn::B) && at(m, PadFn::B) == src(PadFn::A) && padMapValid(m));
    ControllerState cross;
    cross.btnA = true;
    ControllerState out = applyPadMap(cross, m);
    CHECK(!out.btnA && out.btnB);
    // A trigger fed by a button is fully pressed; a button fed by a trigger past halfway.
    assignPadSource(m, PadFn::RT, src(PadFn::RB));  // RT <- R1, RB <- R2
    ControllerState r1;
    r1.btnR1 = true;
    out = applyPadMap(r1, m);
    CHECK(out.triggerR2 == 1.0f && !out.btnR1);
    ControllerState r2;
    r2.triggerR2 = 0.4f;
    CHECK(!applyPadMap(r2, m).btnR1 && applyPadMap(r2, m).triggerR2 == 0.0f);
    r2.triggerR2 = 0.8f;
    CHECK(applyPadMap(r2, m).btnR1);
    // A trigger fed by the other trigger keeps how far it is pressed.
    PadMap t = def;
    assignPadSource(t, PadFn::LT, src(PadFn::RT));
    ControllerState lr;
    lr.triggerR2 = 0.25f;
    CHECK(applyPadMap(lr, t).triggerL2 == 0.25f && applyPadMap(lr, t).triggerR2 == 0.0f);
    // A cleared row: incomplete, named in the missing bits; a source fed twice is not valid.
    PadMap hole = def;
    assignPadSource(hole, PadFn::Y, kPadNone);
    CHECK(!padMapValid(hole) && padMapMissing(hole) == (1u << static_cast<int>(PadFn::Y)));
    CHECK(!applyPadMap(ControllerState{}, hole).btnY);
    PadMap twice = def;
    twice[static_cast<size_t>(PadFn::X)] = src(PadFn::A);
    CHECK(!padMapValid(twice) && padMapMissing(twice) == 0);
    // Filling the hole with a button used elsewhere moves the hole there.
    assignPadSource(hole, PadFn::Y, src(PadFn::X));
    CHECK(at(hole, PadFn::Y) == src(PadFn::X) && at(hole, PadFn::X) == kPadNone);
    // The buttons pressed, by source; Circle confirms swaps A / B back to the pad's.
    ControllerState held;
    held.btnB = true;
    held.triggerL2 = 0.9f;
    CHECK(padButtons(held) == ((1u << static_cast<int>(PadFn::B)) | (1u << static_cast<int>(PadFn::LT))));
    CHECK(physicalSource(src(PadFn::A), true) == src(PadFn::B) && physicalSource(src(PadFn::A), false) == src(PadFn::A) &&
          physicalSource(src(PadFn::Y), true) == src(PadFn::Y));
    CHECK(std::string(padFnName(PadFn::Right)) == "D-pad right");
    // JSON: round trip; anything else is refused and gives the default.
    PadMap back;
    CHECK(readPadMap(padMapJson(m), back) && back == m);
    auto reads = [&](const char* text) {
        auto v = json::parse(text);
        PadMap r = m;
        bool ok = v && readPadMap(*v, r);
        CHECK(r == (ok ? r : def));
        return ok;
    };
    CHECK(reads(R"({"version":1,"map":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]})"));
    CHECK(!reads(R"({"version":2,"map":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]})"));
    CHECK(!reads(R"({"version":1,"map":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14]})"));
    CHECK(!reads(R"({"version":1,"map":[0,0,2,3,4,5,6,7,8,9,10,11,12,13,14,15]})"));
    CHECK(!reads(R"({"version":1,"map":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,255]})"));
    CHECK(!reads(R"({"version":1,"map":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,"x"]})"));
    CHECK(!reads(R"([0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15])"));
    // In the settings and a game's profile: saved and read back; a broken
    // one falls back to the default; an older file (none) keeps the general one.
    xc::app::Settings st;
    st.padMap = m;
    st.perGame["g"] = st.defaultProfile();
    CHECK(st.perGame["g"].padMap == m);
    st.perGame["g"].padMap = t;
    std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/xc_padmap_test.json";
    CHECK(st.save(path));
    xc::app::Settings loaded;
    CHECK(loaded.load(path) && loaded.padMap == m && loaded.perGame["g"].padMap == t);
    xc::platform::writeFileAtomic(path, R"({"padMap":{"version":1,"map":[1,1]},"perGame":{"h":{},"k":{"padMap":7}}})");
    xc::app::Settings broken;
    CHECK(broken.load(path) && broken.padMap == def && broken.perGame["h"].padMap == def && broken.perGame["k"].padMap == def);
    xc::platform::writeFileAtomic(path, R"({"padMap":)" + padMapJson(m).dump() + R"(,"perGame":{"h":{}}})");
    xc::app::Settings older;
    CHECK(older.load(path) && older.perGame["h"].padMap == m);
    std::remove(path.c_str());
}

// The xCloud login: a 5xx is tried again, a refusal is not.
static void testLoginRetry() {
    using namespace xc;
    int calls = 0;
    net::setTestTransport([&](const net::Request&) {
        net::Response r;
        if (++calls < 3) {
            r.status = 500;
            return r;
        }
        r.status = 200;
        r.body = R"({"gsToken":"t","market":"BR","durationInSeconds":3600,"offeringSettings":{"regions":[{"name":"WestUS2","baseUri":"https://w","isDefault":true}]}})";
        return r;
    });
    xcloud::GssvClient gssv;
    std::string err;
    CHECK(gssv.login(auth::XblToken{}, err) && calls == 3);  // (3 s of waits)
    calls = 0;
    net::setTestTransport([&](const net::Request&) {
        ++calls;
        net::Response r;
        r.status = 403;
        return r;
    });
    CHECK(!gssv.login(auth::XblToken{}, err) && calls == 1 && gssv.loginRefused());
    net::setTestTransport({});
}

// Gyro aiming: still is nothing, a turn left aims left (past the games'
// dead zone), more intensity aims further, the push eases back to nothing.
static void testGyro() {
    using namespace xc::input;
    GyroState s;
    float x = 1, y = 1;
    const float still[3] = {0.01f, -0.01f, 0.0f};
    gyroToStick(still, nullptr, 5, s, x, y);
    CHECK(x == 0 && y == 0);
    const float left[3] = {0.0f, 0.5f, 0.0f};  // yaw +: turning left
    for (int i = 0; i < 8; ++i) gyroToStick(left, nullptr, 5, s, x, y);
    CHECK(x < -0.3f && x > -0.6f && std::abs(y) < 1e-6f);
    GyroState s2;
    float x2 = 0, y2 = 0;
    for (int i = 0; i < 8; ++i) gyroToStick(left, nullptr, 10, s2, x2, y2);
    CHECK(x2 < x);
    const float up[3] = {0.3f, 0.0f, 0.0f};  // pitch +: the top tilted up
    GyroState s3;
    for (int i = 0; i < 8; ++i) gyroToStick(up, nullptr, 5, s3, x, y);
    CHECK(y < -0.12f && x == 0);
    const float tiny[3] = {0.0f, 0.03f, 0.0f};  // a slow turn: still past the dead zone
    GyroState s4;
    for (int i = 0; i < 8; ++i) gyroToStick(tiny, nullptr, 5, s4, x, y);
    CHECK(x <= -0.12f);
    for (int i = 0; i < 20; ++i) gyroToStick(still, nullptr, 5, s4, x, y);
    CHECK(x == 0);
    // Held tilted 45 degrees towards the player: a turn about the room's
    // vertical shows on the pad's yaw and roll; it aims as far as held flat.
    const float c = 0.7071f, tilted[3] = {0.0f, 0.5f * c, 0.5f * c}, gravity[3] = {0.0f, c, c};
    GyroState s5;
    float x5 = 0, y5 = 0;
    for (int i = 0; i < 60; ++i) gyroToStick(tilted, gravity, 5, s5, x5, y5);
    CHECK(x5 < -0.3f);
    GyroState flat;
    float xf = 0, yf = 0;
    const float level[3] = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < 60; ++i) gyroToStick(left, level, 5, flat, xf, yf);
    CHECK(std::abs(x5 - xf) < 0.01f);
}

// The desktop's stand-ins for the PS5 backends: nothing connected, the
// settings kept for their getters.
// A font that didn't load (no assets folder) draws and measures nothing
// instead of crashing in stb_truetype.
static void testMissingFont() {
    ui::Font font;
    CHECK(!font.load("/nonexistent/Inter-Regular.ttf") && !font.loaded());
    ui::Canvas c(64, 32);
    c.clear(ui::rgba(1, 2, 3));
    CHECK(font.measure("Hello", 24) == 0);
    CHECK(font.draw(c, "Hello", 5, 4, 24, ui::rgba(255, 255, 255)) == 5);
    CHECK(c.data()[5 * 64 + 10] == ui::rgba(1, 2, 3));
    CHECK(font.centeredY(10, 40, 24) == 10 && font.lineHeight(24) == 24);
    CHECK(font.fit("Hello", 24, 100) == "Hello");
    CHECK(font.wrap("Hello world", 24, 100, 2).size() == 1);
}

// Settings' rows with the capabilities: all of them, in the same order and
// groups as ever, on the PS5; the desktop's hide what can't work there.
static void testVisibleSettingsRows() {
    using ui::SettingsRowPos;
    auto ids = [](const std::vector<SettingsRowPos>& v) {
        std::vector<int> out;
        for (const auto& r : v) out.push_back(r.row);
        return out;
    };
    auto groups = [](const std::vector<SettingsRowPos>& v) {
        std::vector<bool> out;
        for (const auto& r : v) out.push_back(r.groupStart);
        return out;
    };
    const ui::UiCaps ps5;
    auto all = ui::visibleSettingsRows(ps5);
    CHECK((ids(all) == std::vector<int>{0, 8, 1, 9, 10, 2, 11, 3, 4, 5, 6, 15, 14, 12, 7}));
    CHECK((groups(all) == std::vector<bool>{false, false, true, false, false, false, true, true, false, false, false, false, false, false, true}));
    ui::UiCaps pc;
    pc.lightBar = pc.triggerRumble = pc.adaptiveTriggers = pc.gyro = pc.touchpad = false;
    pc.upscalers = pc.sharpen = pc.deband = pc.highRefresh = pc.console = pc.selfUpdate = false;
    pc.openUrl = true;
    auto desk = ui::visibleSettingsRows(pc);
    CHECK((ids(desk) == std::vector<int>{0, 8, 1, 10, 2, 11, 3, 5, 14, 12, 7}));
    CHECK((groups(desk) == std::vector<bool>{false, false, true, false, false, true, true, false, false, false, true}));
    // The desktop app's window: Display mode after the resolution, in the stream's group.
    ui::UiCaps win = pc;
    win.desktopWindow = true;
    auto w = ui::visibleSettingsRows(win);
    CHECK((ids(w) == std::vector<int>{0, 8, 1, 13, 10, 2, 11, 3, 5, 14, 12, 7}));
    CHECK((groups(w) == std::vector<bool>{false, false, true, false, false, false, true, true, false, false, false, true}));
    ui::UiCaps xbox = pc;  // an Xbox pad: impulse triggers, no light bar
    xbox.triggerRumble = true;
    auto x = ids(ui::visibleSettingsRows(xbox));
    CHECK(std::find(x.begin(), x.end(), 4) != x.end() && std::find(x.begin(), x.end(), 6) == x.end());
    // A game's settings.
    CHECK((ids(ui::visibleGameSettingsRows(ps5, true, false)) == std::vector<int>{0, 4, 8, 14, 1, 2, 3, 5, 6, 7, 12, 11, 9, 10}));
    CHECK((ids(ui::visibleGameSettingsRows(ps5, true, true)) == std::vector<int>{0, 4, 8, 14, 1, 2, 3, 5, 6, 7, 12, 11, 13, 9, 10}));
    CHECK((groups(ui::visibleGameSettingsRows(ps5, true, true)) ==
           std::vector<bool>{false, true, false, false, true, false, false, true, false, false, false, false, false, false, true}));
    CHECK((ids(ui::visibleGameSettingsRows(ps5, true)) == std::vector<int>{0, 4, 8, 14, 1, 2, 3, 5, 6, 7, 12, 11, 9, 10}));
    CHECK((groups(ui::visibleGameSettingsRows(ps5, true)) ==
           std::vector<bool>{false, true, false, false, true, false, false, true, false, false, false, false, false, true}));
    CHECK((ids(ui::visibleGameSettingsRows(ps5, false)) == std::vector<int>{0, 4, 8, 14, 1, 2, 3, 5, 6, 7, 12, 11, 9}));
    ui::UiCaps noTouch = ps5;
    noTouch.touchpad = false;
    CHECK((ids(ui::visibleGameSettingsRows(noTouch, true, true)) == std::vector<int>{0, 4, 8, 14, 1, 2, 3, 5, 6, 7, 12, 11, 9, 10}));
    auto game = ui::visibleGameSettingsRows(pc, false);
    CHECK((ids(game) == std::vector<int>{0, 4, 14, 5, 7, 11, 9}));
    CHECK((groups(game) == std::vector<bool>{false, true, false, true, false, false, false}));
}

// The controller's glyphs by pad kind (ui/pad_glyphs.h).
static void testPadGlyphs() {
    using input::PadKind;
    using ui::GlyphStyle;
    using ui::PadButton;
    CHECK(ui::glyphStyleFor(PadKind::DualSense) == GlyphStyle::PlayStation);
    CHECK(ui::glyphStyleFor(PadKind::DualShock4) == GlyphStyle::PlayStation);
    CHECK(ui::glyphStyleFor(PadKind::None) == GlyphStyle::PlayStation);
    CHECK(ui::glyphStyleFor(PadKind::Xbox) == GlyphStyle::Xbox);
    CHECK(ui::glyphStyleFor(PadKind::Switch) == GlyphStyle::Xbox);
    CHECK(ui::glyphStyleFor(PadKind::Generic) == GlyphStyle::Xbox);
    // The PS5's pads, whatever the desktop shows.
    CHECK(ui::glyphStyleFor(input::anyPadCaps().kind) == GlyphStyle::PlayStation);  // null backend: no pad
    const PadButton all[] = {PadButton::Cross, PadButton::Circle, PadButton::Square, PadButton::Triangle,
                             PadButton::Options, PadButton::Touchpad, PadButton::L1, PadButton::R1,
                             PadButton::L2, PadButton::R2, PadButton::L3, PadButton::R3};
    std::string ps, xb;
    for (PadButton b : all) {
        ps += std::string(ui::buttonName(GlyphStyle::PlayStation, b)) + ",";
        xb += std::string(ui::buttonName(GlyphStyle::Xbox, b)) + ",";
    }
    CHECK(ps == "Cross,Circle,Square,Triangle,Options,Touchpad,L1,R1,L2,R2,L3,R3,");
    CHECK(xb == "A,B,X,Y,Menu,View,LB,RB,LT,RT,LS,RS,");
    // Xbox letters: A green, B red, X blue, Y yellow; no colour off the face.
    ui::Color a = ui::faceColor(GlyphStyle::Xbox, PadButton::Cross), b = ui::faceColor(GlyphStyle::Xbox, PadButton::Circle);
    CHECK(a != b && a != 0 && b != 0);
    CHECK(ui::faceColor(GlyphStyle::Xbox, PadButton::L1) == 0);
    CHECK(ui::faceColor(GlyphStyle::PlayStation, PadButton::Cross) == ui::rgba(124, 178, 232));
    CHECK(ui::faceColor(GlyphStyle::Xbox, PadButton::Cross) == ui::rgba(84, 178, 58));
    CHECK(ui::faceColor(GlyphStyle::Xbox, PadButton::Triangle) == ui::rgba(236, 184, 30));
}

// The desktop app's in-app notices (platform/toasts.h).
static void testToasts() {
    using platform::ToastQueue;
    ToastQueue q;
    uint64_t v0 = q.version(1000);
    CHECK(q.visible(1000).empty() && q.version(1000) == v0);
    q.push("one", 1000);
    q.push("", 1000);  // nothing
    CHECK(q.visible(1000) == std::vector<std::string>{"one"});
    uint64_t v1 = q.version(1000);
    CHECK(v1 != v0 && q.version(1001) == v1);  // nothing changed: no redraw
    q.push("two", 2000);
    q.push("three", 2500);
    q.push("four", 3000);  // waits: three on screen at most
    CHECK((q.visible(3000) == std::vector<std::string>{"one", "two", "three"}));
    // "one" has had its time: "four" takes its place, and the version moves.
    uint64_t v2 = q.version(5999);
    CHECK(q.version(6000) != v2);
    CHECK((q.visible(6000) == std::vector<std::string>{"two", "three", "four"}));
    // The same text again restarts it at the end instead of stacking.
    q.push("two", 6500);
    CHECK((q.visible(6500) == std::vector<std::string>{"three", "four", "two"}));
    CHECK((q.visible(7499) == std::vector<std::string>{"three", "four", "two"}));
    CHECK((q.visible(7500) == std::vector<std::string>{"four", "two"}));  // three's time (from 2500) is up
    CHECK((q.visible(11000) == std::vector<std::string>{"two"}));        // four's (from 6000)
    CHECK(q.visible(11500).empty());
    // Bounded: the oldest dropped beyond kMaxQueued.
    for (size_t i = 0; i < ToastQueue::kMaxQueued + 5; ++i) q.push("n" + std::to_string(i), 20000);
    auto shown = q.visible(20000);
    CHECK(shown.size() == ToastQueue::kMaxShown && shown.front() == "n5");
    q.clear();
    CHECK(q.visible(20000).empty());
    // Off by default: the tools keep printing notifications.
    CHECK(!platform::toastsEnabled());
}

static void testNullBackends() {
    CHECK(input::init());
    input::ControllerState pad;
    pad.btnA = true;
    CHECK(!input::poll(pad) && !pad.connected && !pad.btnA);
    CHECK(!input::pollPad(2, pad) && !input::padConnected(0) && input::padUserName(0).empty());
    CHECK(input::padUserId(0) == -1 && input::padActiveAt(0) == 0);
    input::setCircleConfirms(true);
    CHECK(input::circleConfirms());
    input::setCircleConfirms(false);
    CHECK(!input::circleConfirms());
    CHECK(!input::kbmInit(-1));
    input::KbmInput kbm = input::kbmPoll();
    CHECK(!kbm.keyboard && !kbm.mouse && kbm.keys.empty() && !kbm.moved);
    media::HwDecoder hw;
    CHECK(!hw.init(1920, 1088) && !hw.ready());
    display::DisplayCaps caps = display::caps();
    CHECK(!caps.upscalers && !caps.deband && !caps.sharpen && !caps.highRefreshSwitch && !caps.presentTiming);
    CHECK(!display::canSwitchRefresh() && !display::setHighRefresh(true) && !display::bringUpTest(1));
}

// --- M1 input / audio backends (pure parts, null backends) ----------------

// The audio queue: bounded at ~250 ms (cut back to 140 ms, oldest first),
// silence past its end, the volume, and what the periodic log counts.
static void testAudioQueue() {
    media::AudioQueue q;
    std::vector<float> in(48 * 2 * 10, 0.25f);  // 10 ms
    q.push(in.data(), 480);
    CHECK(q.queuedFrames() == 0);  // closed: ignored
    q.open();
    for (int i = 0; i < 24; ++i) q.push(in.data(), 480);  // 240 ms: under the bound
    CHECK(q.queuedFrames() == 11520 && q.stats().trimmed == 0);
    q.push(in.data(), 480);  // 250 ms
    q.push(in.data(), 480);  // 260 ms: past 250, back to 140
    CHECK(q.queuedFrames() == media::AudioQueue::kTrimTo && q.stats().trimmed == 12480 - 6720);
    // The oldest go first: mark the newest frame and find it last.
    float mark[2] = {0.5f, -0.5f};
    q.push(mark, 1);
    std::vector<float> out(6721 * 2);
    q.pull(out.data(), 6721);
    CHECK(out[6720 * 2] == 0.5f && out[6720 * 2 + 1] == -0.5f && out[100 * 2] == 0.25f);
    CHECK(q.queuedFrames() == 0);
    q.pull(out.data(), 256);  // empty: silence, counted
    CHECK(out[0] == 0.0f && out[511] == 0.0f);
    media::AudioQueue::Stats st = q.stats();
    CHECK(st.pulls == 2 && st.empty == 1 && st.frames == 6721 + 256 && st.depthMax == 6721);
    // Volume: clamped to 0..2, applied and clipped at full scale.
    q.setVolume(5.0f);
    CHECK(q.volume() == 2.0f);
    float loud[4] = {0.75f, -0.25f, 0.1f, -0.9f};
    q.push(loud, 2);
    q.pull(out.data(), 2);
    CHECK(out[0] == 1.0f && out[1] == -0.5f && std::abs(out[2] - 0.2f) < 1e-6f && out[3] == -1.0f);
    q.setVolume(-1.0f);
    CHECK(q.volume() == 0.0f);
    q.setVolume(1.0f);
    // ~10 s of sound pulled (480000 frames): logged, and the counts start
    // again. 6979 frames so far, so the 1848th grain of 256 crosses it.
    for (int i = 0; i < 1875; ++i) q.pull(out.data(), 256);
    CHECK(q.stats().pulls == 27 && q.stats().frames == 27 * 256 && q.stats().trimmed == 0);
    q.close();
    q.push(loud, 2);
    CHECK(q.queuedFrames() == 0);
}

// The microphone's pipeline: gain (clipped), mute, the meter, 20 ms frames.
static void testMicPipeline() {
    media::MicPipeline mic;
    std::atomic<float> level{0.0f};
    std::vector<std::vector<int16_t>> frames;
    media::AudioIn::AudioCallback cb = [&](const int16_t* pcm, size_t n) { frames.emplace_back(pcm, pcm + n); };
    std::vector<int16_t> grain(media::MicPipeline::kGrain, 1000);
    for (int i = 0; i < 3; ++i) mic.process(grain.data(), grain.size(), false, 1.0f, level, cb);
    CHECK(frames.empty() && level.load() > 0.3f);  // 768 samples: not a frame yet
    mic.process(grain.data(), grain.size(), false, 1.0f, level, cb);
    CHECK(frames.size() == 1 && frames[0].size() == media::AudioIn::kFrameSize && frames[0][959] == 1000);
    std::vector<int16_t> loud(media::MicPipeline::kGrain, 30000);
    mic.process(loud.data(), loud.size(), false, 1.5f, level, cb);
    CHECK(loud[0] == 32767 && level.load() == 1.0f);  // clipped at full scale
    std::vector<int16_t> half(media::MicPipeline::kGrain, 1000);
    mic.process(half.data(), half.size(), false, 0.5f, level, cb);
    CHECK(half[0] == 500);
    float before = level.load();
    mic.process(grain.data(), grain.size(), true, 1.0f, level, cb);  // muted: silence, the meter falls back
    CHECK(grain[0] == 0 && level.load() < before && level.load() > 0.9f * before);
    CHECK(frames.size() == 1);  // three grains since the first frame: 64 + 768 samples left
    mic.process(grain.data(), grain.size(), true, 1.0f, level, cb);
    CHECK(frames.size() == 2 && frames[1][959] == 0);
}

// The DualSense's trigger effects through SDL: the bytes for off, feedback
// (resistance) and vibration, and where each trigger's block sits.
static void testDs5TriggerEffects() {
    using namespace xc::input;
    uint8_t b[kDs5TriggerBlock];
    ds5TriggerBlock(TriggerCommand{}, b);  // mode 0
    const uint8_t off[11] = {0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(std::equal(b, b + 11, off));
    TriggerCommand weight = triggerCommand(0, 2, 1, 2, false, 0);  // medium resistance: from 3, strength 4
    CHECK(weight.mode == 1 && weight.data[0] == 3 && weight.data[1] == 4);
    ds5TriggerBlock(weight, b);
    const uint8_t feedback[11] = {0x21, 0xF8, 0x03, 0x00, 0xB6, 0x6D, 0x1B, 0, 0, 0, 0};
    CHECK(std::equal(b, b + 11, feedback));
    TriggerCommand vib = triggerCommand(38, 2, 1, 2, false, 0);  // amplitude 4 at 60 Hz, from 0
    CHECK(vib.mode == 3 && vib.data[1] == 4 && vib.data[2] == 60);
    ds5TriggerBlock(vib, b);
    const uint8_t vibration[11] = {0x26, 0xFF, 0x03, 0xDB, 0xB6, 0x6D, 0x1B, 0, 0, 60, 0};
    CHECK(std::equal(b, b + 11, vibration));
    TriggerCommand strongest;  // a pulse at its hardest: strength 8 -> 7 in every zone
    strongest.mode = 1;
    strongest.data[1] = 8;
    ds5TriggerBlock(strongest, b);
    const uint8_t full[11] = {0x21, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 0, 0};
    CHECK(std::equal(b, b + 11, full));
    TriggerCommand silent;  // vibration with no amplitude: off
    silent.mode = 3;
    silent.data[2] = 60;
    ds5TriggerBlock(silent, b);
    CHECK(std::equal(b, b + 11, off));
    uint8_t packet[kDs5EffectsSize];
    ds5TriggerEffects(vib, weight, packet);  // L2 vibrating, R2 weighted
    CHECK(packet[0] == 0x0C && packet[1] == 0 && packet[2] == 0 && packet[9] == 0);
    CHECK(std::equal(packet + kDs5RightTrigger, packet + kDs5RightTrigger + 11, feedback));
    CHECK(std::equal(packet + kDs5LeftTrigger, packet + kDs5LeftTrigger + 11, vibration));
    CHECK(std::all_of(packet + 32, packet + kDs5EffectsSize, [](uint8_t v) { return v == 0; }));  // no light bar
}

// SDL's mouse button mask -> the wire's bits (right and middle swap places).
static void testSdlMouseButtons() {
    using xc::input::sdlMouseButtons;
    CHECK(sdlMouseButtons(0) == 0);
    CHECK(sdlMouseButtons(1) == 1);    // left
    CHECK(sdlMouseButtons(2) == 4);    // middle
    CHECK(sdlMouseButtons(4) == 2);    // right
    CHECK(sdlMouseButtons(8) == 8);    // X1: back
    CHECK(sdlMouseButtons(16) == 16);  // X2: forward
    CHECK(sdlMouseButtons(1 | 4) == 3 && sdlMouseButtons(31) == 31 && sdlMouseButtons(0xFFFFFFE0u) == 0);
    // A controller's touchpad as a pointer device (Linux) isn't the mouse.
    using xc::input::isPadPointerName;
    CHECK(isPadPointerName("Sony Interactive Entertainment DualSense Wireless Controller Touchpad"));
    CHECK(isPadPointerName("DualSense Wireless Controller Touchpad"));  // Bluetooth
    CHECK(isPadPointerName("Wireless Controller Touchpad"));            // DS4, Bluetooth
    CHECK(isPadPointerName("Sony Computer Entertainment DUALSHOCK 4 Wireless Controller Touchpad"));
    CHECK(isPadPointerName("Generic Controller Touchpad"));
    CHECK(!isPadPointerName("ELAN1200:00 04F3:30F7 Touchpad") && !isPadPointerName("SynPS/2 Synaptics TouchPad"));
    CHECK(!isPadPointerName("Logitech G502 HERO Gaming Mouse") && !isPadPointerName("Virtual core pointer (seat0)"));
    CHECK(!isPadPointerName("") && !isPadPointerName(nullptr));
    // One pad's evdev and hidraw nodes lead to its HID device.
    using xc::input::padSysfsLink;
    CHECK(padSysfsLink("/dev/input/event12") == "/sys/class/input/event12/device/device");
    CHECK(padSysfsLink("/dev/hidraw1") == "/sys/class/hidraw/hidraw1/device");
    CHECK(padSysfsLink("/dev/input/js0").empty() && padSysfsLink("/dev/hidraw").empty() &&
          padSysfsLink("/dev/hidraw1/../x").empty() && padSysfsLink(nullptr).empty() && padSysfsLink("").empty());
    using xc::input::padLacksHidapi;
    CHECK(padLacksHidapi(1, false, "/dev/input/event12"));   // a DualSense on evdev
    CHECK(padLacksHidapi(2, false, nullptr));                // a DS4, path unknown
    CHECK(!padLacksHidapi(1, true, "/dev/input/event12"));   // has its light bar
    CHECK(!padLacksHidapi(1, false, "/dev/hidraw1"));        // HIDAPI already
    CHECK(!padLacksHidapi(3, false, "/dev/input/event5"));   // an Xbox pad: nothing to look for
}

// SDL's scancodes are HID usages: the same keys as the PS5's keyboard.
static void testScancodeToVk() {
    using xc::input::scancodeToVk;
    for (int sc = 0; sc < 224; ++sc) CHECK(scancodeToVk(sc) == xc::input::hidToVk(static_cast<uint16_t>(sc)));
    CHECK(scancodeToVk(4) == 'A' && scancodeToVk(29) == 'Z' && scancodeToVk(30) == '1' && scancodeToVk(39) == '0');
    CHECK(scancodeToVk(40) == 0x0D && scancodeToVk(41) == 0x1B && scancodeToVk(44) == 0x20 && scancodeToVk(58) == 0x70);
    CHECK(scancodeToVk(79) == 0x27 && scancodeToVk(82) == 0x26);  // right, up
    const uint8_t mods[8] = {0x11, 0x10, 0x12, 0x5B, 0x11, 0x10, 0x12, 0x5C};  // LCtrl LShift LAlt LGui, then right
    for (int i = 0; i < 8; ++i) CHECK(scancodeToVk(224 + i) == mods[i]);
    CHECK(scancodeToVk(0) == 0 && scancodeToVk(232) == 0 && scancodeToVk(257) == 0 && scancodeToVk(-1) == 0);
}

// SDL's sensors and axes in the units the shared code takes.
static void testSdlSensorUnits() {
    using namespace xc::input;
    const float ms2[3] = {0.0f, 9.80665f, -4.903325f};
    float g[3];
    sdlAccelToG(ms2, g);
    CHECK(g[0] == 0.0f && std::abs(g[1] - 1.0f) < 1e-6f && std::abs(g[2] + 0.5f) < 1e-6f);
    // Held flat and still at 1 g: gyro aiming's "up" stays put, no push.
    GyroState s;
    const float still[3] = {0, 0, 0};
    float x = 1, y = 1;
    for (int i = 0; i < 10; ++i) gyroToStick(still, g, 5, s, x, y);
    CHECK(x == 0 && y == 0);
    CHECK(sdlStickAxis(32767) == 1.0f && sdlStickAxis(-32768) == -1.0f && sdlStickAxis(0) == 0.0f);
    CHECK(std::abs(sdlStickAxis(16384) - 0.5f) < 0.001f);
    CHECK(sdlTriggerAxis(32767) == 1.0f && sdlTriggerAxis(0) == 0.0f && sdlTriggerAxis(-5) == 0.0f);
    uint8_t c[3];
    playerLightColour(0, c);
    CHECK(c[0] == 0 && c[1] == 112 && c[2] == 220);
    playerLightColour(3, c);
    CHECK(c[0] == 225 && c[1] == 90 && c[2] == 175);
    CHECK(triggerMotorLevel(0) == 0 && triggerMotorLevel(128) == 128 && triggerMotorLevel(128, 1) == 64);
    CHECK(triggerMotorLevel(200, 3) == 255 && triggerMotorLevel(1, 4) == 255 && triggerMotorLevel(200, 0) == 0);
}

// The null backends stay honest about M1's additions: no pad and no
// capabilities, capturing the mouse does nothing, sound goes nowhere.
static void testNullBackendsM1() {
    input::PadCaps caps = input::padCaps(0), any = input::anyPadCaps();
    CHECK(caps.kind == input::PadKind::None && !caps.rumble && !caps.triggerRumble && !caps.adaptiveTriggers);
    CHECK(any.kind == input::PadKind::None && !any.lightBar && !any.gyro && !any.touchpad);
    CHECK(input::padDeviceName(0).empty() && input::padCaps(9).kind == input::PadKind::None);
    input::setRumble(255, 255, 100, 0);  // nowhere to go, and nothing breaks
    input::setLightBar(1, 2, 3, 0);
    input::ControllerState pad;
    CHECK(!input::poll(pad) && !input::padConnected(0));
    input::kbmSetCaptured(true);
    input::KbmInput kbm = input::kbmPoll();
    CHECK(!kbm.keyboard && !kbm.mouse && !kbm.moved && kbm.dx == 0);
    input::kbmSetCaptured(false);
    CHECK(media::audioStart() && media::audioStart());
    std::vector<float> sound(48 * 2 * 260, 0.1f);  // 260 ms, never played: kept to the bound
    media::audioPush(sound.data(), 48 * 260);
    CHECK(media::audioQueue().queuedFrames() == media::AudioQueue::kTrimTo);
    media::audioStop();
    CHECK(media::audioQueue().queuedFrames() == 0);
    media::audioPush(sound.data(), 480);
    CHECK(media::audioQueue().queuedFrames() == 0);  // stopped: ignored
    media::AudioIn mic;
    bool called = false;
    CHECK(mic.open([&](const int16_t*, size_t) { called = true; }) && mic.isRecording());
    CHECK(!mic.isHardwareMuted() && mic.level() == 0.0f);
    mic.close();
    CHECK(!mic.isRecording() && !called);
}

int main() {
    testNullBackends();
    testPadGlyphs();
    testToasts();
    testVisibleSettingsRows();
    testMissingFont();
    testNullBackendsM1();
    testAudioQueue();
    testMicPipeline();
    testDs5TriggerEffects();
    testSdlMouseButtons();
    testScancodeToVk();
    testSdlSensorUnits();
    testLoginRetry();
    testGyro();
    testReport();
    testCrashGuard();
    testBuiltinCa();
    testLoginCountry();
    testPrivacyAndRobustness();
    testStoreLeftovers();
    testHostPacer();
    testSubscriptionPlan();
    testMicrosoftHost();
    testAudioEncoder();
    testH264Resync();
    testAudioDecoderEmptyPacket();
    testAudioDecoderPlc();
    testAudioGapLossConcealment();
    testAutoDeband();
    testControllerTuning();
    testUpdater();
    testAccentColor();
    testVersions();
    testStreamMenu();
    testSettingsMigration();
    testMicGain();
    testPerGameSettings();
    testPlaytimeLimit();
    testStrings();
    testRegions();
    testPrices();
    testJson();
    testUrl();
    testInputPacket();
    testMouseKeyboardReports();
    testHidToVk();
    testKeyboardNav();
    testKeyboardAsGamepad();
    testTouchpadAsMouse();
    testKeyMap();
    testPadMap();
    testTeredo();
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
