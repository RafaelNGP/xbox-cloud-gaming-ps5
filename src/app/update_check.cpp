// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/update_check.h"

#include "net/http.h"
#include "util/json.h"

#include <cstdio>
#include <vector>

namespace xc::app {

namespace {

constexpr const char* kLatestRelease = "https://api.github.com/repos/RafaelNGP/xbox-cloud-gaming-ps5/releases/latest";

// "v1.2.3" / "1.2" -> {1, 2, 3} / {1, 2, 0}; empty when it isn't a version.
std::vector<int> parts(const std::string& v) {
    size_t i = !v.empty() && (v[0] == 'v' || v[0] == 'V') ? 1 : 0;
    std::vector<int> out;
    while (out.size() < 3 && i < v.size() && v[i] >= '0' && v[i] <= '9') {
        int n = 0;
        while (i < v.size() && v[i] >= '0' && v[i] <= '9') n = n * 10 + (v[i++] - '0');
        out.push_back(n);
        if (i < v.size() && v[i] == '.') ++i;
    }
    if (!out.empty()) out.resize(3, 0);
    return out;
}

}  // namespace

bool isNewerVersion(const std::string& tag, const std::string& current) {
    auto a = parts(tag), b = parts(current);
    return !a.empty() && !b.empty() && a > b;
}

std::string contentVersionOf(const std::string& version) {
    auto p = parts(version);
    if (p.empty() || p[0] > 99 || p[1] > 9 || p[2] > 99) return {};
    char out[16];
    std::snprintf(out, sizeof out, "%02d.%d%02d.000", p[0], p[1], p[2]);
    return out;
}

bool findLatestRelease(Release& out, std::string& err, const std::string& feedUrl) {
    net::Request req;
    req.url = feedUrl.empty() ? kLatestRelease : feedUrl;
    req.headers = {{"Accept", "application/vnd.github+json"}, {"User-Agent", "PSBox-Cloud-Gaming"}};
    req.timeoutMs = 10000;
    auto r = net::perform(req);
    if (!r.ok()) {
        err = r.status ? "HTTP " + std::to_string(r.status) : r.error;
        return false;
    }
    auto j = json::parse(r.body);
    if (!j || (*j)["tag_name"].str().empty()) {
        err = "no release in the answer";
        return false;
    }
    out = {};
    out.tag = (*j)["tag_name"].str();
    out.notes = (*j)["body"].str();
    const auto& assets = (*j)["assets"];
    for (size_t i = 0; i < assets.size(); ++i) {
        const auto& a = assets[i];
        if (a["name"].str() == "PPSA99810.zip") {
            out.zipUrl = a["browser_download_url"].str();
            out.zipSize = static_cast<long>(a["size"].asInt(-1));
        } else if (a["name"].str() == "PPSA99810.zip.sig") {
            out.sigUrl = a["browser_download_url"].str();
        }
    }
    return true;
}

std::string filterReleaseNotes(const std::string& notes, const std::string& langCode) {
    if (notes.empty()) return {};

    struct Section {
        std::string tag;
        std::string content;
    };
    std::vector<Section> sections;

    size_t pos = 0;
    while (pos < notes.size()) {
        size_t commentStart = notes.find("<!--", pos);
        if (commentStart == std::string::npos) break;

        size_t commentEnd = notes.find("-->", commentStart + 4);
        if (commentEnd == std::string::npos) break;

        size_t p = commentStart + 4;
        while (p < commentEnd && (notes[p] == ' ' || notes[p] == '\t' || notes[p] == '\r' || notes[p] == '\n')) ++p;

        if (p + 4 <= commentEnd &&
            (notes[p] == 'L' || notes[p] == 'l') &&
            (notes[p+1] == 'A' || notes[p+1] == 'a') &&
            (notes[p+2] == 'N' || notes[p+2] == 'n') &&
            (notes[p+3] == 'G' || notes[p+3] == 'g')) {
            p += 4;
            while (p < commentEnd && (notes[p] == ' ' || notes[p] == '\t')) ++p;
            if (p < commentEnd && notes[p] == ':') {
                ++p;
                while (p < commentEnd && (notes[p] == ' ' || notes[p] == '\t')) ++p;
                size_t tagStart = p;
                while (p < commentEnd && notes[p] != ' ' && notes[p] != '\t' && notes[p] != '\r' && notes[p] != '\n' && notes[p] != '-') {
                    ++p;
                }
                if (p < commentEnd && notes[p] == '-' && p + 1 < commentEnd && notes[p+1] != '-') {
                    ++p;
                    while (p < commentEnd && notes[p] != ' ' && notes[p] != '\t' && notes[p] != '\r' && notes[p] != '\n' && notes[p] != '-') {
                        ++p;
                    }
                }
                std::string tag;
                for (size_t ti = tagStart; ti < p; ++ti) {
                    char c = notes[ti];
                    if (c == '_') c = '-';
                    tag += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                }

                if (!tag.empty()) {
                    if (!sections.empty()) {
                        sections.back().content = notes.substr(pos, commentStart - pos);
                    }
                    sections.push_back({tag, {}});
                    pos = commentEnd + 3;
                    continue;
                }
            }
        }
        pos = commentEnd + 3;
    }

    if (sections.empty()) {
        return notes;
    }

    sections.back().content = notes.substr(pos);

    auto trim = [](std::string s) -> std::string {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
            s.pop_back();
        }
        size_t lead = 0;
        while (lead < s.size() && (s[lead] == ' ' || s[lead] == '\t' || s[lead] == '\r' || s[lead] == '\n')) {
            ++lead;
        }
        return lead > 0 ? s.substr(lead) : s;
    };

    for (auto& sec : sections) {
        sec.content = trim(sec.content);
    }

    std::string primaryCode;
    for (char c : langCode) {
        if (c == '-' || c == '_') break;
        primaryCode += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    std::string fullCode;
    for (char c : langCode) {
        if (c == '_') c = '-';
        fullCode += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    auto findSec = [&](const std::string& t) -> const Section* {
        for (const auto& sec : sections) {
            if (sec.tag == t) return &sec;
        }
        return nullptr;
    };

    if (!fullCode.empty()) {
        if (auto* s = findSec(fullCode)) return s->content;
    }
    if (!primaryCode.empty()) {
        if (auto* s = findSec(primaryCode)) return s->content;
    }
    if (auto* s = findSec("EN")) return s->content;
    if (auto* s = findSec("EN-US")) return s->content;

    return sections[0].content;
}

}  // namespace xc::app
