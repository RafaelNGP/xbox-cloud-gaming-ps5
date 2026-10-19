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
#include <functional>
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
    // "XBL3.0 x=...;token" for services on http://xboxlive.com (titlehub...).
    // Memory only: never logged or saved.
    std::string xblAuthorization;
};

// The Game Pass plan in an entitlements answer ("Ultimate"...; empty: none).
std::string subscriptionPlan(const std::string& entitlementsJson);
// For the log: whether that plan renews, or in how many days it ends (no
// date: the log may go into public reports). Empty when unknown.
std::string subscriptionRenewal(const std::string& entitlementsJson, int64_t nowUnix);
// Parses playtime limit from JSON or Protobuf response
bool parsePlaytimeLimit(const std::string& body, int64_t& remaining, int64_t& max);

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

    std::string storePath_;
    MsaClient msa_;
    MsaTokens tokens_;
    Profile profile_;
    XblToken gssvXsts_;  // in memory only, for loginOffering()
    std::string redirectUrl_;
    bool signedIn_ = false;
};

}  // namespace xc::auth
