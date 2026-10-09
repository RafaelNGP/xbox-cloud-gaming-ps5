// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The streaming screen, on the main thread: the game's menu (OPTIONS +
// TOUCHPAD, or a swipe), the touchpad's Xbox button, the controllers sent to
// the game, the console keyboard for its text fields, the picture settings
// (sharpness, upscaling, block smoothing with "auto") and what is laid over
// the picture (the menu or the statistics line).
#pragma once

#include "app/auto_deband.h"
#include "input/controller.h"
#include "ui/app_ui.h"
#include "ui/stream_menu.h"

#include <cstdint>

namespace xc::app {

class StreamPlayer;

class StreamScreen {
public:
    explicit StreamScreen(const ui::Fonts& fonts) : menu_(fonts) {}

    // The controllers' slots, for the menu.
    void setPads(const ui::PadSlots& slots);
    // Each pass while streaming: `pad` the first controller, `nav` its
    // presses, `menuCombo` OPTIONS + TOUCHPAD just pressed.
    void update(const input::ControllerState& pad, ui::NavInput nav, bool menuCombo);
    // Each pass on any other screen: what the stream left is cleared, and a
    // console keyboard still up is closed.
    void idle();

private:
    void applyDeband_();

    ui::StreamMenu menu_;
    const StreamPlayer* overlayPlayer_ = nullptr;
    int streamResolution_ = 0;  // as SettingsChoice::resolution
    bool showStats_ = false, overlayShown_ = false;
    bool touchEnabled_ = false;  // touch input announced on (StreamPlayer::setTouchEnabled)
    struct {
        bool active = false, fired = false;
        float x = 0, y = 0;  // where the finger came down
    } swipe_;
    bool swipeMenu_ = false;  // a swipe asked for the game menu
    int sharpness_ = 0;       // 0..3, as Settings::sharpness
    int deband_ = 3;          // 0..3, as Settings::deband
    AutoDeband autoDeband_;   // the level "auto" uses
    uint32_t debandSeq_ = 0;
    int upscaler_ = 0;  // as Settings::upscaler
    bool padReleased_ = true;
    bool padAttached_[input::kMaxPads] = {};  // controllers 1..3 announced to the stream
    uint32_t playerReconnects_ = 0;
    uint32_t overlaySeq_ = 0;  // g_infoSeq + 1 when drawn; 0 = redraw
    bool hasCustomProfile_ = false;
    std::string gameKey_;
    int triggerStrength_ = 2;
    int deadzone_ = 15;
    bool circleConfirms_ = false;
    bool wasStreaming_ = false;
};

}  // namespace xc::app
