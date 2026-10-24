// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/app_ui_internal.h"

namespace xc::ui {

void AppUi::handleHome(const NavInput& in, UiEvent& ev) {
    // Caller holds mutex_.
    if ((in.scrollRows || in.scrollCards) && !(searching_ && searchOnKeys_)) {
        // Smooth scrolling: the rows and cards it moved by since the last
        // pass, one step at a time (the view glides after the focus).
        auto steps = [&](int n, bool vertical) {
            NavInput step;
            step.nowMs = in.nowMs;
            (vertical ? (n > 0 ? step.down : step.up) : (n > 0 ? step.right : step.left)) = true;
            for (int k = 0; k < std::abs(n); ++k) {
                // In the search's results, sideways stops at the row's edge
                // (left of it is the keyboard).
                if (searching_ && !vertical && (n < 0 ? resultFocus_ % kResultCols == 0
                                                      : (resultFocus_ + 1) % kResultCols == 0))
                    break;
                handleHome(step, ev);
            }
        };
        if (in.scrollRows) steps(in.scrollRows, true);
        if (in.scrollCards) steps(in.scrollCards, false);
        return;
    }
    if (in.l1 || in.r1) {
        // Game Pass, Your games, My consoles, round.
        int n = static_cast<int>(tab_) + (in.r1 ? 1 : 2);
        tab_ = static_cast<Tab>(n % 3);
        searching_ = false;
        dirty_ = true;
        if (tab_ == Tab::Consoles) ev.action = Action::ConsolesShown;
        return;
    }
    if (in.l3 && !searching_) {
        ev.action = Action::RefreshLibrary;
        return;
    }
    if (in.options) {
        screen_ = Screen::Settings;
        settingsRow_ = 0;
        settingsFromError_ = false;
        settingsCountry_ = settings_.loginCountry;
        dropdownOpen_ = false;
        filterList_ = -1;
        dirty_ = true;
        return;
    }
    if (in.triangle && !searching_ && tab_ != Tab::Consoles) {  // search this tab
        searching_ = true;
        searchOnKeys_ = true;
        query_.clear();
        runSearch();
        dirty_ = true;
        return;
    }
    if (searching_) {
        if (searchOnKeys_) {
            if ((in.back || in.triangle) && filterList_ < 0) {  // close the search (Circle closes an open list first)
                searching_ = false;
                dirty_ = true;
                return;
            }
            handleSearchKeys(in);
            return;
        }
        if (in.back || in.triangle || (in.left && resultFocus_ % kResultCols == 0)) {  // back to the keys
            searchOnKeys_ = true;
            dirty_ = true;
            return;
        }
        if (in.square && !query_.empty()) {  // delete without going back to the keys
            query_.pop_back();
            runSearch();
            searchOnKeys_ = results_.empty();
            dirty_ = true;
            return;
        }
        if (moveInGrid(resultFocus_, static_cast<int>(results_.size()), kResultCols, in)) {
            resultScroll_.target = gridTarget(resultFocus_, kResultCols);
            dirty_ = true;
        }
        if (in.accept && resultFocus_ < static_cast<int>(results_.size()))
            openDetails(results_[static_cast<size_t>(resultFocus_)]);
        return;
    }
    switch (tab_) {
        case Tab::Consoles: {
            int n = static_cast<int>(consoles_.size());
            // Search again: Cross with none listed, Triangle otherwise.
            if (consolesKnown_ && ((n == 0 && in.accept) || in.triangle)) {
                if (n == 0) consolesKnown_ = false;  // the spinner while it looks
                ev.action = Action::ConsolesShown;
                dirty_ = true;
                break;
            }
            if (n == 0) break;
            int before = consoleFocus_;
            if (in.right && consoleFocus_ + 1 < n) ++consoleFocus_;
            if (in.left && consoleFocus_ > 0) --consoleFocus_;
            if (consoleFocus_ != before) dirty_ = true;
            if (in.accept) {
                const ConsoleTile& con = consoles_[static_cast<size_t>(consoleFocus_)];
                ev.action = Action::Play;
                ev.game.titleId = con.serverId;
                ev.game.name = con.name.empty() ? "Xbox" : con.name;
                ev.game.homeConsole = true;
            }
            break;
        }
        case Tab::GamePass: {
            if (rows_.empty()) break;
            int rows = static_cast<int>(rows_.size());
            const GameRow& curRow = rows_[static_cast<size_t>(focusRow_)];
            int& col = focusCol_[static_cast<size_t>(focusRow_)];
            int count = static_cast<int>(curRow.tiles.size());

            if (curRow.isGrid) {
                if (in.up && col < kLibraryCols) {
                    if (focusRow_ > 0) {
                        --focusRow_;
                        int upCols = static_cast<int>(rows_[static_cast<size_t>(focusRow_)].tiles.size());
                        focusCol_[static_cast<size_t>(focusRow_)] = std::clamp(focusCol_[static_cast<size_t>(focusRow_)], 0, upCols - 1);
                    }
                } else if (in.down && col + kLibraryCols >= count && col / kLibraryCols == (count - 1) / kLibraryCols) {
                    if (focusRow_ + 1 < rows) {
                        ++focusRow_;
                        int downCols = static_cast<int>(rows_[static_cast<size_t>(focusRow_)].tiles.size());
                        focusCol_[static_cast<size_t>(focusRow_)] = std::clamp(focusCol_[static_cast<size_t>(focusRow_)], 0, downCols - 1);
                    }
                } else {
                    moveInGrid(col, count, kLibraryCols, in);
                }
            } else {
                if (in.down && focusRow_ + 1 < rows) {
                    ++focusRow_;
                    if (rows_[static_cast<size_t>(focusRow_)].isGrid) {
                        int gCols = static_cast<int>(rows_[static_cast<size_t>(focusRow_)].tiles.size());
                        int targetCol = std::min(col % kLibraryCols, gCols - 1);
                        focusCol_[static_cast<size_t>(focusRow_)] = std::max(0, targetCol);
                    }
                }
                if (in.up && focusRow_ > 0) --focusRow_;
                if (in.right && col + 1 < count) ++col;
                if (in.left && col > 0) --col;
            }
            if (in.up || in.down || in.left || in.right) {
                retarget();
                dirty_ = true;
            }
            if (in.accept && focusedTile()) openDetails(*focusedTile());
            if (in.square && focusedTile()) toggleHidden(*focusedTile(), ev);
            break;
        }
        case Tab::Library: {
            if (!libraryTile(0)) break;
            const LibraryLayout& L = libraryLayout();
            int count = static_cast<int>(L.rowOf.size());
            int f = std::min(gridFocus_, count - 1), before = f;
            int row = L.rowOf[static_cast<size_t>(f)], col = L.colOf[static_cast<size_t>(f)];
            int rows = static_cast<int>(L.rowY.size());
            auto rowLen = [&](int r) {
                int next = r + 1 < rows ? L.rowFirst[static_cast<size_t>(r + 1)] : count;
                return next - L.rowFirst[static_cast<size_t>(r)];
            };
            if (in.right && f + 1 < count && L.rowOf[static_cast<size_t>(f + 1)] == row) ++f;
            if (in.left && col > 0) --f;
            if (in.down && row + 1 < rows) f = L.rowFirst[static_cast<size_t>(row + 1)] + std::min(col, rowLen(row + 1) - 1);
            if (in.up && row > 0) f = L.rowFirst[static_cast<size_t>(row - 1)] + std::min(col, rowLen(row - 1) - 1);
            if (f != before) {
                gridFocus_ = f;
                int y = L.rowY[static_cast<size_t>(L.rowOf[static_cast<size_t>(f)])];
                gridScroll_.target = static_cast<float>(std::max(0, y - kGridPitchY - 40));
                dirty_ = true;
            }
            if (in.accept && libraryTile(f)) openDetails(*libraryTile(f));
            if (in.square && libraryTile(f)) {
                GameTile t = *libraryTile(f);
                toggleHidden(t, ev);
            }
            if (in.l2 || in.r2) {
                // Jump between sections: the account's games, games to buy,
                // hidden games. L2 goes to the start of this section first.
                int starts[3] = {0, static_cast<int>(owned_.size()),
                                 static_cast<int>(owned_.size() + purchasable_.size())};
                int total = starts[2] + static_cast<int>(hiddenTiles_.size());
                int section = f >= starts[2] ? 2 : f >= starts[1] ? 1 : 0;
                int target = f;
                if (in.r2) {
                    for (int k = section + 1; k < 3; ++k)
                        if (starts[k] < total && (k == 2 ? !hiddenTiles_.empty() : starts[k] < starts[k + 1])) {
                            target = starts[k];
                            break;
                        }
                } else if (f != starts[section]) {
                    target = starts[section];
                } else {
                    for (int k = section - 1; k >= 0; --k)
                        if (starts[k] < starts[k + 1]) {
                            target = starts[k];
                            break;
                        }
                }
                if (target != f) {
                    gridFocus_ = target;
                    int section2 = target >= starts[2] ? 2 : target >= starts[1] ? 1 : 0;
                    gridScroll_.target = static_cast<float>(L.headerY[section2]);  // its heading at the top
                    dirty_ = true;
                }
            }
            if (in.r3) {  // next sort order
                librarySort_ = static_cast<LibrarySort>((static_cast<int>(librarySort_) + 1) %
                                                         static_cast<int>(LibrarySort::Count));
                rebuild(true);  // same place on screen, not back to the top
                prefsEvent(ev);
            }
            break;
        }
    }
}

void AppUi::setMouseCursor(float x, float y, bool visible) {
    std::lock_guard<std::mutex> lock(mutex_);
    mouseCursorX_ = std::clamp(x, 0.0f, 1919.0f);
    mouseCursorY_ = std::clamp(y, 0.0f, 1079.0f);
    mouseCursorVisible_ = visible;
    if (visible) {
        mouseCursorVisibleUntil_ = platform::nowMs() + 3000;
    }
    dirty_ = true;
}

UiEvent AppUi::onMouseMove(float x, float y) {
    std::lock_guard<std::mutex> lock(mutex_);
    UiEvent ev;
    mouseCursorX_ = std::clamp(x, 0.0f, 1919.0f);
    mouseCursorY_ = std::clamp(y, 0.0f, 1079.0f);
    mouseCursorVisible_ = true;
    mouseCursorVisibleUntil_ = platform::nowMs() + 3000;
    dirty_ = true;
    if (screen_ == Screen::Streaming || screen_ == Screen::Splash) {
        return ev;
    }
    if (screen_ == Screen::Home && !searching_ && !reportOpen_ && !updatePrompt_ && !quitPrompt_) {
        if (tab_ == Tab::GamePass && y >= 140 && y < kH - 80) {
            constexpr int kGridW = kLibraryCols * kCardPitch - kCardGap;
            const int gridLeft = (kW - kGridW) / 2;
            int afterGridOffset = 0;
            for (size_t r = 0; r < rows_.size(); ++r) {
                float rel = static_cast<float>(r) - rowY_.value;
                int rowBaseY = kRowTop + static_cast<int>(std::lround(rel * kRowPitch)) + afterGridOffset;
                const GameRow& row = rows_[r];
                if (row.isGrid) {
                    int offset = static_cast<int>(std::lround(rowScroll_[r].value));
                    int cardY = rowBaseY - offset + 48;
                    for (size_t i = 0; i < row.tiles.size(); ++i) {
                        int c_col = static_cast<int>(i) % kLibraryCols;
                        int c_row = static_cast<int>(i) / kLibraryCols;
                        int cy = cardY + c_row * kGridPitchY;
                        int cx = gridLeft + c_col * kCardPitch;
                        if (x >= cx && x < cx + kCard && y >= cy && y < cy + kCard) {
                            if (focusRow_ != static_cast<int>(r) || focusCol_[r] != static_cast<int>(i)) {
                                focusRow_ = static_cast<int>(r);
                                focusCol_[r] = static_cast<int>(i);
                                retarget();
                                dirty_ = true;
                            }
                            return ev;
                        }
                    }
                    int gridHeight = 48 + ((static_cast<int>(row.tiles.size()) + kLibraryCols - 1) / kLibraryCols) * kGridPitchY;
                    afterGridOffset += gridHeight - offset - kRowPitch;
                } else {
                    int cardY = rowBaseY + 48;
                    int scroll = static_cast<int>(std::lround(rowScroll_[r].value));
                    int firstCol = std::max(0, scroll / kCardPitch - 1);
                    for (size_t col = static_cast<size_t>(firstCol); col < row.tiles.size(); ++col) {
                        int cx = kMargin + static_cast<int>(col) * kCardPitch - scroll;
                        if (cx > kW) break;
                        if (x >= cx && x < cx + kCard && y >= cardY && y < cardY + kCard) {
                            if (focusRow_ != static_cast<int>(r) || focusCol_[r] != static_cast<int>(col)) {
                                focusRow_ = static_cast<int>(r);
                                focusCol_[r] = static_cast<int>(col);
                                retarget();
                                dirty_ = true;
                            }
                            return ev;
                        }
                    }
                }
            }
        } else if (tab_ == Tab::Library && y >= 150 && y < kH - 80) {
            constexpr int kGridW = kLibraryCols * kCardPitch - kCardGap;
            const int left = (kW - kGridW) / 2;
            const LibraryLayout& L = libraryLayout();
            int offset = static_cast<int>(std::lround(gridScroll_.value));
            for (size_t i = 0; i < L.rowOf.size(); ++i) {
                int cy = kLibraryTop + L.rowY[static_cast<size_t>(L.rowOf[i])] - offset;
                int cx = left + L.colOf[i] * kCardPitch;
                if (x >= cx && x < cx + kCard && y >= cy && y < cy + kCard) {
                    if (gridFocus_ != static_cast<int>(i)) {
                        gridFocus_ = static_cast<int>(i);
                        dirty_ = true;
                    }
                    return ev;
                }
            }
        }
    } else if (screen_ == Screen::Settings) {
        if (dropdownOpen_ || tester_ != Tester::None || pickerOpen_ || countryPickerOpen_ || reportOpen_) {
            return ev;
        }
        constexpr int kRowW = 1140, kRowH = 30, kGap = 2, kGroupGap = 4;
        int curY = 184;
        for (const auto& [i, groupStart] : visibleSettingsRows(caps_)) {
            if (groupStart) curY += kGroupGap;
            if (x >= kMargin && x < kMargin + kRowW && y >= curY && y < curY + kRowH) {
                if (settingsRow_ != i) {
                    settingsRow_ = i;
                    dirty_ = true;
                }
                return ev;
            }
            curY += kRowH + kGap;
        }
    } else if (screen_ == Screen::Details) {
        if (achievementsOpen_ || reportOpen_) {
            return ev;
        }
        if (gameSettingsOpen_) {
            constexpr int kModalW = 1140, kModalH = 960;
            constexpr int kRowH = 40, kGap = 4, kGroupGap = 7;
            const auto rows = visibleGameSettingsRows(caps_, activeGameHasCustom_, activeGameMouseSupported_);
            const auto most = visibleGameSettingsRows(caps_, true, activeGameMouseSupported_), all = visibleGameSettingsRows(UiCaps{}, true, true);
            auto groups = [](const std::vector<SettingsRowPos>& v) {
                return static_cast<int>(std::count_if(v.begin(), v.end(), [](const SettingsRowPos& r) { return r.groupStart; }));
            };
            const int modalH = kModalH - static_cast<int>(all.size() - most.size()) * (kRowH + kGap) -
                               (groups(all) - groups(most)) * kGroupGap;
            Rect panel{(kW - kModalW) / 2, (kH - modalH) / 2, kModalW, modalH};
            int curY = panel.y + 92;
            for (int pos = 0; pos < static_cast<int>(rows.size()); ++pos) {
                if (rows[static_cast<size_t>(pos)].groupStart) curY += kGroupGap;
                Rect r{panel.x + 50, curY, panel.w - 100, kRowH};
                if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
                    if (gameSettingsRow_ != pos) {
                        gameSettingsRow_ = pos;
                        dirty_ = true;
                    }
                    return ev;
                }
                curY += kRowH + kGap;
            }
            return ev;
        }
        auto inRect = [&](const Rect& r) {
            return r.w > 0 && r.h > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
        };
        Rect play = playRect_.w > 0 ? playRect_ : Rect{kMargin, 620, 300, 76};
        if (inRect(play) || inRect(claimRect_)) {
            if (detailButtonFocus_ != 0) {
                detailButtonFocus_ = 0;
                dirty_ = true;
            }
        } else if (inRect(achRect_)) {
            if (detailButtonFocus_ != 1) {
                detailButtonFocus_ = 1;
                dirty_ = true;
            }
        }
    }
    return ev;
}

UiEvent AppUi::onMouseDown(float x, float y, int button) {
    std::lock_guard<std::mutex> lock(mutex_);
    UiEvent ev;
    mouseCursorX_ = std::clamp(x, 0.0f, 1919.0f);
    mouseCursorY_ = std::clamp(y, 0.0f, 1079.0f);
    mouseCursorVisible_ = true;
    mouseCursorVisibleUntil_ = platform::nowMs() + 3000;
    dirty_ = true;
    if (screen_ == Screen::Streaming || screen_ == Screen::Splash) {
        return ev;
    }
    if (button == 3) {
        // Right click: Back
        if (reportOpen_) {
            reportOpen_ = false;
            dirty_ = true;
            return ev;
        }
        if (screen_ == Screen::Home && updatePrompt_) {
            if (showingUpdateNotes_) {
                showingUpdateNotes_ = false;
            } else {
                ev.action = Action::UpdateLater;
                updatePrompt_ = false;
            }
            dirty_ = true;
            return ev;
        }
        if (countryPickerOpen_) {
            countryPickerOpen_ = false;
            dirty_ = true;
            return ev;
        }
        if (screen_ == Screen::Home && quitPrompt_) {
            quitPrompt_ = false;
            dirty_ = true;
            return ev;
        }
        if (screen_ == Screen::Settings) {
            if (pickerOpen_) {
                handleColourPicker(NavInput{.back = true});
                dirty_ = true;
                return ev;
            }
            if (tester_ == Tester::SignInCheck || tester_ == Tester::NetworkTest || tester_ == Tester::Donation) {
                tester_ = Tester::None;
                dirty_ = true;
                return ev;
            }
            if (tester_ != Tester::None) {
                handleTester(NavInput{.back = true});
                if (tester_ == Tester::None) {
                    ev.action = Action::SettingsChanged;
                    ev.settings = settings_;
                }
                dirty_ = true;
                return ev;
            }
        }
        if (dropdownOpen_) {
            dropdownOpen_ = false;
            dirty_ = true;
            return ev;
        }
        if (achievementsOpen_) {
            achievementsOpen_ = false;
            dirty_ = true;
            return ev;
        }
        if (gameSettingsOpen_) {
            handleGameSettings(NavInput{.back = true}, ev);
            return ev;
        }
        if (searching_) {
            searching_ = false;
            dirty_ = true;
            return ev;
        }
        if (screen_ == Screen::Settings) {
            resetSettingsHoldStart_ = 0;
            screen_ = settingsFromError_ ? Screen::Error : Screen::Home;
            dirty_ = true;
            ev.action = settings_.loginCountry != settingsCountry_ ? Action::LoginCountry : Action::SettingsChanged;
            ev.settings = settings_;
            return ev;
        }
        if (screen_ == Screen::Details) {
            syncHeroToRowsLocked(detail_.productId, detail_.titleId, detail_.heroUrl, detail_.description, detail_.publisher, detail_.categories);
            screen_ = Screen::Home;
            dirty_ = true;
            return ev;
        }
        return ev;
    }
    if (button != 1) return ev;

    if (reportOpen_ || countryPickerOpen_ || (screen_ == Screen::Home && (updatePrompt_ || quitPrompt_))) {
        return ev;
    }

    // Check avatar / profile click to open settings
    if (screen_ == Screen::Home && !searching_) {
        constexpr int kAvatar = 52;
        if (x >= kW - kMargin - kAvatar && x <= kW - kMargin && y >= 40 && y <= 100) {
            screen_ = Screen::Settings;
            settingsRow_ = 0;
            settingsFromError_ = false;
            settingsCountry_ = settings_.loginCountry;
            dropdownOpen_ = false;
            filterList_ = -1;
            dirty_ = true;
            return ev;
        }
    }

    // Check top bar tabs
    if (screen_ == Screen::Home && !searching_ && y >= 45 && y <= 95) {
        const char* labels[3] = {tr(Str::TabGamePass), tr(Str::YourGames), tr(Str::TabConsoles)};
        constexpr int kPx = 22, kPad = 26, kGap = 12, kH = 44, kY = 50;
        int widths[3], total = 0;
        for (int i = 0; i < 3; ++i) {
            widths[i] = fonts_.semibold.measure(labels[i], kPx) + 2 * kPad;
            total += widths[i + 0] + (i ? kGap : 0);
        }
        int tabX = (kW - total) / 2;
        for (int i = 0; i < 3; ++i) {
            if (x >= tabX && x < tabX + widths[i] && y >= kY && y < kY + kH) {
                tab_ = static_cast<Tab>(i);
                searching_ = false;
                dirty_ = true;
                if (tab_ == Tab::Consoles) ev.action = Action::ConsolesShown;
                return ev;
            }
            tabX += widths[i] + kGap;
        }
    }

    if (screen_ == Screen::Home && !searching_) {
        if (tab_ == Tab::GamePass && y >= 140 && y < kH - 80) {
            constexpr int kGridW = kLibraryCols * kCardPitch - kCardGap;
            const int gridLeft = (kW - kGridW) / 2;
            int afterGridOffset = 0;
            for (size_t r = 0; r < rows_.size(); ++r) {
                float rel = static_cast<float>(r) - rowY_.value;
                int rowBaseY = kRowTop + static_cast<int>(std::lround(rel * kRowPitch)) + afterGridOffset;
                const GameRow& row = rows_[r];
                if (row.isGrid) {
                    int offset = static_cast<int>(std::lround(rowScroll_[r].value));
                    int cardY = rowBaseY - offset + 48;
                    for (size_t i = 0; i < row.tiles.size(); ++i) {
                        int c_col = static_cast<int>(i) % kLibraryCols;
                        int c_row = static_cast<int>(i) / kLibraryCols;
                        int cy = cardY + c_row * kGridPitchY;
                        int cx = gridLeft + c_col * kCardPitch;
                        if (x >= cx && x < cx + kCard && y >= cy && y < cy + kCard) {
                            focusRow_ = static_cast<int>(r);
                            focusCol_[r] = static_cast<int>(i);
                            openDetails(row.tiles[i]);
                            return ev;
                        }
                    }
                    int gridHeight = 48 + ((static_cast<int>(row.tiles.size()) + kLibraryCols - 1) / kLibraryCols) * kGridPitchY;
                    afterGridOffset += gridHeight - offset - kRowPitch;
                } else {
                    int cardY = rowBaseY + 48;
                    int scroll = static_cast<int>(std::lround(rowScroll_[r].value));
                    int firstCol = std::max(0, scroll / kCardPitch - 1);
                    for (size_t col = static_cast<size_t>(firstCol); col < row.tiles.size(); ++col) {
                        int cx = kMargin + static_cast<int>(col) * kCardPitch - scroll;
                        if (cx > kW) break;
                        if (x >= cx && x < cx + kCard && y >= cardY && y < cardY + kCard) {
                            focusRow_ = static_cast<int>(r);
                            focusCol_[r] = static_cast<int>(col);
                            openDetails(row.tiles[col]);
                            return ev;
                        }
                    }
                }
            }
        } else if (tab_ == Tab::Library) {
            if (y >= 150 && y < kH - 80) {
                constexpr int kGridW = kLibraryCols * kCardPitch - kCardGap;
                const int left = (kW - kGridW) / 2;
                const LibraryLayout& L = libraryLayout();
                int offset = static_cast<int>(std::lround(gridScroll_.value));
                for (size_t i = 0; i < L.rowOf.size(); ++i) {
                    int cy = kLibraryTop + L.rowY[static_cast<size_t>(L.rowOf[i])] - offset;
                    int cx = left + L.colOf[i] * kCardPitch;
                    if (x >= cx && x < cx + kCard && y >= cy && y < cy + kCard) {
                        gridFocus_ = static_cast<int>(i);
                        const GameTile* t = libraryTile(static_cast<int>(i));
                        if (t) openDetails(*t);
                        return ev;
                    }
                }
            }
            constexpr int kAvatar = 52;
            int right = kW - kMargin - kAvatar - 16;
            if (x >= right - 200 && x <= right && y >= 110 && y <= 145) {
                librarySort_ = static_cast<LibrarySort>((static_cast<int>(librarySort_) + 1) %
                                                       static_cast<int>(LibrarySort::Count));
                rebuild(true);
                prefsEvent(ev);
                dirty_ = true;
                return ev;
            }
        }
    } else if (screen_ == Screen::Details) {
        if (gameSettingsOpen_) {
            constexpr int kModalW = 1140, kModalH = 960;
            constexpr int kRowH = 40, kGap = 4, kGroupGap = 7;
            const auto rows = visibleGameSettingsRows(caps_, activeGameHasCustom_, activeGameMouseSupported_);
            const auto most = visibleGameSettingsRows(caps_, true, activeGameMouseSupported_), all = visibleGameSettingsRows(UiCaps{}, true, true);
            auto groups = [](const std::vector<SettingsRowPos>& v) {
                return static_cast<int>(std::count_if(v.begin(), v.end(), [](const SettingsRowPos& r) { return r.groupStart; }));
            };
            const int modalH = kModalH - static_cast<int>(all.size() - most.size()) * (kRowH + kGap) -
                               (groups(all) - groups(most)) * kGroupGap;
            Rect panel{(kW - kModalW) / 2, (kH - modalH) / 2, kModalW, modalH};
            int curY = panel.y + 92;
            for (int pos = 0; pos < static_cast<int>(rows.size()); ++pos) {
                if (rows[static_cast<size_t>(pos)].groupStart) curY += kGroupGap;
                Rect r{panel.x + 50, curY, panel.w - 100, kRowH};
                if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
                    gameSettingsRow_ = pos;
                    handleGameSettings(NavInput{.accept = true}, ev);
                    dirty_ = true;
                    return ev;
                }
                curY += kRowH + kGap;
            }
            if (x < panel.x || x >= panel.x + panel.w || y < panel.y || y >= panel.y + panel.h) {
                handleGameSettings(NavInput{.back = true}, ev);
            }
            return ev;
        }
        if (achievementsOpen_) {
            return ev;
        }
        const GameTile* g = &detail_;
        if (g->productId.empty() && g->titleId.empty()) return ev;
        bool isF2P = isFreeGame(*g);
        bool effectivePlayable = isEffectivePlayableLocked(*g);
        auto inRect = [&](const Rect& r) {
            return r.w > 0 && r.h > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
        };
        if (!effectivePlayable && isF2P && !g->playable) {
            Rect claim = claimRect_.w > 0 ? claimRect_ : Rect{kMargin, 620, 380, 68};
            if (inRect(claim)) {
                detailButtonFocus_ = 0;
                ev.action = Action::ClaimFreeGame;
                ev.game = detail_;
                ev.game.freeInStore = true;
                return ev;
            }
        } else if (effectivePlayable) {
            Rect play = playRect_.w > 0 ? playRect_ : Rect{kMargin, 620, 300, 76};
            if (inRect(play)) {
                detailButtonFocus_ = 0;
                ev.action = Action::Play;
                ev.game = detail_;
                return ev;
            }
            if (inRect(achRect_) && !detail_.xboxTitleId.empty()) {
                detailButtonFocus_ = 1;
                openAchievements();
                return ev;
            }
        }
    } else if (screen_ == Screen::Settings) {
        if (tester_ != Tester::None || pickerOpen_) {
            return ev;
        }
        if (dropdownOpen_) {
            auto options = settingOptions(settingsRow_);
            constexpr int kItemH = 62, kVisible = 8, kListW = 640;
            int n = static_cast<int>(options.size());
            int visible = std::min(n, kVisible);
            constexpr int kRowW = 1140, kRowH = 30, kGap = 2, kGroupGap = 4;
            Rect panel{kMargin + kRowW - kListW, 184, kListW, visible * kItemH + 16};
            int curY = 184;
            for (const auto& [i, groupStart] : visibleSettingsRows(caps_)) {
                if (groupStart) curY += kGroupGap;
                if (i == settingsRow_) {
                    panel.y = curY + kRowH + 8;
                    break;
                }
                curY += kRowH + kGap;
            }
            if (panel.y + panel.h > kH - 100) panel.y = std::max(120, kH - 100 - panel.h);

            if (x >= panel.x && x < panel.x + panel.w && y >= panel.y && y < panel.y + panel.h) {
                for (int k = 0; k < visible; ++k) {
                    int i = dropdownTop_ + k;
                    Rect item{panel.x + 8, panel.y + 8 + k * kItemH, panel.w - 16, kItemH};
                    if (x >= item.x && x < item.x + item.w && y >= item.y && y < item.y + item.h && i < n) {
                        int before = settingSelected(settingsRow_);
                        applySetting(settingsRow_, i);
                        dropdownOpen_ = false;
                        dirty_ = true;
                        if (settingsRow_ == 6 && i == 1) {
                            openColourPicker(before);
                        } else if (settingsRow_ != 8) {
                            ev.action = Action::SettingsChanged;
                            ev.settings = settings_;
                        }
                        return ev;
                    }
                }
            } else {
                dropdownOpen_ = false;
                dirty_ = true;
                return ev;
            }
        }
        constexpr int kRowW = 1140, kRowH = 30, kGap = 2, kGroupGap = 4;
        int curY = 184;
        for (const auto& [i, groupStart] : visibleSettingsRows(caps_)) {
            if (groupStart) curY += kGroupGap;
            if (x >= kMargin && x < kMargin + kRowW && y >= curY && y < curY + kRowH) {
                settingsRow_ = i;
                if (i == 14) {
                    openPadTester();
                    return ev;
                } else if (i == 3 || i == 4 || i == 12) {
                    openTester(i == 3 ? Tester::Sticks : i == 4 ? Tester::Triggers : Tester::Keys);
                    return ev;
                } else if (i == 17) {
                    openTester(Tester::SignInCheck);
                    ev.action = Action::CheckSignIn;
                    return ev;
                } else if (i == 18) {
                    openTester(Tester::NetworkTest);
                    ev.action = Action::RunNetworkTest;
                    return ev;
                } else if (i == 19) {
                    openTester(Tester::Donation);
                    return ev;
                } else if (i == 7) {
                    if (!caps_.selfUpdate) {
                        if (caps_.openUrl) {
                            ev.action = Action::OpenUrl;
                            ev.url = kReleasesPage;
                        }
                    } else if (!updateAvailable_.empty() && !updateChecking_) {
                        ev.action = Action::UpdateNow;
                        ev.settings = settings_;
                    }
                    return ev;
                } else if (i == 10) {
                    int n = static_cast<int>(settingOptions(10).size());
                    if (n > 0) applySetting(10, (settingSelected(10) + 1) % n);
                    ev.action = Action::SettingsChanged;
                    ev.settings = settings_;
                    dirty_ = true;
                    return ev;
                } else if (i == 23) {
                    return ev;
                } else if (i == 5) {
                    settings_.circleConfirms = !settings_.circleConfirms;
                    ev.action = Action::SettingsChanged;
                    ev.settings = settings_;
                    dirty_ = true;
                    return ev;
                } else {
                    auto opts = settingOptions(i);
                    if (opts.size() > 2) {
                        dropdownOpen_ = true;
                        dropdownIndex_ = settingSelected(i);
                        dropdownTop_ = std::max(0, dropdownIndex_ - 4);
                        dirty_ = true;
                        return ev;
                    }
                    changeSetting(1);
                    if (i != 8) {
                        ev.action = Action::SettingsChanged;
                        ev.settings = settings_;
                    }
                    dirty_ = true;
                    return ev;
                }
            }
            curY += kRowH + kGap;
        }
    }
    return ev;
}

UiEvent AppUi::onMouseWheel(float dx, float dy) {
    std::lock_guard<std::mutex> lock(mutex_);
    UiEvent ev;
    mouseCursorVisible_ = true;
    mouseCursorVisibleUntil_ = platform::nowMs() + 3000;
    dirty_ = true;
    if (screen_ == Screen::Streaming || screen_ == Screen::Splash) {
        return ev;
    }
    if (screen_ == Screen::Home && updatePrompt_ && showingUpdateNotes_) {
        updateNotesScroll_ = std::clamp(updateNotesScroll_ - static_cast<int>(dy * 2), 0, updateNotesMaxScroll_);
        dirty_ = true;
        return ev;
    }
    if (screen_ == Screen::Home && !searching_ && !updatePrompt_ && !quitPrompt_ && !reportOpen_) {
        if (tab_ == Tab::Library) {
            const LibraryLayout& L = libraryLayout();
            float maxScroll = L.rowY.empty() ? 0.0f : static_cast<float>(std::max(0, L.rowY.back() + kGridPitchY - (kH - kLibraryTop)));
            gridScroll_.target = std::clamp(gridScroll_.target - dy * 140.0f, 0.0f, maxScroll);
            animating_ = true;
            dirty_ = true;
        } else if (tab_ == Tab::GamePass) {
            if (!rows_.empty()) {
                const GameRow& curRow = rows_[static_cast<size_t>(focusRow_)];
                if (curRow.isGrid) {
                    int totalRows = (static_cast<int>(curRow.tiles.size()) + kLibraryCols - 1) / kLibraryCols;
                    float maxScroll = std::max(0.0f, static_cast<float>(totalRows * kGridPitchY - (kH - kRowTop - 48)));
                    if (dy > 0 && rowScroll_[static_cast<size_t>(focusRow_)].target <= 0.0f) {
                        if (focusRow_ > 0) {
                            --focusRow_;
                            retarget();
                            animating_ = true;
                            dirty_ = true;
                        }
                    } else {
                        rowScroll_[static_cast<size_t>(focusRow_)].target =
                            std::clamp(rowScroll_[static_cast<size_t>(focusRow_)].target - dy * 140.0f, 0.0f, maxScroll);
                        animating_ = true;
                        dirty_ = true;
                    }
                } else {
                    if (std::abs(dy) > std::abs(dx)) {
                        int nextRow = std::clamp(static_cast<int>(rowY_.target - dy), 0, static_cast<int>(rows_.size()) - 1);
                        if (nextRow != focusRow_) {
                            focusRow_ = nextRow;
                            retarget();
                            animating_ = true;
                            dirty_ = true;
                        }
                    } else if (dx != 0.0f) {
                        float maxScroll = std::max(0.0f, static_cast<float>(static_cast<int>(curRow.tiles.size()) * kCardPitch - (kW - kMargin)));
                        rowScroll_[static_cast<size_t>(focusRow_)].target =
                            std::clamp(rowScroll_[static_cast<size_t>(focusRow_)].target - dx * 160.0f, 0.0f, maxScroll);
                        animating_ = true;
                        dirty_ = true;
                    }
                }
            }
        }
    } else if (screen_ == Screen::Details && achievementsOpen_) {
        achScroll_ = std::max(0, achScroll_ - static_cast<int>(dy * 3));
        dirty_ = true;
    }
    return ev;
}

UiEvent AppUi::handle(const NavInput& in) {
    std::lock_guard<std::mutex> lock(mutex_);
    UiEvent ev;
    if (screen_ == Screen::Splash) {
        return ev;
    }
    if (reportOpen_) {  // the report's QR code: Circle (or Cross) closes it
        if (in.accept && caps_.openUrl) {
            // A desktop: Cross opens the form in the browser (the panel stays).
            ev.action = Action::OpenUrl;
            ev.url = reportUrl_;
        } else if (in.back || in.accept) {
            reportOpen_ = false, dirty_ = true;
        }
        return ev;
    }
    if (screen_ == Screen::Home && updatePrompt_) {
        if (showingUpdateNotes_) {
            if (in.up) {
                if (updateNotesScroll_ > 0) {
                    --updateNotesScroll_;
                    dirty_ = true;
                }
            } else if (in.down) {
                if (updateNotesScroll_ < updateNotesMaxScroll_) {
                    ++updateNotesScroll_;
                    dirty_ = true;
                }
            } else if (in.l1 || in.l2) {
                if (updateNotesScroll_ > 0) {
                    updateNotesScroll_ = std::max(0, updateNotesScroll_ - 5);
                    dirty_ = true;
                }
            } else if (in.r1 || in.r2) {
                if (updateNotesScroll_ < updateNotesMaxScroll_) {
                    updateNotesScroll_ = std::min(updateNotesMaxScroll_, updateNotesScroll_ + 5);
                    dirty_ = true;
                }
            } else if (in.scrollRows != 0) {
                int next = std::clamp(updateNotesScroll_ + in.scrollRows, 0, updateNotesMaxScroll_);
                if (next != updateNotesScroll_) {
                    updateNotesScroll_ = next;
                    dirty_ = true;
                }
            } else if (in.back || in.accept || in.square) {
                showingUpdateNotes_ = false;
                dirty_ = true;
            }
            return ev;
        }
        // 3 buttons: 0 Update now, 1 What's new, 2 Not now
        if (in.square) {
            showingUpdateNotes_ = true;
            updateNotesScroll_ = 0;
            formatUpdateNotes();
            dirty_ = true;
            return ev;
        }
        if (in.left) {
            promptFocus_ = (promptFocus_ + 2) % 3;
            dirty_ = true;
        } else if (in.right) {
            promptFocus_ = (promptFocus_ + 1) % 3;
            dirty_ = true;
        }
        if (in.accept) {
            if (promptFocus_ == 0) {
                ev.action = Action::UpdateNow;
                updatePrompt_ = false;
                dirty_ = true;
            } else if (promptFocus_ == 1) {
                showingUpdateNotes_ = true;
                updateNotesScroll_ = 0;
                formatUpdateNotes();
                dirty_ = true;
            } else {
                ev.action = Action::UpdateLater;
                updatePrompt_ = false;
                dirty_ = true;
            }
        } else if (in.back) {
            ev.action = Action::UpdateLater;
            updatePrompt_ = false;
            dirty_ = true;
        }
        return ev;
    }
    if (countryPickerOpen_) {
        // "Sign in as another country": up / down, Cross signs in as the
        // one chosen, Circle closes it unchanged.
        int n = 1 + static_cast<int>(std::size(kLoginCountries));
        if (in.down && countryIndex_ + 1 < n) ++countryIndex_, dirty_ = true;
        if (in.up && countryIndex_ > 0) --countryIndex_, dirty_ = true;
        if (in.accept) {
            settings_.loginCountry = countryIndex_ ? kLoginCountries[countryIndex_ - 1] : "";
            ev.action = Action::LoginCountry;
            ev.settings = settings_;
        }
        if (in.accept || in.back) countryPickerOpen_ = false, dirty_ = true;
        return ev;
    }
    switch (screen_) {
        case Screen::Home:
            if (quitPrompt_) {  // two buttons, Close / Cancel: left / right picks, Cross presses, Circle cancels
                if (in.left || in.right) promptFocus_ = in.left ? 0 : 1, dirty_ = true;
                if (in.accept && promptFocus_ == 0) ev.action = Action::Quit;
                if (in.accept || in.back) quitPrompt_ = false, dirty_ = true;
                break;
            }
            // A desktop window has no system menu to close it from (fullscreen,
            // a controller in hand): Circle / Esc at the top of the home screen
            // asks. On the PS5 the system closes the app; Circle does nothing here.
            if (caps_.desktopWindow && in.back && !searching_) {
                quitPrompt_ = true;
                promptFocus_ = 1;  // Cancel first: a second Circle / Esc never closes by accident
                dirty_ = true;
                break;
            }
            handleHome(in, ev);
            break;
        case Screen::SignIn:
            // A desktop: Cross (Enter) opens the sign-in page in the browser.
            if (in.accept && caps_.openUrl) {
                ev.action = Action::OpenUrl;
                ev.url = codeUrl_.empty() ? "https://www.microsoft.com/link" : codeUrl_;
            }
            break;
        case Screen::Settings:
            if (pickerOpen_) {
                handleColourPicker(in);
                if (!pickerOpen_ && in.accept) {
                    ev.action = Action::SettingsChanged;
                    ev.settings = settings_;
                }
                break;
            }
            if (tester_ == Tester::SignInCheck) {
                if (in.back) {
                    tester_ = Tester::None;
                    dirty_ = true;
                } else if (in.accept && !signInCheck_.running) {
                    ev.action = Action::CheckSignIn;
                }
                break;
            }
            if (tester_ == Tester::NetworkTest) {
                if (in.back) {
                    tester_ = Tester::None;
                    dirty_ = true;
                } else if (in.accept && !networkTest_.running) {
                    ev.action = Action::RunNetworkTest;
                }
                break;
            }
            if (tester_ == Tester::Donation) {
                if (in.back || in.accept) {
                    tester_ = Tester::None;
                    dirty_ = true;
                }
                break;
            }
            if (tester_ != Tester::None) {
                handleTester(in);
                if (tester_ == Tester::None) {
                    ev.action = Action::SettingsChanged;
                    ev.settings = settings_;
                }
                break;
            }
            if (dropdownOpen_) {
                // The list: up / down, Cross picks, Circle closes it unchanged.
                int n = static_cast<int>(settingOptions(settingsRow_).size());
                if (in.down && dropdownIndex_ + 1 < n) ++dropdownIndex_;
                if (in.up && dropdownIndex_ > 0) --dropdownIndex_;
                int before = settingSelected(settingsRow_);
                if (in.accept) applySetting(settingsRow_, dropdownIndex_);
                if (in.accept || in.back) dropdownOpen_ = false;
                if (in.accept && settingsRow_ == 6 && dropdownIndex_ == 1) {
                    openColourPicker(before);
                } else if (in.accept && settingsRow_ != 8) {
                    ev.action = Action::SettingsChanged;
                    ev.settings = settings_;
                }
                dirty_ = true;
                break;
            }
            if (auto rows = visibleSettingsRows(caps_); true) {  // the rows on screen, in their order
                int pos = posOf(rows, settingsRow_);
                if (in.down && pos + 1 < static_cast<int>(rows.size())) {
                    settingsRow_ = rows[static_cast<size_t>(pos + 1)].row;
                    resetSettingsHoldStart_ = 0;
                    dirty_ = true;
                } else if (in.up && pos > 0) {
                    settingsRow_ = rows[static_cast<size_t>(pos - 1)].row;
                    resetSettingsHoldStart_ = 0;
                    dirty_ = true;
                }
            }
            if (settingsRow_ == 23) {
                if (in.acceptHeld) {
                    if (!resetSettingsHoldStart_) resetSettingsHoldStart_ = in.nowMs ? in.nowMs : 1;
                    if (in.nowMs - resetSettingsHoldStart_ >= 5000) {
                        ev.action = Action::ResetSettings;
                        resetSettingsHoldStart_ = 0;
                    }
                    animating_ = true;
                    dirty_ = true;
                } else if (resetSettingsHoldStart_) {
                    resetSettingsHoldStart_ = 0;
                    dirty_ = true;
                }
            } else {
                resetSettingsHoldStart_ = 0;
            }
            if (in.right && settingsRow_ != 23) {
                changeSetting(+1);
                if (settingsRow_ != 8) {
                    ev.action = Action::SettingsChanged;
                    ev.settings = settings_;
                }
            }
            if (in.left && settingsRow_ != 23) {
                changeSetting(-1);
                if (settingsRow_ != 8) {
                    ev.action = Action::SettingsChanged;
                    ev.settings = settings_;
                }
            }
            if (in.square && settingsRow_ == 16) {
                ev.action = Action::TestQueueAlert;
            }
            if (in.accept && settingsRow_ == 14) {
                openPadTester();  // controller buttons: their own screen
            } else if (in.accept && (settingsRow_ == 3 || settingsRow_ == 4 || settingsRow_ == 12)) {
                // Dead zone, trigger vibration, keyboard keys: their own screens instead of a list.
                openTester(settingsRow_ == 3 ? Tester::Sticks : settingsRow_ == 4 ? Tester::Triggers : Tester::Keys);
            } else if (in.accept && settingsRow_ == 17) {
                openTester(Tester::SignInCheck);
                ev.action = Action::CheckSignIn;
            } else if (in.accept && settingsRow_ == 18) {
                openTester(Tester::NetworkTest);
                ev.action = Action::RunNetworkTest;
            } else if (in.accept && settingsRow_ == 19) {
                openTester(Tester::Donation);
            } else if (in.accept && settingsRow_ == 7) {
                // Updates: a newer version is installed (the settings saved
                // first). Where the app doesn't install it, the release's
                // page opens in the browser instead.
                if (!caps_.selfUpdate) {
                    if (caps_.openUrl) {
                        ev.action = Action::OpenUrl;
                        ev.url = kReleasesPage;
                    }
                } else if (!updateAvailable_.empty() && !updateChecking_) {
                    ev.action = Action::UpdateNow;
                    ev.settings = settings_;
                }
            } else if (in.accept && settingsRow_ == 10) {
                // Inline bitrate slider: cycles to next option, no dropdown modal
                int n = static_cast<int>(settingOptions(10).size());
                if (n > 0) {
                    applySetting(10, (settingSelected(10) + 1) % n);
                    ev.action = Action::SettingsChanged;
                    ev.settings = settings_;
                    dirty_ = true;
                }
            } else if (in.accept && settingsRow_ == 23) {
                // Handled via hold
            } else if (in.accept) {
                dropdownOpen_ = true;
                dropdownIndex_ = settingSelected(settingsRow_);
                dropdownTop_ = 0;
                dirty_ = true;
            }
            if (in.back || in.options) {
                resetSettingsHoldStart_ = 0;
                // Back where Settings was opened. A new sign-in country
                // takes a new sign-in.
                screen_ = settingsFromError_ ? Screen::Error : Screen::Home;
                dirty_ = true;
                ev.action = settings_.loginCountry != settingsCountry_ ? Action::LoginCountry : Action::SettingsChanged;
                ev.settings = settings_;
            } else if (in.triangle) {
                ev.action = Action::Report;  // "Report a problem"
            }
            break;
        case Screen::Details:
            if (achievementsOpen_) {
                if (in.up && achScroll_ > 0) {
                    achScroll_--;
                    dirty_ = true;
                }
                int maxScroll = 0;
                if (auto it = achievements_.find(detail_.xboxTitleId); it != achievements_.end()) {
                    maxScroll = std::max(0, static_cast<int>(it->second.items.size()) - 5);
                }
                if (in.down && achScroll_ < maxScroll) {
                    achScroll_++;
                    dirty_ = true;
                }
                if (in.back || in.l3) {
                    achievementsOpen_ = false;
                    dirty_ = true;
                }
                break;
            }
            if (gameSettingsOpen_) {
                handleGameSettings(in, ev);
                break;
            }
            if (in.options) {
                openGameSettings();
                dirty_ = true;
                break;
            }
            {
                bool isF2P = isFreeGame(detail_);
                bool effectivePlayable = isEffectivePlayableLocked(detail_);
                bool hasAchievements = effectivePlayable && !detail_.xboxTitleId.empty();
                if (!hasAchievements && detailButtonFocus_ == 1) {
                    detailButtonFocus_ = 0;
                }
                if (in.right && hasAchievements && detailButtonFocus_ == 0) {
                    detailButtonFocus_ = 1;
                    dirty_ = true;
                } else if (in.left && detailButtonFocus_ == 1) {
                    detailButtonFocus_ = 0;
                    dirty_ = true;
                }
                if (in.accept) {
                    if (detailButtonFocus_ == 1 && hasAchievements) {
                        openAchievements();
                    } else if (!effectivePlayable && isF2P && !detail_.playable) {
                        ev.action = Action::ClaimFreeGame;
                        ev.game = detail_;
                        ev.game.freeInStore = true;
                    } else if (effectivePlayable) {
                        ev.action = Action::Play;
                        ev.game = detail_;
                    }
                }
            }
            if (in.square) toggleHidden(detail_, ev);  // the page stays open
            if (in.triangle) toggleFavorite(detail_, ev);
            if (in.r3 && !isAcquired(detail_)) toggleWishlist(detail_, ev);
            if (in.l3 && !detail_.xboxTitleId.empty()) openAchievements();
            if (in.back) {
                syncHeroToRowsLocked(detail_.productId, detail_.titleId, detail_.heroUrl, detail_.description, detail_.publisher, detail_.categories);
                screen_ = Screen::Home;
                dirty_ = true;
            }
            break;
        case Screen::Launching:
            if (in.back) ev.action = Action::CancelLaunch;
            break;
        case Screen::Error:
            if (!errorGame_.titleId.empty() || !errorGame_.productId.empty()) {
                if (in.accept) {
                    if ((isFreeGame(errorGame_) || errorGame_.freeInStore) && !errorGame_.playable) {
                        ev.action = Action::ClaimFreeGame;
                        ev.game = errorGame_;
                        ev.game.freeInStore = true;
                    } else if (errorGame_.playable && !errorGame_.titleId.empty()) {
                        ev.action = Action::Play;
                        ev.game = errorGame_;
                    } else {
                        openDetails(errorGame_);
                    }
                } else if (in.back && errorGame_.homeConsole) {
                    screen_ = Screen::Home;  // back to My consoles
                    tab_ = Tab::Consoles;
                    dirty_ = true;
                } else if (in.back) {
                    openDetails(errorGame_);
                }
            } else if (in.accept) {
                ev.action = Action::Retry;
            } else if ((in.square || in.back) && !regionRefused_ && (!rows_.empty() || !owned_.empty())) {
                screen_ = Screen::Home;
                loading_ = false;
                dirty_ = true;
            }
            if (regionRefused_ && in.square) {
                countryPickerOpen_ = true;
                countryIndex_ = settingSelected(8);
                dirty_ = true;
            } else if (regionRefused_ && in.options) {  // Settings, on the sign-in country
                screen_ = Screen::Settings;
                settingsRow_ = 8;
                settingsFromError_ = true;
                settingsCountry_ = settings_.loginCountry;
                dropdownOpen_ = false;
                dirty_ = true;
            }
            if (in.triangle) {  // "Report a problem", with the error shown
                ev.action = Action::Report;
                ev.reportWhat = error_;
            }
            break;
        default: break;
    }
    // Sign out: hold TOUCHPAD for 5 s on the home or error screen.
    bool canSignOut = screen_ == Screen::Home || screen_ == Screen::Error;
    if (canSignOut && in.touchpad && !signOutLatched_) {
        if (!signOutHoldStart_) signOutHoldStart_ = in.nowMs ? in.nowMs : 1;
        if (in.nowMs - signOutHoldStart_ >= kSignOutHoldMs) {
            ev.action = Action::SignOut;
            signOutHoldStart_ = 0;
            signOutLatched_ = true;
        }
        dirty_ = true;
    } else if (!in.touchpad || !canSignOut) {
        if (signOutHoldStart_) dirty_ = true;
        signOutHoldStart_ = 0;
        if (!in.touchpad) signOutLatched_ = false;
    }
    if (displayModeChanged_) {
        // A desktop's display mode applies (and is saved) as it is picked,
        // not when Settings closes.
        displayModeChanged_ = false;
        if (ev.action == Action::None) {
            ev.action = Action::SettingsChanged;
            ev.settings = settings_;
        }
    }
    return ev;
}

// --- Rendering -----------------------------------------------------------------

}  // namespace xc::ui
