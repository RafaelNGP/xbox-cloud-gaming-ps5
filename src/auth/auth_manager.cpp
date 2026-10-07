#include "auth/auth_manager.h"

#include "platform/platform.h"
#include "util/json.h"
#include "util/log.h"

#include <cstdio>

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
    if (!platform::readFile(storePath_, text)) return false;
    auto j = json::parse(text);
    if (!j) {
        XC_LOGW("ignoring unreadable token store %s", storePath_.c_str());
        return false;
    }
    // A refresh token issued to another client id is useless: start over.
    if ((*j)["clientId"].str() != msa_.config().clientId) return false;
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
        XC_LOGE("could not write token store %s", storePath_.c_str());
        return false;
    }
    return true;
}

void AuthManager::signOut() {
    tokens_ = {};
    profile_ = {};
    std::remove(storePath_.c_str());
}

bool AuthManager::deviceCodeSignIn(const DeviceCodeCallback& onCode, std::string& err,
                                   const std::atomic<bool>* cancel) {
    DeviceCode dc;
    if (!msa_.startDeviceCode(dc, err)) return false;
    XC_LOGI("device code %s at %s (expires in %ds)", dc.userCode.c_str(), dc.verificationUri.c_str(),
            dc.expiresInSec);
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
            case PollResult::SlowDown: interval += 5; break;
            case PollResult::Expired: err = "the sign-in code expired; try again"; return false;
            case PollResult::Denied: err = "sign-in was declined"; return false;
            case PollResult::Error: err = pollErr; return false;
        }
    }
    err = "the sign-in code expired; try again";
    return false;
}

bool AuthManager::xboxChain(xcloud::GssvClient& gssv, std::string& err) {
    XblToken user;
    // login.live.com tokens for a custom client id use the "t=" ticket form;
    // fall back to "d=" in case the service expects the MSAL form.
    if (!xblUserToken(tokens_.accessToken, "t=", user, err)) {
        std::string err2;
        if (!xblUserToken(tokens_.accessToken, "d=", user, err2)) return false;
        err.clear();
    }

    XblToken profileXsts;
    std::string perr;
    if (xstsToken(user, rp::kXboxLive, profileXsts, perr)) {
        profile_.gamertag = profileXsts.gamertag;
        profile_.xuid = profileXsts.xuid;
    } else {
        XC_LOGW("profile XSTS: %s", perr.c_str());
    }

    XblToken gssvXsts;
    if (!xstsToken(user, rp::kGssv, gssvXsts, err)) return false;
    return gssv.login(gssvXsts, err);
}

bool AuthManager::signIn(xcloud::GssvClient& gssv, const DeviceCodeCallback& onCode, std::string& err,
                         const std::atomic<bool>* cancel) {
    bool fresh = false;
    if (!tokens_.refreshToken.empty()) {
        MsaTokens t;
        bool networkError = false;
        if (msa_.refresh(tokens_.refreshToken, t, err, {}, &networkError)) {
            tokens_ = t;
        } else if (networkError) {
            // Offline or the service is down: keep the stored account.
            return false;
        } else {
            XC_LOGW("stored sign-in no longer valid (%s); asking again", err.c_str());
            tokens_ = {};
        }
    }
    if (tokens_.accessToken.empty()) {
        if (!deviceCodeSignIn(onCode, err, cancel)) return false;
        fresh = true;
    }
    if (!xboxChain(gssv, err)) {
        // A just-granted token that fails means the account itself is the
        // problem (no Xbox profile, no Game Pass...): keep it for diagnosis.
        if (!fresh) XC_LOGW("Xbox sign-in failed with stored token: %s", err.c_str());
        save();
        return false;
    }
    save();
    XC_LOGI("signed in as %s", profile_.gamertag.empty() ? "(unknown gamertag)" : profile_.gamertag.c_str());
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
