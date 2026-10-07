// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Microsoft account (MSA) OAuth via login.live.com: device-code sign-in and
// refresh. The device-code flow fits a console: the TV shows a short code
// (and a QR code) and the user signs in with their real account on a phone.
#pragma once

#include <cstdint>
#include <string>

namespace xc::auth {

struct MsaConfig {
    // Public client of the Xbox Game Pass mobile app, the same family of IDs
    // used by open-source Xbox clients. Overridable for testing.
    std::string clientId = "000000004c20a908";
    std::string scope = "service::user.auth.xboxlive.com::MBI_SSL";
};

struct DeviceCode {
    std::string userCode;
    std::string deviceCode;
    std::string verificationUri;  // e.g. https://www.microsoft.com/link
    int intervalSec = 5;
    int expiresInSec = 900;
};

struct MsaTokens {
    std::string accessToken;
    std::string refreshToken;
    std::string userId;
    int64_t expiresAt = 0;  // unix seconds
};

enum class PollResult { Pending, SlowDown, Success, Expired, Denied, Error };

class MsaClient {
public:
    explicit MsaClient(MsaConfig cfg = {}) : cfg_(std::move(cfg)) {}

    bool startDeviceCode(DeviceCode& out, std::string& err) const;
    PollResult pollDeviceCode(const DeviceCode& dc, MsaTokens& out, std::string& err) const;
    // `scope` empty = the configured sign-in scope. `networkError` (optional)
    // tells a transport failure apart from a rejected token.
    bool refresh(const std::string& refreshToken, MsaTokens& out, std::string& err,
                 const std::string& scope = {}, bool* networkError = nullptr) const;

    const MsaConfig& config() const { return cfg_; }

private:
    MsaConfig cfg_;
};

int64_t unixNow();

}  // namespace xc::auth
