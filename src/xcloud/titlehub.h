// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Xbox Live titlehub: which console generation a game was made for. The
// store catalogs list Xbox 360 games that run through backward
// compatibility as Xbox One games; titlehub's "devices" still name Xbox 360.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace xc::xcloud {

// Short platform codes for the badges.
constexpr const char* kPlatform360 = "360";
constexpr const char* kPlatformOne = "ONE";
constexpr const char* kPlatformSeries = "XS";

// `auth`: "XBL3.0 x=...;token" for http://xboxlive.com. Fills `out`
// (xbox title id -> platform code) for the ids titlehub knows; batched.
bool fetchPlatforms(const std::string& auth, const std::vector<std::string>& xboxTitleIds,
                    std::map<std::string, std::string>& out, std::string& err);

}  // namespace xc::xcloud
