// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/update_check.h"

#include "net/http.h"
#include "util/json.h"

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

std::string latestReleaseTag() {
    net::Request req;
    req.url = kLatestRelease;
    req.headers = {{"Accept", "application/vnd.github+json"}, {"User-Agent", "PSBox-Cloud-Gaming"}};
    req.timeoutMs = 10000;
    auto r = net::perform(req);
    if (!r.ok()) return {};
    auto j = json::parse(r.body);
    return j ? (*j)["tag_name"].str() : std::string();
}

}  // namespace xc::app
