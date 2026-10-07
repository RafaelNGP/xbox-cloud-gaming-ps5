// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// User-facing text. English is the default; other languages add a column to
// the table in strings.cpp and are picked with setLanguage().
#pragma once

#include <string>

namespace xc::ui {

enum class Str {
    AppName,
    CloudGaming,
    SigningIn,
    RequestingCode,
    LoadingGames,
    SignInTitle,
    SignInStep1,
    SignInStep2,
    SignInScan,
    SignInWaiting,
    JumpBackIn,
    Play,
    Back,
    Select,
    SignOut,
    Cancel,
    TryAgain,
    GettingReady,
    InQueue,       // "%s" = estimated wait
    Connecting,
    StartingStream,
    LeaveHint,
    StreamEnded,
    ErrorTitle,
    SignedInAs,    // "%s" = gamertag
    NoGames,
    PublisherBy,   // "%s" = publisher
    Count
};

enum class Language { English };

void setLanguage(Language lang);
Language language();
// Catalog language tag for the current language, e.g. "en-us".
const char* catalogLanguage();

const char* tr(Str id);
// tr() with one "%s" substituted.
std::string trf(Str id, const std::string& arg);

}  // namespace xc::ui
