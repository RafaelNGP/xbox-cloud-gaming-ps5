// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The streaming screen on the main thread; see stream_screen.h.
#include "app/stream_screen.h"

#include "app/autoplay.h"
#include "app/ps5_app.h"
#include "app/stream_player.h"
#include "display/display.h"
#include "platform/platform.h"
#include "ui/strings.h"
#include "util/log.h"

#include <algorithm>
#include <cmath>

namespace xc::app {
namespace {

// How much CAS each sharpness level mixes in (display::setSharpness).
constexpr int kSharpAmount[] = {0, 96, 176, 256};

// The game menu's upscaling: 0 FSR, 1 Anime4K, 2 FSR + clean-up, 3 Anime4K +
// clean-up (Anime4K Restore before the upscale).
void applyUpscaler(int mode) {
    display::setUpscaler(mode & 1);
    display::setRestore(mode >= 2);
}

// The console keyboard answering a game's text field (main thread): the
// request it answers, and the stream that asked.
std::string g_keyboardFor;
const app::StreamPlayer* g_keyboardPlayer = nullptr;

platform::KeyboardKind keyboardKind(int inputScope) {
    switch (inputScope) {
    case 1: return platform::KeyboardKind::Url;
    case 5: return platform::KeyboardKind::Email;
    case 29:
    case 32: return platform::KeyboardKind::Number;
    case 31: return platform::KeyboardKind::Password;
    default: return platform::KeyboardKind::Text;
    }
}

// Opens the keyboard for the game's next text request and sends back what
// was typed. `player` null: the stream is gone, a keyboard still up is
// closed by the player and its text dropped.
void updateTextInput(app::StreamPlayer* player) {
    if (!g_keyboardFor.empty()) {
        std::string text;
        platform::KeyboardStatus st = platform::pollSystemKeyboard(text);
        if (st == platform::KeyboardStatus::Open) return;
        if (player && player == g_keyboardPlayer)
            player->answerTextInput(g_keyboardFor, st == platform::KeyboardStatus::Accepted, text);
        g_keyboardFor.clear();
        g_keyboardPlayer = nullptr;
        return;
    }
    stream::TextInputRequest req;
    if (!player || !player->takeTextInput(req)) return;
    std::string title = req.title.empty() ? req.description : req.title;
    size_t max = req.maxLength > 0 ? static_cast<size_t>(req.maxLength) : 256;
    if (platform::openSystemKeyboard(title, req.defaultText, max, keyboardKind(req.inputScope))) {
        g_keyboardFor = req.id;
        g_keyboardPlayer = player;
    } else {
        player->answerTextInput(req.id, false, std::string());
    }
}

}  // namespace

void StreamScreen::setPads(const ui::PadSlots& slots) {
    menu_.setPads(slots);
    if (menu_.isOpen()) overlaySeq_ = 0;
}

void StreamScreen::applyDeband_() { display::setDeband(deband_ == 3 ? autoDeband_.level() : deband_); }

void StreamScreen::update(const input::ControllerState& pad, ui::NavInput nav, bool menuCombo) {
    std::lock_guard<std::mutex> lock(g_playerMutex);
    if (g_player != overlayPlayer_ || (g_player && g_player->reconnects() != playerReconnects_)) {
        // A new stream, or a new session after a reconnection: the
        // other controllers are announced again.
        for (bool& a : padAttached_) a = false;
        playerReconnects_ = g_player ? g_player->reconnects() : 0;
    }
    if (g_player != overlayPlayer_) {  // a new stream
        overlayPlayer_ = g_player;
        wasStreaming_ = true;
        menu_.close();
        ui::GameTile tile;
        {
            std::lock_guard<std::mutex> argLock(g_argMutex);
            tile = g_playTile;
        }
        gameKey_ = gameProfileKey(tile.productId, tile.titleId);

        std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
        bool hasCustom = false;
        GameProfile prof = g_settings.profileForGame(tile.productId, tile.titleId, &hasCustom);
        hasCustomProfile_ = hasCustom;

        streamResolution_ = prof.resolution;
        showStats_ = g_settings.streamStats;
        sharpness_ = g_autoplay.sharpness >= 0 ? g_autoplay.sharpness : prof.sharpness;
        display::setSharpness(kSharpAmount[std::clamp(sharpness_, 0, 3)]);
        deband_ = g_autoplay.deband >= 0 ? g_autoplay.deband : prof.deband;
        autoDeband_.reset();
        applyDeband_();
        upscaler_ = g_autoplay.upscaler >= 0 ? g_autoplay.upscaler : prof.upscaler;
        if (g_autoplay.restore) upscaler_ = (upscaler_ & 1) | 2;
        applyUpscaler(upscaler_);

        // Apply controller profile settings
        triggerStrength_ = prof.triggerStrength;
        deadzone_ = prof.deadzoneLeft;
        circleConfirms_ = prof.circleConfirms;
        input::setDeadzone(prof.deadzoneLeft / 100.0f, prof.deadzoneRight / 100.0f);
        input::setTriggerFeel(prof.triggerStrength, prof.triggerHz, prof.triggerResistance, prof.triggerPulses);
        input::setCircleConfirms(prof.circleConfirms);
        if (g_ui) g_ui->setCircleConfirms(prof.circleConfirms);

        overlaySeq_ = 0;
    }
    if (g_player) {
        updateTextInput(g_player);
        // OPTIONS + TOUCHPAD opens the menu (and closes it again).
        bool wasOpen = menu_.isOpen();
        if (!wasOpen && (menuCombo || swipeMenu_) && g_keyboardFor.empty()) {
            swipeMenu_ = false;
            menu_.setCircleConfirms(circleConfirms_);
            bool allow1440;
            {
                std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
                allow1440 = allow1440Locked();
            }
            menu_.setDebandInUse(autoDeband_.level());
            menu_.open(streamResolution_, showStats_, sharpness_, deband_, upscaler_, g_playingHome, allow1440,
                       hasCustomProfile_, triggerStrength_, deadzone_, circleConfirms_);
            overlaySeq_ = 0;
        } else if (wasOpen) {
            switch (menu_.handle(nav)) {
            case ui::MenuAction::Leave: g_cancel = true; break;
            case ui::MenuAction::XboxButton:
                // A short press, once the menu is gone: the Xbox guide opens.
                g_xboxButtonUntil = platform::nowMs() + 250;
                g_player->requestKeyframe();
                XC_LOGI("menu_: Xbox button");
                break;
            case ui::MenuAction::Close: g_player->requestKeyframe(); break;  // a clean picture back in the game
            case ui::MenuAction::Resolution:
                streamResolution_ = menu_.resolution();
                g_tierPicked = true;
                // The user's own Xbox: 1080p is its top tier ("1440").
                g_player->requestResolution(streamResolution_ == 1 ? "720HQ"
                                            : streamResolution_ == 2 || g_playingHome ? "1440"
                                                                                      : "1080HQ");
                {
                    std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
                    if (hasCustomProfile_) {
                        g_settings.perGame[gameKey_].resolution = streamResolution_;
                        g_settings.save(settingsPath());
                    }
                }
                break;
            case ui::MenuAction::ProfileToggle: {
                hasCustomProfile_ = menu_.hasCustomProfile();
                std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
                if (hasCustomProfile_) {
                    GameProfile p;
                    p.sharpness = sharpness_;
                    p.deband = deband_;
                    p.upscaler = upscaler_;
                    p.resolution = streamResolution_;
                    p.deadzoneLeft = deadzone_;
                    p.deadzoneRight = deadzone_;
                    p.triggerStrength = triggerStrength_;
                    p.triggerHz = g_settings.triggerHz;
                    p.triggerResistance = g_settings.triggerResistance;
                    p.triggerPulses = g_settings.triggerPulses;
                    p.circleConfirms = circleConfirms_;
                    g_settings.perGame[gameKey_] = p;
                } else {
                    g_settings.perGame.erase(gameKey_);
                    auto def = g_settings.defaultProfile();
                    sharpness_ = def.sharpness;
                    deband_ = def.deband;
                    upscaler_ = def.upscaler;
                    streamResolution_ = def.resolution;
                    deadzone_ = def.deadzoneLeft;
                    triggerStrength_ = def.triggerStrength;
                    circleConfirms_ = def.circleConfirms;
                    display::setSharpness(kSharpAmount[sharpness_]);
                    applyDeband_();
                    applyUpscaler(upscaler_);
                    input::setDeadzone(deadzone_ / 100.0f, deadzone_ / 100.0f);
                    input::setTriggerFeel(triggerStrength_, g_settings.triggerHz, g_settings.triggerResistance, g_settings.triggerPulses);
                    input::setCircleConfirms(circleConfirms_);
                    menu_.setProfileValues(streamResolution_, sharpness_, deband_, upscaler_,
                                           triggerStrength_, deadzone_, circleConfirms_, false);
                }
                g_settings.save(settingsPath());
                if (g_ui) g_ui->setPerGameSettings(g_settings.perGame);
                break;
            }
            case ui::MenuAction::Sharpness: {
                sharpness_ = menu_.sharpness();
                display::setSharpness(kSharpAmount[sharpness_]);
                std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
                if (hasCustomProfile_) g_settings.perGame[gameKey_].sharpness = sharpness_;
                else g_settings.sharpness = sharpness_;
                g_settings.save(settingsPath());
                if (g_ui) g_ui->setPerGameSettings(g_settings.perGame);
                break;
            }
            case ui::MenuAction::Upscaler: {
                upscaler_ = menu_.upscaler();
                applyUpscaler(upscaler_);
                std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
                if (hasCustomProfile_) g_settings.perGame[gameKey_].upscaler = upscaler_;
                else g_settings.upscaler = upscaler_;
                g_settings.save(settingsPath());
                if (g_ui) g_ui->setPerGameSettings(g_settings.perGame);
                break;
            }
            case ui::MenuAction::Deband: {
                deband_ = menu_.deband();
                applyDeband_();
                std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
                if (hasCustomProfile_) g_settings.perGame[gameKey_].deband = deband_;
                else g_settings.deband = deband_;
                g_settings.save(settingsPath());
                if (g_ui) g_ui->setPerGameSettings(g_settings.perGame);
                break;
            }
            case ui::MenuAction::Triggers: {
                triggerStrength_ = menu_.triggerStrength();
                std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
                input::setTriggerFeel(triggerStrength_, g_settings.triggerHz, g_settings.triggerResistance, g_settings.triggerPulses);
                if (hasCustomProfile_) g_settings.perGame[gameKey_].triggerStrength = triggerStrength_;
                else g_settings.triggerStrength = triggerStrength_;
                g_settings.save(settingsPath());
                if (g_ui) g_ui->setPerGameSettings(g_settings.perGame);
                break;
            }
            case ui::MenuAction::Deadzone: {
                deadzone_ = menu_.deadzone();
                std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
                input::setDeadzone(deadzone_ / 100.0f, deadzone_ / 100.0f);
                if (hasCustomProfile_) {
                    g_settings.perGame[gameKey_].deadzoneLeft = deadzone_;
                    g_settings.perGame[gameKey_].deadzoneRight = deadzone_;
                } else {
                    g_settings.deadzoneLeft = deadzone_;
                    g_settings.deadzoneRight = deadzone_;
                }
                g_settings.save(settingsPath());
                if (g_ui) g_ui->setPerGameSettings(g_settings.perGame);
                break;
            }
            case ui::MenuAction::ConfirmButton: {
                circleConfirms_ = menu_.circleConfirms();
                input::setCircleConfirms(circleConfirms_);
                if (g_ui) g_ui->setCircleConfirms(circleConfirms_);
                std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
                if (hasCustomProfile_) g_settings.perGame[gameKey_].circleConfirms = circleConfirms_;
                else g_settings.circleConfirms = circleConfirms_;
                g_settings.save(settingsPath());
                if (g_ui) g_ui->setPerGameSettings(g_settings.perGame);
                break;
            }
            case ui::MenuAction::Stats: {
                showStats_ = menu_.statsOn();
                std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
                g_settings.streamStats = showStats_;
                g_settings.save(settingsPath());
                break;
            }
            default: break;
            }
            overlaySeq_ = 0;
        }
        input::ControllerState sent = pad;
        if (g_syntheticA) sent.btnA = true;
        // While another window has the focus on the cloud console (a
        // publisher's page such as NetEase's terms in Marvel Rivals,
        // which then shows its own pad-driven cursor), touch input is
        // on, as the web client has it on touch screens.
        bool unfocused = !g_player->titleFocused();
        if (unfocused != touchEnabled_) {
            touchEnabled_ = unfocused;
            g_player->setTouchEnabled(unfocused);
        }
        // The menu and the keyboard have the pad while they are up,
        // and the buttons that closed them until they are let go.
        bool held = pad.dpadUp || pad.dpadDown || pad.dpadLeft || pad.dpadRight || pad.btnA || pad.btnB ||
                    pad.btnX || pad.btnY || pad.btnOptions || pad.btnTouchpad;
        if (menu_.isOpen() || !g_keyboardFor.empty()) padReleased_ = false;
        else if (!held) padReleased_ = true;
        if (!padReleased_ || menuCombo) sent = input::ControllerState{};
        // Swiping up or right on the touchpad is the Xbox button (once
        // per swipe; the games never see the touchpad's touches).
        if (pad.touching && !menu_.isOpen() && g_keyboardFor.empty()) {
            if (!swipe_.active) swipe_ = {true, false, pad.touchX, pad.touchY};
            float dx = pad.touchX - swipe_.x, dy = swipe_.y - pad.touchY;  // dy > 0: up
            bool right = dx > 0.30f && std::abs(dy) < 0.30f, up = dy > 0.40f && std::abs(dx) < 0.30f;
            bool left = dx < -0.30f && std::abs(dy) < 0.30f, down = dy < -0.40f && std::abs(dx) < 0.30f;
            if (!swipe_.fired && (right || up)) {
                swipe_.fired = true;
                g_xboxButtonUntil = platform::nowMs() + 250;
                XC_LOGI("touchpad swipe_ %s: Xbox button", right ? "right" : "up");
            } else if (!swipe_.fired && (left || down)) {
                swipe_.fired = true;
                swipeMenu_ = true;  // opened on the next pass, as by OPTIONS + TOUCHPAD
                XC_LOGI("touchpad swipe_ %s: game menu_", left ? "left" : "down");
            }
        } else {
            swipe_.active = false;
        }
        if (platform::nowMs() < g_xboxButtonUntil) sent.btnNexus = true;
        g_player->sendInput(sent);
        // The other players' controllers, straight to the game.
        for (int i = 1; i < input::kMaxPads; ++i) {
            input::ControllerState other;
            bool on = input::pollPad(i, other);
            if (on != padAttached_[i]) {
                padAttached_[i] = on;
                g_player->setPadConnected(i, on);
                std::string who = std::to_string(i + 1);
                std::string name = input::padUserName(i);
                if (!name.empty()) who += " (" + name + ")";
                // The PS5 user's name on the TV, not in the log.
                platform::notify(ui::trf(on ? ui::Str::PadConnected : ui::Str::PadDisconnected, who),
                                 std::string("controller ") + std::to_string(i + 1) +
                                     (on ? " connected" : " disconnected"));
            }
            if (on) g_player->sendInput(other, i);
        }
    }
    // The overlay: the menu, else the statistics line, redrawn when
    // the numbers or the menu change.
    uint32_t seq = g_infoSeq;
    if (deband_ == 3 && seq != debandSeq_) {
        // Auto block smoothing, fed once a second (auto_deband.h).
        debandSeq_ = seq;
        double mbps;
        {
            std::lock_guard<std::mutex> infoLock(g_infoMutex);
            mbps = g_streamInfo.mbps;
        }
        if (autoDeband_.update(mbps)) {
            int level = autoDeband_.level();
            applyDeband_();
            menu_.setDebandInUse(level);
            overlaySeq_ = 0;
            XC_LOGI("block smoothing auto: %s at %.1f Mbps", level == 2 ? "high" : level == 1 ? "low" : "off", mbps);
        }
    }
    if (overlaySeq_ != seq + 1) {
        overlaySeq_ = seq + 1;
        ui::StreamInfo info;
        {
            std::lock_guard<std::mutex> infoLock(g_infoMutex);
            info = g_streamInfo;
        }
        if (menu_.isOpen()) {
            ui::Canvas c = menu_.renderMenu(info);
            display::setOverlay(c.data(), ui::StreamMenu::kMenuX, ui::StreamMenu::kMenuY, c.width(), c.height(), 235);
        } else if (showStats_ && seq) {
            ui::Canvas c = menu_.renderStats(info);
            display::setOverlay(c.data(), ui::StreamMenu::kStatsX, ui::StreamMenu::kStatsY, c.width(), c.height(), 200);
        } else {
            display::setOverlay(nullptr, 0, 0, 0, 0, 0);
        }
        overlayShown_ = true;
    }
}

void StreamScreen::idle() {
    if (!g_keyboardFor.empty()) updateTextInput(nullptr);
    touchEnabled_ = false;  // each stream starts with touch off
    if (overlayShown_) {
        display::setOverlay(nullptr, 0, 0, 0, 0, 0);
        overlayShown_ = false;
        menu_.close();
    }
    if (wasStreaming_) {
        wasStreaming_ = false;
        overlayPlayer_ = nullptr;
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        input::setDeadzone(g_settings.deadzoneLeft / 100.0f, g_settings.deadzoneRight / 100.0f);
        input::setTriggerFeel(g_settings.triggerStrength, g_settings.triggerHz, g_settings.triggerResistance, g_settings.triggerPulses);
        input::setCircleConfirms(g_settings.circleConfirms);
        if (g_ui) g_ui->setCircleConfirms(g_settings.circleConfirms);
    }
}

}  // namespace xc::app
