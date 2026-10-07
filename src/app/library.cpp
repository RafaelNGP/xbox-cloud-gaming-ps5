// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/library.h"

#include "ui/strings.h"
#include "util/log.h"
#include "xcloud/catalog.h"

#include <algorithm>
#include <map>

namespace xc::app {

namespace {

ui::GameTile toTile(const xcloud::Product& p, const std::string& titleId) {
    ui::GameTile t;
    t.productId = p.productId;
    t.titleId = titleId.empty() ? p.xcloudTitleId : titleId;
    t.name = p.title;
    t.publisher = p.publisher;
    t.description = p.description;
    t.tileUrl = p.tileUrl.empty() ? p.posterUrl : p.tileUrl;
    t.heroUrl = p.heroUrl;
    t.categories = p.categories;
    return t;
}

struct ListSpec {
    const char* sigl;
    size_t limit;
};

}  // namespace

std::vector<ui::GameRow> Library::rows() const {
    std::vector<ui::GameRow> rows;
    for (const auto& r : layout_) {
        ui::GameRow row;
        row.title = r.title;
        row.gamePassBadges = r.badges;
        for (const auto& [pid, tid] : r.items) {
            auto it = products_.find(pid);
            if (it == products_.end()) continue;
            if (tid.empty() && it->second.xcloudTitleId.empty()) continue;
            ui::GameTile tile = toTile(it->second, tid);
            if (ownershipKnown_)
                tile.playable = ownedTitles_.count(tile.titleId) || ownedProducts_.count(tile.productId);
            row.tiles.push_back(std::move(tile));
        }
        if (!row.tiles.empty()) rows.push_back(std::move(row));
    }
    return rows;
}

bool Library::load(xcloud::GssvClient& gssv, const std::string& language, const RowsCallback& onRows,
                   std::string& err) {
    market_ = gssv.session().market.empty() ? "US" : gssv.session().market;
    language_ = language;
    layout_.clear();
    products_.clear();
    std::string lastErr;

    std::vector<xcloud::Title> recent;
    if (gssv.listTitles(recent, lastErr, true)) {
        RowIds r{ui::tr(ui::Str::JumpBackIn), false, {}};
        std::vector<std::string> ids;
        for (const auto& t : recent)
            if (!t.productId.empty()) {
                r.items.emplace_back(t.productId, t.titleId);
                ids.push_back(t.productId);
            }
        if (xcloud::fetchProducts(ids, market_, language_, products_, lastErr, false)) {
            layout_.push_back(std::move(r));
            onRows(rows());
        }
    }

    // The Game Pass catalog first: it decides what the other lists may show.
    xcloud::ProductList all;
    std::string e;
    if (xcloud::fetchList(xcloud::sigl::kAllGames, market_, language_, all, e)) {
        gamePass_.insert(all.productIds.begin(), all.productIds.end());
    } else {
        XC_LOGW("%s", e.c_str());
        lastErr = e;
    }

    const ListSpec lists[] = {{xcloud::sigl::kRecentlyAdded, 40},
                              {xcloud::sigl::kMostPopular, 40},
                              {xcloud::sigl::kLeavingSoon, 40},
                              {xcloud::sigl::kAllGames, 120}};
    for (const auto& spec : lists) {
        xcloud::ProductList list;
        if (spec.sigl == xcloud::sigl::kAllGames && !all.productIds.empty()) {
            list = all;
        } else if (!xcloud::fetchList(spec.sigl, market_, language_, list, e)) {
            XC_LOGW("%s", e.c_str());
            lastErr = e;
            continue;
        }
        // "Most popular on cloud" also lists games to buy first (Cuphead...).
        if (!gamePass_.empty()) {
            size_t before = list.productIds.size();
            list.productIds.erase(std::remove_if(list.productIds.begin(), list.productIds.end(),
                                                 [&](const std::string& id) { return !gamePass_.count(id); }),
                                  list.productIds.end());
            if (list.productIds.size() != before)
                XC_LOGI("library: %s: %zu of %zu not in Game Pass, left out", list.title.c_str(),
                        before - list.productIds.size(), before);
        }
        if (list.productIds.size() > spec.limit) list.productIds.resize(spec.limit);
        std::vector<std::string> missing;
        for (const auto& id : list.productIds)
            if (!products_.count(id)) missing.push_back(id);
        if (!missing.empty() && !xcloud::fetchProducts(missing, market_, language_, products_, e, false)) {
            XC_LOGW("%s", e.c_str());
            lastErr = e;
            continue;
        }
        RowIds r{list.title, true, {}};
        for (const auto& id : list.productIds) r.items.emplace_back(id, std::string());
        layout_.push_back(std::move(r));
        onRows(rows());
    }
    if (rows().empty()) {
        err = lastErr.empty() ? "no games found" : lastErr;
        return false;
    }
    return true;
}

void Library::loadOwned(xcloud::GssvClient gssv, const RowsCallback& onRows, const std::atomic<bool>* stop) {
    std::vector<xcloud::Title> titles;
    std::string err;
    if (!gssv.listTitles(titles, err, false)) {
        XC_LOGW("%s", err.c_str());  // rows stay as they are, all playable
        return;
    }
    if (stop && *stop) return;
    std::vector<std::pair<std::string, std::string>> mine;  // productId, titleId
    for (const auto& t : titles) {
        if (!t.hasEntitlement) continue;
        ownedTitles_.insert(t.titleId);
        if (t.productId.empty()) continue;
        ownedProducts_.insert(t.productId);
        if (!gamePass_.count(t.productId)) mine.emplace_back(t.productId, t.titleId);
    }
    std::vector<std::string> missing;
    for (const auto& m : mine)
        if (!products_.count(m.first)) missing.push_back(m.first);
    if (!missing.empty() && !xcloud::fetchProducts(missing, market_, language_, products_, err, false))
        XC_LOGW("%s", err.c_str());
    if (stop && *stop) return;
    // Alphabetical; products the catalog doesn't know have no name or art.
    mine.erase(std::remove_if(mine.begin(), mine.end(), [&](const auto& m) { return !products_.count(m.first); }),
               mine.end());
    std::sort(mine.begin(), mine.end(), [&](const auto& a, const auto& b) {
        return products_[a.first].title < products_[b.first].title;
    });
    RowIds row{ui::tr(ui::Str::YourGames), false, std::move(mine)};
    size_t at = 0;  // after "Jump back in"
    if (!layout_.empty() && !layout_.front().badges) at = 1;
    size_t count = row.items.size();
    layout_.insert(layout_.begin() + static_cast<long>(at), std::move(row));
    ownershipKnown_ = true;
    XC_LOGI("library: account can stream %zu titles; %zu outside Game Pass", ownedTitles_.size(), count);
    onRows(rows());
}

void Library::hydrate(const RowsCallback& onRows, const std::atomic<bool>* stop) {
    // Row order, so what is on screen first gets its art first.
    std::vector<std::string> order;
    for (const auto& r : layout_)
        for (const auto& item : r.items) {
            auto it = products_.find(item.first);
            if (it != products_.end() && it->second.heroUrl.empty() &&
                std::find(order.begin(), order.end(), item.first) == order.end())
                order.push_back(item.first);
        }
    // A small first batch: the cards on screen get their hero art quickly.
    for (size_t i = 0, n = 6; i < order.size(); i += n, n = 20) {
        if (stop && *stop) return;
        std::vector<std::string> batch(order.begin() + static_cast<long>(i),
                                       order.begin() + static_cast<long>(std::min(order.size(), i + n)));
        std::map<std::string, xcloud::Product> full;
        std::string e;
        if (!xcloud::fetchProducts(batch, market_, language_, full, e, true)) {
            XC_LOGW("%s", e.c_str());
            return;
        }
        for (auto& [id, p] : full) products_[id] = std::move(p);
        if (stop && *stop) return;
        onRows(rows());
    }
    XC_LOGI("library: %zu rows, %zu products with details", layout_.size(), products_.size());
}

}  // namespace xc::app
