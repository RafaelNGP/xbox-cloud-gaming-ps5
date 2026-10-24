// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/strings.h"

#include <array>
#include <atomic>

namespace xc::ui {

namespace {

constexpr size_t kLangs = static_cast<size_t>(Language::Count);
constexpr size_t kStrs = static_cast<size_t>(Str::Count);

std::atomic<int> g_language{0};

struct LangInfo {
    const char* name;
    const char* code;
    const char* catalog;
    const char* locale;
};

constexpr std::array<LangInfo, kLangs> kInfo = {{
    {"English", "en", "en-us", "en-US"},
    {"Português (Brasil)", "pt-BR", "pt-br", "pt-BR"},
    {"Español", "es", "es-es", "es-ES"},
    {"Français", "fr", "fr-fr", "fr-FR"},
    {"Deutsch", "de", "de-de", "de-DE"},
    {"Italiano", "it", "it-it", "it-IT"},
}};

using Column = std::array<const char*, kStrs>;

#include "ui/i18n/strings_en.inc"
#include "ui/i18n/strings_pt_br.inc"
#include "ui/i18n/strings_es.inc"
#include "ui/i18n/strings_fr.inc"
#include "ui/i18n/strings_de.inc"
#include "ui/i18n/strings_it.inc"

constexpr std::array<const Column*, kLangs> kTable = {&kEnglish, &kPortugueseBR, &kSpanish,
                                                      &kFrench,  &kGerman,       &kItalian};

const LangInfo& info() { return kInfo[static_cast<size_t>(g_language.load())]; }

}  // namespace

void setLanguage(Language lang) {
    if (static_cast<size_t>(lang) < kLangs) g_language = static_cast<int>(lang);
}

Language language() { return static_cast<Language>(g_language.load()); }

const char* languageName(Language lang) {
    auto i = static_cast<size_t>(lang);
    return i < kLangs ? kInfo[i].name : "";
}

const char* languageCode(Language lang) {
    auto i = static_cast<size_t>(lang);
    return i < kLangs ? kInfo[i].code : "en";
}

Language languageFromCode(const std::string& code) {
    for (size_t i = 0; i < kLangs; ++i)
        if (code == kInfo[i].code) return static_cast<Language>(i);
    return Language::English;
}

const char* catalogLanguage() { return info().catalog; }
const char* gameLocale() { return info().locale; }

const char* tr(Str id) {
    auto i = static_cast<size_t>(id);
    if (i >= kStrs) return "";
    const char* s = (*kTable[static_cast<size_t>(g_language.load())])[i];
    return s && *s ? s : kEnglish[i];
}

std::string trf(Str id, const std::string& arg) {
    std::string s = tr(id);
    size_t at = s.find("%s");
    if (at != std::string::npos) s.replace(at, 2, arg);
    return s;
}

std::string trf(Str id, const std::string& arg1, const std::string& arg2) {
    std::string s = tr(id);
    size_t at = s.find("%s");
    if (at != std::string::npos) s.replace(at, 2, arg1);
    at = s.find("%s");
    if (at != std::string::npos) s.replace(at, 2, arg2);
    return s;
}

std::string touchpadActionLabel(int actionIndex) {
    switch (actionIndex) {
    case 0: return tr(Str::TouchpadActNone);
    case 1: return tr(Str::TouchpadActGameMenu);
    case 2: return tr(Str::TouchpadActXboxButton);
    case 3: return tr(Str::TouchpadActToggleStats);
    case 4: return tr(Str::TouchpadActToggleMic);
    case 5: return tr(Str::TouchpadActCycleGyro);
    case 6: return tr(Str::TouchpadActCycleTrigger);
    case 7: return "View (Select)";
    case 8: return "Menu (Start)";
    case 9: return "LS (L3)";
    case 10: return "RS (R3)";
    case 11: return "A";
    case 12: return "B";
    case 13: return "X";
    case 14: return "Y";
    case 15: return "LB (L1)";
    case 16: return "RB (R1)";
    case 17: return std::string(tr(Str::DPad)) + " ↑";
    case 18: return std::string(tr(Str::DPad)) + " ↓";
    case 19: return std::string(tr(Str::DPad)) + " ←";
    case 20: return std::string(tr(Str::DPad)) + " →";
    default: return tr(Str::TouchpadActNone);
    }
}

const char* touchpadTriggerLabel(int triggerIndex) {
    switch (triggerIndex) {
    case 0: return tr(Str::TouchpadSwipeUp);
    case 1: return tr(Str::TouchpadSwipeDown);
    case 2: return tr(Str::TouchpadSwipeLeft);
    case 3: return tr(Str::TouchpadSwipeRight);
    case 4: return tr(Str::TouchpadClickLeft);
    case 5: return tr(Str::TouchpadClickRight);
    default: return "";
    }
}

}  // namespace xc::ui
