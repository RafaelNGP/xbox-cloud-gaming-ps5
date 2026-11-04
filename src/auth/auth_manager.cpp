// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "auth/auth_manager.h"

#include "net/http.h"
#include "platform/platform.h"
#include "util/json.h"
#include "util/log.h"

#include <algorithm>
#include <vector>
#include <map>
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
    marketplaceXsts_ = {};
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
        if (err.find("HTTP ") == std::string::npos ||
            !xblUserToken(tokens_.accessToken, "d=", user, err2, &redirectUrl_)) {
            return false;
        }
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

// Content Access (contentaccess.exp.xboxservices.com/all/v1), protobuf:
//   1: passes by user  {1: PUID, 2: passes}
//   2: products        {1: product ID, 2: {2 (repeated): {1: PUID, 2: access}}}
//      access          {1: user access types, 2: aggregate access types,
//                       11: streaming gameplay {1: program, 2: remaining s, 4: max s}}
// The cloud time is the product's (the layout as owenselles/CloudNow decodes
// it): the account's own allowance shows as the same remaining / max on all of
// its limited products.
struct PbReader {
    const uint8_t* p = nullptr;
    const uint8_t* end = nullptr;
    bool ok = true;
    uint64_t value = 0;  // the last varint field's
    uint64_t varint() {
        uint64_t v = 0;
        for (int shift = 0; shift < 64 && p < end; shift += 7) {
            const uint8_t b = *p++;
            v |= static_cast<uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) return v;
        }
        ok = false;
        return 0;
    }
    // The next field: its number and wire type, `sub` the bytes of a
    // length-delimited one; the reader moves past it in every case.
    bool next(uint32_t& field, uint32_t& wire, PbReader& sub) {
        if (!ok || p >= end) return false;
        const uint64_t tag = varint();
        if (!ok) return false;
        field = static_cast<uint32_t>(tag >> 3);
        wire = static_cast<uint32_t>(tag & 7);
        sub = PbReader{p, p};
        switch (wire) {
            case 0: value = varint(); return ok;
            case 1:
                if (end - p < 8) return ok = false;
                p += 8;
                return true;
            case 5:
                if (end - p < 4) return ok = false;
                p += 4;
                return true;
            case 2: {
                const uint64_t n = varint();
                if (!ok || n > static_cast<uint64_t>(end - p)) return ok = false;
                sub = PbReader{p, p + n};
                p += n;
                return true;
            }
            default: return ok = false;
        }
    }
    static std::string text(const PbReader& sub) {
        return std::string(reinterpret_cast<const char*>(sub.p), static_cast<size_t>(sub.end - sub.p));
    }
};

// One user's access to a product: its streaming time, when it has one.
void readProductAccess(PbReader r, int64_t& remaining, int64_t& max) {
    uint32_t f = 0, w = 0;
    PbReader sub;
    while (r.next(f, w, sub)) {
        if (f != 11 || w != 2) continue;
        PbReader g = sub, unused;
        while (g.next(f, w, unused)) {
            if (w == 0 && f == 2) remaining = static_cast<int64_t>(g.value);
            else if (w == 0 && f == 4) max = static_cast<int64_t>(g.value);
        }
    }
}

bool parseContentAccessProto(const std::string& body, ContentAccessPlaytime& out) {
    const auto* begin = reinterpret_cast<const uint8_t*>(body.data());
    PbReader top{begin, begin + body.size()};
    std::string puid;  // the user: the first one with passes
    std::vector<PbReader> products;
    uint32_t f = 0, w = 0;
    PbReader sub;
    int fields = 0;
    while (top.next(f, w, sub)) {
        ++fields;
        if (w != 2) continue;
        if (f == 1 && puid.empty()) {
            PbReader e = sub, v;
            while (e.next(f, w, v))
                if (f == 1 && w == 2) puid = PbReader::text(v);
        } else if (f == 2) {
            products.push_back(sub);
        }
    }
    if (!top.ok || fields == 0 || (puid.empty() && products.empty())) return false;
    // The account's record, after the products ({1: PUID, 2: {bucket: {2:
    // remaining, 3: updated, 4: max, 5: ?}}}): its cloud time, when limited.
    top = PbReader{begin, begin + body.size()};
    while (top.next(f, w, sub)) {
        if (f != 3 || w != 2) continue;
        PbReader rec = sub, v;
        std::string who;
        std::vector<PbReader> buckets;
        while (rec.next(f, w, v)) {
            if (f == 1 && w == 2) who = PbReader::text(v);
            if (f == 2 && w == 2) buckets.push_back(v);
        }
        if (!puid.empty() && who != puid) continue;
        for (PbReader b : buckets) {
            PbReader entry;
            while (b.next(f, w, entry)) {
                if (w != 2) continue;
                const uint32_t bucket = f;
                int64_t remaining = -1, max = -1;
                PbReader g = entry, unused;
                while (g.next(f, w, unused)) {
                    auto seconds = [](uint64_t x) {
                        return x >= static_cast<uint64_t>(INT64_MAX) ? ContentAccessPlaytime::kUnlimited : static_cast<int64_t>(x);
                    };
                    if (w == 0 && f == 2) remaining = seconds(g.value);
                    else if (w == 0 && f == 4) max = seconds(g.value);
                }
                out.accountBuckets[static_cast<int>(bucket)] = {remaining, max};
            }
        }
    }
    for (PbReader prod : products) {
        std::string productId;
        int64_t remaining = -1, max = -1;
        PbReader v;
        while (prod.next(f, w, v)) {
            if (f == 1 && w == 2) productId = PbReader::text(v);
            if (f != 2 || w != 2) continue;
            PbReader data = v, entry;
            while (data.next(f, w, entry)) {
                if (f != 2 || w != 2) continue;
                PbReader e = entry, x, access;
                std::string who;
                bool haveAccess = false;
                while (e.next(f, w, x)) {
                    if (f == 1 && w == 2) who = PbReader::text(x);
                    if (f == 2 && w == 2) access = x, haveAccess = true;
                }
                if (haveAccess && (puid.empty() || who == puid)) readProductAccess(access, remaining, max);
            }
        }
        ++out.products;
        if (remaining >= 0 || max >= 0) out.byProduct[productId] = {remaining, max};
    }
    return true;
}

void collectJson(const json::Value& val, ContentAccessPlaytime& out) {
    if (val.isObject()) {
        int64_t remaining = -1, max = -1;
        auto num = [](const json::Value& v) {
            return v.isString() ? static_cast<int64_t>(std::strtoll(v.str().c_str(), nullptr, 10)) : v.asInt();
        };
        for (const auto& [k, v] : val.members()) {
            if (k == "remainingGameplayTimeInSeconds" || k == "remainingGameplayTime") remaining = num(v);
            else if (k == "maxGameplayTimeInSeconds" || k == "maxGameplayTime") max = num(v);
            else collectJson(v, out);
        }
        if (remaining >= 0 || max >= 0) {
            std::string id = val["productId"].str("#" + std::to_string(out.byProduct.size()));
            out.byProduct[id] = {remaining, max};
            ++out.products;
        }
    } else if (val.isArray()) {
        for (const auto& e : val.items()) collectJson(e, out);
    }
}

}  // namespace

bool parseContentAccessPlaytime(const std::string& body, ContentAccessPlaytime& out) {
    out = {};
    if (body.empty()) return false;
    if (auto j = json::parse(body)) {
        collectJson(*j, out);
    } else if (!parseContentAccessProto(body, out)) {
        return false;
    }
    // The account's allowance: one remaining / max shared by (nearly) all of
    // its limited products, a few of them at least.
    std::map<std::pair<int64_t, int64_t>, int> counts;
    for (const auto& [id, t] : out.byProduct)
        if (t.second > 0) ++counts[t], ++out.limited;
    for (const auto& [t, n] : counts)
        if (n > out.commonCount) out.commonCount = n, out.common = t;
    if (out.limited >= 3 && out.commonCount * 5 >= out.limited * 4) {
        out.accountRemaining = out.common.first;
        out.accountMax = out.common.second;
    }
    // The account's own record wins: its first bucket that carries a time
    // (2 so far); all bits set there means no limit.
    for (const auto& [bucket, t] : out.accountBuckets) {
        if (t.first < 0 && t.second < 0) continue;
        if (t.first == ContentAccessPlaytime::kUnlimited || t.second == ContentAccessPlaytime::kUnlimited) {
            out.unlimited = true;
            out.accountRemaining = out.accountMax = -1;
        } else {
            out.unlimited = false;
            out.accountRemaining = t.first;
            out.accountMax = t.second;
        }
        break;
    }
    return true;
}

bool parsePlaytimeLimit(const std::string& body, int64_t& remaining, int64_t& max) {
    ContentAccessPlaytime t;
    remaining = max = -1;
    if (!parseContentAccessPlaytime(body, t) || t.accountRemaining < 0) return false;
    remaining = t.accountRemaining;
    max = t.accountMax;
    return true;
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
    marketplaceXsts_ = mp;
    market_ = market;
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

void AuthManager::refreshCloudTime() {
    if (!marketplaceXsts_.token.empty()) fetchPlaytimeLimit(marketplaceXsts_, market_);
}

bool queryCloudTime(const CloudTimeQuery& q, ContentAccessPlaytime& out, int timeoutMs) {
    net::Request req;
    req.method = "GET";
    req.timeoutMs = timeoutMs;
    std::string m = q.market.empty() ? "US" : q.market;
    req.url = "https://contentaccess.exp.xboxservices.com/all/v1?market=" + m + "&offering=xgpuweb";
    req.headers = {
        {"Authorization", q.marketplace.authorizationHeader()},
        {"Accept", "application/json, application/x-protobuf"},
        {"X-MS-API-Version", "1.0"},
        {"MS-CV", "PSBoxPlaytime.1"}
    };
    auto r = net::perform(req);
    if (!r.ok()) {
        XC_LOGW("playtime limit: %s", r.status ? ("HTTP " + std::to_string(r.status)).c_str() : r.error.c_str());
        return false;
    }
    // Diagnostics: the answer kept as it came when asked (a file named
    // contentaccess.request in the data folder; removed once done).
    if (std::string asked; platform::readFile(platform::dataDir() + "/contentaccess.request", asked)) {
        std::remove((platform::dataDir() + "/contentaccess.request").c_str());
        platform::writeFileAtomic(platform::dataDir() + "/contentaccess.bin", r.body);
        XC_LOGI("cloud time: the answer saved to contentaccess.bin (%s)", r.header("content-type").c_str());
    }
    if (!parseContentAccessPlaytime(r.body, out)) {
        XC_LOGW("cloud time: an answer of %zu bytes not understood", r.body.size());
        return false;
    }
    out.bytes = r.body.size();
    return true;
}

void AuthManager::fetchPlaytimeLimit(const XblToken& mp, const std::string& market) {
    ContentAccessPlaytime t;
    if (!queryCloudTime({mp, market}, t)) return;
    profile_.playtimeRemainingSeconds = t.accountRemaining;
    profile_.playtimeMaxSeconds = t.accountMax;
    profile_.playtimeUnlimited = t.unlimited;
    profile_.productPlaytime = t.byProduct;
    // What the service says, for the log: how many products carry a time
    // limit and the remaining / max they share most.
    std::map<std::pair<int64_t, int64_t>, int> counts;
    for (const auto& [id, v] : t.byProduct) ++counts[v];
    std::vector<std::pair<int, std::pair<int64_t, int64_t>>> ranked;
    for (const auto& [v, n] : counts) ranked.push_back({n, v});
    std::sort(ranked.rbegin(), ranked.rend());
    std::string common;
    auto hm = [](int64_t s) {
        if (s < 0) return std::string("-");
        char b[32];
        std::snprintf(b, sizeof b, "%lldh%02lld", static_cast<long long>(s / 3600), static_cast<long long>(s % 3600 / 60));
        return std::string(b);
    };
    for (size_t i = 0; i < ranked.size() && i < 3; ++i)
        common += (i ? ", " : "") + hm(ranked[i].second.first) + " of " + hm(ranked[i].second.second) + " x" +
                  std::to_string(ranked[i].first);
    std::string buckets;
    auto hmu = [&](int64_t s) { return s == ContentAccessPlaytime::kUnlimited ? std::string("unlimited") : hm(s); };
    for (const auto& [b, v] : t.accountBuckets)
        buckets += (buckets.empty() ? "" : ", ") + std::to_string(b) + ": " + hmu(v.first) + " of " + hmu(v.second);
    XC_LOGI("cloud time: %zu bytes, %d products, %zu with time data (%d with a maximum)%s%s; account record [%s]: %s",
            t.bytes, t.products, t.byProduct.size(), t.limited, common.empty() ? "" : ": ", common.c_str(),
            buckets.c_str(),
            t.unlimited ? "unlimited"
            : t.accountRemaining >= 0 ? (hm(t.accountRemaining) + " left of " + hm(t.accountMax)).c_str()
                                      : "no limit seen");
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

bool AuthManager::marketplaceAuthorization(std::string& outAuthHeader, std::string& err) {
    if (!marketplaceXsts_.token.empty() && !refreshDue(parseIsoUtc(marketplaceXsts_.notAfter), unixNow())) {
        outAuthHeader = marketplaceXsts_.authorizationHeader();
        return true;
    }
    bool refreshedMsa = false;
    if (tokens_.accessToken.empty() || (tokens_.expiresAt > 0 && refreshDue(tokens_.expiresAt, unixNow()))) {
        if (tokens_.refreshToken.empty()) {
            err = "not signed in";
            return false;
        }
        MsaTokens t;
        if (!msa_.refresh(tokens_.refreshToken, t, err)) return false;
        if (t.userId.empty()) t.userId = tokens_.userId;
        tokens_ = t;
        save();
        refreshedMsa = true;
    }
    XblToken userToken;
    auto fetchUserToken = [&]() {
        if (xblUserToken(tokens_.accessToken, "t=", userToken, err, &redirectUrl_)) return true;
        std::string err2;
        if (err.find("HTTP ") != std::string::npos &&
            xblUserToken(tokens_.accessToken, "d=", userToken, err2, &redirectUrl_)) {
            err.clear();
            return true;
        }
        return false;
    };
    if (!fetchUserToken()) {
        if (!refreshedMsa && !tokens_.refreshToken.empty() && err.find("HTTP ") != std::string::npos) {
            MsaTokens t;
            if (msa_.refresh(tokens_.refreshToken, t, err)) {
                if (t.userId.empty()) t.userId = tokens_.userId;
                tokens_ = t;
                save();
                if (!fetchUserToken()) return false;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }
    if (!xstsToken(userToken, rp::kMarketplace, marketplaceXsts_, err, &redirectUrl_)) {
        return false;
    }
    outAuthHeader = marketplaceXsts_.authorizationHeader();
    return true;
}

}  // namespace xc::auth
