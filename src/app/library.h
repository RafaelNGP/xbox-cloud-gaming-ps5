// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Builds the home screen rows: "Jump back in" (the account's recently played
// cloud titles) followed by the Game Pass lists xbox.com/play shows.
//
// Two phases: load() fetches every row with the light catalog payload (the
// home screen can show right away); hydrate() then adds hero art and
// descriptions in the background and can run on its own thread.
#pragma once

#include "ui/app_ui.h"
#include "xcloud/catalog.h"
#include "xcloud/gssv.h"

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace xc::app {

class Library {
public:
    using RowsCallback = std::function<void(const std::vector<ui::GameRow>&)>;

    // Calls `onRows` after each row arrives; false only when nothing loaded.
    bool load(xcloud::GssvClient& gssv, const std::string& language, const RowsCallback& onRows, std::string& err);
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
};

}  // namespace xc::app
