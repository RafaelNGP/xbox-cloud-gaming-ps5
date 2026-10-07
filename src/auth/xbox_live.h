// Xbox Live token exchange: MSA access token -> Xbox user token -> XSTS
// token for a relying party (gssv for xCloud, xboxlive.com for profile data).
#pragma once

#include <string>

namespace xc::auth {

struct XblToken {
    std::string token;
    std::string userHash;  // "uhs"
    std::string gamertag;  // only for relying parties that return it
    std::string xuid;
    std::string notAfter;  // ISO-8601

    // Value of the Authorization header used by *.xboxlive.com services.
    std::string authorizationHeader() const { return "XBL3.0 x=" + userHash + ";" + token; }
};

namespace rp {
constexpr const char* kGssv = "http://gssv.xboxlive.com/";
constexpr const char* kXboxLive = "http://xboxlive.com";
}  // namespace rp

// `rpsPrefix` is "t=" for login.live.com tokens issued to a custom client id,
// "d=" for MSAL/v2.0 tokens.
bool xblUserToken(const std::string& msaAccessToken, const std::string& rpsPrefix, XblToken& out,
                  std::string& err);
bool xstsToken(const XblToken& userToken, const std::string& relyingParty, XblToken& out,
               std::string& err);

}  // namespace xc::auth
