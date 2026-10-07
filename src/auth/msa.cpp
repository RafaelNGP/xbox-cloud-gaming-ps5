#include "auth/msa.h"

#include "net/http.h"
#include "util/json.h"
#include "util/log.h"

#include <ctime>

namespace xc::auth {

namespace {
constexpr const char* kConnectUrl = "https://login.live.com/oauth20_connect.srf";
constexpr const char* kTokenUrl = "https://login.live.com/oauth20_token.srf";

net::Response postForm(const char* url, const std::vector<std::pair<std::string, std::string>>& fields) {
    net::Request req;
    req.method = "POST";
    req.url = url;
    req.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
    req.body = net::formEncode(fields);
    return net::perform(req);
}

bool readTokens(const json::Value& v, MsaTokens& out) {
    // The console-transfer purpose answers with "lpt" instead of access_token.
    out.accessToken = v["access_token"].str(v["lpt"].str());
    if (v["refresh_token"].isString()) out.refreshToken = v["refresh_token"].str();
    out.userId = v["user_id"].str(out.userId);
    out.expiresAt = unixNow() + v["expires_in"].asInt(3600);
    return !out.accessToken.empty();
}

std::string describe(const net::Response& r) {
    if (r.status == 0) return r.error;
    auto j = json::parse(r.body);
    std::string msg = "HTTP " + std::to_string(r.status);
    if (j) {
        if (auto e = (*j)["error"].str(); !e.empty()) msg += " " + e;
        if (auto d = (*j)["error_description"].str(); !d.empty()) msg += ": " + d;
    }
    return msg;
}
}  // namespace

int64_t unixNow() { return static_cast<int64_t>(std::time(nullptr)); }

bool MsaClient::startDeviceCode(DeviceCode& out, std::string& err) const {
    auto r = postForm(kConnectUrl, {{"client_id", cfg_.clientId},
                                    {"scope", cfg_.scope},
                                    {"response_type", "device_code"}});
    auto j = json::parse(r.body);
    if (!r.ok() || !j) {
        err = "device code request failed: " + describe(r);
        return false;
    }
    out.userCode = (*j)["user_code"].str();
    out.deviceCode = (*j)["device_code"].str();
    out.verificationUri = (*j)["verification_uri"].str("https://www.microsoft.com/link");
    out.intervalSec = static_cast<int>((*j)["interval"].asInt(5));
    out.expiresInSec = static_cast<int>((*j)["expires_in"].asInt(900));
    if (out.userCode.empty() || out.deviceCode.empty()) {
        err = "device code response missing fields";
        return false;
    }
    return true;
}

PollResult MsaClient::pollDeviceCode(const DeviceCode& dc, MsaTokens& out, std::string& err) const {
    auto r = postForm(kTokenUrl, {{"client_id", cfg_.clientId},
                                  {"device_code", dc.deviceCode},
                                  {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}});
    if (r.status == 0) {
        err = r.error;
        return PollResult::Pending;  // transient network error: keep polling
    }
    auto j = json::parse(r.body);
    if (r.ok() && j && readTokens(*j, out)) return PollResult::Success;
    std::string e = j ? (*j)["error"].str() : std::string();
    if (e == "authorization_pending") return PollResult::Pending;
    if (e == "slow_down") return PollResult::SlowDown;
    if (e == "expired_token") return PollResult::Expired;
    if (e == "authorization_declined" || e == "access_denied") return PollResult::Denied;
    err = describe(r);
    return PollResult::Error;
}

bool MsaClient::refresh(const std::string& refreshToken, MsaTokens& out, std::string& err,
                        const std::string& scope, bool* networkError) const {
    auto r = postForm(kTokenUrl, {{"client_id", cfg_.clientId},
                                  {"grant_type", "refresh_token"},
                                  {"refresh_token", refreshToken},
                                  {"scope", scope.empty() ? cfg_.scope : scope}});
    if (networkError) *networkError = r.status == 0 || r.status >= 500;
    auto j = json::parse(r.body);
    if (!r.ok() || !j || !readTokens(*j, out)) {
        err = "token refresh failed: " + describe(r);
        return false;
    }
    if (out.refreshToken.empty()) out.refreshToken = refreshToken;
    return true;
}

}  // namespace xc::auth
