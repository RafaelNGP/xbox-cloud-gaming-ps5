// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Is there a newer release of the app on GitHub?
#pragma once

#include <string>

namespace xc::app {

// "v0.4.0" against "0.3.0": true when `tag` is a later version. Tags that
// aren't versions are never newer.
bool isNewerVersion(const std::string& tag, const std::string& current);

// The package's contentVersion (sce_sys/param.json) for a version:
// "0.8.1" -> "00.801.000", as tools/release.sh writes it; empty when it
// isn't one.
std::string contentVersionOf(const std::string& version);

// A release and the files the updater needs from it.
struct Release {
    std::string tag;     // "v0.9.0"
    std::string zipUrl;  // PPSA99810.zip
    std::string sigUrl;  // PPSA99810.zip.sig, its signature
    long zipSize = -1;
};

// The latest GitHub release, or the release `feedUrl` describes (the same
// JSON; the update test's local server). False with `err` when there is
// none or it can't be asked (no network, rate limit).
bool findLatestRelease(Release& out, std::string& err, const std::string& feedUrl = {});

}  // namespace xc::app
