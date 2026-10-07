// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "xcloud/gssv.h"

#include "auth/msa.h"
#include "net/http.h"
#include "util/json.h"
#include "platform/platform.h"
#include "util/log.h"

#include <cstdlib>
#include <ctime>
#include <map>

namespace xc::xcloud {

namespace {
constexpr const char* kClientHeader = "XboxComBrowser";

// Sent like the Windows Xbox app does: "Windows" plus a 1080p display is what
// gets a 1080p stream; an Android device with a 720p display gets 720p.
const std::string& deviceInfo(Resolution res) {
    auto build = [](bool hd, int w = 1920, int h = 1080) {
        json::Value env = json::Value::object();
        env.set("clientAppId", "Microsoft.GamingApp");
        env.set("clientAppType", "native");
        env.set("clientAppVersion", "2203.1001.4.0");
        env.set("clientSdkVersion", "8.5.2");
        env.set("httpEnvironment", "prod");
        env.set("sdkInstallId", "");
        json::Value app = json::Value::object();
        app.set("env", env);
        json::Value hw = json::Value::object();
        hw.set("make", hd ? "Microsoft" : "Google");
        hw.set("model", hd ? "Surface Pro" : "Pixel");
        hw.set("sdktype", "native");
        json::Value os = json::Value::object();
        os.set("name", hd ? "Windows 11" : "Android");
        os.set("ver", hd ? "22631.2715" : "14");
        os.set("platform", hd ? "desktop" : "phone");
        json::Value dims = json::Value::object();
        dims.set("widthInPixels", hd ? w : 1280);
        dims.set("heightInPixels", hd ? h : 720);
        json::Value dpi = json::Value::object();
        dpi.set("dpiX", 1);
        dpi.set("dpiY", 1);
        json::Value display = json::Value::object();
        display.set("dimensions", dims);
        display.set("pixelDensity", dpi);
        json::Value dev = json::Value::object();
        dev.set("hw", hw);
        dev.set("os", os);
        dev.set("displayInfo", display);
        json::Value v = json::Value::object();
        v.set("appInfo", app);
        v.set("dev", dev);
        return v.dump();
    };
    // 1440p: the xbox.com web client (SDK 10.6.62) in Edge on a 1440p display,
    // field for field as its X-MS-Device-Info interceptor builds it.
    static const std::string qhd = [] {
        json::Value env = json::Value::object();
        env.set("clientAppId", "www.xbox.com");
        env.set("clientAppType", "browser");
        env.set("clientAppVersion", "1.0.2609.0802");
        env.set("clientSdkVersion", "10.6.62");
        env.set("httpEnvironment", "prod");
        env.set("sdkInstallId", "");
        json::Value app = json::Value::object();
        app.set("env", env);
        json::Value dims = json::Value::object();
        dims.set("heightInPixels", 1440);
        dims.set("widthInPixels", 2560);
        json::Value dpi = json::Value::object();
        dpi.set("dpiX", 1);
        dpi.set("dpiY", 1);
        json::Value display = json::Value::object();
        display.set("dimensions", dims);
        display.set("pixelDensity", dpi);
        json::Value browser = json::Value::object();
        browser.set("browserName", "edge");
        browser.set("browserVersion", "141.0.0.0");
        json::Value hw = json::Value::object();
        hw.set("make", "Microsoft");
        hw.set("model", "Windows");
        hw.set("platformType", "desktop");
        hw.set("sdkType", "web");
        json::Value os = json::Value::object();
        os.set("name", "windows");
        os.set("ver", "10.0");
        os.set("platform", "desktop");
        json::Value dev = json::Value::object();
        dev.set("displayInfo", display);
        dev.set("browser", browser);
        dev.set("hw", hw);
        dev.set("os", os);
        json::Value v = json::Value::object();
        v.set("appInfo", app);
        v.set("dev", dev);
        return v.dump();
    }();
    static const std::string hd = build(true), sd = build(false);
    return res == Resolution::P720 ? sd : res == Resolution::P1440 ? qhd : hd;
}

std::string describe(const net::Response& r) {
    if (r.status == 0) return r.error;
    std::string s = "HTTP " + std::to_string(r.status);
    if (!r.body.empty()) s += ": " + r.body.substr(0, 300);
    return s;
}

SessionState parseState(const std::string& s) {
    if (s == "Provisioning") return SessionState::Provisioning;
    if (s == "WaitingForResources") return SessionState::WaitingForResources;
    if (s == "ReadyToConnect") return SessionState::ReadyToConnect;
    if (s == "Provisioned") return SessionState::Provisioned;
    if (s == "Failed") return SessionState::Failed;
    return SessionState::Unknown;
}

// The SDP/ICE endpoints wrap their payload as a JSON *string* inside
// "exchangeResponse"; unwrap it.
std::optional<json::Value> exchangePayload(const json::Value& v) {
    const auto& ex = v["exchangeResponse"];
    if (ex.isString()) return json::parse(ex.asString());
    if (ex.isObject() || ex.isArray()) return ex;
    return std::nullopt;
}
}  // namespace

const char* toString(SessionState s) {
    switch (s) {
        case SessionState::Provisioning: return "Provisioning";
        case SessionState::WaitingForResources: return "WaitingForResources";
        case SessionState::ReadyToConnect: return "ReadyToConnect";
        case SessionState::Provisioned: return "Provisioned";
        case SessionState::Failed: return "Failed";
        default: return "Unknown";
    }
}

const Region* GssvLogin::defaultRegion() const {
    for (const auto& r : regions)
        if (r.isDefault) return &r;
    return regions.empty() ? nullptr : &regions.front();
}

std::string GssvClient::url(const std::string& path) const { return region_.baseUri + path; }

std::string GssvClient::sessionUrl(const std::string& suffix) const {
    std::string p = sessionPath_;
    if (!p.empty() && p.front() != '/') p.insert(p.begin(), '/');
    return region_.baseUri + p + suffix;
}

static net::Request authed(Resolution res, const GssvLogin& l, std::string method, std::string url,
                           std::string body = {}) {
    net::Request req;
    req.method = std::move(method);
    req.url = std::move(url);
    req.body = std::move(body);
    req.headers = {{"Authorization", "Bearer " + l.gsToken},
                   {"x-gssv-client", kClientHeader},
                   {"X-MS-Device-Info", deviceInfo(res)}};
    if (!req.body.empty() || req.method == "POST") req.headers.emplace_back("Content-Type", "application/json");
    return req;
}

bool GssvClient::login(const auth::XblToken& gssvXsts, std::string& err) {
    json::Value body = json::Value::object();
    body.set("token", gssvXsts.token);
    body.set("offeringId", offering_);

    net::Request req;
    req.method = "POST";
    req.url = "https://" + offering_ + ".gssv-play-prod.xboxlive.com/v2/login/user";
    req.headers = {{"Content-Type", "application/json"}, {"x-gssv-client", kClientHeader}};
    req.body = body.dump();
    auto r = net::perform(req);
    auto j = json::parse(r.body);
    if (!r.ok() || !j || !(*j)["gsToken"].isString()) {
        err = "xCloud login failed: " + describe(r);
        if (r.status == 403) err += " (no Game Pass Ultimate, or xCloud is not available in this region)";
        return false;
    }
    if (std::getenv("XC_DUMP_LOGIN")) {
        // Diagnostics: everything except the token itself.
        json::Value copy = *j;
        copy.set("gsToken", "<redacted>");
        XC_LOGI("login response: %s", copy.dump().substr(0, 6000).c_str());
    }
    login_ = {};
    login_.gsToken = (*j)["gsToken"].str();
    login_.market = (*j)["market"].str();
    login_.expiresAt = auth::unixNow() + (*j)["durationInSeconds"].asInt(3600);
    for (const auto& r : (*j)["offeringSettings"]["regions"].items()) {
        Region reg;
        reg.name = r["name"].str();
        reg.baseUri = r["baseUri"].str();
        reg.isDefault = r["isDefault"].asBool();
        if (!reg.baseUri.empty()) login_.regions.push_back(std::move(reg));
    }
    if (const Region* def = login_.defaultRegion()) {
        region_ = *def;
    } else {
        err = "xCloud login returned no regions";
        return false;
    }
    XC_LOGI("xCloud login ok: market=%s region=%s (%zu regions)", login_.market.c_str(), region_.name.c_str(),
            login_.regions.size());
    return true;
}

bool GssvClient::listTitles(std::vector<Title>& out, std::string& err, bool recentOnly) {
    out.clear();
    uint64_t started = platform::nowMs();
    std::string continuation;
    do {
        std::string path = recentOnly ? "/v2/titles/mru?mr=25" : "/v2/titles?mr=200";
        if (!continuation.empty()) path += "&ct=" + net::urlEncode(continuation);
        auto r = net::perform(authed(resolution_, login_, "GET", url(path)));
        auto j = json::parse(r.body);
        if (!r.ok() || !j) {
            err = "title list failed: " + describe(r);
            return false;
        }
        for (const auto& t : (*j)["results"].items()) {
            Title title;
            title.titleId = t["titleId"].str();
            title.productId = t["details"]["productId"].str();
            title.hasEntitlement = t["details"]["hasEntitlement"].asBool();
            if (!title.titleId.empty()) out.push_back(std::move(title));
        }
        continuation = recentOnly ? std::string() : (*j)["continuationToken"].str();
    } while (!continuation.empty() && out.size() < 5000);
    XC_LOGI("title list%s: %zu titles in %llu ms", recentOnly ? " (recent)" : "", out.size(),
            static_cast<unsigned long long>(platform::nowMs() - started));
    return true;
}

bool GssvClient::hydrateTitles(std::vector<Title>& titles, const std::string& market, const std::string& lang,
                               std::string& err) {
    std::map<std::string, std::vector<Title*>> byProduct;
    for (auto& t : titles)
        if (!t.productId.empty()) byProduct[t.productId].push_back(&t);

    std::vector<std::string> ids;
    for (const auto& [id, _] : byProduct) ids.push_back(id);

    constexpr size_t kBatch = 20;
    for (size_t i = 0; i < ids.size(); i += kBatch) {
        std::string list;
        for (size_t k = i; k < ids.size() && k < i + kBatch; ++k) {
            if (!list.empty()) list += ',';
            list += ids[k];
        }
        net::Request req;
        req.url = "https://displaycatalog.mp.microsoft.com/v7.0/products?bigIds=" + list +
                  "&market=" + market + "&languages=" + lang;
        auto r = net::perform(req);
        auto j = json::parse(r.body);
        if (!r.ok() || !j) {
            err = "catalog lookup failed: " + describe(r);
            return false;
        }
        for (const auto& p : (*j)["Products"].items()) {
            auto it = byProduct.find(p["ProductId"].str());
            if (it == byProduct.end()) continue;
            const auto& loc = p["LocalizedProperties"][0];
            std::string name = loc["ProductTitle"].str();
            std::string image;
            for (const char* purpose : {"Poster", "BoxArt", "Tile"}) {
                for (const auto& img : loc["Images"].items()) {
                    if (img["ImagePurpose"].str() == purpose) {
                        image = img["Uri"].str();
                        break;
                    }
                }
                if (!image.empty()) break;
            }
            if (image.rfind("//", 0) == 0) image = "https:" + image;
            for (Title* t : it->second) {
                t->name = name;
                t->imageUrl = image;
            }
        }
    }
    return true;
}

bool GssvClient::startSession(const std::string& titleId, const std::string& locale, std::string& err) {
    json::Value settings = json::Value::object();
    settings.set("nanoVersion", "V3;WebrtcTransport.dll");
    settings.set("enableTextToSpeech", false);
    settings.set("highContrast", 0);
    settings.set("locale", locale);
    settings.set("useIceConnection", false);
    settings.set("timezoneOffsetMinutes", 0);
    settings.set("sdkType", "web");
    settings.set("osName", resolution_ == Resolution::P720 ? "android" : "windows");

    json::Value body = json::Value::object();
    body.set("clientSessionId", "");
    body.set("titleId", titleId);
    body.set("systemUpdateGroup", "");
    body.set("settings", settings);
    body.set("serverId", "");
    body.set("fallbackRegionNames", json::Value::array());

    std::string kind = offering_ == "xhome" ? "home" : "cloud";
    auto r = net::perform(authed(resolution_, login_, "POST", url("/v5/sessions/" + kind + "/play"), body.dump()));
    auto j = json::parse(r.body);
    if (!r.ok() || !j || !(*j)["sessionPath"].isString()) {
        err = "session start failed: " + describe(r);
        return false;
    }
    sessionPath_ = (*j)["sessionPath"].str();
    XC_LOGI("session created: %s", sessionPath_.c_str());
    return true;
}

bool GssvClient::sessionState(SessionStatus& out, std::string& err) {
    auto r = net::perform(authed(resolution_, login_, "GET", sessionUrl("/state")));
    auto j = json::parse(r.body);
    if (!r.ok() || !j) {
        err = "session state failed: " + describe(r);
        return false;
    }
    out.raw = (*j)["state"].str();
    out.state = parseState(out.raw);
    out.errorCode = (*j)["errorDetails"]["code"].str();
    out.errorMessage = (*j)["errorDetails"]["message"].str();
    return true;
}

int GssvClient::waitTimeSeconds() {
    auto r = net::perform(authed(resolution_, login_, "GET", sessionUrl("/waittime")));
    auto j = json::parse(r.body);
    if (!r.ok() || !j) return -1;
    return static_cast<int>((*j)["estimatedTotalWaitTimeInSeconds"].asInt(-1));
}

bool GssvClient::connect(const std::string& msaTransferToken, std::string& err) {
    json::Value body = json::Value::object();
    body.set("userToken", msaTransferToken);
    auto r = net::perform(authed(resolution_, login_, "POST", sessionUrl("/connect"), body.dump()));
    if (!r.ok()) {
        err = "session connect failed: " + describe(r);
        return false;
    }
    return true;
}

bool GssvClient::sendSdpOffer(const std::string& sdp, std::string& err) {
    auto ver = [](int lo, int hi) {
        json::Value v = json::Value::object();
        v.set("minVersion", lo);
        v.set("maxVersion", hi);
        return v;
    };
    json::Value chatCfg = json::Value::object();
    chatCfg.set("bytesPerSample", 2);
    chatCfg.set("expectedClipDurationMs", 20);
    json::Value fmt = json::Value::object();
    fmt.set("codec", "opus");
    fmt.set("container", "webm");
    chatCfg.set("format", fmt);
    chatCfg.set("numChannels", 1);
    chatCfg.set("sampleFrequencyHz", 24000);

    json::Value cfg = json::Value::object();
    cfg.set("chatConfiguration", chatCfg);
    cfg.set("chat", ver(1, 1));
    cfg.set("control", ver(1, 3));
    cfg.set("input", ver(1, 9));
    cfg.set("message", ver(1, 1));

    json::Value body = json::Value::object();
    body.set("messageType", "offer");
    body.set("sdp", sdp);
    body.set("requestId", "1");
    body.set("configuration", cfg);
    auto r = net::perform(authed(resolution_, login_, "POST", sessionUrl("/sdp"), body.dump()));
    if (!r.ok()) {
        err = "SDP offer failed: " + describe(r);
        return false;
    }
    return true;
}

bool GssvClient::pollSdpAnswer(std::string& answer, std::string& err) {
    answer.clear();
    auto r = net::perform(authed(resolution_, login_, "GET", sessionUrl("/sdp")));
    if (r.status == 204) return true;  // not ready yet
    auto j = json::parse(r.body);
    if (!r.ok() || !j) {
        err = "SDP answer failed: " + describe(r);
        return false;
    }
    auto payload = exchangePayload(*j);
    if (!payload) return true;
    if ((*payload)["status"].str("success") != "success") {
        err = "SDP negotiation rejected: " + payload->dump();
        return false;
    }
    answer = (*payload)["sdp"].str();
    return true;
}

bool GssvClient::sendIceCandidates(const std::vector<IceCandidate>& candidates, std::string& err) {
    json::Value list = json::Value::array();
    for (const auto& c : candidates) {
        json::Value v = json::Value::object();
        v.set("candidate", c.candidate);
        v.set("sdpMid", c.sdpMid);
        v.set("sdpMLineIndex", c.sdpMLineIndex);
        list.push(v);
    }
    json::Value body = json::Value::object();
    body.set("messageType", "iceCandidate");
    body.set("candidate", list);
    auto r = net::perform(authed(resolution_, login_, "POST", sessionUrl("/ice"), body.dump()));
    if (!r.ok()) {
        err = "ICE upload failed: " + describe(r);
        return false;
    }
    return true;
}

bool GssvClient::pollIceCandidates(std::vector<IceCandidate>& out, std::string& err) {
    out.clear();
    auto r = net::perform(authed(resolution_, login_, "GET", sessionUrl("/ice")));
    if (r.status == 204) return true;
    auto j = json::parse(r.body);
    if (!r.ok() || !j) {
        err = "ICE poll failed: " + describe(r);
        return false;
    }
    auto payload = exchangePayload(*j);
    if (!payload) return true;
    for (const auto& c : payload->items()) {
        IceCandidate ic;
        ic.candidate = c["candidate"].str();
        ic.sdpMid = c["sdpMid"].str();
        ic.sdpMLineIndex = static_cast<int>(c["sdpMLineIndex"].asInt());
        if (!ic.candidate.empty()) out.push_back(std::move(ic));
    }
    return true;
}

bool GssvClient::keepalive(std::string& err) {
    auto r = net::perform(authed(resolution_, login_, "POST", sessionUrl("/keepalive")));
    if (!r.ok()) {
        err = "keepalive failed: " + describe(r);
        return false;
    }
    return true;
}

void GssvClient::stopSession() {
    if (sessionPath_.empty()) return;
    net::perform(authed(resolution_, login_, "DELETE", sessionUrl("")));
    XC_LOGI("session stopped: %s", sessionPath_.c_str());
    sessionPath_.clear();
}

}  // namespace xc::xcloud
