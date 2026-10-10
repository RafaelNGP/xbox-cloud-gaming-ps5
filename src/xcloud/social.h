// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Xbox Live Social & Presence via PeopleHub API.
#pragma once

#include <string>
#include <vector>

namespace xc::xcloud {

struct FriendPresence {
    std::string xuid;
    std::string gamertag;
    std::string gamerpicUrl;
    std::string presenceState;  // "Online", "Offline"
    std::string presenceText;
    std::string titleId;        // Xbox Live Title ID (decimal string, e.g. "1828326430")
    std::string device;
    bool isGame = false;
};

// Fetches the signed-in user's friends list and their active presence.
// Uses PeopleHub API with contract version 2.
// `xblAuth`: "XBL3.0 x=...;token" for http://xboxlive.com.
bool fetchFriendsPresence(const std::string& xblAuth, std::vector<FriendPresence>& out, std::string& err,
                          const std::string& language = "en-US");

}  // namespace xc::xcloud
