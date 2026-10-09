// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// User-facing text in every supported language. English is the default; the
// user picks another in Settings. To add a language: a Language value, its
// codes in strings.cpp, and a column in the table there.
#pragma once

#include <string>

namespace xc::ui {

enum class Str {
    AppName,
    CloudGaming,
    SigningIn,
    RequestingCode,
    LoadingGames,
    SignInTitle,
    SignInStep1,
    SignInStep2,
    SignInScan,
    SignInWaiting,
    JumpBackIn,
    Play,
    Back,
    Select,
    SignOut,
    Cancel,
    TryAgain,
    GettingReady,
    InQueue,       // "%s" = estimated wait
    Connecting,
    StartingStream,
    MenuGestureHint,  // the game menu's footer: the touchpad gestures
    StreamEnded,
    ErrorTitle,
    SignedInAs,    // "%s" = gamertag
    NoGames,
    PublisherBy,   // "%s" = publisher
    Settings,
    Language,
    Resolution,
    Region,
    RegionAuto,    // "%s" = the account's default region
    Res1080,
    Res720,
    Res1440,
    SettingsNote,
    Change,
    HoldSignOut,
    SigningOutIn,     // "%s" = seconds left
    SignInFailed,     // "%s" = technical detail
    LibraryFailed,    // "%s" = technical detail
    StreamFailed,     // "%s" = technical detail
    IdleWarning,      // "%s" = seconds until the server disconnects
    YourGames,        // row of the account's own (non-Game Pass) games
    NotPlayable,      // the account can't stream this game
    TabGamePass,
    TabSearch,
    SearchHint,
    NoResults,
    ResultsCount,     // "%s" = number
    KeySpace,
    KeyDelete,
    KeyClear,
    GamesCount,       // "%s" = number
    KeyType,          // hint: press to type the focused key
    TryingRegion,     // "%s" = region name
    AvailableToBuy,   // section of games that stream once bought
    BuyBadge,         // short, on the card
    BuyToPlay,
    BuyHint,
    ScanToBuy,
    SearchIn,         // "%s" = tab name
    Free,             // price 0 (free-to-play games still "bought" in the store)
    Hide,
    Unhide,
    HiddenSection,    // section of hidden games in "Your games"
    HiddenHint,
    HiddenToast,
    UnhiddenToast,
    SortLabel,        // "%s" = sort name
    SortRecent,
    SortAZ,
    SortConsole,
    SortHint,         // hint for R3
    FilterFree,
    FilterCheapest,
    FilterAllConsoles,
    OneGame,          // GamesCount for exactly one
    Sections,         // hint for L2 / R2: jump between sections
    UpdatingList,     // the lists refresh in the background
    MenuTitle,        // the in-game menu
    MenuStats,        // show the statistics over the game
    On,
    Off,
    MenuResolution,
    MenuLeave,
    StatLatency,
    StatBitrate,
    StatFrameRate,
    StatLoss,
    StatRegion,
    StatDecode,
    ResolutionNote,   // the server may not grant the tier asked for
    MenuSharpness,    // sharpening filter over the video
    SharpOff,
    SharpLow,
    SharpMedium,
    SharpHigh,
    Deadzone,         // settings: stick dead zone
    TriggerRumble,    // settings: vibration in the triggers
    ConfirmButton,    // settings: which button is Xbox A
    Activated,        // feminine on/off where the language needs it
    Deactivated,
    UpdateAvailable,  // the update pop-up's title; "%s" = the new version
    Controllers,      // in-game menu: the pads in use
    PadConnected,     // "%s" = "2 (user name)"
    PadDisconnected,  // "%s" = "2 (user name)"
    MenuDeband,       // smoothing of compression blocks (deband)
    StatOnScreen,     // network arrival to on the TV
    Reconnecting,     // the connection dropped mid-game
    Reconnected,
    LightBar,         // settings: the DualSense light bar's colour
    MenuUpscaler,     // game menu: how the picture is upscaled to 4K
    UpscalerAi,       // the Anime4K network
    FreeToPlay,       // details: a free-to-play game not on the account yet
    FreeHint,
    ScanToGet,
    NoEntitlementFree,  // "%s" = the game
    NoEntitlement,      // "%s" = the game
    TabConsoles,        // the third tab: the user's own Xbox consoles
    ConsoleOn,
    ConsoleSleeping,
    ConsoleOff,
    ConsolesLoading,
    NoConsoles,         // heading when none is found
    ConsolesHint,       // under the tab's title
    WakingConsole,      // the loading screen while the Xbox wakes up
    WakeFailed,         // "%s" = the console's name
    ConsoleStep1,       // how a console shows up, three steps
    ConsoleStep2,
    ConsoleStep3,
    ScanForHelp,        // under the QR code of Microsoft's Remote Play help
    SearchAgain,        // refresh the console list
    MenuXboxButton,     // game menu: press the Xbox button (opens the Xbox guide)
    MenuEndStream,      // game menu on the user's own Xbox: instead of "Leave the game"
    EndedOnXbox,        // toast: the Xbox ended the stream
    EndedByOtherDevice, // toast: another device took the stream over
    EndedXboxOff,       // toast: the Xbox was turned off during the stream
    StreamingStuck,     // "%s" = the console: its Remote Play service didn't start
    GestureHint,        // at the start of a stream (the first three): the touchpad gestures
    UpscalerFsrClean,   // game menu: FSR after Anime4K Restore (cleans compression artefacts)
    UpscalerAiClean,    // Anime4K upscale after Anime4K Restore
    DebandAuto,         // "%s" = the level in use now; block smoothing follows the bitrate
    UpdatePrompt,       // the update pop-up's text
    UpdateNow,          // button
    NotNow,             // button
    Updates,            // Settings row
    UpToDate,           // "%s" = this version
    UpdateReady,        // "%s" = the new version: Settings, Cross updates
    UpdateChecking,     // Settings, while it asks
    UpdatingTo,         // "%s" = the new version
    UpdateDownloading,  // "%s" = percent
    UpdateVerifying,
    UpdateInstalling,
    UpdateRestarting,
    UpdateReopen,       // notification when it couldn't restart
    UpdateFailed,       // toast; nothing was changed
    LightBarGame,       // Settings > Light bar: the game's colour
    LightBarCustom,     // Settings > Light bar: one the user picks
    LightBarColour,     // the colour picker's title
    ColourPickerHelp,   // under the colour picker
    TriggerLight,       // trigger vibration strengths (after Deactivated)
    TriggerMedium,      // also the middle frequency
    TriggerStrong,
    TriggerMax,
    TriggerIntensity,   // the trigger tester's rows
    TriggerFrequency,
    FrequencyLow,
    FrequencyHigh,
    TriggerTestHelp,    // under the trigger tester
    TriggerLevel,       // "%s" = 0..8, the strength sent to the trigger
    StickLeft,          // the stick tester
    StickRight,
    StickPosition,      // legend: the grey dot
    StickGameGets,      // legend: the green dot
    StickTestHelp,      // under the stick tester
    TriggerResistance,  // the trigger tester: the triggers' weight
    TriggerStyle,       // the trigger tester: how the game's vibration is felt
    StyleVibration,
    StylePulses,        // the resistance pushing and letting go
    FilterMode,         // search filter (a list): how a game can be played
    FilterGenre,        // search filter (a list)
    FilterLanguage,     // search filter (a list): translated into the app's language
    ModeAll,
    ModeSingle,
    ModeOnlineMulti,
    ModeOnlineCoop,
    ModeLocal,          // local multiplayer or co-op, split screen
    GenreAll,
    LanguageAll,
    LanguageNoun,       // this language's name, as the filters say it
    LangSubtitles,      // "%s" = LanguageNoun: subtitles or menus in it
    LangAudio,          // "%s" = LanguageNoun: spoken in it
    Count
};

enum class Language { English, PortugueseBR, Spanish, French, German, Italian, Count };

void setLanguage(Language lang);
Language language();
// The language's own name ("Português (Brasil)").
const char* languageName(Language lang);
// Stable code saved in settings.json ("pt-BR"); fromCode() falls back to English.
const char* languageCode(Language lang);
Language languageFromCode(const std::string& code);
// Catalog language tag for the current language, e.g. "pt-br".
const char* catalogLanguage();
// Locale asked of the cloud game, e.g. "pt-BR".
const char* gameLocale();

const char* tr(Str id);
// tr() with one "%s" substituted.
std::string trf(Str id, const std::string& arg);

}  // namespace xc::ui
