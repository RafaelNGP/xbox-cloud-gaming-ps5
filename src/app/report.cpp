// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/report.h"

#include <cctype>
#include <cstring>
#include <set>

namespace xc::app::report {

namespace {

constexpr const char* kForm =
    "https://github.com/RafaelNGP/xbox-cloud-gaming-ps5/issues/new?template=bug_report.yml";
constexpr const char* kSessionStart = "=== PSBox Cloud Gaming starting ===";

// A query value: unreserved characters and a few safe ones as they are,
// space as '+', the rest percent-encoded.
std::string encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || (c && std::strchr("-_.~:/@!$'()*,;", c))) out += static_cast<char>(c);
        else if (c == ' ') out += '+';
        else out += '%', out += hex[c >> 4], out += hex[c & 15];
    }
    return out;
}

bool isTokenChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '='; }

}  // namespace

std::string sanitize(const std::string& text, const std::vector<std::string>& privateWords) {
    std::string s = text;
    for (const auto& w : privateWords) {
        if (w.size() < 3) continue;
        for (size_t at = s.find(w); at != std::string::npos; at = s.find(w, at + 6)) s.replace(at, w.size(), "<name>");
    }
    std::string out;
    for (size_t i = 0; i < s.size();) {
        // A token: "eyJ..." (JWT) or what follows "XBL3.0 x=".
        if (s.compare(i, 3, "eyJ") == 0 || s.compare(i, 9, "XBL3.0 x=") == 0) {
            size_t j = i + (s[i] == 'X' ? 9 : 3);
            while (j < s.size() && (isTokenChar(s[j]) || s[j] == ';')) ++j;
            out += "<token>";
            i = j;
            continue;
        }
        // An IPv4 address: four numbers with dots, not part of a longer number.
        if (std::isdigit(static_cast<unsigned char>(s[i])) && (i == 0 || !std::isalnum(static_cast<unsigned char>(s[i - 1])))) {
            size_t j = i;
            int parts = 0;
            while (parts < 4) {
                size_t k = j;
                while (k < s.size() && std::isdigit(static_cast<unsigned char>(s[k])) && k - j < 3) ++k;
                if (k == j) break;
                ++parts;
                j = k;
                if (parts < 4 && j < s.size() && s[j] == '.') ++j;
                else break;
            }
            if (parts == 4 && (j >= s.size() || !std::isdigit(static_cast<unsigned char>(s[j])))) {
                out += "<ip>";
                i = j;
                continue;
            }
        }
        // A long hexadecimal id (24+ digits).
        if (std::isxdigit(static_cast<unsigned char>(s[i])) && (i == 0 || !std::isalnum(static_cast<unsigned char>(s[i - 1])))) {
            size_t j = i;
            while (j < s.size() && std::isxdigit(static_cast<unsigned char>(s[j]))) ++j;
            if (j - i >= 24 && (j >= s.size() || !std::isalnum(static_cast<unsigned char>(s[j])))) {
                out += "<id>";
                i = j;
                continue;
            }
        }
        out += s[i++];
    }
    // E-mail addresses: the word around an '@' with a dot after it.
    for (size_t at = out.find('@'); at != std::string::npos; at = out.find('@', at + 1)) {
        size_t a = at, b = at + 1;
        while (a > 0 && (isTokenChar(out[a - 1]) || out[a - 1] == '+')) --a;
        while (b < out.size() && isTokenChar(out[b])) ++b;
        if (a < at && out.find('.', at) < b) {
            out.replace(a, b - a, "<email>");
            at = a;
        }
    }
    return out;
}

std::string logExcerpt(const std::string& logText, bool previousSession, size_t lastLines) {
    // The sessions' starts; the one wanted runs to the next (or the end).
    std::vector<size_t> starts;
    for (size_t at = logText.find(kSessionStart); at != std::string::npos; at = logText.find(kSessionStart, at + 1)) {
        size_t lineStart = logText.rfind('\n', at);
        starts.push_back(lineStart == std::string::npos ? 0 : lineStart + 1);
    }
    if (starts.empty()) starts.push_back(0);
    size_t idx = starts.size() - 1;
    if (previousSession) {
        if (starts.size() < 2) return {};
        idx = starts.size() - 2;
    }
    size_t from = starts[idx], to = idx + 1 < starts.size() ? starts[idx + 1] : logText.size();
    std::vector<std::string> lines;
    for (size_t at = from; at < to;) {
        size_t nl = logText.find('\n', at);
        if (nl == std::string::npos || nl > to) nl = to;
        if (nl > at) lines.push_back(logText.substr(at, nl - at));
        at = nl + 1;
    }
    // The first line (the version's start), warnings and errors, the game
    // sessions' summaries, and the
    // last lines; in order, each once, each at most 160 characters.
    std::set<size_t> keep;
    for (size_t i = 0; i < lines.size(); ++i)
        if (i == 0 || lines[i].find("[E]") != std::string::npos || lines[i].find("[W]") != std::string::npos ||
            lines[i].find("session summary:") != std::string::npos ||
            i + lastLines >= lines.size())
            keep.insert(i);
    std::string out;
    size_t last = 0;
    for (size_t i : keep) {
        if (!out.empty() && i > last + 1) out += "...\n";
        out += lines[i].substr(0, 160) + "\n";
        last = i;
    }
    return out;
}

std::string issueUrl(const Info& info, size_t maxLength) {
    std::string head = std::string(kForm) + "&title=" + encode(info.title) + "&what=" + encode(info.what) +
                       "&release=" + encode(info.release) + "&firmware=" + encode(info.firmware) + "&log=";
    // The log's end matters most: whole lines are dropped from its start
    // until the link fits.
    std::string log = info.log;
    for (;;) {
        std::string url = head + encode(log);
        if (url.size() <= maxLength || log.empty()) return url.size() <= maxLength ? url : head;
        size_t nl = log.find('\n', log.compare(0, 4, "...\n") == 0 ? 4 : 0);
        log = nl == std::string::npos ? std::string() : "...\n" + log.substr(nl + 1);
    }
}

std::string shorten(const std::string& text, size_t maxBytes) {
    if (text.size() <= maxBytes) return text;
    size_t n = maxBytes;
    while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) --n;  // a continuation byte
    return text.substr(0, n);
}

}  // namespace xc::app::report
