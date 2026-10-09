// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/update_check.h"

#include "net/http.h"
#include "util/json.h"

#include <cstdio>
#include <vector>

namespace xc::app {

namespace {

constexpr const char* kLatestRelease = "https://api.github.com/repos/RafaelNGP/xbox-cloud-gaming-ps5/releases/latest";

// "v1.2.3" / "1.2" -> {1, 2, 3} / {1, 2, 0}; empty when it isn't a version.
std::vector<int> parts(const std::string& v) {
    size_t i = !v.empty() && (v[0] == 'v' || v[0] == 'V') ? 1 : 0;
    std::vector<int> out;
    while (out.size() < 3 && i < v.size() && v[i] >= '0' && v[i] <= '9') {
        int n = 0;
        while (i < v.size() && v[i] >= '0' && v[i] <= '9') n = n * 10 + (v[i++] - '0');
        out.push_back(n);
        if (i < v.size() && v[i] == '.') ++i;
    }
    if (!out.empty()) out.resize(3, 0);
    return out;
}

}  // namespace

bool isNewerVersion(const std::string& tag, const std::string& current) {
    auto a = parts(tag), b = parts(current);
    return !a.empty() && !b.empty() && a > b;
}

std::string contentVersionOf(const std::string& version) {
    auto p = parts(version);
    if (p.empty() || p[0] > 99 || p[1] > 9 || p[2] > 99) return {};
    char out[16];
    std::snprintf(out, sizeof out, "%02d.%d%02d.000", p[0], p[1], p[2]);
    return out;
}

bool findLatestRelease(Release& out, std::string& err, const std::string& feedUrl) {
    net::Request req;
    req.url = feedUrl.empty() ? kLatestRelease : feedUrl;
    req.headers = {{"Accept", "application/vnd.github+json"}, {"User-Agent", "PSBox-Cloud-Gaming"}};
    req.timeoutMs = 10000;
    auto r = net::perform(req);
    if (!r.ok()) {
        err = r.status ? "HTTP " + std::to_string(r.status) : r.error;
        return false;
    }
    auto j = json::parse(r.body);
    if (!j || (*j)["tag_name"].str().empty()) {
        err = "no release in the answer";
        return false;
    }
    out = {};
    out.tag = (*j)["tag_name"].str();
    const auto& assets = (*j)["assets"];
    for (size_t i = 0; i < assets.size(); ++i) {
        const auto& a = assets[i];
        if (a["name"].str() == "PPSA99810.zip") {
            out.zipUrl = a["browser_download_url"].str();
            out.zipSize = static_cast<long>(a["size"].asInt(-1));
        } else if (a["name"].str() == "PPSA99810.zip.sig") {
            out.sigUrl = a["browser_download_url"].str();
        }
    }
    return true;
}

}  // namespace xc::app
