// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Offline unit tests for the portable core (no network).
#include "net/http.h"
#include "stream/input_packet.h"
#include "stream/stream_session.h"
#include "ui/app_ui.h"
#include "ui/stream_menu.h"
#include "ui/strings.h"
#include "util/json.h"
#include "xcloud/prices.h"
#include "xcloud/regions.h"

#include <cstdio>
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
    press([](xc::ui::NavInput& n) { n.down = true; });
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::Stats);
    CHECK(menu.statsOn());
    press([](xc::ui::NavInput& n) { n.down = true; });
    press([](xc::ui::NavInput& n) { n.left = true; });  // 1080p -> 720p
    CHECK(menu.resolution() == 1);
    press([](xc::ui::NavInput& n) { n.left = true; });  // wraps to 1440p
    CHECK(menu.resolution() == 2);
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::Resolution);
    press([](xc::ui::NavInput& n) { n.up = true; });
    press([](xc::ui::NavInput& n) { n.up = true; });
    press([](xc::ui::NavInput& n) { n.up = true; });  // wraps to "Leave game"
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::Leave);
    CHECK(!menu.isOpen());
    menu.open(1, true);
    CHECK(press([](xc::ui::NavInput& n) { n.back = true; }) == MenuAction::Close);
}

int main() {
    testStreamMenu();
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
