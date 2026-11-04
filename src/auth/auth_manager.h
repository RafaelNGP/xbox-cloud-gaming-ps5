// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Ties sign-in together: stored refresh token (or a fresh device-code
// sign-in) -> Xbox Live -> XSTS -> xCloud login. The UI only drives this and
// shows the device code; it never sees passwords.
#pragma once

#include "auth/msa.h"
#include "auth/xbox_live.h"
#include "xcloud/gssv.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace xc::auth {

struct Profile {
    std::string gamertag;
    std::string xuid;
    std::string gamerpicUrl;  // empty until fetched
    std::string gamerscore;   // digits; empty until fetched
    // The Game Pass plan: "Ultimate", "Premium", "Essential", "PC"; empty
    // when none or unknown (the store service didn't answer).
    std::string subscription;
    // Cloud playtime remaining and max in seconds (-1 when uncapped/unknown).
    int64_t playtimeRemainingSeconds = -1;
    int64_t playtimeMaxSeconds = -1;
    bool playtimeUnlimited = false;  // the service says the account has no limit
    // Each product's cloud time (remaining, max seconds; -1 absent), by product ID.
    std::map<std::string, std::pair<int64_t, int64_t>> productPlaytime;
    // "XBL3.0 x=...;token" for services on http://xboxlive.com (titlehub...).
    // Memory only: never logged or saved.
    std::string xblAuthorization;
};

// The Game Pass plan in an entitlements answer ("Ultimate"...; empty: none).
std::string subscriptionPlan(const std::string& entitlementsJson);
// For the log: whether that plan renews, or in how many days it ends (no
// date: the log may go into public reports). Empty when unknown.
std::string subscriptionRenewal(const std::string& entitlementsJson, int64_t nowUnix);
// The cloud streaming time in a Content Access answer (protobuf, or JSON):
// each product's remaining and maximum seconds (-1 when absent), and the
// account's allowance when (nearly) all of its limited products share one.
struct ContentAccessPlaytime {
    int products = 0, limited = 0;  // products seen, those with a maximum
    std::map<std::string, std::pair<int64_t, int64_t>> byProduct;  // product ID -> remaining, max
    std::pair<int64_t, int64_t> common{-1, -1};  // the most frequent remaining / max
    int commonCount = 0;
    // The account's own record (top-level field 3): its time buckets by
    // field number, remaining / max seconds (-1 absent; kUnlimited for
    // "no limit", sent as all bits set).
    static constexpr int64_t kUnlimited = INT64_MAX;
    std::map<int, std::pair<int64_t, int64_t>> accountBuckets;
    int64_t accountRemaining = -1, accountMax = -1;  // -1: no limit seen
    bool unlimited = false;                          // the account's record says so
    size_t bytes = 0;                                // the answer's size (queryCloudTime)
};
bool parseContentAccessPlaytime(const std::string& body, ContentAccessPlaytime& out);
// Just the account's allowance (false and -1 when there is none to tell).
bool parsePlaytimeLimit(const std::string& body, int64_t& remaining, int64_t& max);
// What asking for the cloud time needs, copied: a stream asks again from
// its own thread while the sign-in carries on.
struct CloudTimeQuery {
    XblToken marketplace;
    std::string market;
};
// Asks Content Access for the account's cloud time; false when the service
// did not answer or the answer was not understood.
bool queryCloudTime(const CloudTimeQuery& q, ContentAccessPlaytime& out, int timeoutMs = 20000);

// The tokens are renewed this long before they expire.
constexpr int64_t kRefreshMarginSec = 600;
// Whether a token that expires at `expiresAt` (unix seconds; 0: none yet)
// is to be renewed at `now`.
bool refreshDue(int64_t expiresAt, int64_t now, int64_t marginSec = kRefreshMarginSec);

class AuthManager {
public:
    // Called while waiting for the user to sign in on another device.
    using DeviceCodeCallback = std::function<void(const DeviceCode&)>;

    explicit AuthManager(std::string storePath, MsaConfig cfg = {});

    bool hasStoredAccount() const { return !tokens_.refreshToken.empty(); }
    // The last signIn() went through (until signOut()).
    bool signedIn() const { return signedIn_; }
    // False when the last sign-in couldn't be written to the store file: the
    // next start asks for a code again.
    bool saved() const { return saved_; }

    // Full sign-in. Uses the stored refresh token when possible; otherwise
    // starts the device-code flow and calls `onCode`. Blocking; `cancel` may be
    // set from another thread to abort the device-code wait.
    bool signIn(xcloud::GssvClient& gssv, const DeviceCodeCallback& onCode, std::string& err,
                const std::atomic<bool>* cancel = nullptr);

    // MSA token for the console-transfer purpose, required by the session
    // /connect step of cloud sessions.
    bool consoleTransferToken(std::string& out, std::string& err);

    // The cloud time asked for again (after a stream).
    void refreshCloudTime();
    CloudTimeQuery cloudTimeQuery() const { return {marketplaceXsts_, market_}; }

    // After signIn(): logs `other` (another offering, such as "xhome" for
    // the user's own consoles) in with the same Xbox token.
    bool loginOffering(xcloud::GssvClient& other, std::string& err) const;

    // After signIn(): keeps the sign-in usable. When the XSTS token expires
    // within kRefreshMarginSec (or `force`: a request was refused), the chain
    // runs again from the stored refresh token: MSA refresh (the rotated
    // refresh token saved at once) -> Xbox user token -> XSTS (the profile's
    // too: profile().xblAuthorization) -> `gssv`'s login. When only `gssv`'s
    // login is due, just that login. `gssv`'s region stays as chosen; it may
    // be null (the XSTS only). True when nothing was due or the renewal
    // worked; `renewed` says whether anything was renewed. A refused refresh
    // keeps the stored account (the next start decides). No token is logged.
    bool ensureFresh(xcloud::GssvClient* gssv, std::string& err, bool force = false, bool* renewed = nullptr);

    void signOut();
    const Profile& profile() const { return profile_; }
    const std::string& redirectUrl() const { return redirectUrl_; }
    // Returns Authorization header ("XBL3.0 x=...;token") for Microsoft Store calls (rp::kMarketplace).
    bool marketplaceAuthorization(std::string& outAuthHeader, std::string& err);

private:
    bool load();
    bool save() const;
    mutable bool saved_ = true;
    bool deviceCodeSignIn(const DeviceCodeCallback& onCode, std::string& err, const std::atomic<bool>* cancel);
    // `full`: the profile's extras too (gamerpic, plan, playtime): not on a renewal.
    bool xboxChain(xcloud::GssvClient* gssv, std::string& err, bool full = true);
    void fetchGamerpic(const XblToken& xsts);
    void fetchSubscription(const XblToken& userToken, const std::string& market);
    void fetchPlaytimeLimit(const XblToken& mp, const std::string& market);
    std::string market_;  // the last sign-in's, for refreshCloudTime()

    std::string storePath_;
    MsaClient msa_;
    MsaTokens tokens_;
    Profile profile_;
    XblToken gssvXsts_;  // in memory only, for loginOffering()
    XblToken marketplaceXsts_;  // in memory only, for Microsoft Store operations
    std::string redirectUrl_;
    bool signedIn_ = false;
};

}  // namespace xc::auth
