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
    // "XBL3.0 x=...;token" for services on http://xboxlive.com (titlehub...).
    // Memory only: never logged or saved.
    std::string xblAuthorization;
};

// The Game Pass plan in an entitlements answer ("Ultimate"...; empty: none).
std::string subscriptionPlan(const std::string& entitlementsJson);
// For the log: whether that plan renews, or in how many days it ends (no
// date: the log may go into public reports). Empty when unknown.
std::string subscriptionRenewal(const std::string& entitlementsJson, int64_t nowUnix);

class AuthManager {
public:
    // Called while waiting for the user to sign in on another device.
    using DeviceCodeCallback = std::function<void(const DeviceCode&)>;

    explicit AuthManager(std::string storePath, MsaConfig cfg = {});

    bool hasStoredAccount() const { return !tokens_.refreshToken.empty(); }
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

    void signOut();
    const Profile& profile() const { return profile_; }

private:
    bool load();
    bool save() const;
    mutable bool saved_ = true;
    bool deviceCodeSignIn(const DeviceCodeCallback& onCode, std::string& err, const std::atomic<bool>* cancel);
    bool xboxChain(xcloud::GssvClient& gssv, std::string& err);
    void fetchGamerpic(const XblToken& xsts);
    void fetchSubscription(const XblToken& userToken);

    std::string storePath_;
    MsaClient msa_;
    MsaTokens tokens_;
    Profile profile_;
    XblToken gssvXsts_;  // in memory only, for loginOffering()
};

}  // namespace xc::auth
