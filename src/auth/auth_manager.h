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
};

class AuthManager {
public:
    // Called while waiting for the user to sign in on another device.
    using DeviceCodeCallback = std::function<void(const DeviceCode&)>;

    explicit AuthManager(std::string storePath, MsaConfig cfg = {});

    bool hasStoredAccount() const { return !tokens_.refreshToken.empty(); }

    // Full sign-in. Uses the stored refresh token when possible; otherwise
    // starts the device-code flow and calls `onCode`. Blocking; `cancel` may be
    // set from another thread to abort the device-code wait.
    bool signIn(xcloud::GssvClient& gssv, const DeviceCodeCallback& onCode, std::string& err,
                const std::atomic<bool>* cancel = nullptr);

    // MSA token for the console-transfer purpose, required by the session
    // /connect step of cloud sessions.
    bool consoleTransferToken(std::string& out, std::string& err);

    void signOut();
    const Profile& profile() const { return profile_; }

private:
    bool load();
    bool save() const;
    bool deviceCodeSignIn(const DeviceCodeCallback& onCode, std::string& err, const std::atomic<bool>* cancel);
    bool xboxChain(xcloud::GssvClient& gssv, std::string& err);
    void fetchGamerpic(const XblToken& xsts);

    std::string storePath_;
    MsaClient msa_;
    MsaTokens tokens_;
    Profile profile_;
};

}  // namespace xc::auth
