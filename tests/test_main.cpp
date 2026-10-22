// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Offline unit tests for the portable core (no network).
#include "app/auto_deband.h"
#include "app/library.h"
#include "app/settings.h"
#include "app/update_check.h"
#include "app/updater.h"
#include "app/web_server.h"
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
#include "input/pad_usb.h"
#include "net/http.h"
#include "platform/platform.h"
#include "platform/toasts.h"
#include "stream/input_packet.h"
#include "stream/stream_session.h"
#include "ui/app_ui.h"
#include "ui/accent_color.h"
#include "ui/pad_glyphs.h"
#include "ui/input_mode.h"
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
#include "auth/xbox_live.h"
#include "ui/image_cache.h"
#include "xcloud/catalog.h"
#include "xcloud/gssv.h"
#include "xcloud/titlehub.h"
#include "xcloud/prices.h"
#include "xcloud/purchase.h"
#include "xcloud/regions.h"
#include "xcloud/achievements.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <ctime>
#include <map>
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
// Settings' screen and settings.json share one mapping: every field the
// screen carries comes back as it was (Settings -> choice -> Settings), and
// the fields it doesn't carry are left alone.
static void testSettingsChoiceRoundTrip() {
    using namespace xc::input;
    xc::app::Settings s;
    s.language = "pt-BR";
    s.resolution = "720p";
    s.region = "Brazil South";
    s.loginCountry = "jp";
    s.deadzoneLeft = 7, s.deadzoneRight = 21;
    s.triggerStrength = 4, s.triggerHz = 2, s.triggerResistance = 3, s.triggerPulses = true, s.triggerProfile = 2;
    s.circleConfirms = true;
    s.lightBarMode = 1, s.lightBarColour = 0xFF112233;
    s.sharpness = 3, s.deband = 1, s.upscaler = 0;
    s.videoDecoder = 1;
    s.highRefresh = true;
    s.maxBitrateMbps = 15, s.gameVolume = 150;
    assignKey(s.keyMap, KeyAction::A, 0, 'E');
    assignPadSource(s.padMap, PadFn::A, static_cast<uint8_t>(PadFn::B));
    CHECK(padMapValid(s.padMap));
    s.gyroMode = 2, s.gyroLevel = 9;
    s.desktopFullscreen = false;
    s.touchpadMouse = true;
    s.queueReadyAlert = false;
    s.titlePlaytime = {{"game1", 3600}};
    // Not carried by the screen: must survive applyChoice untouched.
    s.keyboardPlayer = 3;
    s.streamStats = true;
    s.favorites = {"P1"};

    ui::SettingsChoice c = ui::choiceFromSettings(s);
    CHECK(c.language == static_cast<int>(ui::Language::PortugueseBR));
    CHECK(c.resolution == 1 && c.triggerProfile == 2 && c.lightBarColour == 0xFF112233u && !c.fullscreen);
    CHECK(c.videoDecoder == 1);
    CHECK(!c.queueReadyAlert && c.titlePlaytime.at("game1") == 3600);

    xc::app::Settings back;  // defaults, then what the screen carries
    back.keyboardPlayer = 3;
    back.streamStats = true;
    back.favorites = {"P1"};
    ui::applyChoice(c, back);
    CHECK(back.language == s.language);
    CHECK(back.resolution == s.resolution);
    CHECK(back.region == s.region && back.loginCountry == s.loginCountry);
    CHECK(back.deadzoneLeft == s.deadzoneLeft && back.deadzoneRight == s.deadzoneRight);
    CHECK(back.triggerStrength == s.triggerStrength && back.triggerHz == s.triggerHz &&
          back.triggerResistance == s.triggerResistance && back.triggerPulses == s.triggerPulses &&
          back.triggerProfile == s.triggerProfile);
    CHECK(back.circleConfirms == s.circleConfirms);
    CHECK(back.lightBarMode == s.lightBarMode && back.lightBarColour == s.lightBarColour);
    CHECK(back.sharpness == s.sharpness && back.deband == s.deband && back.upscaler == s.upscaler);
    CHECK(back.videoDecoder == s.videoDecoder);
    CHECK(back.highRefresh == s.highRefresh);
    CHECK(back.maxBitrateMbps == s.maxBitrateMbps && back.gameVolume == s.gameVolume);
    CHECK(back.queueReadyAlert == s.queueReadyAlert && back.titlePlaytime == s.titlePlaytime);
    CHECK(back.keyMap == s.keyMap && back.padMap == s.padMap);
    CHECK(back.gyroMode == s.gyroMode && back.gyroLevel == s.gyroLevel);
    CHECK(back.desktopFullscreen == s.desktopFullscreen && back.touchpadMouse == s.touchpadMouse);
    CHECK(back.keyboardPlayer == 3 && back.streamStats && back.favorites.size() == 1);
    CHECK(ui::formatPlaytime(0) == "< 1 min");
    CHECK(ui::formatPlaytime(45) == "< 1 min");
    CHECK(ui::formatPlaytime(60) == "1 min");
    CHECK(ui::formatPlaytime(2700) == "45 min");
    CHECK(ui::formatPlaytime(3600) == "1 h");
    CHECK(ui::formatPlaytime(3660) == "1 h 1 min");
    CHECK(ui::formatPlaytime(8100) == "2 h 15 min");
    // Every resolution, both ways ("1440p" from older versions reads as the top tier).
    for (const char* res : {"1080p", "720p", "best"}) {
        xc::app::Settings r;
        r.resolution = res;
        xc::app::Settings out;
        ui::applyChoice(ui::choiceFromSettings(r), out);
        CHECK(out.resolution == res);
    }
    xc::app::Settings old;
    old.resolution = "1440p";
    CHECK(ui::choiceFromSettings(old).resolution == 2);
    // An incomplete controller map from the screen never replaces a valid one.
    ui::SettingsChoice broken = c;
    broken.padMap[0] = kPadNone;
    xc::app::Settings kept = s;
    ui::applyChoice(broken, kept);
    CHECK(kept.padMap == s.padMap);
}

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
    s.videoDecoder = 0;
    s.deadzoneLeft = 12;
    s.deadzoneRight = 18;
    s.triggerStrength = 3;
    s.circleConfirms = false;

    // Default profile reflects global settings.
    auto def = s.defaultProfile();
    CHECK(def.sharpness == 1 && def.deband == 2 && def.upscaler == 0 && def.videoDecoder == 0);
    CHECK(def.deadzoneLeft == 12 && def.deadzoneRight == 18 && def.triggerStrength == 3);
    CHECK(!def.circleConfirms);
    CHECK(!def.highRefresh);  // off unless chosen
    CHECK(!def.touchpadMouse);

    // Unknown games return default profile with hasCustom = false.
    bool hasCustom = true;
    auto prof1 = s.profileForGame("GAME1", "", &hasCustom);
    CHECK(!hasCustom);
    CHECK(prof1.sharpness == 1 && prof1.deadzoneLeft == 12 && prof1.videoDecoder == 0);

    // Add per-game profile for GAME1.
    xc::app::GameProfile p1;
    p1.sharpness = 3;
    p1.deband = 0;
    p1.upscaler = 3;
    p1.videoDecoder = 1;  // override global HW with SW
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
    CHECK(prof1Custom.videoDecoder == 1);
    CHECK(prof1Custom.locale == "pt-BR");

    hasCustom = true;
    auto prof2 = s.profileForGame("GAME2", "", &hasCustom);
    CHECK(!hasCustom);
    CHECK(prof2.sharpness == 1 && !prof2.circleConfirms && !prof2.touchpadMouse && prof2.locale.empty() && prof2.videoDecoder == 0);

    // Title ID fallback when productId is empty.
    s.perGame["CONSOLE_SERVER_1"] = p1;
    auto consoleProf = s.profileForGame("", "CONSOLE_SERVER_1", &hasCustom);
    CHECK(hasCustom);
    CHECK(consoleProf.sharpness == 3 && consoleProf.circleConfirms && consoleProf.locale == "pt-BR" && consoleProf.videoDecoder == 1);

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
    CHECK(loadedP1.highRefresh && loadedP1.touchpadMouse && loadedP1.locale == "pt-BR" && loadedP1.videoDecoder == 1);
    CHECK(loadedP1.deadzoneLeft == 5 && loadedP1.deadzoneRight == 8 && loadedP1.triggerStrength == 1);

    // Remove game profile restores default.
    reloaded.perGame.erase("GAME1");
    auto afterErase = reloaded.profileForGame("GAME1", "", &hasCustom);
    CHECK(!hasCustom);
    CHECK(afterErase.sharpness == reloaded.sharpness && afterErase.locale.empty() && afterErase.videoDecoder == reloaded.videoDecoder);

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

static void testSplashInputBlocking() {
    ui::Fonts fonts;
    ui::ImageCache images([] {});
    ui::AppUi app(fonts, images);
    CHECK(app.screen() == ui::Screen::Splash);

    ui::NavInput in;
    in.accept = true;
    in.down = true;
    in.options = true;
    in.triangle = true;
    in.r1 = true;
    ui::UiEvent ev = app.handle(in);
    CHECK(ev.action == ui::Action::None);
    CHECK(app.screen() == ui::Screen::Splash);

    app.showSplash("Signing in...");
    CHECK(app.screen() == ui::Screen::Splash);
    ev = app.handle(in);
    CHECK(ev.action == ui::Action::None);

    app.showHome();
    CHECK(app.screen() == ui::Screen::Home);
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
    press([](xc::ui::NavInput& n) { n.down = true; });  // DualSense speaker volume
    CHECK(press([](xc::ui::NavInput& n) { n.left = true; }) == MenuAction::PadSpeakerVolume && menu.padSpeakerVolume() == 95);
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

    // Platform in report URL and immutable platform header in log
    app::report::Info infoPlat{"t", "what", "1.4.0", "13.00", "[Platform: PlayStation 5 (PPSA99810)]\nlog line 1\n", "", "PlayStation 5 (PPSA99810)"};
    std::string urlPlat = app::report::issueUrl(infoPlat);
    CHECK(urlPlat.find("&platform=PlayStation+5+(PPSA99810)") != std::string::npos);
    CHECK(urlPlat.find("%5BPlatform:+PlayStation+5+(PPSA99810)%5D") != std::string::npos);

    // Platform header automatically injected if missing from log
    app::report::Info infoNoHdr{"t", "what", "1.4.0", "13.00", "raw log line\n", "", "PlayStation 5 (PPSA99810)"};
    std::string urlNoHdr = app::report::issueUrl(infoNoHdr);
    CHECK(urlNoHdr.find("%5BPlatform:+PlayStation+5+(PPSA99810)%5D") != std::string::npos);

    // Truncation preserves [Platform: ...]
    std::string bigLog = "[Platform: PlayStation 5 (PPSA99810)]\n";
    for (int i = 0; i < 50; ++i) bigLog += "lots of long log lines to trigger shortening " + std::to_string(i) + "\n";
    app::report::Info infoTrunc{"t", "what", "1.4.0", "13.00", bigLog, "", "PlayStation 5 (PPSA99810)"};
    std::string urlTrunc = app::report::issueUrl(infoTrunc, 350);
    CHECK(urlTrunc.find("%5BPlatform:+PlayStation+5+(PPSA99810)%5D") != std::string::npos);

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
    bool isInvalidGrant = false;
    status = 429, answer = "";
    CHECK(!msa.refresh("r", tokens, err, {}, &networkError, &isInvalidGrant) && networkError && !isInvalidGrant);
    status = 400, answer = R"({"error":"invalid_grant"})";
    isInvalidGrant = false;
    CHECK(!msa.refresh("r", tokens, err, {}, &networkError, &isInvalidGrant) && !networkError && isInvalidGrant);
    status = 500, answer = "Internal Server Error";
    isInvalidGrant = false;
    CHECK(!msa.refresh("r", tokens, err, {}, &networkError, &isInvalidGrant) && !isInvalidGrant);

    // XSTS Redirect URL parsing test
    auth::XblToken userToken{"user_token", "uhs"};
    auth::XblToken xstsOut;
    std::string redir;
    status = 401;
    answer = R"({"Identity":"0","XErr":2148916238,"Message":"","Redirect":"https://start.ui.xboxlive.com/AddChild"})";
    CHECK(!auth::xstsToken(userToken, auth::rp::kGssv, xstsOut, err, &redir));
    CHECK(redir == "https://start.ui.xboxlive.com/AddChild");
    CHECK(err.find("Redirect: https://start.ui.xboxlive.com/AddChild") != std::string::npos);
    CHECK(err.find("child account") != std::string::npos);

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

// The sign-in kept up: renewed before it expires (10 minutes ahead), only
// the xCloud login when only it is due, the whole chain when the XSTS is
// (or a request was refused), the region kept, the rotated refresh token
// saved at once, a refused refresh keeping the stored account.
static void testTokenRefresh() {
    using namespace xc::auth;
    CHECK(refreshDue(0, 1000));                // never logged in
    CHECK(refreshDue(900, 1000));              // expired
    CHECK(refreshDue(1000 + 600, 1000));       // within the margin
    CHECK(!refreshDue(1000 + 601, 1000));
    CHECK(parseIsoUtc("1970-01-02T00:00:00Z") == 86400);
    CHECK(parseIsoUtc("2026-10-19T09:12:34.1234567Z") == 1792401154);
    CHECK(parseIsoUtc("2024-02-29T23:59:59Z") == 1709251199);
    CHECK(parseIsoUtc("") == 0 && parseIsoUtc("soon") == 0 && parseIsoUtc("2026-13-01T00:00:00Z") == 0);

    std::string dir = "/tmp/xcloud-auth-test";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    std::string store = dir + "/account.json";
    xc::platform::writeFileAtomic(store, R"({"version":1,"clientId":"000000004c20a908","refreshToken":"_rt-0"})");
    int64_t xstsLife = 16 * 3600, loginLife = 300;
    int serial = 0, logins = 0;
    bool refuseRefresh = false;
    std::map<std::string, int> hits;
    auto iso = [](int64_t t) {
        std::time_t tt = static_cast<std::time_t>(t);
        std::tm utc{};
        gmtime_r(&tt, &utc);
        char text[40];
        std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%S.0000000Z", &utc);
        return std::string(text);
    };
    net::setTestTransport([&](const net::Request& req) {
        net::Response r;
        r.status = 200;
        const std::string& u = req.url;
        if (u.find("login.live.com/oauth20_token.srf") != std::string::npos) {
            ++hits["msa"];
            if (refuseRefresh) {
                r.status = 400;
                r.body = R"({"error":"invalid_grant"})";
            } else {
                r.body = R"({"access_token":"at","expires_in":3600,"refresh_token":"_rt-)" + std::to_string(++serial) + R"("})";
            }
        } else if (u.find("user.auth.xboxlive.com") != std::string::npos) {
            ++hits["user"];
            r.body = R"({"Token":"ut","NotAfter":")" + iso(unixNow() + 14 * 86400) + R"(","DisplayClaims":{"xui":[{"uhs":"h"}]}})";
        } else if (u.find("xsts.auth.xboxlive.com") != std::string::npos) {
            ++hits["xsts"];
            r.body = R"({"Token":"xt)" + std::to_string(serial) + R"(","NotAfter":")" + iso(unixNow() + xstsLife) +
                     R"(","DisplayClaims":{"xui":[{"uhs":"h","gtg":"Tester","xid":"1"}]}})";
        } else if (u.find("gssv-play-prod.xboxlive.com/v2/login/user") != std::string::npos) {
            ++hits["login"];
            r.body = R"({"gsToken":"gs)" + std::to_string(++logins) + R"(","market":"US","durationInSeconds":)" +
                     std::to_string(loginLife) +
                     R"(,"offeringSettings":{"regions":[{"name":"EastUS","baseUri":"https://eus.example","isDefault":true},)"
                     R"({"name":"BrazilSouth","baseUri":"https://brs.example","isDefault":false}]}})";
        } else {
            ++hits["other"];  // gamerpic, plan, playtime: optional
            r.status = 404;
            r.body = "{}";
        }
        return r;
    });
    auto storedRefresh = [&] {
        std::string text;
        xc::platform::readFile(store, text);
        auto j = json::parse(text);
        return j ? (*j)["refreshToken"].str() : std::string();
    };
    std::string err;
    bool renewed = true;
    {
        AuthManager am(store);
        xcloud::GssvClient gssv;
        CHECK(!am.ensureFresh(&gssv, err, false, &renewed) && !renewed && hits.empty());  // not signed in: nothing sent
        CHECK(am.signIn(gssv, {}, err) && am.signedIn());
        CHECK(storedRefresh() == "_rt-1");
        // The xCloud login expires in 5 minutes: only it is renewed, in the region chosen.
        gssv.setRegion(gssv.session().regions[1]);
        std::string token = gssv.session().gsToken;
        loginLife = 14400;
        hits.clear();
        CHECK(am.ensureFresh(&gssv, err, false, &renewed) && renewed);
        CHECK(hits.size() == 1 && hits["login"] == 1);
        CHECK(gssv.session().gsToken != token && gssv.region().name == "BrazilSouth");
        // Nothing due: nothing sent.
        hits.clear();
        CHECK(am.ensureFresh(&gssv, err, false, &renewed) && !renewed && hits.empty());
        CHECK(am.ensureFresh(nullptr, err, false, &renewed) && !renewed && hits.empty());
        // A refused request: the whole chain, without the profile's extras;
        // the rotated refresh token saved at once.
        CHECK(am.ensureFresh(&gssv, err, true, &renewed) && renewed);
        CHECK(hits["msa"] == 1 && hits["user"] == 1 && hits["xsts"] == 2 && hits["login"] == 1 && hits["other"] == 0);
        CHECK(storedRefresh() == "_rt-2" && !am.profile().xblAuthorization.empty());
        CHECK(gssv.region().name == "BrazilSouth");
        // The XSTS about to expire: renewed without being asked.
        xstsLife = 120;
        CHECK(am.ensureFresh(&gssv, err, true, &renewed));  // now holds a 2-minute XSTS
        xstsLife = 16 * 3600;
        hits.clear();
        CHECK(am.ensureFresh(&gssv, err, false, &renewed) && renewed && hits["msa"] == 1 && hits["login"] == 1);
        hits.clear();
        CHECK(am.ensureFresh(&gssv, err, false, &renewed) && !renewed && hits.empty());
        // The refresh refused: false, and the stored account stays.
        refuseRefresh = true;
        std::string before = storedRefresh();
        CHECK(!am.ensureFresh(&gssv, err, true, &renewed) && !renewed);
        CHECK(err.find("token refresh failed") != std::string::npos);
        CHECK(storedRefresh() == before && am.hasStoredAccount());
        CHECK(err.find(before) == std::string::npos);  // no token in the message

        // signIn failure: 500 error preserves account.json; invalid_grant deletes it
        {
            AuthManager amTransient(store);
            net::setTestTransport([&](const net::Request&) {
                net::Response r;
                r.status = 500;
                r.body = "Internal Server Error";
                return r;
            });
            CHECK(!amTransient.signIn(gssv, {}, err));
            CHECK(storedRefresh() == before && amTransient.hasStoredAccount());

            net::setTestTransport([&](const net::Request&) {
                net::Response r;
                r.status = 400;
                r.body = R"({"error":"invalid_grant"})";
                return r;
            });
            CHECK(!amTransient.signIn(gssv, {}, err));
            CHECK(storedRefresh().empty() && !amTransient.hasStoredAccount());
        }
    }
    net::setTestTransport({});
    std::system(("rm -rf " + dir).c_str());
}

// Kept-alive connections: the end of a chunked body from its framing, and a
// connection kept only when the server keeps it and the body ended exactly.
static void testHttpKeepAlive() {
    using net::chunkedLength;
    using net::keepAliveAfter;
    std::string whole = "4\r\nWiki\r\n5;x=1\r\npedia\r\n0\r\n\r\n";
    CHECK(chunkedLength(whole) == static_cast<long long>(whole.size()));
    for (size_t n = 0; n < whole.size(); ++n) CHECK(chunkedLength(whole.substr(0, n)) == 0);  // more to come
    std::string trailers = "3\r\nabc\r\n0\r\nX-Trailer: 1\r\n\r\n";
    CHECK(chunkedLength(trailers) == static_cast<long long>(trailers.size()));
    CHECK(chunkedLength(trailers.substr(0, trailers.size() - 2)) == 0);
    // Data that happens to end like a last chunk is not the end.
    std::string tricky = "5\r\nab0\r\n\r\n";
    CHECK(chunkedLength(tricky) == 0);
    CHECK(chunkedLength(tricky + "0\r\n\r\n") == static_cast<long long>(tricky.size() + 5));
    // Bytes after the end: the length stops at the end, so the caller sees them.
    CHECK(chunkedLength(whole + "HTTP/1.1 200 OK") == static_cast<long long>(whole.size()));
    CHECK(chunkedLength("zz\r\nab\r\n0\r\n\r\n") == -1);
    CHECK(chunkedLength("2\r\nabXX0\r\n\r\n") == -1);
    CHECK(chunkedLength("ffffffffffffffff\r\nab\r\n") == -1);
    CHECK(chunkedLength(std::string(2000, '1')) == -1);  // a size line that never ends

    CHECK(keepAliveAfter(200, false, "", 10, 10));
    CHECK(keepAliveAfter(404, false, "keep-alive", 0, 0));
    CHECK(!keepAliveAfter(200, false, "close", 10, 10));
    CHECK(!keepAliveAfter(200, false, "Close", 10, 10));
    CHECK(!keepAliveAfter(200, false, "", 10, 12));   // bytes past the body
    CHECK(!keepAliveAfter(200, false, "", 10, 8));    // body cut short
    CHECK(!keepAliveAfter(200, false, "", -1, 10));   // ended by the server closing
    CHECK(!keepAliveAfter(0, false, "", 0, 0));       // no answer
    CHECK(!keepAliveAfter(200, true, "", 10, 10));    // HTTP/1.0 closes by default
    CHECK(keepAliveAfter(200, true, "Keep-Alive", 10, 10));
}

// The home rows come from the network on every load(): the cache's rows
// (kept for the instant start) are never added to them, and a cache that
// holds every row twice heals at the next save.
static void testLibraryRowsOnce() {
    std::string dir = "/tmp/xcloud-library-test";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    std::string cache = dir + "/library.json";
    net::setTestTransport([](const net::Request& req) {
        net::Response r;
        r.status = 200;
        if (req.url.find("/sigls/") != std::string::npos) {
            std::string id = req.url.substr(req.url.find("id=") + 3);
            id = id.substr(0, id.find('&'));
            r.body = R"([{"siglId":")" + id + R"(","title":"List )" + id + R"("},{"id":"P1"},{"id":"P2"}])";
        } else if (req.url.find("/v3/products") != std::string::npos) {
            r.body = R"({"Products":{"P1":{"XCloudTitleId":"T1","ProductTitle":"Game One"},)"
                     R"("P2":{"XCloudTitleId":"T2","ProductTitle":"Game Two"}}})";
        } else {
            r.status = 503;  // the account's titles: not needed here
        }
        return r;
    });
    auto titlesOf = [](const std::vector<ui::GameRow>& rows) {
        std::vector<std::string> t;
        for (const auto& r : rows) t.push_back(r.title);
        return t;
    };
    auto unique = [](std::vector<std::string> t) {
        std::sort(t.begin(), t.end());
        return std::adjacent_find(t.begin(), t.end()) == t.end();
    };
    auto layoutSize = [&] {
        std::string text;
        if (!xc::platform::readFile(cache, text)) return size_t(0);
        auto j = json::parse(text);
        return j ? (*j)["layout"].size() : size_t(0);
    };
    xcloud::GssvClient gssv;
    std::string err;
    size_t fresh = 0;
    {
        app::Library lib;
        lib.setCachePath(cache);
        CHECK(lib.load(gssv, "en-US", [] {}, err));
        fresh = lib.rows().size();
        CHECK(fresh >= 4 && unique(titlesOf(lib.rows())));
        lib.saveCache();
        CHECK(layoutSize() == fresh);
    }
    {
        // The next start: the cache has rows, load() still shows each once.
        app::Library lib;
        lib.setCachePath(cache);
        CHECK(lib.load(gssv, "en-US", [] {}, err));
        CHECK(lib.rows().size() == fresh && unique(titlesOf(lib.rows())));
        lib.saveCache();
        CHECK(layoutSize() == fresh);
    }
    {
        // A cache written with every row twice (the old bug).
        std::string text;
        CHECK(xc::platform::readFile(cache, text));
        auto j = json::parse(text);
        CHECK(j.has_value());
        if (j) {
            json::Value doubled = json::Value::array();
            for (int k = 0; k < 2; ++k)
                for (const auto& row : (*j)["layout"].items()) doubled.push(row);
            j->set("layout", doubled);
            xc::platform::writeFileAtomic(cache, j->dump());
        }
        CHECK(layoutSize() == 2 * fresh);
        app::Library instant;  // the instant start: each row once
        instant.setCachePath(cache);
        CHECK(instant.loadCache(true));
        CHECK(instant.rows().size() == fresh && unique(titlesOf(instant.rows())));
        app::Library lib;
        lib.setCachePath(cache);
        CHECK(lib.load(gssv, "en-US", [] {}, err));
        CHECK(lib.rows().size() == fresh);
        lib.saveCache();
        CHECK(layoutSize() == fresh);  // healed
    }
    net::setTestTransport({});
    std::system(("rm -rf " + dir).c_str());
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

// A padding-only RTP packet (payload all padding) inside a frame is skipped,
// not a reason to fail that frame; a payload larger than a buffer slot is a
// lost packet (NACKed, then the frame dropped), never a cut NAL unit.
static void testH264PaddingPacket() {
    xc::stream::VideoReceiveStats stats;
    int keyframeAsks = 0;
    xc::stream::H264Receiver rx([&] { ++keyframeAsks; }, stats, 60);
    auto packet = [](uint16_t seq, uint32_t ts, bool marker, std::vector<uint8_t> payload, uint8_t padding = 0) {
        std::vector<uint8_t> b = {static_cast<uint8_t>(0x80 | (padding ? 0x20 : 0)),
                                  static_cast<uint8_t>((marker ? 0x80 : 0) | 102),
                                  static_cast<uint8_t>(seq >> 8), static_cast<uint8_t>(seq),
                                  static_cast<uint8_t>(ts >> 24), static_cast<uint8_t>(ts >> 16),
                                  static_cast<uint8_t>(ts >> 8), static_cast<uint8_t>(ts), 0, 0, 0, 7};
        b.insert(b.end(), payload.begin(), payload.end());
        for (uint8_t i = 1; i <= padding; ++i) b.push_back(i == padding ? padding : 0);
        return rtc::make_message(reinterpret_cast<const std::byte*>(b.data()),
                                 reinterpret_cast<const std::byte*>(b.data()) + b.size());
    };
    auto nal = [](uint8_t type) { return std::vector<uint8_t>{type, 0xAA, 0xBB}; };
    size_t framesOut = 0;
    auto feed = [&](rtc::message_vector in) {
        rx.incoming(in, [](rtc::message_ptr) { return true; });
        framesOut += in.size();
    };
    // A boundary, a key frame, a P frame, then a padding-only packet opening
    // the next P frame (same timestamp, no marker), that P frame and another.
    feed({packet(0, 0, true, nal(0x41)), packet(1, 1500, true, nal(0x65)), packet(2, 3000, true, nal(0x41)),
          packet(3, 4500, false, {}, 4), packet(4, 4500, true, nal(0x41)), packet(5, 6000, true, nal(0x41))});
    CHECK(framesOut == 4);
    CHECK(stats.framesDropped == 0 && keyframeAsks == 0);
    // A payload too large for a slot: a hole (NACKed), then the frame dropped.
    std::vector<uint8_t> big(2000, 0xCC);
    big[0] = 0x41;
    uint64_t nacksBefore = stats.nacks;
    feed({packet(6, 7500, true, big), packet(7, 9000, true, nal(0x41))});
    CHECK(framesOut == 4);  // waiting for the retransmission
    CHECK(stats.nacks > nacksBefore && stats.lost >= 1);
    xc::platform::sleepMs(100);
    feed({});
    CHECK(framesOut == 4 && stats.framesDropped >= 1 && keyframeAsks >= 1);
    // The key frame asked for resumes the picture.
    feed({packet(8, 10500, true, nal(0x65)), packet(9, 12000, true, nal(0x41))});
    CHECK(framesOut == 6);
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
    uint64_t now = 1000;
    xc::input::keyboardAsGamepad(in, stick, out, now);
    CHECK(out.connected && out.btnA && out.leftStickX == 1.0f && out.leftStickY == -1.0f);
    CHECK(out.triggerR2 == 1.0f && out.triggerL2 == 0.0f);
    CHECK(out.rightStickX > 0.4f && out.rightStickY == 0.0f);
    in = {};
    in.keyboard = true;
    for (int i = 0; i < 20; ++i) xc::input::keyboardAsGamepad(in, stick, out, now += 8);  // the mouse still: back to the centre
    CHECK(out.rightStickX == 0.0f && !out.btnA);

    // The same mouse speed gives the same stick whether the passes are 4 or
    // 8 ms apart (the counts a pass covers halve with it).
    auto stickAfter = [](uint64_t passMs, float countsPerMs, uint64_t forMs) {
        xc::input::MouseStick st;
        xc::input::ControllerState o;
        xc::input::KbmInput mv;
        mv.mouse = true;
        uint64_t t = 5000;
        xc::input::keyboardAsGamepad(mv, st, o, t);  // a still first pass: the clock starts
        for (uint64_t el = 0; el < forMs; el += passMs) {
            mv.dx = static_cast<int32_t>(countsPerMs * static_cast<float>(passMs));
            xc::input::keyboardAsGamepad(mv, st, o, t += passMs);
        }
        return o.rightStickX;
    };
    for (uint64_t forMs : {8u, 24u, 48u, 400u}) {
        float at4 = stickAfter(4, 0.75f, forMs), at8 = stickAfter(8, 0.75f, forMs);
        CHECK(std::abs(at4 - at8) < 0.01f);
    }
    CHECK(std::abs(stickAfter(4, 0.75f, 400) - 0.5f) < 0.01f);  // half of full speed: half the stick
    CHECK(std::abs(stickAfter(8, 0.75f, 400) - 0.5f) < 0.01f);
    CHECK(stickAfter(4, 3.0f, 400) > 0.999f && stickAfter(8, 3.0f, 400) > 0.999f);  // faster: all the way

    // A wheel notch: Y held ~50 ms, then let go ~30 ms, at either pass length.
    for (uint64_t passMs : {4u, 8u}) {
        xc::input::MouseStick st;
        in = {};
        in.keyboard = true;
        in.wheel = 2;  // two notches: two presses
        uint64_t t = 9000, downMs = 0, presses = 0;
        bool was = false;
        for (uint64_t el = 0; el < 400; el += passMs) {
            xc::input::keyboardAsGamepad(in, st, out, t);
            in.wheel = 0;
            if (out.btnY) downMs += passMs;
            if (out.btnY && !was) ++presses;
            was = out.btnY;
            t += passMs;
        }
        CHECK(presses == 2 && !out.btnY);
        CHECK(downMs >= 2 * 40 && downMs <= 2 * 56);  // ~48 ms each
    }
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
    keyboardAsGamepad(in, stick, out, 1000, map);
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
    CHECK((ids(all) == std::vector<int>{0, 8, 17, 1, 20, 9, 10, 2, 18, 14, 12, 3, 15, 4, 5, 6, 16, 19, 7}));
    CHECK((groups(all) == std::vector<bool>{false, false, false, true, false, false, false, false, false, true, false, false, false, false, false, false, false, true, false}));
    ui::UiCaps pc;
    pc.lightBar = pc.triggerRumble = pc.adaptiveTriggers = pc.gyro = pc.touchpad = false;
    pc.upscalers = pc.sharpen = pc.deband = pc.highRefresh = pc.console = pc.selfUpdate = false;
    pc.openUrl = true;
    auto desk = ui::visibleSettingsRows(pc);
    CHECK((ids(desk) == std::vector<int>{0, 8, 17, 1, 20, 10, 2, 18, 14, 12, 3, 5, 16, 19, 7}));
    CHECK((groups(desk) == std::vector<bool>{false, false, false, true, false, false, false, false, true, false, false, false, false, true, false}));
    // The desktop app's window: Display mode after the resolution, in the stream's group.
    ui::UiCaps win = pc;
    win.desktopWindow = true;
    auto w = ui::visibleSettingsRows(win);
    CHECK((ids(w) == std::vector<int>{0, 8, 17, 1, 20, 13, 10, 2, 18, 14, 12, 3, 5, 16, 19, 7}));
    CHECK((groups(w) == std::vector<bool>{false, false, false, true, false, false, false, false, false, true, false, false, false, false, true, false}));
    ui::UiCaps xbox = pc;  // an Xbox pad: impulse triggers, no light bar
    xbox.triggerRumble = true;
    auto x = ids(ui::visibleSettingsRows(xbox));
    CHECK(std::find(x.begin(), x.end(), 4) != x.end() && std::find(x.begin(), x.end(), 6) == x.end());
    // A game's settings.
    CHECK((ids(ui::visibleGameSettingsRows(ps5, true, false)) == std::vector<int>{0, 4, 8, 15, 14, 1, 2, 3, 5, 6, 7, 12, 11, 9, 10}));
    CHECK((ids(ui::visibleGameSettingsRows(ps5, true, true)) == std::vector<int>{0, 4, 8, 15, 14, 1, 2, 3, 5, 6, 7, 12, 11, 13, 9, 10}));
    CHECK((groups(ui::visibleGameSettingsRows(ps5, true, true)) ==
           std::vector<bool>{false, true, false, false, false, true, false, false, true, false, false, false, false, false, false, true}));
    CHECK((ids(ui::visibleGameSettingsRows(ps5, false)) == std::vector<int>{0, 4, 8, 15, 14, 1, 2, 3, 5, 6, 7, 12, 11, 9}));
    ui::UiCaps noTouch = ps5;
    noTouch.touchpad = false;
    CHECK((ids(ui::visibleGameSettingsRows(noTouch, true, true)) == std::vector<int>{0, 4, 8, 15, 14, 1, 2, 3, 5, 6, 7, 12, 11, 9, 10}));
    auto game = ui::visibleGameSettingsRows(pc, false);
    CHECK((ids(game) == std::vector<int>{0, 4, 15, 14, 5, 7, 11, 9}));
    CHECK((groups(game) == std::vector<bool>{false, true, false, false, true, false, false, false}));
    // How far down the autoplay tests walk to each row (autoplay.cpp's downsTo).
    CHECK(ui::settingsRowPosition(ps5, 1) == 3 && ui::settingsRowPosition(ps5, 3) == 11);
    CHECK(ui::settingsRowPosition(ps5, 16) == 16);
    CHECK(ui::settingsRowPosition(ps5, 5) == 14 && ui::settingsRowPosition(ps5, 6) == 15);
    CHECK(ui::settingsRowPosition(ps5, 12) == 10 && ui::settingsRowPosition(ps5, 14) == 9);
    CHECK(ui::settingsRowPosition(ps5, 7) == 18 && ui::settingsRowPosition(ps5, 19) == 17 && ui::settingsRowPosition(ps5, 2) == 7);
    CHECK(ui::settingsRowPosition(pc, 5) == 11 && ui::settingsRowPosition(pc, 6) == -1 && ui::settingsRowPosition(pc, 4) == -1);
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

static void testVideoDecoder() {
    media::VideoDecoder dec;
    CHECK(dec.init(1, false));
    CHECK(!dec.isHardware());
    CHECK(dec.needsKeyframe());

    media::Picture pic;
    CHECK(!dec.decode(nullptr, 0, pic));
    CHECK(!dec.decode(nullptr, 100, pic));
    uint8_t garbage[16] = {0, 0, 0, 1, 0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(!dec.decode(garbage, 0, pic));
    CHECK(!dec.decode(garbage, sizeof garbage, pic));
    CHECK(dec.needsKeyframe());

    // Hardware init preference test (on non-PS5 desktop without HW device it gracefully stays/falls back to software)
    media::VideoDecoder hwDec;
    CHECK(hwDec.init(1, true));

    // splitAccessUnits tests
    std::vector<uint8_t> stream = {0, 0, 0, 1, 9, 0x10, 0, 0, 0, 1, 7, 0x42, 0, 0, 0, 1, 8, 0xCE, 0, 0, 0, 1, 5, 0x88};
    auto aus = media::splitAccessUnits(stream.data(), stream.size());
    CHECK(!aus.empty());
}

static void testDrawNv12() {
    // Boundary and parameter checks
    CHECK(!display::drawNv12(nullptr, nullptr, 0, 0, 0, 0, false));
    CHECK(!display::drawNv12(nullptr, nullptr, 1920, 1920, 1920, 1080, false));
    uint8_t dummy[16] = {};
    CHECK(!display::drawNv12(dummy, dummy, 10, 10, -1, 1080, false));
    CHECK(!display::drawNv12(dummy, dummy, 10, 10, 1920, -1, false));
    CHECK(!display::drawNv12(dummy, dummy, 100, 100, 1920, 1080, false));  // stride < width

    // Valid dummy frame in null display backend
    std::vector<uint8_t> y(1920 * 1080, 128);
    std::vector<uint8_t> uv(1920 * 540, 128);
    CHECK(display::drawNv12(y.data(), uv.data(), 1920, 1920, 1920, 1080, false));
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

static void testPurchase() {
    using namespace xcloud;
    std::string err;
    int status = 200;
    std::string body;
    net::Request lastReq;

    net::setTestTransport([&](const net::Request& req) {
        lastReq = req;
        net::Response r;
        r.status = status;
        r.body = body;
        return r;
    });

    // 1. fetchPurchaseOffer for a free product
    body = R"({
        "Products": [
            {
                "ProductId": "FREE123",
                "DisplaySkuAvailabilities": [
                    {
                        "Sku": {"SkuId": "0001"},
                        "Availabilities": [
                            {
                                "AvailabilityId": "AVAIL_FREE",
                                "Actions": ["Details", "Purchase"],
                                "OrderManagementData": {
                                    "Price": {
                                        "ListPrice": 0.0,
                                        "MSRP": 0.0,
                                        "CurrencyCode": "USD"
                                    }
                                }
                            }
                        ]
                    }
                ]
            }
        ]
    })";

    PurchaseOffer offer;
    CHECK(fetchPurchaseOffer("FREE123", "US", "en-us", offer, err));
    CHECK(offer.productId == "FREE123");
    CHECK(offer.skuId == "0001");
    CHECK(offer.availabilityId == "AVAIL_FREE");
    CHECK(offer.isFree);
    CHECK(offer.listPrice == 0.0);
    CHECK(lastReq.url.find("displaycatalog.mp.microsoft.com") != std::string::npos);

    // 2. fetchPurchaseOffer for a paid product: isFree must be false
    body = R"({
        "Products": [
            {
                "ProductId": "PAID123",
                "DisplaySkuAvailabilities": [
                    {
                        "Sku": {"SkuId": "0002"},
                        "Availabilities": [
                            {
                                "AvailabilityId": "AVAIL_PAID",
                                "Actions": ["Purchase"],
                                "OrderManagementData": {
                                    "Price": {
                                        "ListPrice": 69.99,
                                        "MSRP": 69.99,
                                        "CurrencyCode": "USD"
                                    }
                                }
                            }
                        ]
                    }
                ]
            }
        ]
    })";
    PurchaseOffer paidOffer;
    CHECK(fetchPurchaseOffer("PAID123", "US", "en-us", paidOffer, err));
    CHECK(!paidOffer.isFree);
    CHECK(paidOffer.listPrice == 69.99);

    // 3. Safety check: claimFreeProduct MUST refuse paid products
    CHECK(!claimFreeProduct("XBL3.0 x=test;token", paidOffer, "US", "en-US", err));
    CHECK(err.find("safety block") != std::string::npos);

    // 4. claimFreeProduct with empty auth or incomplete offer
    PurchaseOffer invalidOffer = offer;
    invalidOffer.availabilityId.clear();
    CHECK(!claimFreeProduct("XBL3.0 x=test;token", invalidOffer, "US", "en-US", err));
    CHECK(!claimFreeProduct("", offer, "US", "en-US", err));

    // 5. Successful claim of free product (2-step POST create and PUT finalize)
    net::Request lastPostReq, lastPutReq;
    net::setTestTransport([&](const net::Request& req) {
        net::Response r;
        if (req.method == "POST") {
            lastPostReq = req;
            r.status = 201;
            r.body = R"({"orderId":"ORDER-12345","orderState":"Editing"})";
        } else if (req.method == "PUT") {
            lastPutReq = req;
            r.status = 200;
            r.body = R"({"orderId":"ORDER-12345","orderState":"Purchased"})";
        }
        return r;
    });

    CHECK(claimFreeProduct("XBL3.0 x=test;token", offer, "US", "en-US", err));
    CHECK(lastPostReq.url.find("purchase.mp.microsoft.com/v7.0/users/me/orders") != std::string::npos);
    std::string authVal;
    for (const auto& h : lastPostReq.headers) {
        if (h.first == "Authorization") authVal = h.second;
    }
    CHECK(authVal == "XBL3.0 x=test;token");
    auto postJson = json::parse(lastPostReq.body);
    CHECK(postJson && (*postJson)["items"].isArray());
    CHECK((*postJson)["items"][0]["productId"].str() == "FREE123");
    CHECK((*postJson)["items"][0]["skuId"].str() == "0001");
    CHECK((*postJson)["items"][0]["availabilityId"].str() == "AVAIL_FREE");
    CHECK((*postJson)["items"][0]["price"].asNumber() == 0.0);
    CHECK((*postJson)["clientContext"]["client"].str() == "Xbox");
    CHECK(!(*postJson)["orderId"].str().empty());

    CHECK(lastPutReq.method == "PUT");
    auto putJson = json::parse(lastPutReq.body);
    CHECK(putJson && (*putJson)["orderState"].str() == "Purchased");

    // 6. claimFreeProduct error handling (must extract innererror code/message, not innererror.data[0] productId)
    net::setTestTransport([&](const net::Request&) {
        net::Response r;
        r.status = 400;
        r.body = R"({"code":"PaymentRequired","innererror":{"code":"MissingPaymentInstrument","data":["FREE123"],"message":"Payment method needed"}})";
        return r;
    });
    CHECK(!claimFreeProduct("XBL3.0 x=test;token", offer, "US", "en-US", err));
    CHECK(err.find("Payment method needed") != std::string::npos);
    CHECK(err != "FREE123");

    // 7. UserAlreadyOwnsContent on finalize (HTTP 403, Issue #104) is treated as already acquired
    net::setTestTransport([&](const net::Request& req) {
        net::Response r;
        if (req.method == "POST") {
            r.status = 201;
            r.body = R"({"orderId":"ORDER-12345","orderState":"Editing"})";
        } else {
            r.status = 403;
            r.body = R"({"code":"Forbidden","innererror":{"code":"UserAlreadyOwnsContent","data":["9P22THR57FV7"],"message":"Purchase policy restricts user from purchasing certain content multiple times"}})";
        }
        return r;
    });
    CHECK(claimFreeProduct("XBL3.0 x=test;token", offer, "US", "en-US", err));

    // 8. fetchPurchaseOffer skips RemediationRequired upsells and picks the non-remediated free availability
    net::setTestTransport([&](const net::Request&) {
        net::Response r;
        r.status = 200;
        r.body = R"({
            "Products": [{
                "DisplaySkuAvailabilities": [{
                    "Sku": {"SkuId": "0001"},
                    "Availabilities": [
                        {
                            "AvailabilityId": "AVAIL_UPSELL_REMEDIATION",
                            "Actions": ["Purchase"],
                            "RemediationRequired": true,
                            "OrderManagementData": {"Price": {"ListPrice": 0.0, "MSRP": 0.0, "CurrencyCode": "USD"}}
                        },
                        {
                            "AvailabilityId": "AVAIL_DIRECT_FREE",
                            "Actions": ["Purchase"],
                            "RemediationRequired": false,
                            "OrderManagementData": {"Price": {"ListPrice": 0.0, "MSRP": 0.0, "CurrencyCode": "USD"}}
                        }
                    ]
                }]
            }]
        })";
        return r;
    });
    PurchaseOffer directOffer;
    CHECK(fetchPurchaseOffer("FREE123", "US", "en-us", directOffer, err));
    CHECK(directOffer.isFree);
    CHECK(directOffer.availabilityId == "AVAIL_DIRECT_FREE");

    // 9. fetchPurchaseOffer accepts actions like "Browse" or "License" for zero-priced games
    net::setTestTransport([](const net::Request&) {
        net::Response r;
        r.status = 200;
        r.body = R"({
            "Products": [{
                "ProductId": "JUSTDANCE",
                "DisplaySkuAvailabilities": [{
                    "Sku": { "SkuId": "0001" },
                    "Availabilities": [{
                        "AvailabilityId": "AVAIL_JUSTDANCE",
                        "Actions": ["Browse", "License"],
                        "OrderManagementData": {
                            "Price": { "ListPrice": 0.0, "MSRP": 0.0, "CurrencyCode": "BRL" }
                        }
                    }]
                }]
            }]
        })";
        return r;
    });
    PurchaseOffer trialOffer;
    CHECK(fetchPurchaseOffer("JUSTDANCE", "BR", "pt-br", trialOffer, err));
    CHECK(trialOffer.isFree);
    CHECK(trialOffer.availabilityId == "AVAIL_JUSTDANCE");

    net::setTestTransport({});
}

static void testWishlist() {
    using namespace xc::app;
    std::string tmp = "/tmp/xcloud-tests-wishlist.json";
    Settings s;
    s.wishlist = {"PROD_A", "PROD_B"};
    CHECK(s.save(tmp));

    Settings loaded;
    CHECK(loaded.load(tmp));
    CHECK(loaded.wishlist.size() == 2);
    if (loaded.wishlist.size() == 2) {
        CHECK(loaded.wishlist[0] == "PROD_A");
        CHECK(loaded.wishlist[1] == "PROD_B");
    }
    ::unlink(tmp.c_str());

    Library lib;
    lib.setWishlist({"PROD_WISH"});
    auto rows = lib.rows();
    // Standalone wishlist shelf must not exist in Game Pass home rows
    for (const auto& r : rows) {
        CHECK(r.title != ui::tr(ui::Str::Wishlist));
    }

    // AppUi Wishlist tests
    ui::Fonts fonts;
    ui::ImageCache images([] {});
    ui::AppUi app(fonts, images);

    ui::GameTile ownedTile, unownedTileA, unownedTileB;
    ownedTile.productId = "OWNED_1";
    ownedTile.titleId = "TITLE_OWNED_1";
    ownedTile.name = "Owned Game";

    unownedTileA.productId = "UNOWNED_A";
    unownedTileA.titleId = "TITLE_UNOWNED_A";
    unownedTileA.name = "Game A";

    unownedTileB.productId = "UNOWNED_B";
    unownedTileB.titleId = "TITLE_UNOWNED_B";
    unownedTileB.name = "Game B";

    // Set owned and purchasable: unownedTileB is wishlisted initially.
    // Also include ownedTile in wishlist initially to verify rebuild() prunes acquired games!
    app.setPrefs({}, ui::LibrarySort::AZ, {}, {"OWNED_1", "UNOWNED_B"});
    app.setOwned({ownedTile}, {unownedTileA, unownedTileB}, true);

    // In purchasable_, UNOWNED_B should be at index 0 because it's wishlisted, even though Game A precedes Game B alphabetically!
    ui::GameTile firstPurchasable;
    CHECK(app.purchasableAt(0, firstPurchasable));
    CHECK(firstPurchasable.productId == "UNOWNED_B");

    // Search filtering with Wishlist
    app.setSearchPools({}, {unownedTileA, unownedTileB});
    app.showFilteredSearch(ui::Tab::Library, false, 0, "", 0, -1);
    app.chooseFilter(ui::AppUi::Filter::Wishlist, 1);
    auto res = app.searchResults(10);
    // Only UNOWNED_B should be returned because it's in the wishlist
    CHECK(res.size() == 1);
    if (!res.empty()) {
        CHECK(res[0].first == "Game B");
    }

    // Test details screen: open details for ownedTile, press R3 -> should NOT add to wishlist!
    app.showDetails(ownedTile);
    ui::NavInput r3Press;
    r3Press.r3 = true;
    app.handle(r3Press);

    // Verify search with wishlist filter again: ownedTile was NOT added!
    app.showFilteredSearch(ui::Tab::Library, false, 0, "", 0, -1);
    app.chooseFilter(ui::AppUi::Filter::Wishlist, 1);
    res = app.searchResults(10);
    CHECK(res.size() == 1);
    if (!res.empty()) {
        CHECK(res[0].first == "Game B");
    }

    // Open details for unownedTileA, press R3 -> should be added to wishlist!
    app.showDetails(unownedTileA);
    app.handle(r3Press);

    app.showFilteredSearch(ui::Tab::Library, false, 0, "", 0, -1);
    app.chooseFilter(ui::AppUi::Filter::Wishlist, 1);
    res = app.searchResults(10);
    // Now both UNOWNED_B and UNOWNED_A are wishlisted!
    CHECK(res.size() == 2);

    // Press R3 again on unownedTileA -> toggles off!
    app.showDetails(unownedTileA);
    app.handle(r3Press);

    app.showFilteredSearch(ui::Tab::Library, false, 0, "", 0, -1);
    app.chooseFilter(ui::AppUi::Filter::Wishlist, 1);
    res = app.searchResults(10);
    CHECK(res.size() == 1);

    // Test price updating via setPrices while wishlist filter is active
    ui::PriceInfo pi;
    pi.list = 49.99;
    pi.msrp = 49.99;
    pi.now = "R$ 49,99";
    app.setPrices({{"UNOWNED_B", pi}});
    res = app.searchResults(10);
    CHECK(res.size() == 1);
    CHECK(res[0].second.now == "R$ 49,99");

    // Add UNOWNED_A to wishlist
    app.showDetails(unownedTileA);
    app.handle(r3Press);

    // Test pruning of acquired game matching titleId
    ui::GameTile newlyOwned;
    newlyOwned.productId = "DIFF_PID";
    newlyOwned.titleId = "TITLE_UNOWNED_A";
    newlyOwned.name = "Game A";
    app.setOwned({ownedTile, newlyOwned}, {unownedTileA, unownedTileB}, true);

    app.showFilteredSearch(ui::Tab::Library, false, 0, "", 0, -1);
    app.chooseFilter(ui::AppUi::Filter::Wishlist, 1);
    res = app.searchResults(10);
    // UNOWNED_A must be pruned because newlyOwned has matching titleId
    CHECK(res.size() == 1);
    CHECK(res[0].first == "Game B");

    // Unwishlist UNOWNED_B to test empty state
    app.showDetails(unownedTileB);
    app.handle(r3Press);

    app.showFilteredSearch(ui::Tab::Library, false, 0, "", 0, -1);
    app.chooseFilter(ui::AppUi::Filter::Wishlist, 1);
    res = app.searchResults(10);
    CHECK(res.empty());

    // Empty wishlist string test
    CHECK(std::string(ui::tr(ui::Str::WishlistEmpty)).find("vazia") != std::string::npos ||
          std::string(ui::tr(ui::Str::WishlistEmpty)).find("empty") != std::string::npos);
}

static void testBlock1Features() {
    using namespace xc;
    ui::Fonts fonts;
    ui::ImageCache images([] {});
    ui::AppUi app(fonts, images);

    // 1. 3x3 filter grid verification
    app.showFilteredSearch(ui::Tab::Library, false, 0, "", 0, -1);
    CHECK(app.filterRow(0).size() == 3); // Wishlist, Free, Cheapest
    CHECK(app.filterRow(1).size() == 3); // Mode, Genre, Publisher
    CHECK(app.filterRow(2).size() == 3); // Console, Language, MouseKeyboard
    CHECK(app.filterRow(3).empty());

    app.showFilteredSearch(ui::Tab::GamePass, false, 0, "", 0, -1);
    CHECK(app.filterRow(0).size() == 3); // Plan, Console, MouseKeyboard
    CHECK(app.filterRow(1).size() == 3); // Mode, Genre, Publisher
    CHECK(app.filterRow(2).size() == 1); // Language
    CHECK(app.filterRow(3).empty());

    // 2. isFreeGame and ClaimFreeGame for zero-priced games
    ui::GameTile trialGame;
    trialGame.productId = "9P0LHV4DV2BG";
    trialGame.titleId = "TITLE_JUSTDANCE";
    trialGame.name = "Just Dance Trial";
    trialGame.purchasable = false; // Test that even when purchasable is false, zero price enables claiming
    trialGame.playable = false;
    trialGame.freeInStore = false; // Microsoft doesn't set freeInStore for trials

    // Before prices are loaded, not free
    CHECK(!app.isFreeGame(trialGame));

    // When prices.json returns list=0 and msrp=0
    ui::PriceInfo zeroPrice;
    zeroPrice.list = 0.0;
    zeroPrice.msrp = 0.0;
    zeroPrice.now = "Grátis";
    app.setPrices({{"9P0LHV4DV2BG", zeroPrice}});

    CHECK(app.isFreeGame(trialGame));

    // Opening details and pressing accept should produce Action::ClaimFreeGame
    app.showDetails(trialGame);
    ui::NavInput acceptPress;
    acceptPress.accept = true;
    auto ev = app.handle(acceptPress);
    CHECK(ev.action == ui::Action::ClaimFreeGame);
    CHECK(ev.game.productId == "9P0LHV4DV2BG");
    CHECK(ev.game.freeInStore == true);

    // After acquiring the game, it becomes owned/playable: pressing accept produces Action::Play
    app.markTileOwned("9P0LHV4DV2BG", "TITLE_JUSTDANCE");
    ev = app.handle(acceptPress);
    CHECK(ev.action == ui::Action::Play);

    // 3. Search filter navigation across 3x3 grid
    app.showFilteredSearch(ui::Tab::Library, false, 0, "", 0, -1);
    // Move down from wide keys to filter row 0, 1, 2
    ui::NavInput navDown;
    navDown.down = true;
    app.handle(navDown);
    app.handle(navDown);
    // Move left and right
    ui::NavInput navRight;
    navRight.right = true;
    app.handle(navRight);
    ui::NavInput navLeft;
    navLeft.left = true;
    app.handle(navLeft);
}

static void testAchievements() {
    using namespace xc::xcloud;
    TitleAchievements ach;
    std::string err;
    CHECK(!fetchTitleAchievements("", "123", "TITLE1", ach, err));
    CHECK(err == "xblAuth empty");
    CHECK(!fetchTitleAchievements("XBL3.0 x=test;tok", "123", "", ach, err));
    CHECK(err == "xboxTitleId empty");
}

static void testBlock2Features() {
    // 1. Parsing of bilingual release notes
    std::string notes =
        "<!-- LANG:PT -->\n"
        "## Novidades 1.3.4\n"
        "- Alto-falante no menu\n"
        "- Novos tetos de bitrate\n"
        "<!-- LANG:EN -->\n"
        "## What's new 1.3.4\n"
        "- Speaker in menu\n"
        "- New bitrate caps\n";
    std::string pt = app::filterReleaseNotes(notes, "pt-BR");
    CHECK(pt.find("Novidades 1.3.4") != std::string::npos);
    CHECK(pt.find("What's new") == std::string::npos);

    std::string en = app::filterReleaseNotes(notes, "en");
    CHECK(en.find("What's new 1.3.4") != std::string::npos);
    CHECK(en.find("Novidades 1.3.4") == std::string::npos);

    // Fallback to EN when lang tag is not found
    std::string es = app::filterReleaseNotes(notes, "es");
    CHECK(es.find("What's new 1.3.4") != std::string::npos);

    // No tags: returns whole text
    std::string plain = "Simple release notes without tags.";
    CHECK(app::filterReleaseNotes(plain, "pt-BR") == plain);

    // Test tag spacing and casing variations
    std::string noSpaceNotes = "<!--LANG:PT-->\nNotas compactas\n<!--LANG:EN-->\nCompact notes";
    CHECK(app::filterReleaseNotes(noSpaceNotes, "pt-BR") == "Notas compactas");
    CHECK(app::filterReleaseNotes(noSpaceNotes, "en") == "Compact notes");

    std::string spaceColonNotes = "<!-- LANG: PT -->\nNotas com espaco\n<!-- LANG: EN -->\nSpaced notes";
    CHECK(app::filterReleaseNotes(spaceColonNotes, "pt-BR") == "Notas com espaco");
    CHECK(app::filterReleaseNotes(spaceColonNotes, "en") == "Spaced notes");

    std::string lowerNotes = "<!-- lang: pt-br -->\nNotas minusculas\n<!-- lang: en-us -->\nLowercase notes";
    CHECK(app::filterReleaseNotes(lowerNotes, "pt-BR") == "Notas minusculas");
    CHECK(app::filterReleaseNotes(lowerNotes, "en") == "Lowercase notes");

    // Ensure delimiter tags do not leak into output
    CHECK(app::filterReleaseNotes(notes, "pt-BR").find("<!--") == std::string::npos);
    CHECK(app::filterReleaseNotes(notes, "en").find("<!--") == std::string::npos);

    // 2. Settings serialization of padSpeakerVolume
    app::Settings s;
    CHECK(s.padSpeakerVolume == 100);
    s.padSpeakerVolume = 75;
    std::string tmpPath = "/tmp/test_settings_block2.json";
    CHECK(s.save(tmpPath));

    app::Settings loaded;
    CHECK(loaded.load(tmpPath));
    CHECK(loaded.padSpeakerVolume == 75);
    std::remove(tmpPath.c_str());

    // Test media::setPadSpeakerVolume and padSpeakerVolume
    media::setPadSpeakerVolume(60);
    CHECK(media::padSpeakerVolume() == 60);
    media::setPadSpeakerVolume(-10);
    CHECK(media::padSpeakerVolume() == 0);
    media::setPadSpeakerVolume(150);
    CHECK(media::padSpeakerVolume() == 100);

    // 3. Bitrate caps in UI
    ui::Fonts dummyFonts{};
    ui::ImageCache images([] {});
    ui::AppUi app(dummyFonts, images);

    app::Settings bs;
    bs.maxBitrateMbps = 50;
    ui::SettingsChoice bc = ui::choiceFromSettings(bs);
    CHECK(bc.maxBitrateMbps == 50);
    app::Settings bsBack;
    ui::applyChoice(bc, bsBack);
    CHECK(bsBack.maxBitrateMbps == 50);

    bs.maxBitrateMbps = 45;
    bc = ui::choiceFromSettings(bs);
    CHECK(bc.maxBitrateMbps == 45);
    ui::applyChoice(bc, bsBack);
    CHECK(bsBack.maxBitrateMbps == 45);

    // Test settings navigation in AppUi
    app.showHome();
    ui::NavInput openOpt;
    openOpt.options = true;
    app.handle(openOpt);
    CHECK(app.screen() == ui::Screen::Settings);

    // Navigate down to BitrateCap row (row 10)
    ui::NavInput navDownRow;
    navDownRow.down = true;
    while (app.settingsFocus() != 10) {
        app.handle(navDownRow);
    }
    CHECK(app.settingsFocus() == 10);

    // Test inline slider navigation on BitrateCap (row 10)
    CHECK(app.settings().maxBitrateMbps == 0);

    // Pressing left at 0 (Automatic) should clamp at 0
    ui::NavInput navLeft;
    navLeft.left = true;
    app.handle(navLeft);
    CHECK(app.settings().maxBitrateMbps == 0);

    // Pressing right increases the bitrate in 5 Mbps increments up to 50 Mbps
    ui::NavInput navRight;
    navRight.right = true;
    const int expectedMbps[] = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50};
    for (int exp : expectedMbps) {
        app.handle(navRight);
        CHECK(app.settings().maxBitrateMbps == exp);
    }

    // Pressing right at 50 Mbps should clamp at 50 Mbps
    app.handle(navRight);
    CHECK(app.settings().maxBitrateMbps == 50);

    // Pressing left reduces the bitrate from 50 to 45 Mbps
    app.handle(navLeft);
    CHECK(app.settings().maxBitrateMbps == 45);

    // Pressing accept (Cross) on row 10 cycles/alternates to next option (no dropdown opened)
    ui::NavInput acc;
    acc.accept = true;
    app.handle(acc);
    CHECK(app.settings().maxBitrateMbps == 50);
    app.handle(acc);
    CHECK(app.settings().maxBitrateMbps == 0); // wrapped to Automatic
    app.handle(acc);
    CHECK(app.settings().maxBitrateMbps == 5);

    // Non-exact bitrate snapping test:
    // If a legacy or custom setting such as 22 Mbps is loaded, it should snap to nearest option (20 Mbps)
    auto st = app.settings();
    st.maxBitrateMbps = 22;
    app.setSettings(st);
    app.handle(navRight); // from snapped 20 Mbps -> 25 Mbps
    CHECK(app.settings().maxBitrateMbps == 25);
    app.handle(navLeft);  // 25 -> 20 Mbps
    CHECK(app.settings().maxBitrateMbps == 20);
    app.handle(navLeft);  // 20 -> 15 Mbps
    CHECK(app.settings().maxBitrateMbps == 15);

    // Test rendering on row 10 with controller and keyboard hints
    ui::Canvas canvas(1920, 1080);
    ui::g_keyboardHints = false;
    app.render(canvas, 1000);
    ui::g_keyboardHints = true;
    app.render(canvas, 1000);
    ui::g_keyboardHints = false;

    // Test keyLabel icon index alignment
    CHECK(std::string(ui::keyLabel(0)) == "Enter");
    CHECK(std::string(ui::keyLabel(1)) == "Esc");
    CHECK(std::string(ui::keyLabel(2)) == "Tab");
    CHECK(std::string(ui::keyLabel(3)) == "F");
    CHECK(std::string(ui::keyLabel(4)) == "Del");
    CHECK(std::string(ui::keyLabel(5)) == "H");
    CHECK(std::string(ui::keyLabel(6)) == "R");
    CHECK(std::string(ui::keyLabel(7)) == "Shift");
    CHECK(std::string(ui::keyLabel(8)) == "Z C");
    CHECK(std::string(ui::keyLabel(9)) == "<- ->");
    CHECK(std::string(ui::keyLabel(10)) == "");
    CHECK(std::string(ui::keyLabel(-1)) == "");

    // Close Settings
    ui::NavInput navBack;
    navBack.back = true;
    app.handle(navBack);
    CHECK(app.screen() == ui::Screen::Home);

    // 4. StreamMenu with PadSpeakerVolumeItem
    ui::StreamMenu menu(dummyFonts);
    menu.open(0, false, 0, 1, 0, false, true, false, 2, 15, false, 0.0f, false, 100);
    CHECK(menu.padSpeakerVolume() == 100);

    // When at 100%, right should not trigger PadSpeakerVolume action
    ui::NavInput down;
    down.down = true;
    for (int i = 0; i < 12; ++i) menu.handle(down);

    ui::NavInput right;
    right.right = true;
    auto actAt100 = menu.handle(right);
    CHECK(actAt100 == ui::MenuAction::None);
    CHECK(menu.padSpeakerVolume() == 100);

    // Left should decrease volume by 5%
    ui::NavInput left;
    left.left = true;
    auto act = menu.handle(left);
    CHECK(act == ui::MenuAction::PadSpeakerVolume);
    CHECK(menu.padSpeakerVolume() == 95);

    // Test decreasing down to 0%
    for (int i = 0; i < 20; ++i) menu.handle(left);
    CHECK(menu.padSpeakerVolume() == 0);

    // When at 0%, left should not trigger PadSpeakerVolume action
    auto actAt0 = menu.handle(left);
    CHECK(actAt0 == ui::MenuAction::None);
    CHECK(menu.padSpeakerVolume() == 0);

    // Rendering at 0% should render valid canvas
    ui::StreamInfo sinfo;
    auto cMute = menu.renderMenu(sinfo);
    CHECK(cMute.width() == ui::StreamMenu::kMenuW);

    // Right should increase by 5%
    act = menu.handle(right);
    CHECK(act == ui::MenuAction::PadSpeakerVolume);
    CHECK(menu.padSpeakerVolume() == 5);

    // 5. Update prompt with 3 buttons and Square shortcut
    app.showHome();
    app.setUpdateState("v1.3.3", "v1.3.4", false, notes);
    app.offerUpdate(notes);
    CHECK(app.updateOffered());
    CHECK(!app.showingUpdateNotes());

    // Navigation between 3 buttons: left wraps from 0 to 2
    ui::NavInput navL;
    navL.left = true;
    app.handle(navL); // promptFocus_ becomes 2 (Not now)
    ui::NavInput navR;
    navR.right = true;
    app.handle(navR); // promptFocus_ wraps to 0 (Update now)
    app.handle(navR); // promptFocus_ is 1 (What's new)

    // Press accept on What's new -> opens notes modal
    ui::NavInput accept;
    accept.accept = true;
    auto ev = app.handle(accept);
    CHECK(ev.action == ui::Action::None);
    CHECK(app.showingUpdateNotes());

    // Up / Down scroll in notes modal
    ui::NavInput navDown;
    navDown.down = true;
    app.handle(navDown);
    ui::NavInput navUp;
    navUp.up = true;
    app.handle(navUp);

    // Shoulder buttons page scrolling (L1/R1)
    ui::NavInput navR1;
    navR1.r1 = true;
    app.handle(navR1);
    ui::NavInput navL1;
    navL1.l1 = true;
    app.handle(navL1);

    // Right stick smooth scrolling
    ui::NavInput stickScroll;
    stickScroll.scrollRows = 2;
    app.handle(stickScroll);
    stickScroll.scrollRows = -2;
    app.handle(stickScroll);

    // Back closes notes modal
    ui::NavInput back;
    back.back = true;
    app.handle(back);
    CHECK(!app.showingUpdateNotes());
    CHECK(app.updateOffered());

    // Square shortcut opens notes modal directly
    ui::NavInput sq;
    sq.square = true;
    app.handle(sq);
    CHECK(app.showingUpdateNotes());
    app.handle(back);
    CHECK(!app.showingUpdateNotes());
}

static void testBlock3Features() {
    using namespace xc;

    // 1. Content ratings parsing
    {
        // ClassInd for BR
        auto jBr = json::parse(R"({
            "ContentRatings": [
                {
                    "RatingSystem": "ClassInd",
                    "RatingId": "14",
                    "RatingDescriptors": ["Violência", "Drogas Lícitas"]
                },
                {
                    "RatingSystem": "ESRB",
                    "RatingId": "Teen",
                    "RatingDescriptors": ["Violence", "Blood"]
                }
            ]
        })");
        CHECK(jBr.has_value());
        std::string age, sys;
        std::vector<std::string> desc;
        xcloud::parseContentRatings(*jBr, "BR", age, sys, desc);
        CHECK(sys == "ClassInd");
        CHECK(age == "14+");
        CHECK(desc.size() == 2 && desc[0] == "Violência");

        // ESRB for US
        std::string ageUs, sysUs;
        std::vector<std::string> descUs;
        xcloud::parseContentRatings(*jBr, "US", ageUs, sysUs, descUs);
        CHECK(sysUs == "ESRB");
        CHECK(ageUs == "T");
        CHECK(descUs.size() == 2 && descUs[0] == "Violence");

        // ESRB Mature
        auto jMature = json::parse(R"({
            "MarketProperties": [
                {
                    "ContentRatings": [
                        {
                            "ratingSystem": "ESRB",
                            "ratingId": "Mature 17+",
                            "ratingDescriptors": ["Blood and Gore", "Intense Violence"]
                        }
                    ]
                }
            ]
        })");
        CHECK(jMature.has_value());
        xcloud::parseContentRatings(*jMature, "US", ageUs, sysUs, descUs);
        CHECK(sysUs == "ESRB");
        CHECK(ageUs == "M");
        CHECK(descUs.size() == 2);

        // PEGI for GB / UK
        auto jPegi = json::parse(R"({
            "LocalizedProperties": [
                {
                    "ContentRatings": [
                        {
                            "RatingSystemId": "PEGI",
                            "RatingName": "18",
                            "RatingDescriptor": "Bad Language"
                        }
                    ]
                }
            ]
        })");
        CHECK(jPegi.has_value());
        std::string ageEu, sysEu;
        std::vector<std::string> descEu;
        xcloud::parseContentRatings(*jPegi, "GB", ageEu, sysEu, descEu);
        CHECK(sysEu == "PEGI");
        CHECK(ageEu == "18+");
        CHECK(descEu.size() == 1 && descEu[0] == "Bad Language");

        // Fallback when ClassInd not present in BR market -> falls back to ESRB
        std::string ageFb, sysFb;
        std::vector<std::string> descFb;
        xcloud::parseContentRatings(*jMature, "BR", ageFb, sysFb, descFb);
        CHECK(sysFb == "ESRB");
        CHECK(ageFb == "M");

        // Microsoft Game Pass v3 catalog API singular "ContentRating" object (RemoteHighSapphire0)
        auto jV3Br = json::parse(R"({
            "ContentRating": {
                "LongName": "16 anos",
                "Description": "Para maiores de 16 anos",
                "Age": 16,
                "RatingSystem": "DJCTQ",
                "LocalizedDescriptors": ["Violência"],
                "LocalizedInteractiveElements": ["Usuários Interagem", "Compras no Jogo"]
            }
        })");
        CHECK(jV3Br.has_value());
        std::string ageV3Br, sysV3Br;
        std::vector<std::string> descV3Br;
        xcloud::parseContentRatings(*jV3Br, "BR", ageV3Br, sysV3Br, descV3Br);
        CHECK(sysV3Br == "ClassInd");
        CHECK(ageV3Br == "16+");
        CHECK(descV3Br.size() == 3);
        CHECK(descV3Br[0] == "Violência");
        CHECK(descV3Br[1] == "Usuários Interagem");

        // V3 Livre
        auto jV3Livre = json::parse(R"({
            "ContentRating": {
                "LongName": "Livre",
                "Description": "Para todas as idades",
                "Age": 1,
                "RatingSystem": "DJCTQ",
                "LocalizedDescriptors": []
            }
        })");
        CHECK(jV3Livre.has_value());
        std::string ageLivre, sysLivre;
        std::vector<std::string> descLivre;
        xcloud::parseContentRatings(*jV3Livre, "BR", ageLivre, sysLivre, descLivre);
        CHECK(sysLivre == "ClassInd");
        CHECK(ageLivre == "Livre");

        // V3 ESRB
        auto jV3Esrb = json::parse(R"({
            "ContentRating": {
                "LongName": "TEEN",
                "Age": 13,
                "RatingSystem": "ESRB",
                "LocalizedDescriptors": ["Blood", "Mild Violence"]
            }
        })");
        CHECK(jV3Esrb.has_value());
        std::string ageV3Us, sysV3Us;
        std::vector<std::string> descV3Us;
        xcloud::parseContentRatings(*jV3Esrb, "US", ageV3Us, sysV3Us, descV3Us);
        CHECK(sysV3Us == "ESRB");
        CHECK(ageV3Us == "T");
        CHECK(descV3Us.size() == 2 && descV3Us[0] == "Blood");
    }

    // 2. Local Web Micro-Server
    {
        app::WebServer ws;
        CHECK(!ws.isRunning());
        std::string ip = ws.localIp();
        CHECK(!ip.empty());
        std::string url = ws.url();
        CHECK(url.find(":8080") != std::string::npos);

        // Test OPTIONS (CORS preflight)
        auto optRes = ws.handleRequest("OPTIONS", "/api/search", "");
        CHECK(optRes.status == 204);

        // Test GET / (HTML index)
        auto htmlRes = ws.handleRequest("GET", "/", "");
        CHECK(htmlRes.status == 200);
        CHECK(htmlRes.contentType.find("text/html") != std::string::npos);
        CHECK(htmlRes.body.find("PSBOX CLOUD GAMING") != std::string::npos);
        CHECK(htmlRes.body.find("logo-icon") != std::string::npos);
        CHECK(htmlRes.body.find("#107C41") != std::string::npos);
        CHECK(htmlRes.body.find("tabBtnGamePass") != std::string::npos);
        CHECK(htmlRes.body.find("tabBtnLibrary") != std::string::npos);
        CHECK(htmlRes.body.find("filtersGamePass") != std::string::npos);
        CHECK(htmlRes.body.find("filtersLibrary") != std::string::npos);
        CHECK(htmlRes.body.find("chipWishlist") != std::string::npos);
        CHECK(htmlRes.body.find("chipFree") != std::string::npos);
        CHECK(htmlRes.body.find("chipCheapest") != std::string::npos);
        CHECK(htmlRes.body.find("filterPlan") != std::string::npos);
        CHECK(htmlRes.body.find("filterConsoleGP") != std::string::npos);
        CHECK(htmlRes.body.find("chipMouseGP") != std::string::npos);
        CHECK(htmlRes.body.find("Teclado Remoto para Busca") != std::string::npos);
        CHECK(htmlRes.body.find("Remapeamento de Botões") != std::string::npos);
        CHECK(htmlRes.body.find("Remapeamento de Teclado") != std::string::npos);
        CHECK(htmlRes.body.find("subRemapKey") != std::string::npos);

        CHECK(htmlRes.body.find("navTab-search") != std::string::npos);
        CHECK(htmlRes.body.find("navTab-remap") != std::string::npos);
        CHECK(htmlRes.body.find("navTab-logs") != std::string::npos);
        CHECK(htmlRes.body.find("clamp(") != std::string::npos);
        CHECK(htmlRes.body.find("chipSelectModeGP") != std::string::npos);
        CHECK(htmlRes.body.find("chipSelectLangGP") != std::string::npos);
        CHECK(htmlRes.body.find("chipSelectModeLib") != std::string::npos);
        CHECK(htmlRes.body.find("chipSelectLangLib") != std::string::npos);

        // Test GET /api/settings
        auto getSetRes = ws.handleRequest("GET", "/api/settings", "");
        CHECK(getSetRes.status == 200);
        CHECK(getSetRes.contentType.find("application/json") != std::string::npos);
        auto jSet = json::parse(getSetRes.body);
        CHECK(jSet.has_value() && (*jSet)["ok"].asBool());
        CHECK((*jSet).has("keyMap") && (*jSet)["keyMap"].isArray());
        CHECK((*jSet).has("tab") && (*jSet)["tab"].isString());
        CHECK((*jSet).has("filters") && (*jSet)["filters"].isObject());

        // Test POST /api/search with query only
        auto searchRes = ws.handleRequest("POST", "/api/search", R"({"query":"Gears of War"})");
        CHECK(searchRes.status == 200);
        auto jSearch = json::parse(searchRes.body);
        CHECK(jSearch.has_value() && (*jSearch)["ok"].asBool());
        CHECK((*jSearch)["query"].str() == "Gears of War");

        // Test POST /api/search with tab and interactive filters (Game Pass)
        auto searchGPRes = ws.handleRequest("POST", "/api/search", R"({
            "query": "Halo",
            "tab": "gamepass",
            "filters": {
                "plan": 1,
                "console": 2,
                "mode": 1,
                "language": 2,
                "mouseKeyboard": true
            }
        })");
        CHECK(searchGPRes.status == 200);
        auto jSearchGP = json::parse(searchGPRes.body);
        CHECK(jSearchGP.has_value() && (*jSearchGP)["ok"].asBool());
        CHECK((*jSearchGP)["query"].str() == "Halo");
        CHECK((*jSearchGP)["tab"].str() == "gamepass");
        CHECK((*jSearchGP)["filters"]["plan"].asInt() == 1);
        CHECK((*jSearchGP)["filters"]["console"].asInt() == 2);
        CHECK((*jSearchGP)["filters"]["mode"].asInt() == 1);
        CHECK((*jSearchGP)["filters"]["language"].asInt() == 2);
        CHECK((*jSearchGP)["filters"]["mouseKeyboard"].asBool() == true);

        // Test POST /api/search with tab and interactive filters (Library / Meus jogos)
        auto searchLibRes = ws.handleRequest("POST", "/api/search", R"({
            "query": "Forza",
            "tab": "library",
            "filters": {
                "wishlist": true,
                "free": true,
                "cheapest": false,
                "mode": 3,
                "language": 1
            }
        })");
        CHECK(searchLibRes.status == 200);
        auto jSearchLib = json::parse(searchLibRes.body);
        CHECK(jSearchLib.has_value() && (*jSearchLib)["ok"].asBool());
        CHECK((*jSearchLib)["query"].str() == "Forza");
        CHECK((*jSearchLib)["tab"].str() == "library");
        CHECK((*jSearchLib)["filters"]["wishlist"].asBool() == true);
        CHECK((*jSearchLib)["filters"]["free"].asBool() == true);
        CHECK((*jSearchLib)["filters"]["cheapest"].asBool() == false);
        CHECK((*jSearchLib)["filters"]["mode"].asInt() == 3);
        CHECK((*jSearchLib)["filters"]["language"].asInt() == 1);

        // Test setSearchParams / getSearchParams hooks integration
        {
            std::string hookedQ;
            std::string hookedTab;
            json::Value hookedFilters = json::Value::object();
            app::setWebServerHooks({
                /*getSettings=*/nullptr,
                /*updateSettings=*/nullptr,
                /*getSearchQuery=*/nullptr,
                /*setSearchQuery=*/nullptr,
                /*setSearchParams=*/[&](const std::string* q, const std::string* tab, const json::Value* f) {
                    if (q) hookedQ = *q;
                    if (tab) hookedTab = *tab;
                    if (f && f->isObject()) hookedFilters = *f;
                },
                /*getSearchParams=*/[&](std::string& outQ, std::string& outTab, json::Value& outF) {
                    outQ = hookedQ;
                    outTab = hookedTab.empty() ? "gamepass" : hookedTab;
                    outF = hookedFilters;
                }
            });

            ws.handleRequest("POST", "/api/search", R"({"query":"Starfield","tab":"library","filters":{"wishlist":true,"console":1,"mode":2}})");
            CHECK(hookedQ == "Starfield");
            CHECK(hookedTab == "library");
            CHECK(hookedFilters["wishlist"].asBool() == true);
            CHECK(hookedFilters["console"].asInt() == 1);
            CHECK(hookedFilters["mode"].asInt() == 2);

            auto hookGetRes = ws.handleRequest("GET", "/api/settings", "");
            auto jHookGet = json::parse(hookGetRes.body);
            CHECK(jHookGet.has_value() && (*jHookGet)["query"].str() == "Starfield");
            CHECK((*jHookGet)["tab"].str() == "library");
            CHECK((*jHookGet)["filters"]["wishlist"].asBool() == true);
            CHECK((*jHookGet)["filters"]["mode"].asInt() == 2);

            // Test POST without tab: should resolve current tab from hooks
            auto noTabRes = ws.handleRequest("POST", "/api/search", R"({"query":"Flight Simulator"})");
            auto jNoTab = json::parse(noTabRes.body);
            CHECK(jNoTab.has_value() && (*jNoTab)["ok"].asBool());
            CHECK((*jNoTab)["tab"].str() == "library");  // resolved from hookedTab

            // Clean up hooks
            app::setWebServerHooks({});
        }

        // Test POST /api/settings (bad json, padMap only, and padMap + keyMap)
        auto badSetRes = ws.handleRequest("POST", "/api/settings", "not-a-json");
        CHECK(badSetRes.status == 400);

        auto goodSetRes = ws.handleRequest("POST", "/api/settings", R"({"padMap":{"version":1,"map":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]}})");
        CHECK(goodSetRes.status == 200);

        auto fullSetRes = ws.handleRequest("POST", "/api/settings", R"({
            "padMap": [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15],
            "keyMap": [87,0,83,0,65,0,68,0,38,0,40,0,37,0,39,0,32,0,67,0,82,0,69,0,81,0,70,0,242,0,241,0,16,0,86,243,9,0,27,0]
        })");
        CHECK(fullSetRes.status == 200);

        // Test GET /api/log
        auto logRes = ws.handleRequest("GET", "/api/log", "");
        CHECK(logRes.status == 200);
        CHECK(logRes.contentType.find("text/plain") != std::string::npos);
        CHECK(logRes.extraHeaders.find("Content-Disposition") != std::string::npos);

        // Test 404
        auto notFoundRes = ws.handleRequest("GET", "/api/unknown_endpoint", "");
        CHECK(notFoundRes.status == 404);

        // Test start/stop on a high ephemeral port (e.g. 18088)
        CHECK(ws.start(18088));
        CHECK(ws.isRunning());
        CHECK(ws.port() == 18088);
        ws.stop();
        CHECK(!ws.isRunning());
    }

    // 3. UI Integration: Web QR & Remote Search & Rating Badges
    {
        ui::Fonts dummyFonts{};
        ui::ImageCache images([] {});
        ui::AppUi app(dummyFonts, images);

        app.setWebUrl("http://192.168.1.100:8080");
        CHECK(app.webUrl() == "http://192.168.1.100:8080");

        app.setSearchQuery("Halo");
        CHECK(app.searchQuery() == "Halo");

        // Edge case: setSearchQuery while in streaming or modal mode should NOT yank out of screen
        app.showStreaming();
        CHECK(app.screen() == ui::Screen::Streaming);
        app.setSearchQuery("Forza");
        CHECK(app.screen() == ui::Screen::Streaming);  // Must remain streaming!
        CHECK(app.searchQuery() == "Forza");

        // Test Game Details with rating badge rendering and long descriptors line
        ui::GameTile tile;
        tile.name = "Forza Horizon 5";
        tile.publisher = "Xbox Game Studios";
        tile.description = "Your Ultimate Horizon Adventure awaits!";
        tile.ratingAge = "Livre";
        tile.ratingSystem = "ClassInd";
        tile.ratingDescriptors = {
            "Violência Fantasiosa", "Compras no Jogo", "Interação de Usuários",
            "Linguagem Apropriada", "Elementos Online Adicionais"
        };
        tile.categories = {"Corrida", "Mundo Aberto"};
        tile.playable = true;

        app.showDetails(tile);
        ui::Canvas c(1920, 1080);
        app.render(c, 1000);
        CHECK(c.width() == 1920 && c.height() == 1080);

        // Test other rating badge colors (12+, 14+, 16+, 18+)
        tile.ratingAge = "14+";
        app.showDetails(tile);
        app.render(c, 1000);

        tile.ratingAge = "18+";
        app.showDetails(tile);
        app.render(c, 1000);

        // Test Settings screen rendering with QR Panel
        app.showHome();
        ui::NavInput openOpt;
        openOpt.options = true;
        app.handle(openOpt);
        CHECK(app.screen() == ui::Screen::Settings);
        app.render(c, 1000);
        CHECK(c.width() == 1920);

        // Close Settings
        ui::NavInput back;
        back.back = true;
        app.handle(back);
        CHECK(app.screen() == ui::Screen::Home);
    }
}

void testWebStreamingAndGamepad() {
    xc::app::WebServer srv;
    bool streamingFlag = false;
    std::string currentQuery = "";
    std::string currentTab = "gamepass";
    json::Value currentFilters = json::Value::object();

    xc::app::WebServerHooks hooks;
    hooks.isStreaming = [&]() { return streamingFlag; };
    hooks.getSearchParams = [&](std::string& q, std::string& tab, json::Value& f) {
        q = currentQuery;
        tab = currentTab;
        f = currentFilters;
    };
    hooks.setSearchParams = [&](const std::string* q, const std::string* tab, const json::Value* f) {
        if (q) currentQuery = *q;
        if (tab) currentTab = *tab;
        if (f) currentFilters = *f;
    };
    xc::app::setWebServerHooks(hooks);

    // 1. GET /api/status when not streaming
    {
        auto res = srv.handleRequest("GET", "/api/status", "");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j && j->isObject());
        CHECK((*j)["ok"].asBool(false) == true);
        CHECK((*j)["streaming"].asBool(true) == false);
    }

    // 2. POST /api/search when not streaming should succeed
    {
        auto res = srv.handleRequest("POST", "/api/search", "{\"query\":\"halo\",\"tab\":\"gamepass\"}");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j && (*j)["ok"].asBool(false) == true);
        CHECK(currentQuery == "halo");
    }

    // 3. GET /api/status when streaming is active
    streamingFlag = true;
    {
        auto res = srv.handleRequest("GET", "/api/status", "");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j && (*j)["streaming"].asBool(false) == true);
    }

    // 4. POST /api/search when streaming must be blocked with 409 Conflict
    {
        auto res = srv.handleRequest("POST", "/api/search", "{\"query\":\"gears\",\"tab\":\"gamepass\"}");
        CHECK(res.status == 409);
        auto j = json::parse(res.body);
        CHECK(j && (*j)["ok"].asBool(true) == false);
        CHECK((*j)["error"].asString() == "streaming_active");
        CHECK(currentQuery == "halo"); // Search state was preserved and not changed
    }

    // 5. Virtual gamepad: Player 1 (main controller)
    {
        xc::app::resetVirtualGamepads();
        input::ControllerState s0;
        bool conn0 = false;
        xc::app::virtualGamepadState(0, s0, conn0);
        CHECK(!conn0);

        auto res = srv.handleRequest("POST", "/api/gamepad",
            "{\"player\":1,\"btnA\":true,\"leftStickX\":0.75,\"leftStickY\":-0.5,\"btnNexus\":true}");
        CHECK(res.status == 200);
        bool ok = xc::app::virtualGamepadState(0, s0, conn0);
        CHECK(ok);
        CHECK(conn0);
        CHECK(s0.btnA == true);
        CHECK(s0.btnB == false);
        CHECK(s0.btnNexus == true);
        CHECK(std::abs(s0.leftStickX - 0.75f) < 0.01f);
        CHECK(std::abs(s0.leftStickY - (-0.5f)) < 0.01f);
    }

    // 6. Virtual gamepad: Player 2 (co-op controller)
    {
        input::ControllerState s1;
        bool conn1 = false;
        xc::app::virtualGamepadState(1, s1, conn1);
        CHECK(!conn1);

        auto res = srv.handleRequest("POST", "/api/gamepad",
            "{\"player\":2,\"btnB\":true,\"triggerR2\":0.9,\"btnView\":true,\"dpadUp\":true}");
        CHECK(res.status == 200);
        bool ok = xc::app::virtualGamepadState(1, s1, conn1);
        CHECK(ok);
        CHECK(conn1);
        CHECK(s1.btnB == true);
        CHECK(s1.btnA == false);
        CHECK(s1.btnTouchpad == true); // btnView maps to btnTouchpad
        CHECK(s1.dpadUp == true);
        CHECK(std::abs(s1.triggerR2 - 0.9f) < 0.01f);
        // Test L3 and R3 buttons on Virtual Gamepad
        input::ControllerState s0;
        bool conn0 = false;
        auto resL3R3 = srv.handleRequest("POST", "/api/gamepad",
            "{\"player\":1,\"btnL3\":true,\"btnR3\":true}");
        CHECK(resL3R3.status == 200);
        bool okL3R3 = xc::app::virtualGamepadState(0, s0, conn0);
        CHECK(okL3R3);
        CHECK(s0.btnL3 == true);
        CHECK(s0.btnR3 == true);
    }

    // 7. Reset virtual gamepads
    {
        xc::app::resetVirtualGamepads();
        input::ControllerState s0, s1;
        bool conn0 = false, conn1 = false;
        xc::app::virtualGamepadState(0, s0, conn0);
        xc::app::virtualGamepadState(1, s1, conn1);
        CHECK(!conn0);
        CHECK(!conn1);
    }

    // 8. Test AppUi::detailWanted prevents infinite re-fetching when detailInfo_ exists
    {
        ui::Fonts dummyFonts{};
        ui::ImageCache images([] {});
        ui::AppUi uiApp(dummyFonts, images);
        ui::GameTile tile;
        tile.productId = "PROD_UNRATED";
        tile.name = "Unrated Game";
        tile.description = "Short desc";
        tile.heroUrl = "http://example.com/hero.jpg";
        tile.ratingAge = ""; // Empty age rating

        uiApp.showDetails(tile);
        // First wanted call should ask for details
        std::string asked = uiApp.detailWanted(1000);
        CHECK(asked == "PROD_UNRATED");

        // Simulate priceLoop fetching details and storing in detailInfo_
        uiApp.setDetailInfo("PROD_UNRATED", "Longer desc", "Publisher X", {"Indie"}, "http://example.com/hero.jpg",
                            "", "", {});

        // Subsequent detailWanted calls (even after 5000ms) must NOT ask again
        std::string askedAfter = uiApp.detailWanted(10000);
        CHECK(askedAfter.empty());
    }

    xc::app::setWebServerHooks({});
}

static void testWebSettingsAndStreamMenu() {
    xc::app::WebServer srv;
    bool streamMenuUpdated = false;
    std::string lastStreamAction;
    json::Value lastStreamUpdate;
    json::Value currentSettings = json::Value::object();
    currentSettings.set("bitrateMbps", 25);
    currentSettings.set("resolution", "1080p");
    currentSettings.set("voiceRouting", 1);

    xc::app::WebServerHooks hooks;
    hooks.isStreaming = []() { return true; };
    hooks.getSettings = [&](json::Value& out) {
        out = currentSettings;
    };
    hooks.updateSettings = [&](const json::Value& root) {
        if (root.has("bitrateMbps")) currentSettings.set("bitrateMbps", root["bitrateMbps"].asInt());
        if (root.has("voiceRouting")) currentSettings.set("voiceRouting", root["voiceRouting"].asInt());
        return true;
    };
    hooks.getTelemetry = [](json::Value& stats) {
        stats = json::Value::object();
        stats.set("fps", 60.0);
        stats.set("decodeMs", 3.2);
        stats.set("rtt", 14.5);
        stats.set("packetLoss", 0.0);
        stats.set("bitrateKbps", 15200);
    };
    hooks.getStreamMenuState = [](json::Value& state) {
        state = json::Value::object();
        state.set("volume", 80);
        state.set("micMuted", false);
        state.set("sharpness", 2);
        state.set("casSharpness", 3);
        state.set("deband", true);
        state.set("stats", true);
        state.set("statsMode", 1);
        state.set("touchPadMouse", false);
        state.set("virtualGamepad", true);
    };
    hooks.updateStreamMenu = [&](const json::Value& update) {
        streamMenuUpdated = true;
        lastStreamUpdate = update;
        return true;
    };
    hooks.streamAction = [&](const std::string& action) {
        lastStreamAction = action;
    };
    xc::app::setWebServerHooks(hooks);

    // 1. GET /api/settings
    {
        auto res = srv.handleRequest("GET", "/api/settings", "");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j && j->isObject());
        CHECK((*j)["bitrateMbps"].asInt() == 25);
        CHECK((*j)["resolution"].str() == "1080p");
        CHECK((*j)["voiceRouting"].asInt() == 1);
    }

    // 2. POST /api/settings
    {
        auto res = srv.handleRequest("POST", "/api/settings", R"({"bitrateMbps":40,"voiceRouting":2})");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j && (*j)["ok"].asBool() == true);
        CHECK(currentSettings["bitrateMbps"].asInt() == 40);
        CHECK(currentSettings["voiceRouting"].asInt() == 2);
    }

    // 3. GET /api/stats
    {
        auto res = srv.handleRequest("GET", "/api/stats", "");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j && j->isObject());
        CHECK((*j)["fps"].asNumber() == 60.0);
        CHECK((*j)["decodeMs"].asNumber() == 3.2);
        CHECK((*j)["rtt"].asNumber() == 14.5);
    }

    // 4. GET /api/stream
    {
        auto res = srv.handleRequest("GET", "/api/stream", "");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j && j->isObject());
        CHECK((*j)["volume"].asInt() == 80);
        CHECK((*j)["micMuted"].asBool() == false);
        CHECK((*j)["sharpness"].asInt() == 2);
        CHECK((*j)["casSharpness"].asInt() == 3);
        CHECK((*j)["virtualGamepad"].asBool() == true);
    }

    // 5. POST /api/stream with action
    {
        lastStreamAction.clear();
        auto res = srv.handleRequest("POST", "/api/stream", R"({"action":"disconnect"})");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j && (*j)["ok"].asBool() == true);
        CHECK(lastStreamAction == "disconnect");
    }

    // 6. POST /api/stream with stream menu update
    {
        streamMenuUpdated = false;
        auto res = srv.handleRequest("POST", "/api/stream", R"({"volume":65,"sharpness":1})");
        CHECK(res.status == 200);
        CHECK(streamMenuUpdated == true);
        CHECK(lastStreamUpdate["volume"].asInt() == 65);
        CHECK(lastStreamUpdate["sharpness"].asInt() == 1);
    }

    // 7. Error handling: invalid JSON
    {
        auto res = srv.handleRequest("POST", "/api/stream", "invalid{json");
        CHECK(res.status == 400);
    }

    xc::app::setWebServerHooks({});
}

static void testVoiceRouting() {
    // Default should be 0 (All Audio to TV / Main)
    media::audioSetVoiceRouting(0);
    CHECK(media::audioVoiceRouting() == 0);

    // Test routing switching: 1 = Pad Voice / Speaker
    media::audioSetVoiceRouting(1);
    CHECK(media::audioVoiceRouting() == 1);

    // Values > 1 clamp to 1, < 0 clamp to 0
    media::audioSetVoiceRouting(5);
    CHECK(media::audioVoiceRouting() == 1);

    media::audioSetVoiceRouting(-1);
    CHECK(media::audioVoiceRouting() == 0);

    // Test Settings JSON persistence
    std::string testPath = "/tmp/xcloud-tests-voice.json";
    app::Settings s;
    s.voiceRouting = 1;
    CHECK(s.save(testPath));

    app::Settings loaded;
    CHECK(loaded.load(testPath));
    CHECK(loaded.voiceRouting == 1);
    std::remove(testPath.c_str());

    // Test playVoiceAudio fallback on platforms without pad speaker
    std::vector<float> samplePcm(480 * 2, 0.1f);
    CHECK(media::playVoiceAudio(samplePcm.data(), 480));

    // Test SettingsChoice round-trip
    app::Settings st;
    st.voiceRouting = 1;
    ui::SettingsChoice ch = ui::choiceFromSettings(st);
    CHECK(ch.voiceRouting == 1);
    ch.voiceRouting = 0;
    ui::applyChoice(ch, st);
    CHECK(st.voiceRouting == 0);
}

static void testUsbPad8BitDo() {
    input::ControllerState state{};

    // Null and undersized buffers
    CHECK(!input::parse8BitDoDInputReport(nullptr, 0, state));
    uint8_t shortBuf[10] = {0};
    CHECK(!input::parse8BitDoDInputReport(shortBuf, sizeof(shortBuf), state));

    // Centered sticks, neutral hat, no buttons
    uint8_t neutralReport[16] = {
        128, 128, 128, 128,  // Left X, Y, Right X, Y (center)
        0, 0,                // L2, R2 trigger
        0x08,                // Hat neutral (0x08 > 7, no bitmask)
        0x00,                // Action buttons
        0x00,                // System buttons
        0, 0, 0, 0, 0, 0, 0  // Reserved / telemetry
    };
    CHECK(input::parse8BitDoDInputReport(neutralReport, sizeof(neutralReport), state));
    CHECK(state.connected == true);
    CHECK(std::abs(state.leftStickX) < 0.001f);
    CHECK(std::abs(state.leftStickY) < 0.001f);
    CHECK(std::abs(state.rightStickX) < 0.001f);
    CHECK(std::abs(state.rightStickY) < 0.001f);
    CHECK(state.triggerL2 == 0.0f);
    CHECK(state.triggerR2 == 0.0f);
    CHECK(!state.btnA && !state.btnB && !state.btnX && !state.btnY);
    CHECK(!state.dpadUp && !state.dpadDown && !state.dpadLeft && !state.dpadRight);

    // Deflected sticks & triggers
    uint8_t motionReport[16] = {
        255, 0, 0, 255,      // Left stick right/up, Right stick left/down
        255, 128,            // L2 fully pressed, R2 half
        0x08,
        0x00,
        0x00,
        0, 0, 0, 0, 0, 0, 0
    };
    CHECK(input::parse8BitDoDInputReport(motionReport, sizeof(motionReport), state));
    CHECK(state.leftStickX > 0.95f);
    CHECK(state.leftStickY < -0.95f);
    CHECK(state.rightStickX < -0.95f);
    CHECK(state.rightStickY > 0.95f);
    CHECK(state.triggerL2 == 1.0f);
    CHECK(std::abs(state.triggerR2 - (128.0f / 255.0f)) < 0.02f);

    // Action buttons (data[7])
    uint8_t btnReport[16] = {
        128, 128, 128, 128,
        0, 0,
        0x08,
        0x01 | 0x02 | 0x04 | 0x08 | 0x10 | 0x20, // A, B, X, Y, L1, R1
        0x00,
        0, 0, 0, 0, 0, 0, 0
    };
    CHECK(input::parse8BitDoDInputReport(btnReport, sizeof(btnReport), state));
    CHECK(state.btnA && state.btnB && state.btnX && state.btnY && state.btnL1 && state.btnR1);

    // D-Pad Hat Switch (0 = Up, 2 = Right, 4 = Down, 6 = Left)
    uint8_t dpadReports[4][16] = {
        {128, 128, 128, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // Up
        {128, 128, 128, 128, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // Right
        {128, 128, 128, 128, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // Down
        {128, 128, 128, 128, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0}  // Left
    };
    CHECK(input::parse8BitDoDInputReport(dpadReports[0], 16, state) && state.dpadUp && !state.dpadDown);
    CHECK(input::parse8BitDoDInputReport(dpadReports[1], 16, state) && state.dpadRight && !state.dpadLeft);
    CHECK(input::parse8BitDoDInputReport(dpadReports[2], 16, state) && state.dpadDown && !state.dpadUp);
    CHECK(input::parse8BitDoDInputReport(dpadReports[3], 16, state) && state.dpadLeft && !state.dpadRight);

    // System buttons (data[8])
    uint8_t sysReport[16] = {
        128, 128, 128, 128,
        0, 0,
        0x08,
        0x00,
        0x01 | 0x02 | 0x04 | 0x08 | 0x10, // Touchpad/Select, Options/Start, Nexus/Home, L3, R3
        0, 0, 0, 0, 0, 0, 0
    };
    CHECK(input::parse8BitDoDInputReport(sysReport, sizeof(sysReport), state));
    CHECK(state.btnTouchpad && state.btnOptions && state.btnNexus && state.btnL3 && state.btnR3);

    // Test injection & polling API
    input::padUsbInit();
    input::padUsbInjectReportForTesting(sysReport, sizeof(sysReport));
    input::ControllerState polled{};
    CHECK(input::pollUsbPad(polled));
    CHECK(polled.btnNexus == true);
    CHECK(polled.btnL3 == true);

    input::padUsbInjectReportForTesting(nullptr, 0);
    input::padUsbShutdown();
}

static void testDesktopMouseNav() {
    ui::Fonts dummyFonts{};
    ui::ImageCache images([] {});
    ui::AppUi uiApp(dummyFonts, images);

    ui::GameRow row;
    row.title = "Test Row";
    row.isGrid = false;
    ui::GameTile tile1;
    tile1.productId = "GAME_001";
    tile1.name = "First Game";
    ui::GameTile tile2;
    tile2.productId = "GAME_002";
    tile2.name = "Second Game";
    row.tiles.push_back(tile1);
    row.tiles.push_back(tile2);
    uiApp.setRows({row});
    uiApp.showHome();

    // 1. In default PS5 mode (caps_.desktopWindow == false), mouse events must be ignored
    {
        auto evMove = uiApp.onMouseMove(300.0f, 300.0f);
        CHECK(evMove.action == ui::Action::None);
        auto evClick = uiApp.onMouseDown(300.0f, 300.0f, 1);
        CHECK(evClick.action == ui::Action::None);
        auto evWheel = uiApp.onMouseWheel(0.0f, 1.0f);
        CHECK(evWheel.action == ui::Action::None);
    }

    // 2. Enable desktopWindow capability (Desktop / Steam Deck mode)
    ui::UiCaps caps{};
    caps.desktopWindow = true;
    uiApp.setCaps(caps);

    // 3. Hovering over a card updates focus
    {
        // First card is at kMargin (96) + 0, cardY = kRowTop (470) + 48 = 518
        auto ev = uiApp.onMouseMove(120.0f, 550.0f);
        CHECK(ev.action == ui::Action::None);
    }

    // 4. Clicking a card opens details
    {
        auto ev = uiApp.onMouseDown(120.0f, 550.0f, 1);
        CHECK(ev.action == ui::Action::None);
        // AppUi should now be showing details
        CHECK(uiApp.detailWanted(100) == "GAME_001");
    }

    // 5. Right-click (button 3) returns back to Home screen
    {
        auto ev = uiApp.onMouseDown(500.0f, 500.0f, 3);
        CHECK(ev.action == ui::Action::None);
        // After returning to Home, detailWanted should be empty
        CHECK(uiApp.detailWanted(100).empty());
    }

    // 6. Mouse wheel scrolling
    {
        auto evWheel = uiApp.onMouseWheel(0.0f, 2.0f);
        CHECK(evWheel.action == ui::Action::None);
    }

    // 7. Avatar / profile click opens Settings screen
    {
        auto evClick = uiApp.onMouseDown(1920 - 96 - 20, 60.0f, 1);
        CHECK(uiApp.screen() == ui::Screen::Settings);
    }

    // 8. In Settings, clicking a dropdown setting opens the dropdown
    {
        // Language row (row 0) at y = 190
        auto evClick = uiApp.onMouseDown(200.0f, 200.0f, 1);
        // First right click closes the dropdown
        auto evClose = uiApp.onMouseDown(200.0f, 200.0f, 3);
        CHECK(uiApp.screen() == ui::Screen::Settings);
        // Second right click exits back to Home
        auto evBack = uiApp.onMouseDown(200.0f, 200.0f, 3);
        CHECK(uiApp.screen() == ui::Screen::Home);
    }
}

int main() {
    testBlock3Features();
    testWishlist();
    testAchievements();
    testBlock2Features();
    testPurchase();
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
    testTokenRefresh();
    testGyro();
    testReport();
    testCrashGuard();
    testBuiltinCa();
    testLoginCountry();
    testPrivacyAndRobustness();
    testHttpKeepAlive();
    testStoreLeftovers();
    testLibraryRowsOnce();
    testHostPacer();
    testSubscriptionPlan();
    testMicrosoftHost();
    testAudioEncoder();
    testH264Resync();
    testH264PaddingPacket();
    testAudioDecoderEmptyPacket();
    testAudioDecoderPlc();
    testAudioGapLossConcealment();
    testAutoDeband();
    testControllerTuning();
    testUpdater();
    testAccentColor();
    testVersions();
    testStreamMenu();
    testSplashInputBlocking();
    testSettingsMigration();
    testSettingsChoiceRoundTrip();
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
    testBlock1Features();
    testVideoDecoder();
    testDrawNv12();
    testWebStreamingAndGamepad();
    testWebSettingsAndStreamMenu();
    testVoiceRouting();
    testUsbPad8BitDo();
    testDesktopMouseNav();
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
