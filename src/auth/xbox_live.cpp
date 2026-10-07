// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "auth/xbox_live.h"

#include "net/http.h"
#include "util/json.h"
#include "util/log.h"

namespace xc::auth {

namespace {
net::Response postJson(const std::string& url, const json::Value& body) {
    net::Request req;
    req.method = "POST";
    req.url = url;
    req.headers = {{"Content-Type", "application/json"}, {"x-xbl-contract-version", "1"}};
    req.body = body.dump();
    return net::perform(req);
}

bool readXbl(const net::Response& r, XblToken& out, std::string& err, const char* what) {
    auto j = json::parse(r.body);
    if (!r.ok() || !j || !(*j)["Token"].isString()) {
        err = std::string(what) + " failed: ";
        err += r.status == 0 ? r.error : "HTTP " + std::to_string(r.status);
        if (j && (*j)["XErr"].isNumber()) {
            int64_t x = (*j)["XErr"].asInt();
            err += " XErr " + std::to_string(x);
            // Well-known XSTS errors worth telling the user about.
            if (x == 2148916233) err += " (this Microsoft account has no Xbox profile yet)";
            if (x == 2148916235) err += " (Xbox Live is not available in this account's country)";
            if (x == 2148916238) err += " (child account: needs to be added to a family by an adult)";
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
                  std::string& err) {
    json::Value props = json::Value::object();
    props.set("AuthMethod", "RPS");
    props.set("SiteName", "user.auth.xboxlive.com");
    props.set("RpsTicket", rpsPrefix + msaAccessToken);
    json::Value body = json::Value::object();
    body.set("RelyingParty", "http://auth.xboxlive.com");
    body.set("TokenType", "JWT");
    body.set("Properties", props);
    return readXbl(postJson("https://user.auth.xboxlive.com/user/authenticate", body), out, err,
                   "Xbox user authentication");
}

bool xstsToken(const XblToken& userToken, const std::string& relyingParty, XblToken& out,
               std::string& err) {
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
                   ("XSTS authorization for " + relyingParty).c_str());
}

}  // namespace xc::auth
