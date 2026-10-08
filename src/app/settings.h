// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// User settings, kept in <dataDir>/settings.json.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace xc::app {

struct Settings {
    std::string language = "en";     // ui::languageCode()
    std::string resolution = "1080p";  // "1080p", "720p" or "1440p" (experimental)
    std::string region;                // gssv region name; empty = automatic
    // Round trip to the stream server measured in past sessions, per region (ms).
    std::map<std::string, int> regionRtt;
    // Games hidden with Square (product ids), and the "Your games" order
    // ("recent", "az", "console").
    std::vector<std::string> hidden;
    std::string librarySort = "recent";
    // The statistics line over the game (in-game menu).
    bool streamStats = false;

    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

}  // namespace xc::app
