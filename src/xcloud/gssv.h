// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Client for the xCloud game-streaming service ("gssv"): login, title list,
// and the session lifecycle (provision -> connect -> SDP/ICE -> keepalive).
//
// There is no public documentation; these are the endpoints the xbox.com/play
// web client uses, as also implemented by open-source clients. The service may
// change them, so every response is checked defensively.
#pragma once

#include "auth/xbox_live.h"

#include <string>
#include <vector>

namespace xc::xcloud {

struct Region {
    std::string name;
    std::string baseUri;  // https://<region>.core.gssv-play-prod.xboxlive.com
    bool isDefault = false;
};

struct GssvLogin {
    std::string gsToken;
    std::string market;
    int64_t expiresAt = 0;  // unix seconds
    std::vector<Region> regions;

    const Region* defaultRegion() const;
};

struct Title {
    std::string titleId;    // gssv id used to start a session
    std::string productId;  // Microsoft Store "big id", used for names/art
    std::string name;       // filled in by hydrateTitles()
    std::string imageUrl;   // box art / tile
    bool hasEntitlement = false;
    std::string xboxTitleId;  // Xbox Live title id (titlehub)
    bool isFreeInStore = false;
    // Playable through a subscription (the title's userPrograms has one
    // besides F2P, such as CALLISTO for Game Pass). Entitled but not this:
    // the account's own game, bought or free-to-play.
    bool viaSubscription = false;
};

enum class SessionState { Unknown, Provisioning, WaitingForResources, ReadyToConnect, Provisioned, Failed };

struct SessionStatus {
    SessionState state = SessionState::Unknown;
    std::string raw;          // state string as sent by the service
    std::string errorCode;    // when Failed
    std::string errorMessage;
};

struct IceCandidate {
    std::string candidate;
    std::string sdpMid;
    int sdpMLineIndex = 0;
};

// Stream resolution the service is asked for. It decides from the device the
// client claims to be: a Windows desktop gets 1080p, other devices 720p.
// P1080HQ: 1080p as a Samsung TV (Tizen) asks for it, which the service
// streams at a higher bitrate (Better xCloud's "1080p (HQ)").
enum class Resolution { P1080, P720, P1440, P1080HQ };

class GssvClient {
public:
    // `offering` is "xgpuweb" (Game Pass cloud) or "xhome" (own console).
    explicit GssvClient(std::string offering = "xgpuweb") : offering_(std::move(offering)) {}

    bool login(const auth::XblToken& gssvXsts, std::string& err);
    const GssvLogin& session() const { return login_; }
    void setRegion(const Region& r) { region_ = r; }
    void setResolution(Resolution r) { resolution_ = r; }
    Resolution resolution() const { return resolution_; }
    const Region& region() const { return region_; }

    // Titles available to stream (entitled + Game Pass) and recently played.
    bool listTitles(std::vector<Title>& out, std::string& err, bool recentOnly = false);
    // Fills name/imageUrl from the public Microsoft Store display catalog.
    bool hydrateTitles(std::vector<Title>& titles, const std::string& market, const std::string& lang,
                       std::string& err);

    // --- Session lifecycle -------------------------------------------------
    bool startSession(const std::string& titleId, const std::string& locale, std::string& err);
    bool hasSession() const { return !sessionPath_.empty(); }
    bool sessionState(SessionStatus& out, std::string& err);
    // Estimated queue time in seconds while WaitingForResources (-1 unknown).
    int waitTimeSeconds();
    // Required in ReadyToConnect for cloud sessions: an MSA token for the
    // console-transfer purpose (see AuthManager::consoleTransferToken).
    bool connect(const std::string& msaTransferToken, std::string& err);
    bool sendSdpOffer(const std::string& sdp, std::string& err);
    // Returns true with an empty `answer` while the service has not replied.
    bool pollSdpAnswer(std::string& answer, std::string& err);
    bool sendIceCandidates(const std::vector<IceCandidate>& candidates, std::string& err);
    bool pollIceCandidates(std::vector<IceCandidate>& out, std::string& err);
    bool keepalive(std::string& err);
    void stopSession();

private:
    std::string url(const std::string& path) const;
    std::string sessionUrl(const std::string& suffix) const;

    std::string offering_;
    Resolution resolution_ = Resolution::P1080;
    GssvLogin login_;
    Region region_;
    std::string sessionPath_;
};

const char* toString(SessionState s);

}  // namespace xc::xcloud
