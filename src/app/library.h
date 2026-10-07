// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Builds the home screen rows: "Jump back in" (the account's recently played
// cloud titles), "Your games" (what the account owns outside Game Pass:
// purchases, free-to-play) and the Game Pass lists xbox.com/play shows.
//
// The Game Pass rows only hold Game Pass games: xbox.com's "popular" list
// mixes in "stream your own game" titles that must be bought first, and
// those would fail to start.
//
// Three phases: load() fetches the catalog rows with the light payload (the
// home screen can show right away); loadOwned() then reads the account's
// titles (~12 s for ~2700) and adds "Your games" and the playable flags;
// hydrate() adds hero art and descriptions. The last two can run on their
// own thread, one after the other.
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
    using RowsCallback = std::function<void(const std::vector<ui::GameRow>&)>;

    // Calls `onRows` after each row arrives; false only when nothing loaded.
    bool load(xcloud::GssvClient& gssv, const std::string& language, const RowsCallback& onRows, std::string& err);
    // The account's titles: which games it can play, and the "Your games"
    // row. Takes its own GssvClient (a copy) so it can run on another thread.
    void loadOwned(xcloud::GssvClient gssv, const RowsCallback& onRows, const std::atomic<bool>* stop = nullptr);
    // Hero art and descriptions, in batches; stops early when `stop` is set.
    void hydrate(const RowsCallback& onRows, const std::atomic<bool>* stop = nullptr);

    std::vector<ui::GameRow> rows() const;

private:
    struct RowIds {
        std::string title;
        bool badges = true;
        std::vector<std::pair<std::string, std::string>> items;  // productId, titleId
    };
    std::string market_, language_;
    std::vector<RowIds> layout_;
    std::map<std::string, xcloud::Product> products_;
    std::set<std::string> gamePass_;  // product ids of the Game Pass catalog
    // Filled by loadOwned(): what the account can stream.
    bool ownershipKnown_ = false;
    std::set<std::string> ownedTitles_, ownedProducts_;
};

}  // namespace xc::app
