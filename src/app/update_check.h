// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Is there a newer release of the app on GitHub?
#pragma once

#include <string>

namespace xc::app {

// "v0.4.0" against "0.3.0": true when `tag` is a later version. Tags that
// aren't versions are never newer.
bool isNewerVersion(const std::string& tag, const std::string& current);

// The tag of the latest GitHub release, or empty (no network, rate limit).
std::string latestReleaseTag();

}  // namespace xc::app
