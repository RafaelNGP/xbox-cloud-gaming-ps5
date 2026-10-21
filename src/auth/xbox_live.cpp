// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "auth/xbox_live.h"

#include "net/http.h"
#include "platform/platform.h"
#include "util/json.h"
#include "util/log.h"

#include <cstdio>

namespace xc::auth {

namespace {
net::Response postJson(const std::string& url, const json::Value& body) {
    net::Request req;
    req.method = "POST";
    req.url = url;
    req.headers = {{"Content-Type", "application/json"}, {"x-xbl-contract-version", "1"}};
    req.body = body.dump();
    net::Response r;
    constexpr int kTries = 3;
    for (int attempt = 1; attempt <= kTries; ++attempt) {
        r = net::perform(req);
        bool transient = r.status == 0 || r.status == 408 || r.status == 429 || r.status >= 500;
        if (!transient || attempt == kTries) break;
        XC_LOGW("%s: %s, trying again (%d/%d)", url.c_str(),
                r.status ? ("HTTP " + std::to_string(r.status)).c_str() : r.error.c_str(),
                attempt + 1, kTries);
        platform::sleepMs(1000u << (attempt - 1));
    }
    return r;
}

bool readXbl(const net::Response& r, XblToken& out, std::string& err, const char* what,
             std::string* redirectUrl = nullptr) {
    auto j = json::parse(r.body);
    if (!r.ok() || !j || !(*j)["Token"].isString()) {
        err = std::string(what) + " failed: ";
        err += r.status == 0 ? r.error : "HTTP " + std::to_string(r.status);
        if (j && (*j)["XErr"].isNumber()) {
            int64_t x = (*j)["XErr"].asInt();
            err += " XErr " + std::to_string(x);
            // Well-known XSTS errors worth telling the user about.
            if (x == 2148916227) err += " (this Xbox account is suspended or banned (2148916227))";
            if (x == 2148916233) err += " (this Microsoft account has no Xbox profile yet — sign in once at xbox.com to create a Gamertag (2148916233))";
            if (x == 2148916235) err += " (Xbox Live is not offered in this account's country/region (2148916235))";
            if (x == 2148916236 || x == 2148916237) err += " (age verification or terms acceptance is required at xbox.com in a browser (2148916236))";
            if (x == 2148916238) err += " (child account: an adult must add this account to a Microsoft family at account.microsoft.com/family (2148916238))";
        }
        if (j && (*j)["Redirect"].isString()) {
            std::string red = (*j)["Redirect"].str();
            if (redirectUrl) *redirectUrl = red;
            if (!red.empty()) err += " [Redirect: " + red + "]";
        }
        return false;
    }
    out.token = (*j)["Token"].str();
    out.notAfter = (*j)["NotAfter"].str();
    const auto& xui = (*j)["DisplayClaims"]["xui"][0];
    out.userHash = xui["uhs"].str();
    out.gamertag = xui["gtg"].str();
    out.xuid = xui["xid"].str();
    return true;
}
}  // namespace

bool xblUserToken(const std::string& msaAccessToken, const std::string& rpsPrefix, XblToken& out,
                  std::string& err, std::string* redirectUrl) {
    json::Value props = json::Value::object();
    props.set("AuthMethod", "RPS");
    props.set("SiteName", "user.auth.xboxlive.com");
    props.set("RpsTicket", rpsPrefix + msaAccessToken);
    json::Value body = json::Value::object();
    body.set("RelyingParty", "http://auth.xboxlive.com");
    body.set("TokenType", "JWT");
    body.set("Properties", props);
    return readXbl(postJson("https://user.auth.xboxlive.com/user/authenticate", body), out, err,
                   "Xbox user authentication", redirectUrl);
}

bool xstsToken(const XblToken& userToken, const std::string& relyingParty, XblToken& out,
               std::string& err, std::string* redirectUrl) {
    json::Value users = json::Value::array();
    users.push(userToken.token);
    json::Value props = json::Value::object();
    props.set("SandboxId", "RETAIL");
    props.set("UserTokens", users);
    json::Value body = json::Value::object();
    body.set("RelyingParty", relyingParty);
    body.set("TokenType", "JWT");
    body.set("Properties", props);
    return readXbl(postJson("https://xsts.auth.xboxlive.com/xsts/authorize", body), out, err,
                   ("XSTS authorization for " + relyingParty).c_str(), redirectUrl);
}

int64_t parseIsoUtc(const std::string& iso) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6) return 0;
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 60) return 0;
    // Days since 1970-01-01 (the PS5's libc has no timegm).
    y -= mo <= 2;
    int64_t era = y / 400, yoe = y - era * 400;
    int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + sec;
}

}  // namespace xc::auth
