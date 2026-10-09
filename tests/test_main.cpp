// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Offline unit tests for the portable core (no network).
#include "app/auto_deband.h"
#include "app/settings.h"
#include "app/update_check.h"
#include "app/updater.h"
#include "input/tuning.h"
#include "net/http.h"
#include "platform/platform.h"
#include "stream/input_packet.h"
#include "stream/stream_session.h"
#include "ui/app_ui.h"
#include "ui/accent_color.h"
#include "ui/stream_menu.h"
#include "ui/strings.h"
#include "util/json.h"
#include "xcloud/prices.h"
#include "xcloud/regions.h"

#include <cmath>
#include <cstdio>
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
    press([](xc::ui::NavInput& n) { n.down = true; });  // "Leave game", last
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::Leave);
    CHECK(!menu.isOpen());
    menu.open(1, true);
    CHECK(press([](xc::ui::NavInput& n) { n.back = true; }) == MenuAction::Close);
    // 1440p not offered: a saved 1440p shows as 1080p, and the row cycles 720p / 1080p.
    menu.open(2, false, 0, 1, 0, false, false);
    CHECK(menu.resolution() == 0);
    for (int i = 0; i < 5; ++i) press([](xc::ui::NavInput& n) { n.down = true; });  // to the resolution
    press([](xc::ui::NavInput& n) { n.right = true; });
    CHECK(menu.resolution() == 1);  // 1080p -> wraps to 720p
    press([](xc::ui::NavInput& n) { n.right = true; });
    CHECK(menu.resolution() == 0);  // back to 1080p, never 1440p
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

int main() {
    testAutoDeband();
    testControllerTuning();
    testUpdater();
    testAccentColor();
    testVersions();
    testStreamMenu();
    testSettingsMigration();
    testStrings();
    testRegions();
    testPrices();
    testJson();
    testUrl();
    testInputPacket();
    testTeredo();
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
