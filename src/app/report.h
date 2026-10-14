// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// "Report a problem": a link to the GitHub bug form, filled in, for a QR
// code (the phone opens it, the user reviews and sends it; no token in the
// app, and it works with the PS5 offline). A QR code holds only so much, so
// the log goes in as an excerpt: its error / warning lines and its last
// lines, filtered for private data.
#pragma once

#include <string>
#include <vector>

namespace xc::app::report {

struct Info {
    std::string title;     // the issue's title
    std::string what;      // what happened (the error shown, or the crash)
    std::string release;   // app version
    std::string firmware;  // PS5 system software, when known
    std::string log;       // logExcerpt()'s
};

// The bug form's link, at most `maxLength` characters: the log is cut from
// its start (its end matters most) to fit.
std::string issueUrl(const Info& info, size_t maxLength = 1800);

// From the whole log (sessions start with "=== PSBox Cloud Gaming starting
// ==="): the current session's or, with `previousSession`, the one before
// (the session that went down); its [E] / [W] lines and last lines.
std::string logExcerpt(const std::string& logText, bool previousSession, size_t lastLines = 20);

// Private data out: `privateWords` (gamertag, user names) as <name>, IPv4
// addresses as <ip>, tokens as <token>, e-mail addresses as <email>, long
// hexadecimal ids as <id>.
std::string sanitize(const std::string& text, const std::vector<std::string>& privateWords);

// `text` cut to `maxBytes` at most, not inside a UTF-8 character.
std::string shorten(const std::string& text, size_t maxBytes);

}  // namespace xc::app::report
