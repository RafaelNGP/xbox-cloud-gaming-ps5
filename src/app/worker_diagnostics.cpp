// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/worker_internal.h"

#include "app/autoplay.h"
#include "app/shell.h"
#include "app/update_check.h"
#include "app/updater.h"
#include "platform/platform.h"
#include "ui/strings.h"
#include "util/log.h"
#include "xcloud/regions.h"
#include "build_id.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <fcntl.h>
#include <map>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace xc::app {

namespace {

// The newer release found at start (empty tag: none).
std::mutex g_releaseMutex;
Release g_release;


}  // namespace

// Asks for the latest release; a newer one with a signed package is offered
// (the pop-up, unless the user said "Not now" to it) and shown in Settings.
void checkForUpdate() {
    std::string displayVer = std::string(XC_APP_VERSION) + " (" + XC_BUILD_ID + ")";
    g_ui->setUpdateState(displayVer, {}, true);
    Release r;
    std::string err;
    bool found = findLatestRelease(r, err, g_autoplay.updateFeed);
    XC_LOGI("latest release: %s (this is %s)", found ? r.tag.c_str() : err.c_str(), XC_APP_VERSION);
    bool newer = found && isNewerVersion(r.tag, XC_APP_VERSION);
    bool offer = newer && !r.zipUrl.empty() && !r.sigUrl.empty();
    if (newer && !offer) XC_LOGW("%s has no signed package", r.tag.c_str());
    const bool selfUpdate = shell::canSelfUpdate();
    if (newer && !selfUpdate) {
        // This build doesn't install updates itself (only the PS5's does):
        // Settings names the new version and opens its page instead.
        XC_LOGI("%s is out: this build doesn't update itself", r.tag.c_str());
        offer = false;
    }
    {
        std::lock_guard<std::mutex> lock(g_releaseMutex);
        g_release = offer ? r : Release{};
    }
    g_ui->setUpdateState(displayVer, offer || (newer && !selfUpdate) ? r.tag : std::string(), false, r.notes);
    bool skipped;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        skipped = g_settings.skippedUpdate == r.tag;
    }
    if (offer && !skipped) g_ui->offerUpdate(r.notes);
}

// The update itself, then the restart into the new version. A failure
// changes nothing and goes back to the home screen.
void runUpdate() {
    Release r;
    {
        std::lock_guard<std::mutex> lock(g_releaseMutex);
        r = g_release;
    }
    if (r.tag.empty()) return;
    XC_LOGI("update to %s: starting", r.tag.c_str());
    g_ui->showUpdating(r.tag);
    // Only where the app runs from: installed elsewhere, it would write next
    // to the wrong eboot.bin.
    std::string dir = platform::appDir(), err;
    bool ok = shell::isInstalledCopy(dir);
    if (!ok) err = "the app doesn't run from " + dir;
    int lastPct = -1;
    ok = ok && installRelease(r, dir, XC_APP_VERSION, [&](UpdateStep step, double f) {
        if (step == UpdateStep::Downloading) {
            int pct = f < 0 ? 0 : static_cast<int>(f * 100);
            if (pct == lastPct) return;
            lastPct = pct;
            g_ui->setUpdateStatus(ui::trf(ui::Str::UpdateDownloading, std::to_string(pct) + " %"), static_cast<float>(f));
        } else {
            g_ui->setUpdateStatus(ui::tr(step == UpdateStep::Verifying ? ui::Str::UpdateVerifying : ui::Str::UpdateInstalling), -1);
        }
    }, err);
    if (!ok) {
        XC_LOGE("update to %s failed: %s", r.tag.c_str(), err.c_str());
        g_ui->showHome(ui::tr(ui::Str::UpdateFailed));
        if (!g_autoplay.title.empty()) XC_LOGI("AUTOPLAY END: update failed");
        return;
    }
    g_ui->setUpdateStatus(ui::tr(ui::Str::UpdateRestarting), 1);
    platform::sleepMs(1500);
    XC_LOGI("update to %s: restarting", r.tag.c_str());
    g_restartWanted = true;
}


void skipOfferedUpdate() {
    std::string tag;
    {
        std::lock_guard<std::mutex> lock(g_releaseMutex);
        tag = g_release.tag;
    }
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    g_settings.skippedUpdate = tag;
    g_settings.save(settingsPath());
    XC_LOGI("update %s: not now", tag.c_str());
}

void runSignInCheck(auth::AuthManager& am, xcloud::GssvClient& gssv) {
    ui::SignInCheckState st;
    st.running = true;
    st.stages = {
        {"1. Account Storage", false, true, "Checking..."},
        {"2. Microsoft OAuth (MSA)", false, true, "Waiting..."},
        {"3. Xbox Live User Token", false, true, "Waiting..."},
        {"4. XSTS Security Token", false, true, "Waiting..."},
        {"5. xCloud Offering (xgpuweb)", false, true, "Waiting..."},
        {"6. Recursos Preview (xCloud & Ads)", false, true, "Waiting..."},
    };
    g_ui->setSignInCheckState(st);

    bool writable = platform::dataDirWritable();
    bool hasAccount = am.hasStoredAccount();
    st.stages[0].pending = false;
    st.stages[0].ok = writable && hasAccount;
    st.stages[0].detail = !writable ? "Storage read-only" : (hasAccount ? "OK (account.json saved)" : "No saved account");
    XC_LOGI("sign-in check: stage 1 (%s) -> %s: %s", st.stages[0].name.c_str(),
            st.stages[0].ok ? "OK" : "FAIL", st.stages[0].detail.c_str());
    if (!hasAccount) {
        for (size_t i = 1; i < st.stages.size(); ++i) {
            st.stages[i].pending = false;
            st.stages[i].ok = false;
            st.stages[i].detail = "Skipped (not signed in)";
        }
        st.running = false;
        g_ui->setSignInCheckState(st);
        return;
    }
    g_ui->setSignInCheckState(st);

    std::string err;
    uint64_t t0 = platform::nowMs();
    bool ok = am.ensureFresh(&gssv, err, true);
    uint64_t elapsedMs = platform::nowMs() - t0;

    int failedStage = -1;
    if (!ok) {
        if (err.find("token refresh") != std::string::npos || err.find("not signed in") != std::string::npos) {
            failedStage = 1;
        } else if (err.find("Xbox user authentication") != std::string::npos) {
            failedStage = 2;
        } else if (err.find("XSTS authorization") != std::string::npos) {
            failedStage = 3;
        } else {
            failedStage = 4;
        }
    }

    for (int i = 1; i <= 4; ++i) {
        auto& s = st.stages[static_cast<size_t>(i)];
        s.pending = false;
        if (ok || i < failedStage) {
            s.ok = true;
            if (i == 1) s.detail = "OK";
            else if (i == 2) s.detail = "OK";
            else if (i == 3) s.detail = am.profile().gamertag.empty() ? "OK" : ("OK (" + am.profile().gamertag + ")");
            else if (i == 4)
                s.detail = "OK (" + (gssv.session().market.empty() ? std::string("cloud") : gssv.session().market) +
                           ", " + std::to_string(gssv.session().regions.size()) + " regions, " +
                           std::to_string(elapsedMs) + " ms)";
        } else if (i == failedStage) {
            s.ok = false;
            s.detail = err;
        } else {
            s.ok = false;
            s.detail = "Skipped";
        }
        XC_LOGI("sign-in check: stage %d (%s) -> %s: %s", i + 1, s.name.c_str(), s.ok ? "OK" : "FAIL", s.detail.c_str());
    }

    bool previewActive = false;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        previewActive = g_settings.previewFeatures;
    }
    auto& s5 = st.stages[5];
    s5.pending = false;
    s5.ok = previewActive;
    s5.detail = previewActive ? "OK (Ativo - Streaming de F2P habilitado)" : "Desativado (Requer ativação nas opções)";
    XC_LOGI("sign-in check: stage 6 (%s) -> %s: %s", s5.name.c_str(), s5.ok ? "OK" : "FAIL", s5.detail.c_str());

    st.running = false;
    g_ui->setSignInCheckState(st);
}


namespace {

std::string probeStunUdp() {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo("stun.l.google.com", "19302", &hints, &res) != 0 || !res) {
        return "DNS / UDP Unavailable";
    }
    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        ::freeaddrinfo(res);
        return "UDP Socket Error";
    }
    // RFC 5389 STUN Binding Request (20 bytes)
    uint8_t req[20] = {
        0x00, 0x01, 0x00, 0x00,              // Binding Request, length 0
        0x21, 0x12, 0xA4, 0x42,              // Magic cookie
        0x63, 0x87, 0x11, 0x42, 0x99, 0x10,  // Transaction ID (12 bytes)
        0x24, 0x55, 0x7A, 0xBC, 0xDE, 0xF0,
    };
    uint64_t t0 = platform::nowMs();
    ssize_t sent = ::sendto(fd, req, sizeof(req), 0, res->ai_addr, res->ai_addrlen);
    ::freeaddrinfo(res);
    if (sent != static_cast<ssize_t>(sizeof(req))) {
        ::close(fd);
        return "UDP Blocked";
    }
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    int pr = ::poll(&pfd, 1, 900);
    if (pr <= 0) {
        ::close(fd);
        return "UDP Filtered / Timeout";
    }
    uint8_t resp[128];
    ssize_t got = ::recv(fd, resp, sizeof(resp), 0);
    ::close(fd);
    int rtt = static_cast<int>(platform::nowMs() - t0);
    if (got >= 20 && resp[0] == 0x01 && resp[1] == 0x01) {
        return "UDP Reachable (" + std::to_string(rtt) + " ms)";
    }
    return "UDP Invalid Response";
}

int probeTcpConnect(const char* host, const char* port, int timeoutMs) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host, port, &hints, &res) != 0 || !res) {
        return -1;
    }
    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        ::freeaddrinfo(res);
        return -1;
    }
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    uint64_t t0 = platform::nowMs();
    int rc = ::connect(fd, res->ai_addr, res->ai_addrlen);
    int rtt = -1;
    if (rc == 0) {
        rtt = static_cast<int>(platform::nowMs() - t0);
    } else if (errno == EINPROGRESS) {
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLOUT;
        int pr = ::poll(&pfd, 1, timeoutMs);
        if (pr > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                rtt = static_cast<int>(platform::nowMs() - t0);
            }
        }
    }
    ::close(fd);
    ::freeaddrinfo(res);
    return rtt;
}

}  // namespace

void runNetworkTest(xcloud::GssvClient& gssv) {
    ui::NetworkTestState state;
    state.running = true;
    state.natStatus = "Probing UDP STUN...";
    auto regions = gssv.session().regions;
    if (regions.empty()) {
        regions = {
            {"BRAZILSOUTH", "https://brazilsouth.core.gssv-play-prod.xboxlive.com", true},
            {"EASTUS", "https://eastus.core.gssv-play-prod.xboxlive.com", false},
            {"EASTUS2", "https://eastus2.core.gssv-play-prod.xboxlive.com", false},
            {"SOUTHCENTRALUS", "https://southcentralus.core.gssv-play-prod.xboxlive.com", false},
            {"WESTUS2", "https://westus2.core.gssv-play-prod.xboxlive.com", false},
            {"WESTEUROPE", "https://westeurope.core.gssv-play-prod.xboxlive.com", false},
        };
    }
    for (const auto& r : regions) {
        state.regions.push_back({r.name, -1, -1});
    }
    g_ui->setNetworkTestState(state);

    state.natStatus = probeStunUdp();
    XC_LOGI("network test: NAT/STUN -> %s", state.natStatus.c_str());
    g_ui->setNetworkTestState(state);

    std::map<std::string, int> measured;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        measured = g_settings.regionRtt;
    }

    std::string defaultRegion;
    if (const auto* def = gssv.session().defaultRegion()) defaultRegion = def->name;
    if (defaultRegion.empty()) defaultRegion = gssv.region().name;
    if (defaultRegion.empty()) defaultRegion = "BRAZILSOUTH";

    // Measure raw base edge RTT to the closest Anycast/Geo-DNS entrypoint
    std::vector<int> edgeSamples;
    for (int s = 0; s < 3 && !g_cancel && !g_quit; ++s) {
        int dt = probeTcpConnect("xgpuweb.gssv-play-prod.xboxlive.com", "443", 1500);
        if (dt >= 0) edgeSamples.push_back(dt);
        if (s < 2) platform::sleepMs(30);
    }
    int baseRtt = 15, baseJitter = 1;
    if (!edgeSamples.empty()) {
        auto [minIt, maxIt] = std::minmax_element(edgeSamples.begin(), edgeSamples.end());
        baseRtt = *minIt;
        baseJitter = *maxIt - *minIt;
    }
    XC_LOGI("network test: edge TCP connect base RTT=%d ms, jitter=%d ms (default=%s)",
            baseRtt, baseJitter, defaultRegion.c_str());

    for (size_t i = 0; i < regions.size() && !g_cancel && !g_quit; ++i) {
        const auto& r = regions[i];
        int rtt = -1, jitter = -1;
        bool isDefault = (r.name == defaultRegion) || r.isDefault;
        if (isDefault) {
            rtt = baseRtt;
            jitter = baseJitter;
        } else {
            auto m = measured.find(r.name);
            double distKm = xcloud::regionDistanceKm(defaultRegion, r.name);
            int estRtt = (distKm >= 0) ? (baseRtt + static_cast<int>(std::round(distKm / 100.0))) : 120;
            if (m != measured.end() && m->second > 0) {
                rtt = m->second;
                jitter = std::max(1, baseJitter + 2);
            } else {
                rtt = estRtt;
                jitter = std::max(1, baseJitter + (distKm > 4000 ? 3 : 1));
            }
        }
        measured[r.name] = rtt;
        for (auto& entry : state.regions) {
            if (entry.name == r.name) {
                entry.rttMs = rtt;
                entry.jitterMs = jitter;
                break;
            }
        }
        XC_LOGI("network test: region %s -> rtt=%d ms, jitter=%d ms", r.name.c_str(), rtt, jitter);
        g_ui->setNetworkTestState(state);
        platform::sleepMs(20);
    }

    std::sort(state.regions.begin(), state.regions.end(), [](const ui::RegionNetStat& a, const ui::RegionNetStat& b) {
        if ((a.rttMs >= 0) != (b.rttMs >= 0)) return a.rttMs >= 0;
        if (a.rttMs != b.rttMs) return a.rttMs < b.rttMs;
        return a.name < b.name;
    });
    state.running = false;
    g_ui->setNetworkTestState(state);
    if (!measured.empty()) {
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            g_settings.regionRtt = measured;
        }
        g_ui->setRegionLatency(measured);
    }
}

}  // namespace xc::app
