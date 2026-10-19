// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "auth/auth_manager.h"

#include "net/http.h"
#include "platform/platform.h"
#include "util/json.h"
#include "util/log.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace xc::auth {

namespace {
constexpr const char* kTransferScope =
    "service::http://Passport.NET/purpose::PURPOSE_XBOX_CLOUD_CONSOLE_TRANSFER_TOKEN";
}

AuthManager::AuthManager(std::string storePath, MsaConfig cfg)
    : storePath_(std::move(storePath)), msa_(std::move(cfg)) {
    load();
}

bool AuthManager::load() {
    std::string text;
    if (!platform::readFile(storePath_, text)) {
        XC_LOGI("no saved sign-in (%s)", storePath_.c_str());  // the reports tell "asked again" apart
        return false;
    }
    auto j = json::parse(text);
    if (!j) {
        XC_LOGW("ignoring unreadable token store %s", storePath_.c_str());
        return false;
    }
    // A refresh token issued to another client id is useless: start over.
    if ((*j)["clientId"].str() != msa_.config().clientId) {
        XC_LOGI("saved sign-in from another client id: asking again");
        return false;
    }
    tokens_.refreshToken = (*j)["refreshToken"].str();
    tokens_.userId = (*j)["userId"].str();
    profile_.gamertag = (*j)["gamertag"].str();
    profile_.xuid = (*j)["xuid"].str();
    return !tokens_.refreshToken.empty();
}

bool AuthManager::save() const {
    json::Value v = json::Value::object();
    v.set("version", 1);
    v.set("clientId", msa_.config().clientId);
    v.set("refreshToken", tokens_.refreshToken);
    v.set("userId", tokens_.userId);
    v.set("gamertag", profile_.gamertag);
    v.set("xuid", profile_.xuid);
    if (!platform::writeFileAtomic(storePath_, v.dump())) {
        XC_LOGE("could not write token store %s (errno %d)", storePath_.c_str(), errno);
        saved_ = false;
        return false;
    }
    platform::makePrivate(storePath_);  // the refresh token: the user's alone on a desktop
    saved_ = true;
    return true;
}

void AuthManager::signOut() {
    signedIn_ = false;
    redirectUrl_.clear();
    tokens_ = {};
    profile_ = {};
    gssvXsts_ = {};
    std::remove(storePath_.c_str());
}

bool AuthManager::deviceCodeSignIn(const DeviceCodeCallback& onCode, std::string& err,
                                   const std::atomic<bool>* cancel) {
    DeviceCode dc;
    if (!msa_.startDeviceCode(dc, err)) return false;
    // The code itself stays on the screen: nothing of the sign-in goes to the log.
    XC_LOGI("sign-in code shown (%s, expires in %ds)", dc.verificationUri.c_str(), dc.expiresInSec);
    if (onCode) onCode(dc);

    int interval = dc.intervalSec > 0 ? dc.intervalSec : 5;
    uint64_t deadline = platform::nowMs() + static_cast<uint64_t>(dc.expiresInSec) * 1000u;
    while (platform::nowMs() < deadline) {
        for (int i = 0; i < interval * 10; ++i) {
            if (cancel && cancel->load()) {
                err = "sign-in cancelled";
                return false;
            }
            platform::sleepMs(100);
        }
        MsaTokens t;
        std::string pollErr;
        switch (msa_.pollDeviceCode(dc, t, pollErr)) {
            case PollResult::Success:
                tokens_ = t;
                return true;
            case PollResult::Pending:
                if (!pollErr.empty()) XC_LOGW("device code poll: %s", pollErr.c_str());
                break;
            case PollResult::SlowDown: interval = std::min(interval + 5, 60); break;
            case PollResult::Expired: err = "the sign-in code expired; try again"; return false;
            case PollResult::Denied: err = "sign-in was declined"; return false;
            case PollResult::Error: err = pollErr; return false;
        }
    }
    err = "the sign-in code expired; try again";
    return false;
}

bool AuthManager::xboxChain(xcloud::GssvClient* gssv, std::string& err, bool full) {
    XblToken user;
    // login.live.com tokens for a custom client id use the "t=" ticket form;
    // fall back to "d=" in case the service expects the MSAL form.
    if (!xblUserToken(tokens_.accessToken, "t=", user, err, &redirectUrl_)) {
        std::string err2;
        if (!xblUserToken(tokens_.accessToken, "d=", user, err2, &redirectUrl_)) return false;
        err.clear();
    }

    XblToken profileXsts;
    std::string perr;
    if (xstsToken(user, rp::kXboxLive, profileXsts, perr, &redirectUrl_)) {
        profile_.gamertag = profileXsts.gamertag;
        profile_.xuid = profileXsts.xuid;
        profile_.xblAuthorization = profileXsts.authorizationHeader();
        if (full) fetchGamerpic(profileXsts);
    } else {
        XC_LOGW("profile XSTS: %s", perr.c_str());
    }

    if (!xstsToken(user, rp::kGssv, gssvXsts_, err, &redirectUrl_)) return false;
    if (gssv && !gssv->login(gssvXsts_, err)) return false;
    if (full) fetchSubscription(user, gssv ? gssv->session().market : std::string());
    return true;
}

bool refreshDue(int64_t expiresAt, int64_t now, int64_t marginSec) { return expiresAt - now <= marginSec; }

bool AuthManager::ensureFresh(xcloud::GssvClient* gssv, std::string& err, bool force, bool* renewed) {
    if (renewed) *renewed = false;
    if (gssvXsts_.token.empty() || tokens_.refreshToken.empty()) {
        err = "not signed in";  // only a sign-in made is kept up
        return false;
    }
    int64_t now = unixNow();
    // An unreadable NotAfter: left to the login's own expiry and to a
    // refused request (`force`).
    int64_t xstsUntil = parseIsoUtc(gssvXsts_.notAfter);
    bool xstsDue = force || (xstsUntil > 0 && refreshDue(xstsUntil, now));
    bool loginDue = gssv && (xstsDue || refreshDue(gssv->session().expiresAt, now));
    if (!xstsDue && !loginDue) return true;
    std::string region = gssv ? gssv->region().name : std::string();
    if (xstsDue) {
        XC_LOGI("sign-in: renewing the Xbox tokens (%s)", force ? "a request was refused" : "about to expire");
        MsaTokens t;
        if (!msa_.refresh(tokens_.refreshToken, t, err)) return false;  // the stored account stays
        if (t.userId.empty()) t.userId = tokens_.userId;
        tokens_ = t;
        // A new refresh token each time: saved at once, as signIn() does.
        if (!save()) XC_LOGW("the renewed sign-in could not be saved");
        if (!xboxChain(gssv, err, false)) return false;
    } else {
        XC_LOGI("sign-in: renewing the xCloud login (%s)", gssv->isHome() ? "own Xbox" : "cloud");
        if (!gssv->login(gssvXsts_, err)) return false;
    }
    if (gssv && !region.empty())  // login() picked the default region: the chosen one back
        for (const auto& r : gssv->session().regions)
            if (r.name == region) gssv->setRegion(r);
    if (renewed) *renewed = true;
    return true;
}

namespace {

static uint64_t readVarint(const uint8_t*& ptr, const uint8_t* end, bool& ok) {
    uint64_t val = 0;
    int shift = 0;
    while (ptr < end && shift < 64) {
        uint8_t b = *ptr++;
        val |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) {
            ok = true;
            return val;
        }
        shift += 7;
    }
    ok = false;
    return 0;
}

static void parseProtobufPlaytime(const uint8_t* ptr, const uint8_t* end, int depth, int64_t& remaining, int64_t& max) {
    if (depth > 6) return;
    while (ptr < end) {
        bool ok = false;
        uint64_t tag = readVarint(ptr, end, ok);
        if (!ok) break;
        uint32_t fieldNum = static_cast<uint32_t>(tag >> 3);
        uint32_t wireType = static_cast<uint32_t>(tag & 0x7);
        if (wireType == 0) { // Varint
            uint64_t v = readVarint(ptr, end, ok);
            if (!ok) break;
            if (fieldNum == 1 && remaining < 0) remaining = static_cast<int64_t>(v);
            else if (fieldNum == 2 && max < 0) max = static_cast<int64_t>(v);
        } else if (wireType == 1) { // 64-bit
            if (ptr + 8 > end) break;
            ptr += 8;
        } else if (wireType == 2) { // Length-delimited
            uint64_t len = readVarint(ptr, end, ok);
            if (!ok || ptr + len > end) break;
            parseProtobufPlaytime(ptr, ptr + len, depth + 1, remaining, max);
            ptr += len;
        } else if (wireType == 5) { // 32-bit
            if (ptr + 4 > end) break;
            ptr += 4;
        } else {
            break;
        }
    }
}

}  // namespace

bool parsePlaytimeLimit(const std::string& body, int64_t& remaining, int64_t& max) {
    remaining = -1;
    max = -1;
    if (body.empty()) return false;

    // 1. Try JSON
    auto j = json::parse(body);
    if (j) {
        std::function<void(const json::Value&)> walk = [&](const json::Value& val) {
            if (val.isObject()) {
                for (const auto& [k, v] : val.members()) {
                    if (k == "remainingGameplayTimeInSeconds" || k == "remainingGameplayTime") {
                        if (v.isNumber()) remaining = v.asInt();
                        else if (v.isString()) remaining = std::strtoll(v.str().c_str(), nullptr, 10);
                    } else if (k == "maxGameplayTimeInSeconds" || k == "maxGameplayTime") {
                        if (v.isNumber()) max = v.asInt();
                        else if (v.isString()) max = std::strtoll(v.str().c_str(), nullptr, 10);
                    }
                    walk(v);
                }
            } else if (val.isArray()) {
                for (const auto& elem : val.items()) walk(elem);
            }
        };
        walk(*j);
        if (remaining >= 0) {
            if (max > 0 && max < remaining) std::swap(remaining, max);
            return true;
        }
    }

    // 2. Minimal Protobuf reader
    const uint8_t* ubody = reinterpret_cast<const uint8_t*>(body.data());
    parseProtobufPlaytime(ubody, ubody + body.size(), 0, remaining, max);
    if (remaining >= 0) {
        if (max > 0 && max < remaining) std::swap(remaining, max);
        return true;
    }

    return false;
}

std::string subscriptionPlan(const std::string& body) {
    // The store's entitlements for the Game Pass products, highest plan
    // first (an Ultimate account also has the lower ones).
    static const std::pair<const char*, const char*> kPlans[] = {
        {"CFQ7TTC0KHS0", "Ultimate"}, {"CFQ7TTC0P85B", "Premium"}, {"CFQ7TTC0KGQ8", "PC"}, {"CFQ7TTC0K5DJ", "Essential"}};
    auto j = json::parse(body);
    if (!j) return {};
    const auto& entitlements = (*j)["entitlements"];
    for (const auto& [id, plan] : kPlans) {
        std::string lower = id;
        for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        for (const auto& key : {std::string(id), lower}) {
            const auto& e = entitlements[key];
            if (!e.isObject()) continue;
            std::string status = e["status"].str();
            if (status == "Active" || (status.empty() && e["isSatisfyingEntitlement"].asBool())) return plan;
        }
    }
    return {};
}

std::string subscriptionRenewal(const std::string& body, int64_t nowUnix) {
    static const char* kIds[] = {"CFQ7TTC0KHS0", "CFQ7TTC0P85B", "CFQ7TTC0KGQ8", "CFQ7TTC0K5DJ"};
    auto j = json::parse(body);
    if (!j) return {};
    for (const char* id : kIds) {
        const auto& e = (*j)["entitlements"][id];
        if (!e.isObject() || e["status"].str() != "Active") continue;
        if (e["autoRenew"].asBool()) return "renews automatically";
        // "2026-11-14T00:00:00Z" (or with fractions): the days left.
        int y = 0, m = 0, d = 0;
        if (std::sscanf(e["endDate"].str().c_str(), "%d-%d-%dT", &y, &m, &d) != 3) return "no auto-renew";
        // Days since 1970-01-01 of that date (the PS5's libc has no timegm).
        y -= m <= 2;
        int64_t era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
        int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        int64_t days = era * 146097 + doe - 719468 - nowUnix / 86400;
        return "no auto-renew, ends in " + std::to_string(std::max<int64_t>(days, 0)) + " day(s)";
    }
    return {};
}

void AuthManager::fetchSubscription(const XblToken& userToken, const std::string& market) {
    // What xbox.com shows on the profile: the store service's entitlements
    // for the Game Pass products. Optional: a failure only leaves the plan
    // unknown (the UI then shows "Game Pass" from the titles).
    XblToken mp;
    std::string err;
    if (!xstsToken(userToken, rp::kMarketplace, mp, err)) {
        XC_LOGW("subscription: store XSTS: %s", err.c_str());
        return;
    }
    net::Request req;
    req.method = "POST";
    req.url = "https://emerald.xboxservices.com/xboxcomfd/entitlements/bulk?locale=en-US";
    req.headers = {{"Authorization", mp.authorizationHeader()},
                   {"Content-Type", "application/json"},
                   {"X-MS-API-Version", "1.0"},
                   {"MS-CV", "PSBoxProfile.1"}};
    req.body = R"({"ProductIds":["CFQ7TTC0KHS0","CFQ7TTC0P85B","CFQ7TTC0K5DJ","CFQ7TTC0KGQ8"]})";
    auto r = net::perform(req);
    if (!r.ok()) {
        XC_LOGW("subscription: entitlements: %s", r.status ? ("HTTP " + std::to_string(r.status)).c_str() : r.error.c_str());
    } else {
        if (std::getenv("XC_DUMP_SUBSCRIPTION")) std::printf("%s\n", r.body.c_str());  // diagnostics (CLI)
        profile_.subscription = subscriptionPlan(r.body);
        std::string renewal = subscriptionRenewal(r.body, unixNow());
        XC_LOGI("subscription: %s%s%s%s", profile_.subscription.empty() ? "none" : profile_.subscription.c_str(),
                renewal.empty() ? "" : " (", renewal.c_str(), renewal.empty() ? "" : ")");
    }

    fetchPlaytimeLimit(mp, market);
}

void AuthManager::fetchPlaytimeLimit(const XblToken& mp, const std::string& market) {
    net::Request req;
    req.method = "GET";
    std::string m = market.empty() ? "US" : market;
    req.url = "https://contentaccess.exp.xboxservices.com/all/v1?market=" + m + "&offering=xgpuweb";
    req.headers = {
        {"Authorization", mp.authorizationHeader()},
        {"Accept", "application/json, application/x-protobuf"},
        {"X-MS-API-Version", "1.0"},
        {"MS-CV", "PSBoxPlaytime.1"}
    };
    auto r = net::perform(req);
    if (!r.ok()) {
        XC_LOGW("playtime limit: %s", r.status ? ("HTTP " + std::to_string(r.status)).c_str() : r.error.c_str());
        return;
    }
    parsePlaytimeLimit(r.body, profile_.playtimeRemainingSeconds, profile_.playtimeMaxSeconds);
    if (profile_.playtimeRemainingSeconds >= 0) {
        XC_LOGI("playtime limit: %lld h remaining of %lld h",
                static_cast<long long>(profile_.playtimeRemainingSeconds / 3600),
                static_cast<long long>(profile_.playtimeMaxSeconds / 3600));
    }
}

bool AuthManager::loginOffering(xcloud::GssvClient& other, std::string& err) const {
    if (gssvXsts_.token.empty()) {
        err = "not signed in";
        return false;
    }
    return other.login(gssvXsts_, err);
}

void AuthManager::fetchGamerpic(const XblToken& xsts) {
    net::Request req;
    req.url = "https://profile.xboxlive.com/users/me/profile/settings?settings=GameDisplayPicRaw,Gamerscore";
    req.headers = {{"Authorization", xsts.authorizationHeader()}, {"x-xbl-contract-version", "2"}};
    auto r = net::perform(req);
    auto j = json::parse(r.body);
    if (!r.ok() || !j) {
        XC_LOGW("gamerpic: %s", r.status ? ("HTTP " + std::to_string(r.status)).c_str() : r.error.c_str());
        return;
    }
    for (const auto& s : (*j)["profileUsers"][0]["settings"].items())
        if (s["id"].str() == "GameDisplayPicRaw") profile_.gamerpicUrl = s["value"].str();
        else if (s["id"].str() == "Gamerscore") profile_.gamerscore = s["value"].str();
}

bool AuthManager::signIn(xcloud::GssvClient& gssv, const DeviceCodeCallback& onCode, std::string& err,
                         const std::atomic<bool>* cancel) {
    signedIn_ = false;
    redirectUrl_.clear();
    bool fresh = false;
    if (!tokens_.refreshToken.empty()) {
        MsaTokens t;
        bool networkError = false;
        bool invalidGrant = false;
        if (msa_.refresh(tokens_.refreshToken, t, err, {}, &networkError, &invalidGrant)) {
            tokens_ = t;
            // Microsoft hands out a new refresh token each time: saved at
            // once, so an app closed before the Xbox sign-in ends still
            // starts with the newest one.
            if (!save()) XC_LOGW("the renewed sign-in could not be saved");
        } else if (!invalidGrant) {
            // Offline, 5xx, or transient error: keep the stored account.
            XC_LOGW("sign-in refresh failed: %s; keeping stored account", err.c_str());
            return false;
        } else {
            // Only when Microsoft explicitly returns invalid_grant (revoked or expired).
            XC_LOGW("stored sign-in no longer valid (invalid_grant: %s); asking again", err.c_str());
            tokens_ = {};
            std::remove(storePath_.c_str());
        }
    }
    if (tokens_.accessToken.empty()) {
        if (!deviceCodeSignIn(onCode, err, cancel)) return false;
        fresh = true;
    }
    if (!xboxChain(&gssv, err)) {
        // A just-granted token that fails means the account itself is the
        // problem (no Xbox profile, no Game Pass...): keep it for diagnosis.
        if (!fresh) XC_LOGW("Xbox sign-in failed with stored token: %s", err.c_str());
        save();
        return false;
    }
    save();
    signedIn_ = true;
    XC_LOGI("signed in%s", profile_.gamertag.empty() ? " (no gamertag)" : "");  // the gamertag isn't logged
    return true;
}

bool AuthManager::consoleTransferToken(std::string& out, std::string& err) {
    if (tokens_.refreshToken.empty()) {
        err = "not signed in";
        return false;
    }
    MsaTokens t;
    if (!msa_.refresh(tokens_.refreshToken, t, err, kTransferScope)) return false;
    // Rotate the stored refresh token if the service issued a new one.
    if (!t.refreshToken.empty() && t.refreshToken != tokens_.refreshToken) {
        tokens_.refreshToken = t.refreshToken;
        save();
    }
    out = t.accessToken;
    return true;
}

}  // namespace xc::auth
