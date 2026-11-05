// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include "trophy/trophy_types.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace xc::trophy {

struct ExistingTrophyRecord {
    uint32_t trophyId = 0;
    uint32_t grade = 0;
    uint32_t state = 0; // 0 = locked, 1 = unlocked
    uint64_t timestamp = 0;
};

// Parses a TRPTITLE.DAT buffer to extract existing trophy unlock records and timestamps.
bool parseTrpTitleDat(const uint8_t* data, size_t size, std::map<uint32_t, ExistingTrophyRecord>& outTrophies);

// Returns true if rawData contains a valid PNG signature with 512x512 dimensions.
bool is512x512Png(const uint8_t* data, size_t size);

// Center-crops and resizes raw image data (JPEG or PNG) to a 512x512 RGBA PNG buffer.
bool convertTo512Png(const uint8_t* rawData, size_t rawSize, std::vector<uint8_t>& outPng);

// Builds an nptitle.dat file (160 bytes) for the given titleId.
bool buildNpTitleDat(const std::string& titleId, std::vector<uint8_t>& outNpTitle);

// Builds a Sony PS5 Trophy 2 package (TROPHY.UCP) from a TitleTrophySet.
// If outSha1 is non-null, receives the 20-byte SHA-1 digest written into the UCP header.
bool buildUcp(const TitleTrophySet& set, std::vector<uint8_t>& outUcp, std::vector<uint8_t>* outSha1 = nullptr);

// Builds an npbind.dat file for the given npCommId.
bool buildNpBind(const std::string& npCommId, std::vector<uint8_t>& outNpBind);

// Builds a Sony Universal Data System package (uds.ucp) for the given title.
bool buildUdsUcp(const TitleTrophySet& set, std::vector<uint8_t>& outUdsUcp);

// Builds a PlayStation 5 TRPTITLE.DAT database file from a TitleTrophySet for the given user.
// If ucpSha1 is non-null, writes the 20-byte SHA-1 digest into Tag 0x0100 (matching TROPHY.UCP).
// If existingDat is non-null, parses and preserves existing trophies and timestamps from it.
// If existingDat is null, attempts to read existing TRPTITLE.DAT from /user/home/<userId>/trophy2/nobackup/data/<npCommId>/TRPTITLE.DAT.
bool buildTrpTitleDat(const TitleTrophySet& set, uint32_t userId, std::vector<uint8_t>& outDat,
                      const uint8_t* ucpSha1 = nullptr,
                      const std::vector<uint8_t>* existingDat = nullptr);

// Builds a valid PlayStation 5 param.json metadata buffer for the given titleId and titleName.
std::string buildParamJson(const std::string& titleId, const std::string& titleName);

// Deploys the generated TROPHY.UCP, npbind.dat, TRPTITLE.DAT, and param.json to all required system directories on PS5.
bool deployTrophyPack(const TitleTrophySet& set, uint32_t userId = 0x1ea2f4da);

}  // namespace xc::trophy

