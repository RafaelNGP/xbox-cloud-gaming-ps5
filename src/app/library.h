// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// What the home screen shows, in three tabs:
//
//  - Game Pass: "Jump back in" (recently played Game Pass games) and the Game
//    Pass lists xbox.com/play shows. Only Game Pass games: xbox.com's
//    "popular" list mixes in "stream your own game" titles that must be
//    bought first, and those would fail to start.
//  - Your games: what the account can stream outside Game Pass (purchases,
//    free-to-play), recently played first, then alphabetical.
//  - Search: the Game Pass catalog plus the account's games.
//
// Phases: load() fetches the catalog rows with the light payload (the home
// screen can show right away) and reads the account cache of the previous
// run, so "Your games" is there at once; loadOwned() refreshes the account's
// titles (~12 s for ~2700) and rewrites the cache; loadCatalogNames() adds
// the rest of the catalog for the search; hydrate() adds hero art and
// descriptions. The last three can run on another thread, one after the
// other.
#pragma once

#include "ui/app_ui.h"
#include "xcloud/catalog.h"
#include "xcloud/gssv.h"

#include <atomic>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace xc::app {

class Library {
public:
    // Called whenever rows(), owned() or searchPool() changed.
    using Changed = std::function<void()>;

    // Where the account cache lives (read by load(), written by loadOwned()).
    void setCachePath(std::string path) { cachePath_ = std::move(path); }

    // Calls `changed` after each row arrives; false only when nothing loaded.
    bool load(xcloud::GssvClient& gssv, const std::string& language, const Changed& changed, std::string& err);
    // The account's titles: which games it can play, and "Your games". Takes
    // its own GssvClient (a copy) so it can run on another thread.
    void loadOwned(xcloud::GssvClient gssv, const Changed& changed, const std::atomic<bool>* stop = nullptr);
    // Names and art of the whole Game Pass catalog, for the search.
    void loadCatalogNames(const Changed& changed, const std::atomic<bool>* stop = nullptr);
    // Hero art and descriptions, in batches; stops early when `stop` is set.
    void hydrate(const Changed& changed, const std::atomic<bool>* stop = nullptr);

    std::vector<ui::GameRow> rows() const;
    std::vector<ui::GameTile> owned() const;
    std::vector<ui::GameTile> searchPool() const;
    // True once the account's games are known (from the cache or the service).
    bool ownedKnown() const { return ownershipKnown_; }

private:
    struct RowIds {
        std::string title;
        bool badges = true;
        std::vector<std::pair<std::string, std::string>> items;  // productId, titleId
    };
    ui::GameTile tile(const std::string& productId, const std::string& titleId) const;
    void sortOwned();
    bool loadCache();
    void saveCache() const;

    std::string cachePath_;
    std::string market_, language_;
    std::vector<RowIds> layout_;
    std::map<std::string, xcloud::Product> products_;
    std::vector<std::string> allGames_;  // the Game Pass catalog, in its order
    std::set<std::string> gamePass_;     // ... as a set
    std::vector<std::string> recent_;    // recently played product ids, newest first
    // The account (loadOwned() or the cache).
    bool ownershipKnown_ = false;
    std::set<std::string> ownedTitles_, ownedProducts_;
    std::vector<std::pair<std::string, std::string>> owned_;  // productId, titleId outside Game Pass
};

}  // namespace xc::app
