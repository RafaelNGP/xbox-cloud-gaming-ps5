// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// What the home screen shows, in two tabs:
//
//  - Game Pass: "Jump back in" (recently played Game Pass games) and the Game
//    Pass lists xbox.com/play shows. Only Game Pass games: xbox.com's
//    "popular" list mixes in "stream your own game" titles that must be
//    bought first, and those would fail to start.
//  - Your games: what the account can stream outside Game Pass (purchases,
//    free-to-play), recently played first, then alphabetical; and below it
//    the games that stream in the cloud once bought, most popular first.
//
// Each tab searches its own games. Every game carries the console it was
// made for (Xbox 360 / Xbox One / Series X|S), from titlehub.
//
// Phases: load() fetches the catalog rows with the light payload (the home
// screen can show right away) and reads the cache of the previous run, so
// "Your games" and the console badges are there at once; loadOwned()
// refreshes the account's titles (~12 s for ~2700); loadCatalogNames() adds
// names and art for the search and the games to buy; loadPlatforms() asks
// titlehub for the consoles; hydrate() adds hero art and descriptions. All
// but load() can run on another thread, one after the other.
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
    // Called whenever anything the getters return changed.
    using Changed = std::function<void()>;

    // Where the cache lives (read by load(), written by the later phases).
    void setCachePath(std::string path) { cachePath_ = std::move(path); }

    // Calls `changed` after each row arrives; false only when nothing loaded.
    bool load(xcloud::GssvClient& gssv, const std::string& language, const Changed& changed, std::string& err);
    // The first screen, before anything else: hero art, descriptions and
    // console badges of the first cards of every row and of "Your games".
    void loadFirstScreen(const std::string& xblAuth, const Changed& changed, const std::atomic<bool>* stop = nullptr);
    // The account's titles: what it can play, what it could buy. Takes its own
    // GssvClient (a copy) so it can run on another thread.
    void loadOwned(xcloud::GssvClient gssv, const Changed& changed, const std::atomic<bool>* stop = nullptr);
    // Names and art of the whole Game Pass catalog and of the games to buy.
    void loadCatalogNames(const Changed& changed, const std::atomic<bool>* stop = nullptr);
    // Console generation of every known game; `xblAuth` from the profile.
    void loadPlatforms(const std::string& xblAuth, const Changed& changed, const std::atomic<bool>* stop = nullptr);
    // Hero art, descriptions, play modes and languages (the full details), in
    // batches: the rows, the account's games, then the rest of the search's
    // games; stops early when `stop` is set.
    void hydrate(const Changed& changed, const std::atomic<bool>* stop = nullptr);
    // Active friends playing cloud-accessible games now.
    void loadFriends(const std::string& xblAuth, const Changed& changed, const std::atomic<bool>* stop = nullptr);

    std::vector<ui::GameRow> rows() const;
    std::vector<ui::GameTile> owned() const;
    // Games that stream once bought, most popular first.
    std::vector<ui::GameTile> purchasable() const;
    // What each tab's search looks through.
    std::vector<ui::GameTile> gamePassSearchPool() const;
    std::vector<ui::GameTile> librarySearchPool() const;
    // True once the account's games are known (from the cache or the service).
    bool ownedKnown() const { return ownershipKnown_; }
    // The background refresh of the lists: whether it runs, and (while
    // names come in) how far it is; total 0 = no measure yet.
    struct Progress {
        bool active = false;
        size_t done = 0, total = 0;
    };
    Progress progress() const { return progress_; }

private:
    using Item = std::pair<std::string, std::string>;  // productId, titleId
    struct RowIds {
        std::string title;
        bool badges = true;
        bool isGrid = false;
        std::vector<Item> items;
    };
    ui::GameTile tile(const std::string& productId, const std::string& titleId = {}) const;
    std::vector<ui::GameTile> tiles(const std::vector<Item>& items) const;
    // Full details (hero art, description) for `ids` lacking them, in batches,
    // `changed` after each; false on a network error.
    bool fetchFull(const std::vector<std::string>& ids, const Changed& changed, const std::atomic<bool>* stop,
                   size_t firstBatch = 20);
    // titlehub for the products in `ids` whose console isn't known yet.
    void fetchPlatformsFor(const std::vector<std::string>& ids, const std::string& xblAuth);
    void sortOwned();
    bool loadCache();
    void saveCache() const;

    std::string cachePath_;
    std::string market_, language_;
    std::vector<RowIds> layout_;
    std::map<std::string, xcloud::Product> products_;
    std::vector<std::string> allGames_;  // the Game Pass catalog, in its order
    std::set<std::string> gamePass_;     // ... as a set
    std::vector<std::string> popular_;   // "Most popular on cloud", all of it (Game Pass or not)
    std::vector<std::string> recent_;    // recently played product ids, newest first
    // The account (loadOwned() or the cache).
    bool ownershipKnown_ = false;
    bool ownershipFresh_ = false;            // the account's list of this start (not the cache's) is in
    std::set<std::string> knownGamePass_;    // the Game Pass catalog at the last start (cache)
    std::set<std::string> ownedTitles_, ownedProducts_;
    std::vector<Item> owned_;        // outside Game Pass
    std::vector<Item> purchasable_;  // streamable once bought
    std::set<std::string> purchasableSet_;
    std::set<std::string> freeInStore_;  // product ids free in the store
    // Details from a cache in another language: shown until replaced.
    std::set<std::string> otherLanguage_;
    Progress progress_;
    std::map<std::string, std::string> xboxTitleOf_;  // productId -> Xbox title id
    std::map<std::string, std::string> platform_;     // Xbox title id -> platform code
    // Xbox title ids that have a "... - Xbox Series X|S" product, rebuilt
    // when products_ grows.
    mutable std::set<std::string> seriesSiblings_;
    mutable std::set<std::string> sharedXbox_;  // Xbox title ids two products share (cross-gen pairs)
    mutable size_t siblingsFor_ = 0;
    std::vector<ui::GameTile> friendsTiles_;
};

}  // namespace xc::app
