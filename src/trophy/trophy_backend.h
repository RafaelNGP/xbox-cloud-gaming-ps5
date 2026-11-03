// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include "trophy/trophy_types.h"
#include <cstdint>
#include <string>

namespace xc::trophy {

class ITrophyBackend {
public:
    virtual ~ITrophyBackend() = default;

    // Initializes backend with active PlayStation user ID (e.g. 0x1ea2f4da)
    virtual bool init(uint32_t userId) = 0;

    // Registers the title's trophy set with the system
    virtual bool registerTitle(const TitleTrophySet& set) = 0;

    // Unlocks a trophy for the title (calling native OS API on PS5/PS4)
    virtual bool unlockTrophy(const std::string& npCommId, int trophyId, TrophyGrade grade, const std::string& trophyName = {}) = 0;

    // Checks if trophy is already recorded as unlocked in backend
    virtual bool isTrophyUnlocked(const std::string& npCommId, int trophyId) = 0;

    // Flushes pending trophy commits to the system profile/database
    virtual void flush() {}

    // Backend identifier name
    virtual std::string name() const = 0;
};

// Creates platform-appropriate backend (Prospero PS5, Orbis PS4, or Desktop Null)
ITrophyBackend* createPlatformTrophyBackend();

// Native self-test: register + unlock a trophy set bound to the RUNNING app's
// own title (PSBox PPSA99810) via the normal v1 flow. PS5-only; no-op elsewhere.
void runPsboxNativeSelfTest();

}  // namespace xc::trophy
