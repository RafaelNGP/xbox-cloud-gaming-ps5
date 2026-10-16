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
#include "ui/stream_menu.h"
#include "ui/strings.h"
#include "util/json.h"
#include "auth/auth_manager.h"
#include "stream/h264_receiver.h"
#include "input/kbm.h"
#include "input/keyboard_nav.h"
#include "platform/platform.h"
#include "auth/msa.h"
#include "ui/image_cache.h"
#include "xcloud/gssv.h"
#include "xcloud/titlehub.h"
#include "xcloud/prices.h"
#include "xcloud/regions.h"

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
    CHECK(p[30] == 1 && p[33] == 0);        // physicality LE
    CHECK(p[34] == 0 && p[37] == 1);        // virtual physicality BE

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
    s.perGame["GAME1"] = p1;

    // Now GAME1 returns custom profile, while GAME2 still returns defaults.
    auto prof1Custom = s.profileForGame("GAME1", "", &hasCustom);
    CHECK(hasCustom);
    CHECK(prof1Custom.sharpness == 3 && prof1Custom.upscaler == 3 && prof1Custom.circleConfirms);
    CHECK(prof1Custom.deadzoneLeft == 5 && prof1Custom.triggerStrength == 1);

    hasCustom = true;
    auto prof2 = s.profileForGame("GAME2", "", &hasCustom);
    CHECK(!hasCustom);
    CHECK(prof2.sharpness == 1 && !prof2.circleConfirms);

    // Title ID fallback when productId is empty.
    s.perGame["CONSOLE_SERVER_1"] = p1;
    auto consoleProf = s.profileForGame("", "CONSOLE_SERVER_1", &hasCustom);
    CHECK(hasCustom);
    CHECK(consoleProf.sharpness == 3 && consoleProf.circleConfirms);

    // Save to disk and reload.
    CHECK(s.save(path));
    xc::app::Settings reloaded;
    CHECK(reloaded.load(path));
    CHECK(reloaded.perGame.size() == 2);
    auto loadedP1 = reloaded.profileForGame("GAME1", "", &hasCustom);
    CHECK(hasCustom);
    CHECK(loadedP1.sharpness == 3 && loadedP1.upscaler == 3 && loadedP1.circleConfirms);
    CHECK(loadedP1.highRefresh);
    CHECK(loadedP1.deadzoneLeft == 5 && loadedP1.deadzoneRight == 8 && loadedP1.triggerStrength == 1);

    // Remove game profile restores default.
    reloaded.perGame.erase("GAME1");
    auto afterErase = reloaded.profileForGame("GAME1", "", &hasCustom);
    CHECK(!hasCustom);
    CHECK(afterErase.sharpness == reloaded.sharpness);

    std::remove(path.c_str());
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
    xc::stream::H264Receiver rx([&] { ++keyframeAsks; }, stats);
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

int main() {
    testNullBackends();
    testToasts();
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
    testAutoDeband();
    testControllerTuning();
    testUpdater();
    testAccentColor();
    testVersions();
    testStreamMenu();
    testSettingsMigration();
    testMicGain();
    testPerGameSettings();
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
    testKeyMap();
    testTeredo();
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
