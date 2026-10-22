// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The streaming screen on the main thread; see stream_screen.h.
#include "app/stream_screen.h"

#include "app/app_state.h"
#include "app/autoplay.h"
#include "app/shell.h"
#include "app/stream_player.h"
#include "app/web_server.h"
#include "display/display.h"
#include "input/input_ext.h"
#include "input/kbm.h"
#include "input/keyboard_nav.h"
#include "input/pad_usb.h"
#include "media/audio_out.h"
#include "platform/platform.h"
#include "platform/toasts.h"
#include "ui/strings.h"
#include "ui/toast_view.h"
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

void StreamScreen::setCaps(const ui::UiCaps& caps) {
    if (caps == menu_.capabilities()) return;
    menu_.setCapabilities(caps);
    if (menu_.isOpen()) overlaySeq_ = 0;
}

void StreamScreen::setMouseCaptured_(bool captured) {
    if (captured == mouseCaptured_) return;
    mouseCaptured_ = captured;
    input::kbmSetCaptured(captured);
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
        mouseKeyboard_ = tile.mouseKeyboard && !tile.homeConsole;
        touchMouse_ = {};

        std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
        bool hasCustom = false;
        GameProfile prof = g_settings.profileForGame(tile.productId, tile.titleId, &hasCustom);
        hasCustomProfile_ = hasCustom;
        touchpadMouse_ = prof.touchpadMouse;

        streamResolution_ = prof.resolution;
        showStats_ = g_settings.streamStats;
        menu_.setStatsMask(g_settings.statsMask);
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
        triggerProfile_ = prof.triggerProfile;
        deadzone_ = prof.deadzoneLeft;
        circleConfirms_ = prof.circleConfirms;
        keyMap_ = prof.keyMap;
        padMap_ = prof.padMap;
        gyroMode_ = prof.gyroMode;
        gyroLevel_ = prof.gyroLevel;
        input::setGyro(gyroMode_, gyroLevel_);
        input::setDeadzone(prof.deadzoneLeft / 100.0f, prof.deadzoneRight / 100.0f);
        input::setTriggerFeel(prof.triggerStrength, prof.triggerHz, prof.triggerResistance, prof.triggerPulses, triggerProfile_);
        input::setCircleConfirms(prof.circleConfirms);
        if (g_ui) g_ui->setCircleConfirms(prof.circleConfirms);
        // 120 Hz (VRR) for this game's stream: active when enabled in profile,
        // or defaulted on when the display supports switching refresh (menus stay at 60 Hz).
        bool want120 = prof.highRefresh || (!hasCustom && display::canSwitchRefresh());
        highRefresh_ = want120 && display::setHighRefresh(true);

        media::audioSetRoute(static_cast<media::AudioRoute>(g_settings.audioRoute));
        media::audioSetVoiceRouting(g_settings.voiceRouting);
        if (g_player) {
            g_player->setMicGain(g_settings.micGainFactor());
        }

        overlaySeq_ = 0;
    }
    if (g_player) {
        updateTextInput(g_player);
        // L3 + R3 simultaneous press toggles HUD statistics overlay
        bool l3r3 = pad.btnL3 && pad.btnR3;
        if (l3r3 && !prevL3R3_ && !menu_.isOpen()) {
            showStats_ = !showStats_;
            {
                std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
                g_settings.streamStats = showStats_;
                g_settings.save(settingsPath());
            }
            overlaySeq_ = 0;
            platform::notify(showStats_ ? "Telemetria ativada" : "Telemetria desativada",
                             showStats_ ? "stats on" : "stats off");
        }
        prevL3R3_ = l3r3;

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
            float micLvl = g_player ? g_player->micLevel() : 0.0f;
            bool micMuted = g_player ? (g_player->micMuted() || g_player->isMicHardwareMuted()) : false;
            menu_.setDebandInUse(autoDeband_.level());
            menu_.setGyro(gyroMode_, gyroLevel_);
            menu_.setStatsMask(g_settings.statsMask);
            int padVol = 100;
            {
                std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
                padVol = g_settings.padSpeakerVolume;
            }
            menu_.open(streamResolution_, showStats_, sharpness_, deband_, upscaler_, g_playingHome, allow1440,
                       hasCustomProfile_, triggerStrength_, deadzone_, circleConfirms_, micLvl, micMuted, padVol);
            overlaySeq_ = 0;
        } else if (wasOpen && (menuCombo || swipeMenu_)) {
            // The same gesture or key closes it again.
            swipeMenu_ = false;
            menu_.close();
            overlaySeq_ = 0;
            g_player->requestKeyframe();
        } else if (wasOpen) {
            switch (menu_.handle(nav)) {
            case ui::MenuAction::Leave: g_cancel = true; break;
            case ui::MenuAction::XboxButton:
                // A short press, once the menu is gone: the Xbox guide opens.
                g_xboxButtonUntil = platform::nowMs() + 250;
                g_player->requestKeyframe();
                XC_LOGI("menu_: Xbox button");
                break;
            case ui::MenuAction::PadSpeakerVolume: {
                int vol = menu_.padSpeakerVolume();
                media::setPadSpeakerVolume(vol);
                std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
                g_settings.padSpeakerVolume = vol;
                g_settings.save(settingsPath());
                break;
            }
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
                    p.triggerProfile = triggerProfile_;
                    p.circleConfirms = circleConfirms_;
                    auto had = g_settings.perGame.find(gameKey_);  // not set from this menu: kept
                    p.highRefresh = had != g_settings.perGame.end() ? had->second.highRefresh : g_settings.highRefresh;
                    p.keyMap = had != g_settings.perGame.end() ? had->second.keyMap : g_settings.keyMap;
                    p.padMap = had != g_settings.perGame.end() ? had->second.padMap : g_settings.padMap;
                    p.gyroMode = gyroMode_;
                    p.gyroLevel = gyroLevel_;
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
                    triggerProfile_ = def.triggerProfile;
                    keyMap_ = def.keyMap;
                    padMap_ = def.padMap;
                    circleConfirms_ = def.circleConfirms;
                    gyroMode_ = def.gyroMode;
                    gyroLevel_ = def.gyroLevel;
                    input::setGyro(gyroMode_, gyroLevel_);
                    menu_.setGyro(gyroMode_, gyroLevel_);
                    display::setSharpness(kSharpAmount[sharpness_]);
                    applyDeband_();
                    applyUpscaler(upscaler_);
                    input::setDeadzone(deadzone_ / 100.0f, deadzone_ / 100.0f);
                    input::setTriggerFeel(triggerStrength_, g_settings.triggerHz, g_settings.triggerResistance, g_settings.triggerPulses, triggerProfile_);
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
                refreshSettingsUiLocked();  // else leaving Settings later would put the old value back
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
                refreshSettingsUiLocked();
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
                refreshSettingsUiLocked();
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
                refreshSettingsUiLocked();
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
                refreshSettingsUiLocked();
                if (g_ui) g_ui->setPerGameSettings(g_settings.perGame);
                break;
            }
            case ui::MenuAction::Gyro: {
                gyroMode_ = menu_.gyroMode();
                gyroLevel_ = menu_.gyroLevel();
                input::setGyro(gyroMode_, gyroLevel_);
                std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
                if (hasCustomProfile_) {
                    g_settings.perGame[gameKey_].gyroMode = gyroMode_;
                    g_settings.perGame[gameKey_].gyroLevel = gyroLevel_;
                } else {
                    g_settings.gyroMode = gyroMode_;
                    g_settings.gyroLevel = gyroLevel_;
                }
                g_settings.save(settingsPath());
                refreshSettingsUiLocked();  // the gyro's defaults too
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
                refreshSettingsUiLocked();
                if (g_ui) g_ui->setPerGameSettings(g_settings.perGame);
                break;
            }
            case ui::MenuAction::Stats: {
                showStats_ = menu_.statsOn();
                std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
                g_settings.streamStats = showStats_;
                g_settings.statsMask = menu_.statsMask();
                g_settings.save(settingsPath());
                break;
            }
            default: break;
            }
            overlaySeq_ = 0;
        }
        input::KbmInput touchKbm;
        bool touchDrag = false;
        if (mouseKeyboard_ && touchpadMouse_ && !menu_.isOpen() && g_keyboardFor.empty() && !menuCombo) {
            touchDrag = input::touchpadAsMouse(pad, platform::nowMs(), touchMouse_, touchKbm);
        } else {
            if (touchMouse_.prevButtons != 0) touchKbm.moved = true;  // let go of any held touchpad mouse button
            touchMouse_ = {};
        }
        // The game gets the buttons through this game's map (Settings >
        // Controller buttons); everything above (the menu's combo, the
        // menu itself) read them as they are.
        input::ControllerState padIn = pad;
        if (touchDrag) padIn.btnTouchpad = false;  // held as a mouse button after moving the cursor, not View
        input::ControllerState sent = input::applyPadMap(padIn, padMap_);
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
        // per swipe; the games never see the touchpad's touches). In MKB
        // games the touchpad moves the mouse while the menu is closed.
        bool swipeOn = (!mouseKeyboard_ || !touchpadMouse_) || menu_.isOpen();
        if (swipeOn && pad.touching && !pad.btnTouchpad && g_keyboardFor.empty()) {
            if (!swipe_.active) swipe_ = {true, false, pad.touchX, pad.touchY};
            float dx = pad.touchX - swipe_.x, dy = swipe_.y - pad.touchY;  // dy > 0: up
            bool right = dx > 0.30f && std::abs(dy) < 0.30f, up = dy > 0.40f && std::abs(dx) < 0.30f;
            bool left = dx < -0.30f && std::abs(dy) < 0.30f, down = dy < -0.40f && std::abs(dx) < 0.30f;
            if (!swipe_.fired && (right || up) && !menu_.isOpen()) {
                swipe_.fired = true;
                g_xboxButtonUntil = platform::nowMs() + 250;
                XC_LOGI("touchpad swipe %s: Xbox button", right ? "right" : "up");
            } else if (!swipe_.fired && (left || down)) {
                swipe_.fired = true;
                swipeMenu_ = true;  // opened on the next pass, as by OPTIONS + TOUCHPAD
                XC_LOGI("touchpad swipe %s: game menu", left ? "left" : "down");
            }
        } else {
            swipe_.active = false;
            if (!pad.touching) swipe_.fired = false;
            else if (pad.btnTouchpad) swipe_.fired = true;
        }
        if (!menu_.isOpen()) {
            input::ControllerState vpad0;
            bool vconn0 = false;
            if (virtualGamepadState(0, vpad0, vconn0) && vconn0) {
                input::mergePads(sent, vpad0);
                if (vpad0.btnNexus) {
                    sent.btnNexus = true;
                    g_xboxButtonUntil = platform::nowMs() + 250;
                }
            }
            input::ControllerState usbPad;
            if (input::pollUsbPad(usbPad)) {
                input::mergePads(sent, usbPad);
                if (usbPad.btnNexus) {
                    sent.btnNexus = true;
                    g_xboxButtonUntil = platform::nowMs() + 250;
                }
            }
        }
        if (platform::nowMs() < g_xboxButtonUntil) sent.btnNexus = true;
        g_player->sendInput(sent);
        // The keyboard as another player's controller (Settings): its own
        // slot, announced like a pad; no native mouse and keyboard then.
        // Keys 2-4: that player's controller. With that player's own pad
        // connected the two add up (below); alone, the keyboard is announced
        // in the slot by itself.
        int kbSlot = keyboardPadSlot(kbm_.keyboard || kbm_.mouse);
        kbPad_ = input::ControllerState{};
        if (kbSlot >= 0 && !menu_.isOpen()) input::keyboardAsGamepad(kbm_, mouseStick_, kbPad_, platform::nowMs(), keyMap_);
        int alone = kbSlot >= 0 && !input::padConnected(kbSlot) ? kbSlot : -1;
        if (alone != kbSlotAttached_) {
            if (kbSlotAttached_ >= 0 && !input::padConnected(kbSlotAttached_)) g_player->setPadConnected(kbSlotAttached_, false);
            if (alone >= 0) g_player->setPadConnected(alone, true);
            kbSlotAttached_ = alone;
        }
        if (alone >= 0) {
            g_player->sendInput(kbPad_, alone);  // all released while the app's menu is open
        } else if (kbSlot >= 0) {
            // Added to that player's pad in the loop below.
        }
        if (kbSlot >= 0 && !menu_.isOpen()) {
            if (touchKbm.moved) g_player->sendKbm(touchKbm);
        } else if (!menu_.isOpen()) {
        // USB keyboard and mouse (plus the touchpad mouse in MKB games):
        // straight to the game while the app's menu is closed; F12 is the app's.
            input::KbmInput toGame = kbm_;
            toGame.keys.erase(std::remove_if(toGame.keys.begin(), toGame.keys.end(),
                                             [](const input::KeyEvent& k) { return k.vk == input::kVkAppMenu || k.vk == 0x24; }),
                              toGame.keys.end());
            for (const auto& k : toGame.keys) {
                auto at = std::find(keysInGame_.begin(), keysInGame_.end(), k.vk);
                if (k.pressed && at == keysInGame_.end()) keysInGame_.push_back(k.vk);
                if (!k.pressed && at != keysInGame_.end()) keysInGame_.erase(at);
            }
            toGame.dx += touchKbm.dx;
            toGame.dy += touchKbm.dy;
            toGame.wheel += touchKbm.wheel;
            toGame.buttons |= touchKbm.buttons;
            toGame.moved |= touchKbm.moved;
            g_player->sendKbm(toGame);
        } else if (!keysInGame_.empty() || touchKbm.moved) {
            // The menu opened with keys or a touchpad mouse button down: let go of them.
            input::KbmInput release = touchKbm;
            for (uint8_t vk : keysInGame_) release.keys.push_back({vk, false});
            keysInGame_.clear();
            g_player->sendKbm(release);
        }
        // The other players' controllers, straight to the game.
        for (int i = 1; i < input::kMaxPads; ++i) {
            input::ControllerState other;
            bool on = input::pollPad(i, other);
            input::ControllerState vpad;
            bool vconn = false;
            if (virtualGamepadState(i, vpad, vconn) && vconn) {
                if (on) {
                    input::mergePads(other, vpad);
                } else {
                    other = vpad;
                    on = true;
                }
            }
            if (on) other = input::applyPadMap(other, padMap_);  // the same map as player 1's (the keys keep theirs)
            if (on && i == kbSlot) input::mergePads(other, kbPad_);  // the keyboard plays with this player's pad
            if (on != padAttached_[i]) {
                padAttached_[i] = on;
                g_player->setPadConnected(i, on);
                std::string who = std::to_string(i + 1);
                std::string name = input::padUserName(i);
                if (!name.empty()) who += " (" + name + ")";
                else if (vconn && !input::padConnected(i)) who += " (Web Gamepad)";
                // The PS5 user's name on the TV, not in the log.
                platform::notify(ui::trf(on ? ui::Str::PadConnected : ui::Str::PadDisconnected, who),
                                 std::string("controller ") + std::to_string(i + 1) +
                                     (on ? " connected" : " disconnected"));
            }
            if (on) g_player->sendInput(other, i);
        }
        // The window holds the mouse while the keyboard and mouse go to the
        // game (natively, or as a player's controller): not with the app's
        // menu (or a system keyboard) up, nor while another window has the
        // focus (the desktop shell lets go of it at once then). A no-op on
        // the PS5.
        setMouseCaptured_((kbm_.keyboard || kbm_.mouse) && !menu_.isOpen() && g_keyboardFor.empty() &&
                          shell::hasFocus());
    } else {
        setMouseCaptured_(false);
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
    uint64_t nowMs = platform::nowMs();
    bool menuRedraw = menu_.isOpen() && (nowMs - lastMenuRedrawMs_ >= 40);
    // A desktop's toasts take the overlay while they last (the menu still
    // comes first); the PS5's notifications are the system's.
    uint64_t toasts = platform::toastsEnabled() ? platform::toasts().version(nowMs) : 0;
    if (overlaySeq_ != seq + 1 || menuRedraw || toasts != toastsShown_) {
        overlaySeq_ = seq + 1;
        toastsShown_ = toasts;
        if (menuRedraw) lastMenuRedrawMs_ = nowMs;
        ui::StreamInfo info;
        {
            std::lock_guard<std::mutex> infoLock(g_infoMutex);
            info = g_streamInfo;
        }
        if (menu_.isOpen()) {
            float micLvl = g_player ? g_player->micLevel() : 0.0f;
            bool micMuted = g_player ? (g_player->micMuted() || g_player->isMicHardwareMuted()) : false;
            menu_.setMicLevel(micLvl);
            menu_.setMicMuted(micMuted);
            ui::Canvas c = menu_.renderMenu(info);
            display::setOverlay(c.data(), ui::StreamMenu::kMenuX, ui::StreamMenu::kMenuY, c.width(), c.height(), 235);
        } else if (auto shown = toasts ? platform::toasts().visible(nowMs) : std::vector<std::string>(); !shown.empty()) {
            ui::Canvas c = ui::renderToastPanel(fonts_, shown);
            display::setOverlay(c.data(), (static_cast<int>(display::kWidth) - c.width()) / 2, 28, c.width(), c.height(), 235);
        } else if (showStats_ && seq) {
            if (!info.frametimes.empty()) menu_.setFrametimes(info.frametimes);
            ui::Canvas c = menu_.renderStats(info);
            display::setOverlay(c.data(), ui::StreamMenu::kStatsX, ui::StreamMenu::kStatsY, c.width(), c.height(), 200);
        } else if (info.degraded) {
            ui::Canvas c = menu_.renderWarning();
            display::setOverlay(c.data(), ui::StreamMenu::kWarnRight - c.width(), ui::StreamMenu::kWarnY, c.width(),
                                c.height(), 200);
        } else {
            display::setOverlay(nullptr, 0, 0, 0, 0, 0);
        }
        overlayShown_ = true;
    }
}

bool StreamScreen::menuOpen() const { return menu_.isOpen(); }
void StreamScreen::redrawMenu() { overlaySeq_ = 0; }

int keyboardPadSlot(bool keyboardConnected) {
    int player;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        player = g_settings.keyboardPlayer;
    }
    return keyboardConnected && player > 0 && player < input::kMaxPads ? player : -1;
}

void StreamScreen::idle() {
    setMouseCaptured_(false);  // the stream ended: the mouse is the menus' again
    keysInGame_.clear();
    kbSlotAttached_ = -1;
    mouseStick_ = {};
    if (!g_keyboardFor.empty()) updateTextInput(nullptr);
    touchEnabled_ = false;  // each stream starts with touch off
    if (overlayShown_) {
        display::setOverlay(nullptr, 0, 0, 0, 0, 0);
        overlayShown_ = false;
        menu_.close();
    }
    if (wasStreaming_) {
        wasStreaming_ = false;
        for (bool& a : padAttached_) a = false;
        overlayPlayer_ = nullptr;
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        input::setDeadzone(g_settings.deadzoneLeft / 100.0f, g_settings.deadzoneRight / 100.0f);
        input::setTriggerFeel(g_settings.triggerStrength, g_settings.triggerHz, g_settings.triggerResistance, g_settings.triggerPulses, g_settings.triggerProfile);
        input::setCircleConfirms(g_settings.circleConfirms);
        if (g_ui) g_ui->setCircleConfirms(g_settings.circleConfirms);
        if (highRefresh_) display::setHighRefresh(false);
        highRefresh_ = false;
    }
}

}  // namespace xc::app
