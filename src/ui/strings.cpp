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

constexpr Column kEnglish = {
    "PSBox Cloud Gaming",                             // AppName
    "PSBOX CLOUD GAMING",                             // CloudGaming
    "Signing in...",                                  // SigningIn
    "Getting a sign-in code...",                      // RequestingCode
    "Loading your games...",                          // LoadingGames
    "Sign in to play",                                // SignInTitle
    "On your phone or computer, go to",               // SignInStep1
    "and enter this code",                            // SignInStep2
    "Scan to open the page, then enter the code",     // SignInScan
    "Waiting for you to sign in...",                  // SignInWaiting
    "Jump back in",                                   // JumpBackIn
    "Play",                                           // Play
    "Back",                                           // Back
    "Select",                                         // Select
    "Sign out",                                       // SignOut
    "Cancel",                                         // Cancel
    "Try again",                                      // TryAgain
    "Getting your game ready",                        // GettingReady
    "You're in the queue. Estimated wait: %s",        // InQueue
    "Connecting...",                                  // Connecting
    "Starting the stream...",                         // StartingStream
    "Touchpad: ↓ this menu · ↑ Xbox button",            // MenuGestureHint
    "Stream ended",                                   // StreamEnded
    "Something went wrong",                           // ErrorTitle
    "Signed in as %s",                                // SignedInAs
    "No games to show yet",                           // NoGames
    "By %s",                                          // PublisherBy
    "Settings",                                       // Settings
    "Language",                                       // Language
    "Stream resolution",                              // Resolution
    "Server region",                                  // Region
    "Automatic (%s)",                                 // RegionAuto
    "1080p (Full HD)",                                // Res1080
    "720p (uses less data)",                          // Res720
    "Best (Auto)",                                    // ResBest
    "Resolution and region apply to the next game you start.",  // SettingsNote
    "Change",                                         // Change
    "Adjust",                                         // Adjust
    "Hold to sign out",                               // HoldSignOut
    "Keep holding to sign out (%s)",                  // SigningOutIn
    "Sign-in failed: %s",                             // SignInFailed
    "Sign-in not completed in the browser. Use a personal Microsoft account, complete any security prompts/2FA, and grant access to the app.",  // SignInInteractionRequired
    "Could not load the game list: %s",               // LibraryFailed
    "Could not start the stream: %s",                 // StreamFailed
    "No input for a while: you will be disconnected in %s seconds",  // IdleWarning
    "My games",                                     // YourGames
    "Not available on your account",                  // NotPlayable
    "Game Pass",                                    // TabGamePass
    "Search",                                       // TabSearch
    "Type the name of a game",                      // SearchHint
    "No games found",                               // NoResults
    "%s results",                                   // ResultsCount
    "Space",                                        // KeySpace
    "Delete",                                       // KeyDelete
    "Clear",                                        // KeyClear
    "%s games",                                     // GamesCount
    "Type",                                         // KeyType
    "Trying another region: %s",                    // TryingRegion
    "Available to buy",                              // AvailableToBuy
    "BUY",                                           // BuyBadge
    "Buy this game to play it in the cloud",         // BuyToPlay
    "Buy it on xbox.com or in the Xbox app; it then shows up in My games.", // BuyHint
    "Scan to open the store page",                   // ScanToBuy
    "Search in %s",                                  // SearchIn
    "FREE",                                          // Free
    "Hide",                                          // Hide
    "Show",                                          // Unhide
    "Hidden",                                        // HiddenSection
    "Square shows a game again",                     // HiddenHint
    "Hidden: find it under Hidden, at the end of this tab", // HiddenToast
    "Shown again",                                   // UnhiddenToast
    "Sort: %s",                                      // SortLabel
    "Recent first",                                  // SortRecent
    "A-Z",                                           // SortAZ
    "By console",                                    // SortConsole
    "Sort",                                          // SortHint
    "Free",                                          // FilterFree
    "Lowest price",                                  // FilterCheapest
    "All consoles",                                  // FilterAllConsoles
    "1 game",                                        // OneGame
    "Sections",                                      // Sections
    "Updating the game list...",                     // UpdatingList
    "Game menu",                                     // MenuTitle
    "Statistics",                                    // MenuStats
    "On",                                            // On
    "Off",                                           // Off
    "Stream resolution",                             // MenuResolution
    "Leave game",                                    // MenuLeave
    "Latency",                                       // StatLatency
    "Bitrate",                                       // StatBitrate
    "Frame rate",                                    // StatFrameRate
    "Packet loss",                                   // StatLoss
    "Region",                                        // StatRegion
    "Decoding",                                      // StatDecode
    "The server may keep the current quality",       // ResolutionNote
    "Sharpness",                                     // MenuSharpness
    "Off",                                           // SharpOff
    "Low",                                           // SharpLow
    "Medium",                                        // SharpMedium
    "High",                                          // SharpHigh
    "Stick dead zone",                               // Deadzone
    "Trigger vibration",                             // TriggerRumble
    "Confirm button",                                // ConfirmButton
    "On",                                            // Activated
    "Off",                                           // Deactivated
    "Version %s available",                          // UpdateAvailable
    "Controllers",                                   // Controllers
    "Controller %s connected",                       // PadConnected
    "Controller %s disconnected",                    // PadDisconnected
    "Block smoothing",                               // MenuDeband
    "On screen after",                               // StatOnScreen
    "Connection lost: reconnecting...",              // Reconnecting
    "Reconnected",                                   // Reconnected
    "Light bar",                                     // LightBar
    "Upscaling",                                     // MenuUpscaler
    "AI (Anime4K)",                                  // UpscalerAi
    "Free to play",                                  // FreeToPlay
    "Get it once, free, on xbox.com or in the Xbox app (scan the code with your phone), then press Play.", // FreeHint
    "Scan to get it free",                          // ScanToGet
    "%s is not on your account yet. It is free: scan the code on its page, choose Get, then try again (it can take a minute).", // NoEntitlementFree
    "Your account can't play %s: it is not in your subscription or not bought.", // NoEntitlement
    "My consoles",                                   // TabConsoles
    "On",                                            // ConsoleOn
    "Sleeping",                                      // ConsoleSleeping
    "Off",                                           // ConsoleOff
    "Looking for your consoles...",                  // ConsolesLoading
    "No Xbox found yet",                       // NoConsoles
    "Play your own Xbox here: its screen, games and apps.", // ConsolesHint
    "Waking up your Xbox...",                        // WakingConsole
    "%s did not answer. Check that Remote features are on and the power mode is Sleep, or turn it on, then try again.", // WakeFailed
    "On your Xbox, sign in with this same Microsoft account.", // ConsoleStep1
    "Settings > Devices & connections > Remote features: turn on remote features.", // ConsoleStep2
    "Settings > General > Power options: choose Sleep, so it wakes up by itself.", // ConsoleStep3
    "Scan for Microsoft's help",                     // ScanForHelp
    "Search again",                                  // SearchAgain
    "Xbox button",                                   // MenuXboxButton
    "End the stream",                                // MenuEndStream
    "The stream was ended on the Xbox",               // EndedOnXbox
    "Another device took over the stream",            // EndedByOtherDevice
    "The Xbox was turned off",                        // EndedXboxOff
    "%s got the request but its Remote Play didn't start. Restart it: hold the power button on the console for 10 seconds, turn it back on, then try again.", // StreamingStuck
    "Touchpad: swipe ↓ for the menu, ↑ for the Xbox button", // GestureHint
    "FSR + clean-up",                                // UpscalerFsrClean
    "AI + clean-up",                                 // UpscalerAiClean
    "Auto (%s)",                                     // DebandAuto
    "Update now? The app restarts by itself when it's done.", // UpdatePrompt
    "Update now",                                    // UpdateNow
    "Not now",                                       // NotNow
    "Updates",                                       // Updates
    "Up to date (%s)",                               // UpToDate
    "%s available: update",                          // UpdateReady
    "Checking...",                                   // UpdateChecking
    "Updating to %s",                                // UpdatingTo
    "Downloading... %s",                             // UpdateDownloading
    "Checking the signature...",                     // UpdateVerifying
    "Installing...",                                 // UpdateInstalling
    "Updated. Restarting...",                        // UpdateRestarting
    "PSBox updated: open the app again",             // UpdateReopen
    "Update failed: nothing was changed",            // UpdateFailed
    "Game colour",                                   // LightBarGame
    "Custom colour",                                 // LightBarCustom
    "Light bar colour",                              // LightBarColour
    "Left stick or D-pad: colour    L2 / R2: brightness", // ColourPickerHelp
    "Light",                                         // TriggerLight
    "Medium",                                        // TriggerMedium
    "Strong",                                        // TriggerStrong
    "Max",                                           // TriggerMax
    "Intensity",                                     // TriggerIntensity
    "Frequency",                                     // TriggerFrequency
    "Low",                                           // FrequencyLow
    "High",                                          // FrequencyHigh
    "Press L2 / R2 to feel it: the deeper, the stronger the game would ask", // TriggerTestHelp
    "Level %s",                                      // TriggerLevel
    "Left stick",                                    // StickLeft
    "Right stick",                                   // StickRight
    "Stick position",                                // StickPosition
    "What the game gets",                            // StickGameGets
    "L1 / R1: choose the stick    D-pad ← / →: adjust    Move the sticks to see it", // StickTestHelp
    "Resistance",                                    // TriggerResistance
    "Style",                                         // TriggerStyle
    "Vibration",                                     // StyleVibration
    "Force pulses",                                  // StylePulses
    "Mode",                                          // FilterMode
    "Genre",                                         // FilterGenre
    "Language",                                      // FilterLanguage
    "All modes",                                     // ModeAll
    "Single player",                                 // ModeSingle
    "Online multiplayer",                            // ModeOnlineMulti
    "Online co-op",                                  // ModeOnlineCoop
    "Local / split screen",                          // ModeLocal
    "All genres",                                    // GenreAll
    "Any language",                                  // LanguageAll
    "English",                                       // LanguageNoun
    "Subtitles in %s",                               // LangSubtitles
    "Audio in %s",                                   // LangAudio
    "Game settings",                                 // GameSettings
    "Profile",                                       // Profile
    "Default",                                       // ProfileDefault
    "Custom",                                        // ProfileCustom
    "Reset to default",                              // ResetToDefault
    "Publisher",                                     // FilterPublisher
    "All publishers",                                // PublisherAll
    "Indies & Others",                               // PublisherIndies
    "Friends playing now",                           // FriendsPlayingNow
    "Microphone",                                    // MenuMicrophone
    "Muted",                                         // MicMuted
    "Active",                                        // MicActive
    "Audio output",                                  // MenuAudioOutput
    "TV",                                            // AudioRouteTv
    "DualSense Speaker",                             // AudioRoutePad
    "Report a problem",                              // ReportProblem
    "Scan it with your phone: GitHub's bug form opens, filled in. You review it before sending it.", // ReportScan
    "It includes the app's version, the error and lines of its log, without personal data.", // ReportIncludes
    "PSBox closed unexpectedly last time",           // CrashedLastTime
    "Sign-in country",                                // LoginCountry
    "Off (this console's)",                           // LoginCountryOff
    "Sign in as another country",                     // TryOtherCountry
    "xCloud will be told the sign-in comes from the chosen country. Microsoft doesn't allow this: use it at your own risk. Games may then come from servers far away, with more delay. You can change it later in Settings.", // LoginCountryWarning
    "xCloud refused the sign-in. Cloud gaming may not be offered in your country, or the account may not have a Game Pass plan with cloud gaming.", // RegionRefused
    "xCloud refused the sign-in, even as %s. The account probably doesn't have a Game Pass plan with cloud gaming.", // RegionRefusedAs
    "Brazil",                                         // CountryBR
    "Japan",                                          // CountryJP
    "South Korea",                                    // CountryKR
    "Poland",                                         // CountryPL
    "United States",                                  // CountryUS
    "Secure connections were refused because the PS5's date and time look wrong (it says %s). Set them in Settings > System > Date and Time, then try again.", // ClockWrong
    "A secure connection was refused: something on the network (a DNS server, proxy or filter) is answering for Microsoft's sites. Try without it, then try again.", // ConnectionIntercepted
    "PS5 date and time look wrong (%s): some content can't load. Set them in Settings > System > Date and Time.", // ClockWrongNotice
    "Your sign-in couldn't be saved on the console, so it will be asked again next time. Please report it (Settings > △).", // SignInNotSaved
    "Estimated wait: %s s", // QueueSeconds
    "Estimated wait: %s min", // QueueMinutes
    "120 Hz output (VRR)", // HighRefresh
    "On (in games)", // HighRefreshOn
    "Unstable connection", // ConnectionUnstable
    "Session: %s", // SessionSummary
    "Maximum bitrate", // BitrateCap
    "Automatic", // BitrateAuto
    "Game volume", // GameVolume
    "Keyboard: F12 this menu · Home Xbox button", // MenuKeyHint
    "Mouse & keyboard", // FilterMouseKeyboard
    "Mouse and keyboard", // MouseKeyboardSupport
    "Keyboard and mouse: player %s", // KeyboardPlayer
    "You're in the queue.", // InQueueNoEstimate
    "Keyboard keys", // KeyboardKeys
    "D-pad", // DPad
    "Press a key or mouse button", // PressKey
    "%s is kept by the app", // KeyReserved
    "Change", // ChangeKey
    "Clear", // ClearKey
    "The mouse moves the right stick and a wheel notch presses Triangle. F12 (menu), Home (Xbox button) and 1-4 (player) are kept by the app.", // KeysHelp
    "Gyro aiming", // Gyro
    "Always", // GyroAlways
    "While aiming (L2)", // GyroAiming
    "Gyro intensity", // GyroIntensity
    "Secure connections were refused because this computer's date and time look wrong (it says %s). Correct them in the system's settings, then try again.", // ClockWrongPc
    "This computer's date and time look wrong (%s): some content can't load. Correct them in the system's settings.", // ClockWrongNoticePc
    "Off (this computer's)", // LoginCountryOffPc
    "Open in the browser", // OpenInBrowser
    "%s available: download", // UpdateReadyPc
    "Menu: START + BACK or F12 · Xbox button: Guide or Home", // GestureHintButtons
    "START + BACK or F12: this menu · Guide or Home: Xbox button", // MenuButtonHint
    "Display mode", // DisplayMode
    "Window", // DisplayWindow
    "Fullscreen", // DisplayFullscreen
    "Quit PSBox?", // QuitTitle
    "Quit", // QuitConfirm
    "Controller buttons", // ControllerButtons
    "Press the button for %s", // PadPressButton
    "Missing a button: %s. Give each one a button, or reset or discard the changes.", // PadMissing
    "Discard changes", // DiscardChanges
    "Only the game sees this: the app's menus keep the buttons as they are. Picking a button used by another swaps the two.", // PadHelp
    "Plan", // FilterPlan
    "My plan", // PlanMine
    "All plans", // PlanAll
    "Included with Game Pass %s", // IncludedWithPlan
    "Touchpad as mouse", // TouchpadMouse
    "Favourites", // Favorites
    "Add to favourites", // AddFavorite
    "Remove from favourites", // RemoveFavorite
    "Added to favourites", // AddedToFavorites
    "Removed from favourites", // RemovedFromFavorites
    "Game language", // GameLanguage
    "Monthly cloud gaming limit reached", // MonthlyLimitExceeded
    "Playtime remaining", // PlaytimeRemaining
    "%s h remaining", // HoursRemaining
    "Less than 2 h remaining", // PlaytimeWarning
    "Trigger profile", // TriggerProfile
    "Standard", // TriggerProfileStd
    "Shooter (Hair Trigger)", // TriggerProfileShooter
    "Racing (ABS)", // TriggerProfileRacing
    "Storage %s is not writable: sign-in and settings will not be saved", // StorageNotWritable
    "FTP: run 'chmod 777 %s' or delete the folder and relaunch", // StorageFixHint
    "Customize indicators",                           // StatsCustomize
    "FPS & Resolution",                               // StatsIndicatorFps
    "Bitrate",                                        // StatsIndicatorBitrate
    "Latency (RTT)",                                  // StatsIndicatorLatency
    "Packet loss",                                    // StatsIndicatorLoss
    "Frametime graph",                                // StatsIndicatorGraph
    "Decode & display latency",                       // StatsIndicatorDelay
    "Video quality (QP)",                             // StatsIndicatorQp
    "Microphone status",                              // StatsIndicatorMic
    "Scan the QR code on your phone to resolve this with Microsoft, then press Cross to try again",  // ScanQrToResolve
    "Get Free",                                       // ClaimFree
    "Claiming free license...",                       // Claiming
    "Game added to your account!",                    // ClaimSuccess
    "Failed to claim free game: %s",                  // ClaimFailed
    "This game is not free in the store",             // ProductNotFree
    "Free game. Press Cross to add to your account and start playing",  // FreeAcquireHint
    "DNS lookup failed for Microsoft servers. If you are blocking Sony updates on PS5, make sure you are running NanoDNS with Primary DNS set to your local IP to keep internet access.", // DnsBlockedNanoDns
    "This Microsoft account has no Xbox profile yet. Sign in to xbox.com on your phone or PC to set up your Gamertag, then try again.", // XboxProfileMissing
    "Queue ready! Starting game...",                  // QueueReadyAlert
    "Queue ready chime",                              // QueueReadyNotification
    "Played: %s",                                     // PlaytimePlayed
    "Refresh",                                        // RefreshLibrary
    "Refreshing library...",                          // RefreshingLibrary
    "This Xbox account is suspended or banned. Visit enforcement.xbox.com for details.", // XboxAccountBanned
    "Xbox Live is not offered in this account's country or region.", // XboxRegionNotSupported
    "Age verification or terms acceptance is required. Sign in to xbox.com in a browser, then try again.", // XboxAdultVerificationRequired
    "Child account: an adult must add this account to a Microsoft family at account.microsoft.com/family.", // XboxChildAccountFamily
    "Check my sign-in",                               // CheckSignIn
    "Run check",                                      // RunCheck
    "Network test",                                   // NetworkTest
    "Run test",                                       // RunTest
    "Achievements",                                   // Achievements
    "No achievements",                                // NoAchievements
    "Locked",                                         // AchievementLocked
    "Secret achievement",                             // SecretAchievement
    "Wishlist",                                       // Wishlist
    "Add to wishlist",                                // AddWishlist
    "Remove from wishlist",                           // RemoveWishlist
    "Added to wishlist",                              // AddedToWishlist
    "Removed from wishlist",                          // RemovedFromWishlist
    "%s is on sale!",                                 // WishlistOnSale
    "%s joined Game Pass!",                           // WishlistInGamePass
    "Support the project",                            // SupportProject
    "Support PSBox Development",                      // SupportTitle
    "PSBox is an open-source project passionately maintained to bring the best Xbox Cloud Gaming experience to PlayStation 5. If you enjoy the app and would like to support ongoing development, frequent updates and new features, consider buying me a coffee on Ko-Fi!", // SupportMessage
    "Ko-fi",                                          // SupportKoFi
    "Scan with your phone to support",                // ScanToDonate
    "Test sound",                                     // TestSound
    "Wishlist is empty",                              // WishlistEmpty
    // Loading tips
    "Touchpad: swipe ↓ (or Touchpad + Options) for the menu, ↑ for the Xbox button", // TipTouchpadMenu
    "USB keyboard and mouse plugged into the console are supported in enabled cloud games", // TipKeyboardMouse
    "Claim free games, trials, and discover store promotions directly from the app library", // TipFreeStore
    "Encountered an issue? Open Bug Report to scan a QR code with auto-attached console logs", // TipBugReportQr
    "Enjoying the project? Support future development via Ko-fi in the Settings tab!", // TipSupportKofi
    "Enable 120 Hz display output in Settings for reduced frame latency and smoother response", // Tip120HzVideo
    "Calibrate DualSense stick dead zones in Settings to eliminate stick drift and improve precision", // TipAnalogDeadzone
    "Use the DualSense built-in microphone for party and multiplayer game voice chat", // TipDualSenseMic
    // Settings descriptions
    "Changes the language of the application interface and the preferred audio and subtitles in cloud games", // SettingDescLanguage
    "Sets the maximum streaming video resolution (1080p, 720p, or Best available). Takes effect on the next launch", // SettingDescResolution
    "Selects the Microsoft datacenter region for streaming (Automatic or specific regions). Takes effect on the next launch", // SettingDescRegion
    "Adjusts the inner dead zone of DualSense analog sticks to prevent stick drift and unintended movement", // SettingDescDeadzone
    "Enables or disables impulse trigger vibration emulation on DualSense L2 and R2 adaptive triggers", // SettingDescTriggerRumble
    "Toggles the confirm and back button layout between Cross (Western style) and Circle (Japanese style)", // SettingDescConfirmButton
    "Configures the light bar color and behavior of your DualSense controller during browsing and gameplay", // SettingDescLightBar
    "Checks for and automatically installs official app updates directly on your console without needing a PC", // SettingDescUpdates
    "Allows signing in to your Xbox account via a different geographic region to access alternative catalogs", // SettingDescLoginCountry
    "Enables 120 Hz video output on your console for lower display lag and reduced input latency", // SettingDescHighRefresh
    "Sets the maximum bandwidth limit for video streaming, ensuring higher visual fidelity or saving network data", // SettingDescBitrateCap
    "Adjusts the master audio volume level played during game streaming sessions", // SettingDescGameVolume
    "Maps USB or Bluetooth keyboard keys to the corresponding Xbox controller buttons during gameplay", // SettingDescKeyboardKeys
    "Toggles between windowed and fullscreen display mode on supported desktop platforms", // SettingDescDisplayMode
    "Opens the response test and calibration screen for all buttons and axes on your connected controller", // SettingDescControllerButtons
    "Enables DualSense motion sensors to assist with analog aiming in shooter and action games", // SettingDescGyro
    "Plays a distinctive chime when your wait in the xCloud queue finishes and your game is ready", // SettingDescQueueReady
    "Runs a complete diagnostic on your Xbox Live sign-in, validating access tokens, profile, and subscriptions", // SettingDescCheckSignIn
    "Measures ping (RTT) and jitter to cloud gaming datacenters across multiple global regions", // SettingDescNetworkTest
    "Displays a QR Code to support the developer via Ko-fi and help keep the project actively maintained", // SettingDescSupportProject
    "What's New",                                     // WhatsNew
    "What's New",                                     // WhatsNewTitle
    "Controller speaker",                             // PadSpeakerVolume
    "Mute",                                           // Mute
    "Web Control",                                    // WebControlTitle
    "Connect from your phone",                        // WebControlSubtitle
    "Connect to the same Wi-Fi to type search, remap buttons, use your phone as a virtual controller, report issues or view logs.", // WebControlDesc
    "Video Decoder",                                  // VideoDecoder
    "Hardware (Recommended)",                         // VideoDecoderHw
    "Software (CPU)",                                 // VideoDecoderSw
    "Selects hardware decoding (libSceVideodec2 on PS5, HWAccel on Desktop) or CPU software decoding", // SettingDescVideoDecoder
    "Audio-to-Haptics",                               // AudioHaptics
    "Converts low-frequency game audio into DualSense haptic rumble kicks", // SettingDescAudioHaptics
    "Touchpad shortcuts",                             // TouchpadShortcuts
    "Configures swipe gestures and left/right click actions on the DualSense touchpad", // SettingDescTouchpadShortcuts
    "Swipe Up (↑)",                                   // TouchpadSwipeUp
    "Swipe Down (↓)",                                 // TouchpadSwipeDown
    "Swipe Left (←)",                                 // TouchpadSwipeLeft
    "Swipe Right (→)",                                // TouchpadSwipeRight
    "Left Click",                                     // TouchpadClickLeft
    "Right Click",                                    // TouchpadClickRight
    "Disabled",                                       // TouchpadActNone
    "In-game menu",                                   // TouchpadActGameMenu
    "Xbox button",                                    // TouchpadActXboxButton
    "Toggle stats",                                   // TouchpadActToggleStats
    "Mute/unmute mic",                                // TouchpadActToggleMic
    "Cycle gyro mode",                                // TouchpadActCycleGyro
    "Cycle trigger profile",                          // TouchpadActCycleTrigger
    "Touchpad + Options always opens the in-game menu", // TouchpadHelp
    "Restore default settings",                       // ResetSettings
    "Restores all settings to factory defaults. Hold Cross for 5 seconds to confirm.", // SettingDescResetSettings
    "Settings restored to factory defaults.",         // SettingsResetNotice
    "Hold to reset",                                  // HoldToReset
    "Library is already up to date",                  // LibraryUpToDate
    "Library updated (%s new games)",                 // LibraryUpdatedCount
    "Offline mode",                                   // OfflineMode
};

constexpr Column kPortugueseBR = {
    "PSBox Cloud Gaming",
    "PSBOX CLOUD GAMING",
    "Entrando...",
    "Gerando um código de login...",
    "Carregando seus jogos...",
    "Entre para jogar",
    "No celular ou computador, acesse",
    "e digite este código",
    "Escaneie para abrir a página e digite o código",
    "Aguardando você entrar...",
    "Continue jogando",
    "Jogar",
    "Voltar",
    "Selecionar",
    "Sair da conta",
    "Cancelar",
    "Tentar de novo",
    "Preparando seu jogo",
    "Você está na fila. Espera estimada: %s",
    "Conectando...",
    "Iniciando o streaming...",
    "Touchpad: ↓ este menu · ↑ botão Xbox",
    "Streaming encerrado",
    "Algo deu errado",
    "Conectado como %s",
    "Nenhum jogo para mostrar ainda",
    "Por %s",
    "Configurações",
    "Idioma",
    "Resolução do streaming",
    "Região do servidor",
    "Automática (%s)",
    "1080p (Full HD)",
    "720p (usa menos dados)",
    "Melhor (Auto)",
    "A resolução e a região valem a partir do próximo jogo.",
    "Alterar",
    "Ajustar",
    "Segure para sair da conta",
    "Continue segurando para sair da conta (%s)",
    "Falha ao entrar: %s",
    "O login no navegador não foi concluído. Use uma conta pessoal da Microsoft, conclua verificações de segurança/2FA e autorize o app.",
    "Não foi possível carregar a lista de jogos: %s",
    "Não foi possível iniciar o streaming: %s",
    "Sem atividade: você será desconectado em %s segundos",
    "Meus jogos",
    "Indisponível na sua conta",
    "Game Pass",
    "Pesquisar",
    "Digite o nome de um jogo",
    "Nenhum jogo encontrado",
    "%s resultados",
    "Espaço",
    "Apagar",
    "Limpar",
    "%s jogos",
    "Digitar",
    "Tentando outra região: %s",
    "Disponíveis para comprar",
    "COMPRAR",
    "Compre este jogo para jogá-lo na nuvem",
    "Compre em xbox.com ou no app Xbox; depois ele aparece em Meus jogos.",
    "Escaneie para abrir a página da loja",
    "Pesquisar em %s",
    "GRÁTIS",
    "Ocultar",
    "Mostrar",
    "Ocultos",
    "\xE2\x96\xA1 mostra o jogo de novo",
    "Oculto: veja em Ocultos, no fim desta aba",
    "Visível de novo",
    "Ordenar: %s",
    "Recentes primeiro",
    "A-Z",
    "Por console",
    "Ordenar",
    "Grátis",
    "Menor preço",
    "Todos os consoles",
    "1 jogo",
    "Seções",
    "Atualizando a lista de jogos...",
    "Menu do jogo",
    "Estatísticas",
    "Ligado",
    "Desligado",
    "Resolução do stream",
    "Sair do jogo",
    "Latência",
    "Taxa de bits",
    "Quadros por segundo",
    "Perda de pacotes",
    "Região",
    "Decodificação",
    "O servidor pode manter a qualidade atual",
    "Nitidez",
    "Desligada",
    "Baixa",
    "Média",
    "Alta",
    "Zona morta do analógico",
    "Vibração nos gatilhos",
    "Botão de confirmar",
    "Ativada",
    "Desativada",
    "Versão %s disponível",
    "Controles",
    "Controle %s conectado",
    "Controle %s desconectado",
    "Suavização de blocos",
    "Na tela após",
    "Conexão perdida: reconectando...",
    "Reconectado",
    "Barra de luz",
    "Ampliação",
    "IA (Anime4K)",
    "Grátis para jogar",
    "Pegue uma vez, de graça, no xbox.com ou no app Xbox (escaneie o código com o celular) e depois aperte Jogar.",
    "Escaneie para pegar grátis",
    "%s ainda não está na sua conta. É grátis: escaneie o código na página do jogo, escolha Obter e tente de novo (pode levar um minuto).",
    "Sua conta não pode jogar %s: não está na sua assinatura ou não foi comprado.",
    "Meus consoles",
    "Ligado",
    "Em espera",
    "Desligado",
    "Procurando seus consoles...",
    "Nenhum Xbox encontrado ainda",
    "Jogue no seu próprio Xbox por aqui: a tela, os jogos e os apps dele.",
    "Ligando o seu Xbox...",
    "%s não respondeu. Confira se os Recursos remotos estão ativos e o modo de energia é Suspensão, ou ligue o console, e tente de novo.",
    "No seu Xbox, entre com esta mesma conta Microsoft.",
    "Configurações > Dispositivos e conexões > Recursos remotos: ative os recursos remotos.",
    "Configurações > Geral > Opções de energia: escolha Suspensão, para ele acordar sozinho.",
    "Escaneie para a ajuda da Microsoft",
    "Procurar de novo",
    "Botão Xbox",
    "Encerrar transmissão",
    "A transmissão foi encerrada no Xbox",
    "Outro aparelho assumiu a transmissão",
    "O Xbox foi desligado",
    "%s recebeu o pedido, mas o jogo remoto dele não iniciou. Reinicie o console: segure o botão de ligar do Xbox por 10 segundos, ligue de novo e tente outra vez.",
    "Touchpad: deslize ↓ para o menu, ↑ para o botão Xbox",
    "FSR + limpeza",
    "IA + limpeza",
    "Auto (%s)",
    "Atualizar agora? O app reinicia sozinho ao terminar.",
    "Atualizar agora",
    "Agora não",
    "Atualizações",
    "Atualizado (%s)",
    "%s disponível: atualizar",
    "Verificando...",
    "Atualizando para %s",
    "Baixando... %s",
    "Conferindo a assinatura...",
    "Instalando...",
    "Atualizado. Reiniciando...",
    "PSBox atualizado: abra o app de novo",
    "A atualização falhou: nada foi alterado",
    "Cor do jogo",
    "Cor personalizada",
    "Cor da barra de luz",
    "Analógico esquerdo ou direcional: cor    L2 / R2: brilho",
    "Leve",
    "Média",
    "Forte",
    "Máxima",
    "Intensidade",
    "Frequência",
    "Grave",
    "Aguda",
    "Aperte L2 / R2 para sentir: quanto mais fundo, mais forte o jogo pediria",
    "Nível %s",
    "Analógico esquerdo",
    "Analógico direito",
    "Posição real",
    "O que o jogo recebe",
    "L1 / R1: escolher o analógico    Direcional ← / →: ajustar    Mova os analógicos para ver",
    "Resistência",
    "Estilo",
    "Vibração",
    "Pulsos de força",
    "Modo",
    "Gênero",
    "Idioma",
    "Todos os modos",
    "Um jogador",
    "Multijogador online",
    "Cooperação online",
    "Local / tela dividida",
    "Todos os gêneros",
    "Qualquer idioma",
    "português",
    "Legendas em %s",
    "Dublado em %s",
    "Configurações do jogo",
    "Perfil",
    "Padrão",
    "Personalizado",
    "Restaurar padrão",
    "Editora",
    "Todas as editoras",
    "Indies & Outras",
    "Amigos jogando agora",
    "Microfone",
    "Mutado",
    "Ativo",
    "Saída de áudio",
    "TV",
    "Alto-falante do controle",
    "Relatar problema",
    "Escaneie com o celular: o formulário de bug do GitHub abre preenchido. Você revisa antes de enviar.",
    "Inclui a versão do app, o erro e linhas do log, sem dados pessoais.",
    "O PSBox fechou inesperadamente da última vez",
    "País do login",
    "Desativado (o do console)",
    "Entrar como outro país",
    "O xCloud será informado de que o login vem do país escolhido. A Microsoft não permite isso: use por sua conta e risco. Os jogos podem vir de servidores distantes, com mais atraso. Dá para mudar depois em Configurações.",
    "O xCloud recusou o login. O jogo na nuvem pode não ser oferecido no seu país, ou a conta pode não ter um plano do Game Pass com jogos na nuvem.",
    "O xCloud recusou o login, mesmo como %s. Provavelmente a conta não tem um plano do Game Pass com jogos na nuvem.",
    "Brasil",
    "Japão",
    "Coreia do Sul",
    "Polônia",
    "Estados Unidos",
    "As conexões seguras foram recusadas porque a data e a hora do PS5 parecem erradas (ele diz %s). Acerte em Configurações > Sistema > Data e hora e tente de novo.",
    "Uma conexão segura foi recusada: algo na rede (um servidor DNS, proxy ou filtro) está respondendo no lugar dos sites da Microsoft. Tente sem ele e tente de novo.",
    "A data e a hora do PS5 parecem erradas (%s): parte do conteúdo não carrega. Acerte em Configurações > Sistema > Data e hora.",
    "Não foi possível salvar o login no console, então ele será pedido de novo na próxima vez. Relate o problema (Configurações > △).",
    "Espera estimada: %s s", // QueueSeconds
    "Espera estimada: %s min", // QueueMinutes
    "Saída 120 Hz (VRR)", // HighRefresh
    "Ativada (nos jogos)", // HighRefreshOn
    "Conexão instável", // ConnectionUnstable
    "Sessão: %s", // SessionSummary
    "Bitrate máximo", // BitrateCap
    "Automático", // BitrateAuto
    "Volume do jogo", // GameVolume
    "Teclado: F12 este menu · Home botão Xbox", // MenuKeyHint
    "Teclado e mouse", // FilterMouseKeyboard
    "Teclado e mouse", // MouseKeyboardSupport
    "Teclado e mouse: jogador %s", // KeyboardPlayer
    "Você está na fila.", // InQueueNoEstimate
    "Teclas do teclado", // KeyboardKeys
    "Direcional", // DPad
    "Pressione uma tecla ou botão do mouse", // PressKey
    "%s é reservada pelo app", // KeyReserved
    "Alterar", // ChangeKey
    "Limpar", // ClearKey
    "O mouse move o analógico direito e cada clique da roda aperta Triângulo. F12 (menu), Home (botão Xbox) e 1-4 (jogador) são reservadas pelo app.", // KeysHelp
    "Mira por giroscópio", // Gyro
    "Sempre", // GyroAlways
    "Ao mirar (L2)", // GyroAiming
    "Intensidade do giroscópio", // GyroIntensity
    "As conexões seguras foram recusadas porque a data e a hora deste computador parecem erradas (ele diz %s). Acerte nas configurações do sistema e tente de novo.", // ClockWrongPc
    "A data e a hora deste computador parecem erradas (%s): parte do conteúdo não carrega. Acerte nas configurações do sistema.", // ClockWrongNoticePc
    "Desativado (o do computador)", // LoginCountryOffPc
    "Abrir no navegador", // OpenInBrowser
    "%s disponível: baixar", // UpdateReadyPc
    "Menu: START + BACK ou F12 · Botão Xbox: Guide ou Home", // GestureHintButtons
    "START + BACK ou F12: este menu · Guide ou Home: botão Xbox", // MenuButtonHint
    "Modo de exibição", // DisplayMode
    "Janela", // DisplayWindow
    "Tela cheia", // DisplayFullscreen
    "Sair do PSBox?", // QuitTitle
    "Sair", // QuitConfirm
    "Botões do controle", // ControllerButtons
    "Aperte o botão para %s", // PadPressButton
    "Falta botão para: %s. Dê um botão a cada um, ou restaure o padrão ou descarte as alterações.", // PadMissing
    "Descartar alterações", // DiscardChanges
    "Só o jogo vê isto: os menus do app usam os botões como são. Escolher um botão já usado troca os dois.", // PadHelp
    "Plano", // FilterPlan
    "Meu plano", // PlanMine
    "Todos os planos", // PlanAll
    "Incluído no Game Pass %s", // IncludedWithPlan
    "Touchpad como mouse", // TouchpadMouse
    "Favoritos", // Favorites
    "Adicionar aos favoritos", // AddFavorite
    "Remover dos favoritos", // RemoveFavorite
    "Adicionado aos favoritos", // AddedToFavorites
    "Removido dos favoritos", // RemovedFromFavorites
    "Idioma do jogo", // GameLanguage
    "Limite mensal de jogo em nuvem atingido", // MonthlyLimitExceeded
    "Tempo de jogo restante", // PlaytimeRemaining
    "%s h restantes", // HoursRemaining
    "Menos de 2 h restantes", // PlaytimeWarning
    "Perfil de gatilho", // TriggerProfile
    "Padrão", // TriggerProfileStd
    "Tiroteio (Hair Trigger)", // TriggerProfileShooter
    "Corrida (ABS)", // TriggerProfileRacing
    "Armazenamento %s não permite gravação: login e ajustes não serão salvos", // StorageNotWritable
    "FTP: use 'chmod 777 %s' ou exclua a pasta e reabra o app", // StorageFixHint
    "Personalizar indicadores",                       // StatsCustomize
    "FPS e resolução",                                // StatsIndicatorFps
    "Taxa de bits (Bitrate)",                         // StatsIndicatorBitrate
    "Latência (RTT)",                                 // StatsIndicatorLatency
    "Perda de pacotes",                               // StatsIndicatorLoss
    "Gráfico de frametime",                           // StatsIndicatorGraph
    "Atraso de decodificação e exibição",             // StatsIndicatorDelay
    "Qualidade do vídeo (QP)",                        // StatsIndicatorQp
    "Status do microfone",                            // StatsIndicatorMic
    "Use seu celular para ler o código QR ao lado e resolver na Microsoft, depois pressione Cruz para tentar novamente",  // ScanQrToResolve
    "Adquirir Grátis",                                // ClaimFree
    "Adquirindo licença gratuita...",                 // Claiming
    "Jogo adicionado à sua conta com sucesso!",       // ClaimSuccess
    "Falha ao adquirir o jogo gratuito: %s",          // ClaimFailed
    "Este jogo não é gratuito na loja",               // ProductNotFree
    "Jogo gratuito. Pressione Cruz para adicionar à sua conta e começar a jogar",  // FreeAcquireHint
    "Falha de conexão DNS com os servidores da Microsoft. No PS5, certifique-se de estar rodando o NanoDNS com o DNS Primário configurado para o IP local para manter acesso à internet enquanto bloqueia a Sony.", // DnsBlockedNanoDns
    "Esta conta Microsoft ainda não possui um perfil Xbox. Entre em xbox.com pelo celular ou computador para criar sua Gamertag e tente novamente.", // XboxProfileMissing
    "Fila liberada! Iniciando o jogo...",             // QueueReadyAlert
    "Aviso sonoro de fila pronta",                    // QueueReadyNotification
    "Jogado: %s",                                     // PlaytimePlayed
    "Atualizar",                                      // RefreshLibrary
    "Atualizando biblioteca...",                      // RefreshingLibrary
    "Esta conta Xbox está suspensa ou banida. Acesse enforcement.xbox.com para mais detalhes.", // XboxAccountBanned
    "O Xbox Live não é oferecido no país ou região desta conta.", // XboxRegionNotSupported
    "Verificação de idade ou aceite dos termos necessário. Entre em xbox.com pelo navegador e tente novamente.", // XboxAdultVerificationRequired
    "Conta infantil: um adulto precisa adicionar esta conta a uma família Microsoft em account.microsoft.com/family.", // XboxChildAccountFamily
    "Verificar meu login",                            // CheckSignIn
    "Verificar",                                      // RunCheck
    "Teste de rede",                                  // NetworkTest
    "Testar",                                         // RunTest
    "Conquistas",                                     // Achievements
    "Nenhuma conquista",                              // NoAchievements
    "Bloqueada",                                      // AchievementLocked
    "Conquista secreta",                              // SecretAchievement
    "Lista de desejos",                               // Wishlist
    "Adicionar à lista de desejos",                   // AddWishlist
    "Remover da lista de desejos",                    // RemoveWishlist
    "Adicionado à lista de desejos",                  // AddedToWishlist
    "Removido da lista de desejos",                   // RemovedFromWishlist
    "%s está em promoção!",                           // WishlistOnSale
    "%s entrou no Game Pass!",                        // WishlistInGamePass
    "Apoie o projeto",                                // SupportProject
    "Apoie o Desenvolvimento do PSBox",               // SupportTitle
    "O PSBox é um projeto de código aberto mantido com dedicação para trazer a melhor experiência do Xbox Cloud Gaming ao PlayStation 5. Se você curte o app e quer apoiar o desenvolvimento contínuo, atualizações e novos recursos, considere me pagar um café no Ko-Fi!", // SupportMessage
    "Ko-fi",                                          // SupportKoFi
    "Aponte a câmera do seu celular",                 // ScanToDonate
    "Testar som",                                     // TestSound
    "A lista de desejos está vazia",                  // WishlistEmpty
    // Loading tips
    "Touchpad: deslize ↓ (ou Touchpad + Options) para o menu, ↑ para o botão Xbox", // TipTouchpadMenu
    "Teclado e mouse USB conectados ao console são suportados em jogos compatíveis da nuvem", // TipKeyboardMouse
    "Adquira jogos gratuitos, demonstrações e confira promoções da loja direto pelo aplicativo", // TipFreeStore
    "Encontrou um problema? Abra Relatar Problema para escanear um QR code com logs do console", // TipBugReportQr
    "Gostando do app? Apoie o desenvolvimento contínuo via Ko-fi na aba de Configurações!", // TipSupportKofi
    "Ative a saída de vídeo em 120 Hz nas Configurações para menor latência e resposta mais rápida", // Tip120HzVideo
    "Ajuste a zona morta dos analógicos do DualSense nas Configurações para eliminar drift", // TipAnalogDeadzone
    "Use o microfone integrado do DualSense para bate-papo de voz em grupos e jogos multijogador", // TipDualSenseMic
    // Settings descriptions
    "Altera o idioma da interface do aplicativo e a preferência de áudio e legendas nos jogos da nuvem", // SettingDescLanguage
    "Define a resolução máxima do fluxo de vídeo transmitido (1080p, 720p ou Melhor). Vale a partir do próximo jogo", // SettingDescResolution
    "Seleciona a região de servidores Microsoft para streaming (Automático ou regiões específicas). Vale a partir do próximo jogo", // SettingDescRegion
    "Ajusta a sensibilidade inicial dos analógicos do DualSense para evitar drift ou comandos involuntários", // SettingDescDeadzone
    "Ativa ou desativa a simulação dos motores de vibração nos gatilhos adaptáveis L2 e R2 em jogos compatíveis", // SettingDescTriggerRumble
    "Alterna a função de confirmar e voltar entre Cruz (padrão ocidental) e Círculo (padrão japonês)", // SettingDescConfirmButton
    "Configura a cor e o comportamento da barra de luz do DualSense durante a navegação e a jogatina", // SettingDescLightBar
    "Verifica e instala automaticamente novas versões lançadas do aplicativo diretamente pelo console sem PC", // SettingDescUpdates
    "Permite autenticar sua conta Xbox através de outra região geográfica para acessar catálogos alternativos", // SettingDescLoginCountry
    "Ativa saída de vídeo em 120 Hz no console para menor atraso de exibição e redução da latência de comando", // SettingDescHighRefresh
    "Define o limite máximo de bitrate para o stream de vídeo, garantindo fidelidade visual ou economizando rede", // SettingDescBitrateCap
    "Ajusta o nível geral do volume de áudio reproduzido durante as sessões de streaming dos jogos", // SettingDescGameVolume
    "Mapeia as teclas do teclado USB ou Bluetooth para os botões correspondentes do controle Xbox no jogo", // SettingDescKeyboardKeys
    "Alterna entre modo de janela ou tela cheia em plataformas desktop suportadas", // SettingDescDisplayMode
    "Abre o painel de teste de resposta e calibração de todos os botões e eixos do controle conectado", // SettingDescControllerButtons
    "Permite usar os sensores de movimento do DualSense para auxiliar na mira em jogos de tiro e ação", // SettingDescGyro
    "Toca um aviso sonoro característico quando o tempo de espera na fila do xCloud terminar e o jogo iniciar", // SettingDescQueueReady
    "Realiza um diagnóstico completo da autenticação Xbox Live, validando tokens de acesso, perfil e assinaturas", // SettingDescCheckSignIn
    "Mede o ping (RTT) e a variação (jitter) para servidores de jogos em nuvem de várias regiões globais", // SettingDescNetworkTest
    "Exibe um QR Code para você apoiar o desenvolvedor via Ko-fi e ajudar a manter o projeto ativo e atualizado", // SettingDescSupportProject
    "Novidades",                                      // WhatsNew
    "Novidades",                                      // WhatsNewTitle
    "Alto-falante do controle",                       // PadSpeakerVolume
    "Mudo",                                           // Mute
    "Configuração Web",                               // WebControlTitle
    "Acesse pelo celular",                            // WebControlSubtitle
    "Conecte no mesmo Wi-Fi para pesquisar, remapear botões, usar o celular como controle virtual, relatar problemas ou visualizar o log.", // WebControlDesc
    "Decodificação de Vídeo",                         // VideoDecoder
    "Hardware (Recomendado)",                         // VideoDecoderHw
    "Software (CPU)",                                 // VideoDecoderSw
    "Seleciona decodificação por hardware (libSceVideodec2 no PS5, HWAccel no Desktop) ou por software (CPU)", // SettingDescVideoDecoder
    "Áudio para Vibração Háptica",                     // AudioHaptics
    "Converte graves do áudio em vibração háptica no DualSense", // SettingDescAudioHaptics
    "Atalhos do Touchpad",                            // TouchpadShortcuts
    "Configura gestos de deslize e cliques esquerdo e direito no Touchpad do DualSense", // SettingDescTouchpadShortcuts
    "Deslizar para Cima (↑)",                         // TouchpadSwipeUp
    "Deslizar para Baixo (↓)",                        // TouchpadSwipeDown
    "Deslizar para Esquerda (←)",                     // TouchpadSwipeLeft
    "Deslizar para Direita (→)",                      // TouchpadSwipeRight
    "Clique Esquerdo",                                // TouchpadClickLeft
    "Clique Direito",                                 // TouchpadClickRight
    "Desativado",                                     // TouchpadActNone
    "Menu do jogo",                                   // TouchpadActGameMenu
    "Botão Xbox",                                     // TouchpadActXboxButton
    "Alternar estatísticas",                          // TouchpadActToggleStats
    "Mutar/desmutar microfone",                       // TouchpadActToggleMic
    "Alternar giroscópio",                            // TouchpadActCycleGyro
    "Alternar perfil de gatilho",                     // TouchpadActCycleTrigger
    "Touchpad + Options sempre abre o menu do jogo",  // TouchpadHelp
    "Restaurar configurações padrões",                // ResetSettings
    "Restaura todas as configurações para os padrões de fábrica. Mantenha ✕ por 5 segundos para confirmar.", // SettingDescResetSettings
    "Configurações restauradas para os padrões.",     // SettingsResetNotice
    "Segure para restaurar",                          // HoldToReset
    "Biblioteca já está atualizada",                  // LibraryUpToDate
    "Biblioteca atualizada (%s novos jogos)",         // LibraryUpdatedCount
    "Modo offline",                                   // OfflineMode
};

constexpr Column kSpanish = {
    "PSBox Cloud Gaming",
    "PSBOX CLOUD GAMING",
    "Iniciando sesión...",
    "Obteniendo un código de inicio de sesión...",
    "Cargando tus juegos...",
    "Inicia sesión para jugar",
    "En tu teléfono u ordenador, ve a",
    "e introduce este código",
    "Escanea para abrir la página e introduce el código",
    "Esperando a que inicies sesión...",
    "Seguir jugando",
    "Jugar",
    "Atrás",
    "Seleccionar",
    "Cerrar sesión",
    "Cancelar",
    "Reintentar",
    "Preparando tu juego",
    "Estás en la cola. Espera estimada: %s",
    "Conectando...",
    "Iniciando el streaming...",
    "Panel táctil: ↓ este menú · ↑ botón Xbox",
    "Streaming finalizado",
    "Algo salió mal",
    "Sesión iniciada como %s",
    "Todavía no hay juegos para mostrar",
    "De %s",
    "Ajustes",
    "Idioma",
    "Resolución del streaming",
    "Región del servidor",
    "Automática (%s)",
    "1080p (Full HD)",
    "720p (usa menos datos)",
    "Mejor (Auto)",
    "La resolución y la región se aplican al próximo juego que inicies.",
    "Cambiar",
    "Ajustar",
    "Mantén para cerrar sesión",
    "Sigue manteniendo para cerrar sesión (%s)",
    "Error al iniciar sesión: %s",
    "El inicio de sesión no se completó en el navegador. Usa una cuenta personal de Microsoft, completa la verificación/2FA y autoriza la app.",
    "No se pudo cargar la lista de juegos: %s",
    "No se pudo iniciar el streaming: %s",
    "Sin actividad: se te desconectará en %s segundos",
    "Mis juegos",
    "No disponible en tu cuenta",
    "Game Pass",
    "Buscar",
    "Escribe el nombre de un juego",
    "No se encontraron juegos",
    "%s resultados",
    "Espacio",
    "Borrar",
    "Limpiar",
    "%s juegos",
    "Escribir",
    "Probando otra región: %s",
    "Disponibles para comprar",
    "COMPRAR",
    "Compra este juego para jugarlo en la nube",
    "Cómpralo en xbox.com o en la app de Xbox; luego aparecerá en Mis juegos.",
    "Escanea para abrir la página de la tienda",
    "Buscar en %s",
    "GRATIS",
    "Ocultar",
    "Mostrar",
    "Ocultos",
    "\xE2\x96\xA1 vuelve a mostrar un juego",
    "Oculto: está en Ocultos, al final de esta pestaña",
    "Visible de nuevo",
    "Ordenar: %s",
    "Recientes primero",
    "A-Z",
    "Por consola",
    "Ordenar",
    "Gratis",
    "Menor precio",
    "Todas las consolas",
    "1 juego",
    "Secciones",
    "Actualizando la lista de juegos...",
    "Menú del juego",
    "Estadísticas",
    "Activado",
    "Desactivado",
    "Resolución del stream",
    "Salir del juego",
    "Latencia",
    "Tasa de bits",
    "Fotogramas por segundo",
    "Pérdida de paquetes",
    "Región",
    "Decodificación",
    "El servidor puede mantener la calidad actual",
    "Nitidez",
    "Desactivada",
    "Baja",
    "Media",
    "Alta",
    "Zona muerta del stick",
    "Vibración en los gatillos",
    "Botón de confirmar",
    "Activada",
    "Desactivada",
    "Versión %s disponible",
    "Mandos",
    "Mando %s conectado",
    "Mando %s desconectado",
    "Suavizado de bloques",
    "En pantalla tras",
    "Conexión perdida: reconectando...",
    "Reconectado",
    "Barra de luz",
    "Escalado",
    "IA (Anime4K)",
    "Gratis para jugar",
    "Consíguelo una vez, gratis, en xbox.com o en la app de Xbox (escanea el código con el móvil) y luego pulsa Jugar.",
    "Escanea para conseguirlo gratis",
    "%s aún no está en tu cuenta. Es gratis: escanea el código en su página, elige Obtener y vuelve a intentarlo (puede tardar un minuto).",
    "Tu cuenta no puede jugar a %s: no está en tu suscripción o no lo has comprado.",
    "Mis consolas",
    "Encendida",
    "En reposo",
    "Apagada",
    "Buscando tus consolas...",
    "Aún no se encontró ninguna Xbox",
    "Juega en tu propia Xbox desde aquí: su pantalla, juegos y aplicaciones.",
    "Encendiendo tu Xbox...",
    "%s no respondió. Comprueba que las Funciones remotas estén activadas y el modo de energía sea Suspensión, o enciéndela, y vuelve a intentarlo.",
    "En tu Xbox, inicia sesión con esta misma cuenta Microsoft.",
    "Configuración > Dispositivos y conexiones > Funciones remotas: activa las funciones remotas.",
    "Configuración > General > Opciones de energía: elige Suspensión, para que se encienda sola.",
    "Escanea para ver la ayuda de Microsoft",
    "Buscar de nuevo",
    "Botón Xbox",
    "Terminar la transmisión",
    "La transmisión se terminó en la Xbox",
    "Otro dispositivo tomó la transmisión",
    "La Xbox se apagó",
    "%s recibió la solicitud, pero su juego remoto no se inició. Reiníciala: mantén pulsado el botón de encendido de la consola 10 segundos, enciéndela de nuevo y vuelve a intentarlo.",
    "Panel táctil: desliza ↓ para el menú, ↑ para el botón Xbox",
    "FSR + limpieza",
    "IA + limpieza",
    "Auto (%s)",
    "¿Actualizar ahora? La app se reinicia sola al terminar.",
    "Actualizar ahora",
    "Ahora no",
    "Actualizaciones",
    "Al día (%s)",
    "%s disponible: actualizar",
    "Comprobando...",
    "Actualizando a %s",
    "Descargando... %s",
    "Comprobando la firma...",
    "Instalando...",
    "Actualizado. Reiniciando...",
    "PSBox actualizado: abre la app de nuevo",
    "La actualización falló: no se cambió nada",
    "Color del juego",
    "Color personalizado",
    "Color de la barra de luz",
    "Stick izquierdo o cruceta: color    L2 / R2: brillo",
    "Suave",
    "Media",
    "Fuerte",
    "Máxima",
    "Intensidad",
    "Frecuencia",
    "Grave",
    "Aguda",
    "Pulsa L2 / R2 para sentirlo: cuanto más a fondo, más fuerte lo pediría el juego",
    "Nivel %s",
    "Stick izquierdo",
    "Stick derecho",
    "Posición real",
    "Lo que recibe el juego",
    "L1 / R1: elegir el stick    Cruceta ← / →: ajustar    Mueve los sticks para verlo",
    "Resistencia",
    "Estilo",
    "Vibración",
    "Pulsos de fuerza",
    "Modo",
    "Género",
    "Idioma",
    "Todos los modos",
    "Un jugador",
    "Multijugador en línea",
    "Cooperativo en línea",
    "Local / pantalla dividida",
    "Todos los géneros",
    "Cualquier idioma",
    "español",
    "Subtítulos en %s",
    "Doblado al %s",
    "Ajustes del juego",
    "Perfil",
    "Predeterminado",
    "Personalizado",
    "Restaurar predeterminado",
    "Distribuidora",
    "Todas las distribuidoras",
    "Indies y otras",
    "Amigos jugando ahora",
    "Micrófono",
    "Silenciado",
    "Activo",
    "Salida de audio",
    "TV",
    "Altavoz del mando",
    "Informar de un problema",
    "Escanéalo con el móvil: se abre el formulario de errores de GitHub, ya rellenado. Lo revisas antes de enviarlo.",
    "Incluye la versión de la app, el error y líneas del registro, sin datos personales.",
    "PSBox se cerró inesperadamente la última vez",
    "País de inicio de sesión",
    "Desactivado (el de la consola)",
    "Entrar como otro país",
    "Se le dirá a xCloud que el inicio de sesión viene del país elegido. Microsoft no lo permite: úsalo bajo tu propio riesgo. Los juegos pueden venir de servidores lejanos, con más retraso. Puedes cambiarlo luego en Ajustes.",
    "xCloud rechazó el inicio de sesión. Puede que el juego en la nube no se ofrezca en tu país o que la cuenta no tenga un plan de Game Pass con juego en la nube.",
    "xCloud rechazó el inicio de sesión, incluso como %s. Probablemente la cuenta no tenga un plan de Game Pass con juego en la nube.",
    "Brasil",
    "Japón",
    "Corea del Sur",
    "Polonia",
    "Estados Unidos",
    "Se rechazaron las conexiones seguras porque la fecha y la hora de la PS5 parecen incorrectas (indica %s). Ajústalas en Ajustes > Sistema > Fecha y hora y vuelve a intentarlo.",
    "Se rechazó una conexión segura: algo en la red (un servidor DNS, proxy o filtro) responde en lugar de los sitios de Microsoft. Prueba sin él y vuelve a intentarlo.",
    "La fecha y la hora de la PS5 parecen incorrectas (%s): parte del contenido no carga. Ajústalas en Ajustes > Sistema > Fecha y hora.",
    "No se pudo guardar el inicio de sesión en la consola, así que se pedirá de nuevo la próxima vez. Infórmalo (Ajustes > △).",
    "Espera estimada: %s s", // QueueSeconds
    "Espera estimada: %s min", // QueueMinutes
    "Salida 120 Hz (VRR)", // HighRefresh
    "Activada (en los juegos)", // HighRefreshOn
    "Conexión inestable", // ConnectionUnstable
    "Sesión: %s", // SessionSummary
    "Bitrate máximo", // BitrateCap
    "Automático", // BitrateAuto
    "Volumen del juego", // GameVolume
    "Teclado: F12 este menú · Inicio botón Xbox", // MenuKeyHint
    "Teclado y ratón", // FilterMouseKeyboard
    "Teclado y ratón", // MouseKeyboardSupport
    "Teclado y ratón: jugador %s", // KeyboardPlayer
    "Estás en la cola.", // InQueueNoEstimate
    "Teclas del teclado", // KeyboardKeys
    "Cruceta", // DPad
    "Pulsa una tecla o un botón del ratón", // PressKey
    "La app reserva %s", // KeyReserved
    "Cambiar", // ChangeKey
    "Borrar", // ClearKey
    "El ratón mueve el stick derecho y cada paso de la rueda pulsa Triángulo. F12 (menú), Inicio (botón Xbox) y 1-4 (jugador) las reserva la app.", // KeysHelp
    "Apuntado con giroscopio", // Gyro
    "Siempre", // GyroAlways
    "Al apuntar (L2)", // GyroAiming
    "Intensidad del giroscopio", // GyroIntensity
    "Se rechazaron las conexiones seguras porque la fecha y la hora de este ordenador parecen incorrectas (indica %s). Corrígelas en los ajustes del sistema y vuelve a intentarlo.", // ClockWrongPc
    "La fecha y la hora de este ordenador parecen incorrectas (%s): parte del contenido no carga. Corrígelas en los ajustes del sistema.", // ClockWrongNoticePc
    "Desactivado (el del ordenador)", // LoginCountryOffPc
    "Abrir en el navegador", // OpenInBrowser
    "%s disponible: descargar", // UpdateReadyPc
    "Menú: START + BACK o F12 · Botón Xbox: Guide o Inicio", // GestureHintButtons
    "START + BACK o F12: este menú · Guide o Inicio: botón Xbox", // MenuButtonHint
    "Modo de pantalla", // DisplayMode
    "Ventana", // DisplayWindow
    "Pantalla completa", // DisplayFullscreen
    "¿Salir de PSBox?", // QuitTitle
    "Salir", // QuitConfirm
    "Botones del mando", // ControllerButtons
    "Pulsa el botón para %s", // PadPressButton
    "Falta botón para: %s. Asigna uno a cada función, o restablece o descarta los cambios.", // PadMissing
    "Descartar cambios", // DiscardChanges
    "Solo el juego lo ve: los menús de la app usan los botones tal cual. Elegir un botón ya usado intercambia los dos.", // PadHelp
    "Plan", // FilterPlan
    "Mi plan", // PlanMine
    "Todos los planes", // PlanAll
    "Incluido con Game Pass %s", // IncludedWithPlan
    "Panel táctil como ratón", // TouchpadMouse
    "Favoritos", // Favorites
    "Añadir a favoritos", // AddFavorite
    "Quitar de favoritos", // RemoveFavorite
    "Añadido a favoritos", // AddedToFavorites
    "Eliminado de favoritos", // RemovedFromFavorites
    "Idioma del juego", // GameLanguage
    "Límite mensual de juego en la nube alcanzado", // MonthlyLimitExceeded
    "Tiempo de juego restante", // PlaytimeRemaining
    "%s h restantes", // HoursRemaining
    "Menos de 2 h restantes", // PlaytimeWarning
    "Perfil de gatillos", // TriggerProfile
    "Estándar", // TriggerProfileStd
    "Disparos (Hair Trigger)", // TriggerProfileShooter
    "Carreras (ABS)", // TriggerProfileRacing
    "El almacenamiento %s no permite escritura: la sesión y ajustes no se guardarán", // StorageNotWritable
    "FTP: usa 'chmod 777 %s' o borra la carpeta y reinicia la app", // StorageFixHint
    "Personalizar indicadores",                       // StatsCustomize
    "FPS y resolución",                               // StatsIndicatorFps
    "Tasa de bits (Bitrate)",                         // StatsIndicatorBitrate
    "Latencia (RTT)",                                 // StatsIndicatorLatency
    "Pérdida de paquetes",                            // StatsIndicatorLoss
    "Gráfico de frametime",                           // StatsIndicatorGraph
    "Retardo de decodificación y pantalla",           // StatsIndicatorDelay
    "Calidad de vídeo (QP)",                          // StatsIndicatorQp
    "Estado del micrófono",                           // StatsIndicatorMic
    "Usa tu teléfono para escanear el código QR y resolver con Microsoft, luego presiona Cruz para reintentar",  // ScanQrToResolve
    "Obtener Gratis",                                 // ClaimFree
    "Obteniendo licencia gratuita...",                // Claiming
    "¡Juego añadido a tu cuenta con éxito!",          // ClaimSuccess
    "Error al adquirir el juego gratuito: %s",        // ClaimFailed
    "Este juego no es gratuito en la tienda",         // ProductNotFree
    "Juego gratuito. Presiona Cruz para añadirlo a tu cuenta y empezar a jugar",  // FreeAcquireHint
    "Fallo de conexión DNS con los servidores de Microsoft. En PS5, asegúrate de ejecutar NanoDNS con el DNS primario configurado en tu IP local para mantener acceso a internet bloqueando a Sony.", // DnsBlockedNanoDns
    "Esta cuenta Microsoft aún no tiene un perfil de Xbox. Inicia sesión en xbox.com en tu móvil o PC para crear tu Gamertag y vuelve a intentarlo.", // XboxProfileMissing
    "¡Cola lista! Iniciando el juego...",             // QueueReadyAlert
    "Aviso sonoro de cola lista",                     // QueueReadyNotification
    "Jugado: %s",                                     // PlaytimePlayed
    "Actualizar",                                     // RefreshLibrary
    "Actualizando biblioteca...",                     // RefreshingLibrary
    "Esta cuenta de Xbox está suspendida o bloqueada. Visita enforcement.xbox.com para más detalles.", // XboxAccountBanned
    "Xbox Live no se ofrece en el país o región de esta cuenta.", // XboxRegionNotSupported
    "Se requiere verificación de edad o aceptar los términos. Inicia sesión en xbox.com desde un navegador y vuelve a intentarlo.", // XboxAdultVerificationRequired
    "Cuenta infantil: un adulto debe añadir esta cuenta a una familia de Microsoft en account.microsoft.com/family.", // XboxChildAccountFamily
    "Comprobar mi inicio de sesión",                  // CheckSignIn
    "Comprobar",                                      // RunCheck
    "Prueba de red",                                  // NetworkTest
    "Probar",                                         // RunTest
    "Logros",                                         // Achievements
    "Sin logros",                                     // NoAchievements
    "Bloqueado",                                      // AchievementLocked
    "Logro secreto",                                  // SecretAchievement
    "Lista de deseos",                                // Wishlist
    "Añadir a lista de deseos",                       // AddWishlist
    "Quitar de lista de deseos",                      // RemoveWishlist
    "Añadido a lista de deseos",                      // AddedToWishlist
    "Quitado de lista de deseos",                     // RemovedFromWishlist
    "¡%s está en oferta!",                            // WishlistOnSale
    "¡%s llegó a Game Pass!",                         // WishlistInGamePass
    "Apoya el proyecto",                              // SupportProject
    "Apoya el Desarrollo de PSBox",                   // SupportTitle
    "PSBox es un proyecto de código abierto desarrollado con dedicación para brindar la mejor experiencia de Xbox Cloud Gaming en PlayStation 5. Si disfrutas de la app y deseas apoyar el desarrollo continuo y nuevas funciones, ¡considera invitarme un café en Ko-Fi!", // SupportMessage
    "Ko-fi",                                          // SupportKoFi
    "Escanea con tu teléfono para donar",             // ScanToDonate
    "Probar sonido",                                  // TestSound
    "La lista de deseos está vacía",                  // WishlistEmpty
    // Loading tips
    "Panel táctil: desliza ↓ (o Touchpad + Options) para el menú, ↑ para el botón Xbox", // TipTouchpadMenu
    "Teclado y ratón USB conectados a la consola son compatibles con juegos de la nube habilitados", // TipKeyboardMouse
    "Reclama juegos gratis, demos y descubre promociones de la tienda directamente desde la aplicación", // TipFreeStore
    "¿Encontraste un problema? Abre Notificar problema para escanear un QR con registros de la consola", // TipBugReportQr
    "¿Disfrutas del proyecto? ¡Apoya su desarrollo futuro a través de Ko-fi en Configuración!", // TipSupportKofi
    "Activa salida de vídeo a 120 Hz en Configuración para menor latencia y respuesta más fluida", // Tip120HzVideo
    "Calibra la zona muerta de los sticks en Configuración para eliminar deriva y mejorar la precisión", // TipAnalogDeadzone
    "Usa el micrófono integrado del DualSense para chat de voz en grupos y partidas multijugador", // TipDualSenseMic
    // Settings descriptions
    "Cambia el idioma de la interfaz de la aplicación y la preferencia de audio y subtítulos en juegos en la nube", // SettingDescLanguage
    "Establece la resolución máxima del stream de vídeo (1080p, 720p o Mejor disponible). Se aplica en el próximo juego", // SettingDescResolution
    "Selecciona la región de servidores de Microsoft para streaming (Automático o regiones específicas). Se aplica en el próximo juego", // SettingDescRegion
    "Ajusta la zona muerta interna de los sticks analógicos para evitar deriva y movimientos no deseados", // SettingDescDeadzone
    "Activa o desactiva la vibración de gatillos adaptativos L2 y R2 en juegos compatibles", // SettingDescTriggerRumble
    "Alterna la función de confirmar y volver entre Cruz (estilo occidental) y Círculo (estilo japonés)", // SettingDescConfirmButton
    "Configura el color y comportamiento de la barra luminosa del DualSense en menús y durante el juego", // SettingDescLightBar
    "Comprueba e instala automáticamente actualizaciones oficiales de la aplicación directamente en la consola", // SettingDescUpdates
    "Permite iniciar sesión en tu cuenta de Xbox mediante otra región geográfica para acceder a catálogos alternativos", // SettingDescLoginCountry
    "Activa salida de vídeo a 120 Hz en tu consola para reducir el retardo de pantalla y la latencia de entrada", // SettingDescHighRefresh
    "Define el límite máximo de tasa de bits para streaming de vídeo, asegurando calidad visual o ahorrando red", // SettingDescBitrateCap
    "Ajusta el volumen general del audio reproducido durante las sesiones de juego en la nube", // SettingDescGameVolume
    "Asigna teclas del teclado USB o Bluetooth a los botones correspondientes del mando de Xbox durante el juego", // SettingDescKeyboardKeys
    "Alterna entre modo ventana y pantalla completa en plataformas de escritorio compatibles", // SettingDescDisplayMode
    "Abre la pantalla de prueba de respuesta y calibración de todos los botones y ejes del mando conectado", // SettingDescControllerButtons
    "Permite que los sensores de movimiento del DualSense asistan al apuntado analógico en juegos de disparos", // SettingDescGyro
    "Reproduce un aviso sonoro cuando termina la espera en cola de xCloud y el juego está listo", // SettingDescQueueReady
    "Ejecuta un diagnóstico completo del inicio de sesión de Xbox Live, validando tokens, perfil y suscripciones", // SettingDescCheckSignIn
    "Mide el ping (RTT) y la fluctuación (jitter) a centros de datos de juegos en la nube en múltiples regiones", // SettingDescNetworkTest
    "Muestra un código QR para apoyar al desarrollador mediante Ko-fi y mantener el proyecto activo", // SettingDescSupportProject
    "Novedades",                                      // WhatsNew
    "Novedades",                                      // WhatsNewTitle
    "Altavoz del mando",                              // PadSpeakerVolume
    "Silenciado",                                     // Mute
    "Control Web",                                    // WebControlTitle
    "Conectar desde el móvil",                        // WebControlSubtitle
    "Conéctate al mismo Wi-Fi para buscar, reasignar botones, usar el celular como control virtual, reportar problemas o ver registros.", // WebControlDesc
    "Decodificación de vídeo",                        // VideoDecoder
    "Hardware (Recomendado)",                         // VideoDecoderHw
    "Software (CPU)",                                 // VideoDecoderSw
    "Selecciona decodificación por hardware (libSceVideodec2 en PS5, HWAccel en Desktop) o software (CPU)", // SettingDescVideoDecoder
    "Audio a Vibración Háptica",                       // AudioHaptics
    "Convierte frecuencias graves del audio en vibración háptica en DualSense", // SettingDescAudioHaptics
    "Atajos del panel táctil",                        // TouchpadShortcuts
    "Configura gestos de deslizamiento y clics izquierdo y derecho en el panel táctil DualSense", // SettingDescTouchpadShortcuts
    "Deslizar arriba (↑)",                            // TouchpadSwipeUp
    "Deslizar abajo (↓)",                             // TouchpadSwipeDown
    "Deslizar izquierda (←)",                         // TouchpadSwipeLeft
    "Deslizar derecha (→)",                           // TouchpadSwipeRight
    "Clic izquierdo",                                 // TouchpadClickLeft
    "Clic derecho",                                   // TouchpadClickRight
    "Desactivado",                                    // TouchpadActNone
    "Menú del juego",                                 // TouchpadActGameMenu
    "Botón Xbox",                                     // TouchpadActXboxButton
    "Alternar estadísticas",                          // TouchpadActToggleStats
    "Silenciar/activar micrófono",                    // TouchpadActToggleMic
    "Alternar giroscopio",                            // TouchpadActCycleGyro
    "Alternar perfil de gatillo",                     // TouchpadActCycleTrigger
    "Touchpad + Options siempre abre el menú del juego", // TouchpadHelp
    "Restablecer ajustes predeterminados",            // ResetSettings
    "Restablece todos los ajustes a los valores de fábrica. Mantén ✕ durante 5 segundos para confirmar.", // SettingDescResetSettings
    "Ajustes restablecidos a los valores predeterminados.", // SettingsResetNotice
    "Mantén para restablecer",                        // HoldToReset
    "La biblioteca ya está actualizada",              // LibraryUpToDate
    "Biblioteca actualizada (%s nuevos juegos)",      // LibraryUpdatedCount
    "Modo sin conexión",                              // OfflineMode
};

constexpr Column kFrench = {
    "PSBox Cloud Gaming",
    "PSBOX CLOUD GAMING",
    "Connexion...",
    "Obtention d'un code de connexion...",
    "Chargement de vos jeux...",
    "Connectez-vous pour jouer",
    "Sur votre téléphone ou ordinateur, allez sur",
    "et saisissez ce code",
    "Scannez pour ouvrir la page, puis saisissez le code",
    "En attente de votre connexion...",
    "Reprendre",
    "Jouer",
    "Retour",
    "Sélectionner",
    "Se déconnecter",
    "Annuler",
    "Réessayer",
    "Préparation de votre jeu",
    "Vous êtes dans la file d'attente. Attente estimée : %s",
    "Connexion...",
    "Démarrage du streaming...",
    "Pavé tactile : ↓ ce menu · ↑ bouton Xbox",
    "Streaming terminé",
    "Un problème est survenu",
    "Connecté en tant que %s",
    "Aucun jeu à afficher pour le moment",
    "Par %s",
    "Paramètres",
    "Langue",
    "Résolution du streaming",
    "Région du serveur",
    "Automatique (%s)",
    "1080p (Full HD)",
    "720p (consomme moins de données)",
    "Meilleure (Auto)",
    "La résolution et la région s'appliquent au prochain jeu lancé.",
    "Modifier",
    "Ajuster",
    "Maintenez pour vous déconnecter",
    "Continuez à maintenir pour vous déconnecter (%s)",
    "Échec de la connexion : %s",
    "La connexion dans le navigateur n'a pas été finalisée. Utilisez un compte Microsoft personnel, effectuez les vérifications/2FA et autorisez l'app.",
    "Impossible de charger la liste des jeux : %s",
    "Impossible de démarrer le streaming : %s",
    "Aucune activité : vous serez déconnecté dans %s secondes",
    "Mes jeux",
    "Indisponible sur votre compte",
    "Game Pass",
    "Rechercher",
    "Tapez le nom d'un jeu",
    "Aucun jeu trouvé",
    "%s résultats",
    "Espace",
    "Effacer",
    "Tout effacer",
    "%s jeux",
    "Taper",
    "Essai d'une autre région : %s",
    "Disponibles à l'achat",
    "ACHETER",
    "Achetez ce jeu pour y jouer dans le cloud",
    "Achetez-le sur xbox.com ou dans l'app Xbox ; il apparaîtra ensuite dans Mes jeux.",
    "Scannez pour ouvrir la page du magasin",
    "Rechercher dans %s",
    "GRATUIT",
    "Masquer",
    "Afficher",
    "Masqués",
    "\xE2\x96\xA1 réaffiche un jeu",
    "Masqué : voir Masqués, à la fin de cet onglet",
    "De nouveau visible",
    "Trier : %s",
    "Récents d'abord",
    "A-Z",
    "Par console",
    "Trier",
    "Gratuit",
    "Prix croissant",
    "Toutes les consoles",
    "1 jeu",
    "Sections",
    "Mise \xC3\xA0 jour de la liste des jeux...",
    "Menu du jeu",
    "Statistiques",
    "Activé",
    "Désactivé",
    "Résolution du stream",
    "Quitter le jeu",
    "Latence",
    "Débit",
    "Images par seconde",
    "Perte de paquets",
    "Région",
    "Décodage",
    "Le serveur peut conserver la qualité actuelle",
    "Netteté",
    "Désactivée",
    "Faible",
    "Moyenne",
    "Élevée",
    "Zone morte du stick",
    "Vibration des gâchettes",
    "Bouton de validation",
    "Activée",
    "Désactivée",
    "Version %s disponible",
    "Manettes",
    "Manette %s connectée",
    "Manette %s déconnectée",
    "Lissage des blocs",
    "À l'écran après",
    "Connexion perdue : reconnexion...",
    "Reconnecté",
    "Barre lumineuse",
    "Mise à l'échelle",
    "IA (Anime4K)",
    "Gratuit",
    "Obtenez-le une fois, gratuitement, sur xbox.com ou dans l'app Xbox (scannez le code avec votre téléphone), puis appuyez sur Jouer.",
    "Scannez pour l'obtenir gratuitement",
    "%s n'est pas encore sur votre compte. Il est gratuit : scannez le code sur sa page, choisissez Obtenir, puis réessayez (cela peut prendre une minute).",
    "Votre compte ne peut pas jouer à %s : il n'est pas dans votre abonnement ou n'a pas été acheté.",
    "Mes consoles",
    "Allumée",
    "En veille",
    "Éteinte",
    "Recherche de vos consoles...",
    "Aucune Xbox trouvée pour l'instant",
    "Jouez sur votre propre Xbox d'ici : son écran, ses jeux et ses applications.",
    "Réveil de votre Xbox...",
    "%s n'a pas répondu. Vérifiez que les fonctionnalités à distance sont activées et le mode Veille choisi, ou allumez-la, puis réessayez.",
    "Sur votre Xbox, connectez-vous avec ce même compte Microsoft.",
    "Paramètres > Appareils et connexions > Fonctionnalités à distance : activez-les.",
    "Paramètres > Général > Options d'alimentation : choisissez Veille, pour qu'elle se réveille seule.",
    "Scannez pour l'aide de Microsoft",
    "Rechercher à nouveau",
    "Bouton Xbox",
    "Arrêter la diffusion",
    "La diffusion a été arrêtée sur la Xbox",
    "Un autre appareil a repris la diffusion",
    "La Xbox a été éteinte",
    "%s a reçu la demande, mais son jeu à distance n'a pas démarré. Redémarrez-la : maintenez le bouton d'alimentation de la console 10 secondes, rallumez-la, puis réessayez.",
    "Pavé tactile : glissez ↓ pour le menu, ↑ pour le bouton Xbox",
    "FSR + nettoyage",
    "IA + nettoyage",
    "Auto (%s)",
    "Mettre à jour maintenant ? L'app redémarre toute seule à la fin.",
    "Mettre à jour",
    "Plus tard",
    "Mises à jour",
    "À jour (%s)",
    "%s disponible : mettre à jour",
    "Vérification...",
    "Mise à jour vers %s",
    "Téléchargement... %s",
    "Vérification de la signature...",
    "Installation...",
    "Mis à jour. Redémarrage...",
    "PSBox mis à jour : rouvrez l'app",
    "Échec de la mise à jour : rien n'a changé",
    "Couleur du jeu",
    "Couleur personnalisée",
    "Couleur de la barre lumineuse",
    "Joystick gauche ou croix : couleur    L2 / R2 : luminosité",
    "Légère",
    "Moyenne",
    "Forte",
    "Maximale",
    "Intensité",
    "Fréquence",
    "Grave",
    "Aiguë",
    "Appuyez sur L2 / R2 pour le sentir : plus vous enfoncez, plus le jeu le demanderait fort",
    "Niveau %s",
    "Joystick gauche",
    "Joystick droit",
    "Position réelle",
    "Ce que reçoit le jeu",
    "L1 / R1 : choisir le joystick    Croix ← / → : régler    Bougez les joysticks pour le voir",
    "Résistance",
    "Style",
    "Vibration",
    "Impulsions de force",
    "Mode",
    "Genre",
    "Langue",
    "Tous les modes",
    "Solo",
    "Multijoueur en ligne",
    "Coopération en ligne",
    "Local / écran partagé",
    "Tous les genres",
    "Toutes les langues",
    "français",
    "Sous-titres en %s",
    "Doublé en %s",
    "Paramètres du jeu",
    "Profil",
    "Par défaut",
    "Personnalisé",
    "Réinitialiser",
    "Éditeur",
    "Tous les éditeurs",
    "Indépendants et autres",
    "Amis en train de jouer",
    "Microphone",
    "Coupé",
    "Actif",
    "Sortie audio",
    "TV",
    "Haut-parleur de la manette",
    "Signaler un problème",
    "Scannez-le avec votre téléphone : le formulaire de bug de GitHub s'ouvre, déjà rempli. Vous le relisez avant de l'envoyer.",
    "Il contient la version de l'app, l'erreur et des lignes du journal, sans données personnelles.",
    "PSBox s'est fermé de façon inattendue la dernière fois",
    "Pays de connexion",
    "Désactivé (celui de la console)",
    "Se connecter depuis un autre pays",
    "xCloud sera informé que la connexion vient du pays choisi. Microsoft ne l'autorise pas : utilisez-le à vos risques. Les jeux peuvent alors venir de serveurs éloignés, avec plus de latence. Modifiable plus tard dans Paramètres.",
    "xCloud a refusé la connexion. Le jeu dans le cloud n'est peut-être pas proposé dans votre pays, ou le compte n'a pas d'abonnement Game Pass avec le jeu dans le cloud.",
    "xCloud a refusé la connexion, même en choisissant %s. Le compte n'a probablement pas d'abonnement Game Pass avec le jeu dans le cloud.",
    "Brésil",
    "Japon",
    "Corée du Sud",
    "Pologne",
    "États-Unis",
    "Les connexions sécurisées ont été refusées car la date et l'heure de la PS5 semblent fausses (elle indique %s). Réglez-les dans Paramètres > Système > Date et heure, puis réessayez.",
    "Une connexion sécurisée a été refusée : quelque chose sur le réseau (serveur DNS, proxy ou filtre) répond à la place des sites de Microsoft. Essayez sans, puis réessayez.",
    "La date et l'heure de la PS5 semblent fausses (%s) : une partie du contenu ne se charge pas. Réglez-les dans Paramètres > Système > Date et heure.",
    "Impossible d'enregistrer la connexion sur la console : elle sera redemandée la prochaine fois. Signalez-le (Paramètres > △).",
    "Attente estimée : %s s", // QueueSeconds
    "Attente estimée : %s min", // QueueMinutes
    "Sortie 120 Hz (VRR)", // HighRefresh
    "Activée (en jeu)", // HighRefreshOn
    "Connexion instable", // ConnectionUnstable
    "Session : %s", // SessionSummary
    "Débit maximal", // BitrateCap
    "Automatique", // BitrateAuto
    "Volume du jeu", // GameVolume
    "Clavier : F12 ce menu · Origine bouton Xbox", // MenuKeyHint
    "Clavier et souris", // FilterMouseKeyboard
    "Clavier et souris", // MouseKeyboardSupport
    "Clavier et souris : joueur %s", // KeyboardPlayer
    "Vous êtes dans la file d'attente.", // InQueueNoEstimate
    "Touches du clavier", // KeyboardKeys
    "Croix directionnelle", // DPad
    "Appuyez sur une touche ou un bouton de la souris", // PressKey
    "%s est réservée par l'app", // KeyReserved
    "Modifier", // ChangeKey
    "Effacer", // ClearKey
    "La souris déplace le joystick droit et chaque cran de la molette appuie sur Triangle. F12 (menu), Origine (bouton Xbox) et 1-4 (joueur) sont réservées par l'app.", // KeysHelp
    "Visée gyroscopique", // Gyro
    "Toujours", // GyroAlways
    "En visant (L2)", // GyroAiming
    "Intensité du gyroscope", // GyroIntensity
    "Les connexions sécurisées ont été refusées car la date et l'heure de cet ordinateur semblent fausses (il indique %s). Corrigez-les dans les paramètres du système, puis réessayez.", // ClockWrongPc
    "La date et l'heure de cet ordinateur semblent fausses (%s) : une partie du contenu ne se charge pas. Corrigez-les dans les paramètres du système.", // ClockWrongNoticePc
    "Désactivé (celui de l'ordinateur)", // LoginCountryOffPc
    "Ouvrir dans le navigateur", // OpenInBrowser
    "%s disponible : télécharger", // UpdateReadyPc
    "Menu : START + BACK ou F12 · Bouton Xbox : Guide ou Origine", // GestureHintButtons
    "START + BACK ou F12 : ce menu · Guide ou Origine : bouton Xbox", // MenuButtonHint
    "Mode d'affichage", // DisplayMode
    "Fenêtre", // DisplayWindow
    "Plein écran", // DisplayFullscreen
    "Quitter PSBox ?", // QuitTitle
    "Quitter", // QuitConfirm
    "Boutons de la manette", // ControllerButtons
    "Appuyez sur le bouton pour %s", // PadPressButton
    "Bouton manquant pour : %s. Attribuez-en un à chaque fonction, ou réinitialisez ou annulez les modifications.", // PadMissing
    "Annuler les modifications", // DiscardChanges
    "Seul le jeu est concerné : les menus de l'app gardent les boutons tels quels. Choisir un bouton déjà utilisé échange les deux.", // PadHelp
    "Forfait", // FilterPlan
    "Mon forfait", // PlanMine
    "Tous les forfaits", // PlanAll
    "Inclus avec Game Pass %s", // IncludedWithPlan
    "Pavé tactile comme souris", // TouchpadMouse
    "Favoris", // Favorites
    "Ajouter aux favoris", // AddFavorite
    "Retirer des favoris", // RemoveFavorite
    "Ajouté aux favoris", // AddedToFavorites
    "Retiré des favoris", // RemovedFromFavorites
    "Langue du jeu", // GameLanguage
    "Limite mensuelle de jeu dans le cloud atteinte", // MonthlyLimitExceeded
    "Temps de jeu restant", // PlaytimeRemaining
    "%s h restantes", // HoursRemaining
    "Moins de 2 h restantes", // PlaytimeWarning
    "Profil de gâchettes", // TriggerProfile
    "Standard", // TriggerProfileStd
    "Tir (Hair Trigger)", // TriggerProfileShooter
    "Course (ABS)", // TriggerProfileRacing
    "Le stockage %s n'est pas accessible en écriture : connexion et réglages non enregistrés", // StorageNotWritable
    "FTP : faites 'chmod 777 %s' ou supprimez le dossier et relancez", // StorageFixHint
    "Personnaliser les indicateurs",                  // StatsCustomize
    "IPS et résolution",                              // StatsIndicatorFps
    "Débit binaire (Bitrate)",                        // StatsIndicatorBitrate
    "Latence (RTT)",                                  // StatsIndicatorLatency
    "Perte de paquets",                               // StatsIndicatorLoss
    "Graphique de frametime",                         // StatsIndicatorGraph
    "Latence de décodage et d'affichage",             // StatsIndicatorDelay
    "Qualité vidéo (QP)",                             // StatsIndicatorQp
    "État du microphone",                             // StatsIndicatorMic
    "Scannez le code QR avec votre téléphone pour résoudre ceci chez Microsoft, puis appuyez sur Croix pour réessayer",  // ScanQrToResolve
    "Obtenir gratuitement",                           // ClaimFree
    "Obtention de la licence gratuite...",            // Claiming
    "Jeu ajouté à votre compte avec succès !",        // ClaimSuccess
    "Échec de l'obtention du jeu gratuit : %s",       // ClaimFailed
    "Ce jeu n'est pas gratuit sur le store",          // ProductNotFree
    "Jeu gratuit. Appuyez sur Croix pour l'ajouter à votre compte et jouer",  // FreeAcquireHint
    "Échec de résolution DNS pour les serveurs Microsoft. Sur PS5, veillez à exécuter NanoDNS et à définir le DNS primaire sur votre IP locale pour garder l'accès Internet tout en bloquant Sony.", // DnsBlockedNanoDns
    "Ce compte Microsoft n'a pas encore de profil Xbox. Connectez-vous sur xbox.com sur mobile ou PC pour créer votre Gamertag, puis réessayez.", // XboxProfileMissing
    "File d'attente prête ! Lancement du jeu...",     // QueueReadyAlert
    "Alerte sonore de file prête",                    // QueueReadyNotification
    "Joué : %s",                                      // PlaytimePlayed
    "Actualiser",                                     // RefreshLibrary
    "Actualisation de la bibliothèque...",            // RefreshingLibrary
    "Ce compte Xbox est suspendu ou banni. Consultez enforcement.xbox.com pour plus de détails.", // XboxAccountBanned
    "Xbox Live n'est pas proposé dans le pays ou la région de ce compte.", // XboxRegionNotSupported
    "Vérification d'âge ou acceptation des conditions requise. Connectez-vous sur xbox.com dans un navigateur, puis réessayez.", // XboxAdultVerificationRequired
    "Compte enfant : un adulte doit ajouter ce compte à une famille Microsoft sur account.microsoft.com/family.", // XboxChildAccountFamily
    "Vérifier ma connexion",                          // CheckSignIn
    "Vérifier",                                       // RunCheck
    "Test réseau",                                    // NetworkTest
    "Tester",                                         // RunTest
    "Succès",                                         // Achievements
    "Aucun succès",                                   // NoAchievements
    "Verrouillé",                                     // AchievementLocked
    "Succès secret",                                  // SecretAchievement
    "Liste d'envies",                                 // Wishlist
    "Ajouter aux envies",                             // AddWishlist
    "Retirer des envies",                             // RemoveWishlist
    "Ajouté aux envies",                              // AddedToWishlist
    "Retiré des envies",                              // RemovedFromWishlist
    "%s est en promotion !",                          // WishlistOnSale
    "%s a rejoint le Game Pass !",                    // WishlistInGamePass
    "Soutenir le projet",                             // SupportProject
    "Soutenir le Développement de PSBox",             // SupportTitle
    "PSBox est un projet open source développé avec passion pour offrir la meilleure expérience Xbox Cloud Gaming sur PlayStation 5. Si vous appréciez l'application et souhaitez soutenir les mises à jour et les nouveautés, pensez à m'offrir un café sur Ko-Fi !", // SupportMessage
    "Ko-fi",                                          // SupportKoFi
    "Scannez avec votre téléphone",                   // ScanToDonate
    "Tester le son",                                  // TestSound
    "La liste de souhaits est vide",                  // WishlistEmpty
    // Loading tips
    "Pavé tactile : glissez ↓ (ou Pavé tactile + Options) pour le menu, ↑ pour le bouton Xbox", // TipTouchpadMenu
    "Clavier et souris USB branchés sur la console sont pris en charge dans les jeux du cloud compatibles", // TipKeyboardMouse
    "Obtenez des jeux gratuits, démos et promotions du magasin directement depuis l'application", // TipFreeStore
    "Un problème ? Ouvrez Signaler un problème pour scanner un code QR avec les journaux de la console", // TipBugReportQr
    "Vous appréciez le projet ? Soutenez son développement via Ko-fi dans les Paramètres !", // TipSupportKofi
    "Activez la sortie vidéo 120 Hz dans les Paramètres pour réduire la latence d'affichage et de contrôle", // Tip120HzVideo
    "Calibrez la zone morte des sticks dans les Paramètres pour éliminer le drift et améliorer la précision", // TipAnalogDeadzone
    "Utilisez le microphone intégré de la DualSense pour le chat vocal dans les groupes et jeux multijoueurs", // TipDualSenseMic
    // Settings descriptions
    "Modifie la langue de l'interface de l'application et les préférences audio et sous-titres dans les jeux du cloud", // SettingDescLanguage
    "Définit la résolution vidéo maximale (1080p, 720p ou Meilleure disponible). Prend effet au prochain lancement", // SettingDescResolution
    "Sélectionne la région des serveurs Microsoft pour le streaming (Automatique ou spécifique). Prend effet au prochain lancement", // SettingDescRegion
    "Ajuste la zone morte intérieure des sticks analogiques DualSense pour éviter le drift et les mouvements involontaires", // SettingDescDeadzone
    "Active ou désactive l'émulation des vibrations sur les gâchettes adaptatives L2 et R2 dans les jeux compatibles", // SettingDescTriggerRumble
    "Bascule la disposition des boutons de confirmation et retour entre Croix (occidental) et Rond (japonais)", // SettingDescConfirmButton
    "Configure la couleur et le comportement de la barre lumineuse de votre manette DualSense", // SettingDescLightBar
    "Vérifie et installe automatiquement les mises à jour officielles de l'application directement sur votre console", // SettingDescUpdates
    "Permet de vous connecter à votre compte Xbox via une autre région pour accéder à d'autres catalogues", // SettingDescLoginCountry
    "Active la sortie vidéo 120 Hz sur votre console pour une réactivité accrue et une latence réduite", // SettingDescHighRefresh
    "Définit la limite maximale de débit binaire pour le streaming vidéo, optimisant la qualité ou préservant le réseau", // SettingDescBitrateCap
    "Ajuste le niveau du volume audio principal pendant les sessions de jeu en streaming", // SettingDescGameVolume
    "Associe les touches du clavier USB ou Bluetooth aux boutons correspondants de la manette Xbox en jeu", // SettingDescKeyboardKeys
    "Bascule entre mode fenêtré et plein écran sur les plates-formes de bureau compatibles", // SettingDescDisplayMode
    "Ouvre l'écran de test de réponse et de calibrage de tous les boutons et axes de votre manette connectée", // SettingDescControllerButtons
    "Active les capteurs de mouvement DualSense pour assister la visée analogique dans les jeux de tir", // SettingDescGyro
    "Joue un signal sonore distinctif lorsque l'attente dans la file xCloud est terminée et que le jeu est prêt", // SettingDescQueueReady
    "Exécute un diagnostic complet de la connexion Xbox Live, validant jetons d'accès, profil et abonnements", // SettingDescCheckSignIn
    "Mesure le ping (RTT) et la gigue (jitter) vers les centres de données de jeux en nuage de plusieurs régions", // SettingDescNetworkTest
    "Affiche un code QR pour soutenir le développeur via Ko-fi et contribuer à la maintenance active du projet", // SettingDescSupportProject
    "Nouveautés",                                     // WhatsNew
    "Nouveautés",                                     // WhatsNewTitle
    "Haut-parleur de la manette",                     // PadSpeakerVolume
    "Muet",                                           // Mute
    "Contrôle Web",                                   // WebControlTitle
    "Connexion mobile",                               // WebControlSubtitle
    "Connectez-vous au même Wi-Fi pour chercher, remapper, utiliser le mobile comme manette virtuelle, signaler des soucis ou voir les logs.", // WebControlDesc
    "Décodage vidéo",                                 // VideoDecoder
    "Matériel (Recommandé)",                          // VideoDecoderHw
    "Logiciel (CPU)",                                 // VideoDecoderSw
    "Sélectionne le décodage matériel (libSceVideodec2 sur PS5, HWAccel sur Desktop) ou logiciel (CPU)", // SettingDescVideoDecoder
    "Audio vers vibration haptique",                  // AudioHaptics
    "Convertit les basses fréquences audio en vibrations haptiques sur DualSense", // SettingDescAudioHaptics
    "Raccourcis du pavé tactile",                     // TouchpadShortcuts
    "Configure les gestes de glissement et les clics gauche/droit sur le pavé tactile DualSense", // SettingDescTouchpadShortcuts
    "Glisser vers le haut (↑)",                       // TouchpadSwipeUp
    "Glisser vers le bas (↓)",                        // TouchpadSwipeDown
    "Glisser vers la gauche (←)",                     // TouchpadSwipeLeft
    "Glisser vers la droite (→)",                     // TouchpadSwipeRight
    "Clic gauche",                                    // TouchpadClickLeft
    "Clic droit",                                     // TouchpadClickRight
    "Désactivé",                                      // TouchpadActNone
    "Menu du jeu",                                    // TouchpadActGameMenu
    "Bouton Xbox",                                    // TouchpadActXboxButton
    "Afficher/masquer les stats",                     // TouchpadActToggleStats
    "Couper/activer le micro",                        // TouchpadActToggleMic
    "Changer le mode gyro",                           // TouchpadActCycleGyro
    "Changer le profil de gâchette",                  // TouchpadActCycleTrigger
    "Pavé tactile + Options ouvre toujours le menu du jeu", // TouchpadHelp
    "Restaurer les paramètres par défaut",            // ResetSettings
    "Restaure tous les paramètres aux valeurs d'usine. Maintenez ✕ pendant 5 secondes pour confirmer.", // SettingDescResetSettings
    "Paramètres réinitialisés aux valeurs par défaut.", // SettingsResetNotice
    "Maintenir pour réinitialiser",                   // HoldToReset
    "La bibliothèque est déjà à jour",                // LibraryUpToDate
    "Bibliothèque mise à jour (%s nouveaux jeux)",    // LibraryUpdatedCount
    "Mode hors ligne",                                // OfflineMode
};

constexpr Column kGerman = {
    "PSBox Cloud Gaming",
    "PSBOX CLOUD GAMING",
    "Anmeldung läuft...",
    "Anmeldecode wird abgerufen...",
    "Deine Spiele werden geladen...",
    "Zum Spielen anmelden",
    "Rufe auf deinem Handy oder Computer",
    "auf und gib diesen Code ein",
    "Scannen, um die Seite zu öffnen, dann den Code eingeben",
    "Warte auf deine Anmeldung...",
    "Weiterspielen",
    "Spielen",
    "Zurück",
    "Auswählen",
    "Abmelden",
    "Abbrechen",
    "Erneut versuchen",
    "Dein Spiel wird vorbereitet",
    "Du bist in der Warteschlange. Geschätzte Wartezeit: %s",
    "Verbindung wird hergestellt...",
    "Streaming wird gestartet...",
    "Touchpad: ↓ dieses Menü · ↑ Xbox-Taste",
    "Streaming beendet",
    "Etwas ist schiefgelaufen",
    "Angemeldet als %s",
    "Noch keine Spiele vorhanden",
    "Von %s",
    "Einstellungen",
    "Sprache",
    "Streaming-Auflösung",
    "Serverregion",
    "Automatisch (%s)",
    "1080p (Full HD)",
    "720p (verbraucht weniger Daten)",
    "Beste (Auto)",
    "Auflösung und Region gelten ab dem nächsten gestarteten Spiel.",
    "Ändern",
    "Anpassen",
    "Halten zum Abmelden",
    "Weiter halten zum Abmelden (%s)",
    "Anmeldung fehlgeschlagen: %s",
    "Die Anmeldung im Browser wurde nicht abgeschlossen. Verwende ein persönliches Microsoft-Konto, schließe Sicherheitsabfragen/2FA ab und gewähre Zugriff.",
    "Die Spieleliste konnte nicht geladen werden: %s",
    "Das Streaming konnte nicht gestartet werden: %s",
    "Keine Aktivität: Die Verbindung wird in %s Sekunden getrennt",
    "Meine Spiele",
    "Für dein Konto nicht verfügbar",
    "Game Pass",
    "Suchen",
    "Gib den Namen eines Spiels ein",
    "Keine Spiele gefunden",
    "%s Ergebnisse",
    "Leerzeichen",
    "Löschen",
    "Leeren",
    "%s Spiele",
    "Tippen",
    "Andere Region wird versucht: %s",
    "Zum Kaufen verfügbar",
    "KAUFEN",
    "Kaufe dieses Spiel, um es in der Cloud zu spielen",
    "Kaufe es auf xbox.com oder in der Xbox-App; danach erscheint es unter Meine Spiele.",
    "Scannen, um die Store-Seite zu öffnen",
    "Suchen in %s",
    "KOSTENLOS",
    "Ausblenden",
    "Einblenden",
    "Ausgeblendet",
    "\xE2\x96\xA1 blendet ein Spiel wieder ein",
    "Ausgeblendet: unter Ausgeblendet, am Ende dieses Tabs",
    "Wieder sichtbar",
    "Sortieren: %s",
    "Zuletzt gespielt",
    "A-Z",
    "Nach Konsole",
    "Sortieren",
    "Kostenlos",
    "Niedrigster Preis",
    "Alle Konsolen",
    "1 Spiel",
    "Abschnitte",
    "Spieleliste wird aktualisiert...",
    "Spielmenü",
    "Statistiken",
    "An",
    "Aus",
    "Stream-Auflösung",
    "Spiel verlassen",
    "Latenz",
    "Bitrate",
    "Bilder pro Sekunde",
    "Paketverlust",
    "Region",
    "Dekodierung",
    "Der Server behält eventuell die aktuelle Qualität bei",
    "Schärfe",
    "Aus",
    "Niedrig",
    "Mittel",
    "Hoch",
    "Stick-Totzone",
    "Trigger-Vibration",
    "Bestätigungstaste",
    "An",
    "Aus",
    "Version %s verfügbar",
    "Controller",
    "Controller %s verbunden",
    "Controller %s getrennt",
    "Blockglättung",
    "Auf dem Bildschirm nach",
    "Verbindung verloren: neu verbinden...",
    "Wieder verbunden",
    "Lichtleiste",
    "Hochskalierung",
    "KI (Anime4K)",
    "Free-to-play",
    "Einmal kostenlos auf xbox.com oder in der Xbox-App holen (Code mit dem Handy scannen), dann Spielen drücken.",
    "Scannen, um es kostenlos zu holen",
    "%s ist noch nicht in deinem Konto. Es ist kostenlos: Code auf seiner Seite scannen, Abrufen wählen und erneut versuchen (kann eine Minute dauern).",
    "Dein Konto kann %s nicht spielen: nicht in deinem Abo oder nicht gekauft.",
    "Meine Konsolen",
    "An",
    "Im Standby",
    "Aus",
    "Deine Konsolen werden gesucht...",
    "Noch keine Xbox gefunden",
    "Spiele hier auf deiner eigenen Xbox: ihr Bildschirm, ihre Spiele und Apps.",
    "Deine Xbox wird geweckt...",
    "%s hat nicht geantwortet. Prüfe, ob die Remotefunktionen aktiv sind und der Energiemodus Ruhezustand ist, oder schalte sie ein und versuche es erneut.",
    "Melde dich auf deiner Xbox mit genau diesem Microsoft-Konto an.",
    "Einstellungen > Geräte & Verbindungen > Remotefunktionen: Remotefunktionen aktivieren.",
    "Einstellungen > Allgemein > Energieoptionen: Ruhezustand wählen, damit sie von selbst aufwacht.",
    "Scannen für die Hilfe von Microsoft",
    "Erneut suchen",
    "Xbox-Taste",
    "Streaming beenden",
    "Das Streaming wurde auf der Xbox beendet",
    "Ein anderes Gerät hat das Streaming übernommen",
    "Die Xbox wurde ausgeschaltet",
    "%s hat die Anfrage erhalten, aber Remote Play ist nicht gestartet. Starte sie neu: Halte die Ein/Aus-Taste der Konsole 10 Sekunden gedrückt, schalte sie wieder ein und versuche es erneut.",
    "Touchpad: ↓ wischen für das Menü, ↑ für die Xbox-Taste",
    "FSR + Bereinigung",
    "KI + Bereinigung",
    "Auto (%s)",
    "Jetzt aktualisieren? Die App startet danach von selbst neu.",
    "Jetzt aktualisieren",
    "Nicht jetzt",
    "Updates",
    "Aktuell (%s)",
    "%s verfügbar: aktualisieren",
    "Wird geprüft...",
    "Aktualisierung auf %s",
    "Wird geladen... %s",
    "Signatur wird geprüft...",
    "Wird installiert...",
    "Aktualisiert. Neustart...",
    "PSBox aktualisiert: App erneut öffnen",
    "Update fehlgeschlagen: nichts wurde geändert",
    "Farbe des Spiels",
    "Eigene Farbe",
    "Farbe der Lichtleiste",
    "Linker Stick oder Steuerkreuz: Farbe    L2 / R2: Helligkeit",
    "Leicht",
    "Mittel",
    "Stark",
    "Maximal",
    "Stärke",
    "Frequenz",
    "Tief",
    "Hoch",
    "L2 / R2 drücken zum Fühlen: je tiefer, desto stärker würde es das Spiel verlangen",
    "Stufe %s",
    "Linker Stick",
    "Rechter Stick",
    "Tatsächliche Position",
    "Was das Spiel bekommt",
    "L1 / R1: Stick wählen    Steuerkreuz ← / →: einstellen    Sticks bewegen zum Sehen",
    "Widerstand",
    "Stil",
    "Vibration",
    "Kraftimpulse",
    "Modus",
    "Genre",
    "Sprache",
    "Alle Modi",
    "Einzelspieler",
    "Online-Mehrspieler",
    "Online-Koop",
    "Lokal / geteilter Bildschirm",
    "Alle Genres",
    "Alle Sprachen",
    "Deutsch",
    "Untertitel: %s",
    "Sprachausgabe: %s",
    "Spieleinstellungen",
    "Profil",
    "Standard",
    "Benutzerdefiniert",
    "Auf Standard zurücksetzen",
    "Publisher",
    "Alle Publisher",
    "Indies & Andere",
    "Freunde spielen jetzt",
    "Mikrofon",
    "Stumm",
    "Aktiv",
    "Audioausgabe",
    "TV",
    "Controller-Lautsprecher",
    "Problem melden",
    "Mit dem Handy scannen: Das Fehlerformular auf GitHub öffnet sich ausgefüllt. Du prüfst es vor dem Senden.",
    "Enthalten sind die App-Version, der Fehler und Zeilen des Logs, ohne persönliche Daten.",
    "PSBox wurde beim letzten Mal unerwartet beendet",
    "Anmeldeland",
    "Aus (das der Konsole)",
    "Als anderes Land anmelden",
    "xCloud wird mitgeteilt, dass die Anmeldung aus dem gewählten Land kommt. Microsoft erlaubt das nicht: Nutzung auf eigenes Risiko. Spiele kommen dann eventuell von weit entfernten Servern, mit mehr Verzögerung. Später in den Einstellungen änderbar.",
    "xCloud hat die Anmeldung abgelehnt. Cloud Gaming wird in deinem Land vielleicht nicht angeboten, oder das Konto hat kein Game-Pass-Abo mit Cloud Gaming.",
    "xCloud hat die Anmeldung abgelehnt, auch mit %s. Das Konto hat wahrscheinlich kein Game-Pass-Abo mit Cloud Gaming.",
    "Brasilien",
    "Japan",
    "Südkorea",
    "Polen",
    "USA",
    "Sichere Verbindungen wurden abgelehnt, weil Datum und Uhrzeit der PS5 falsch zu sein scheinen (sie zeigt %s). Stelle sie unter Einstellungen > System > Datum und Uhrzeit ein und versuche es erneut.",
    "Eine sichere Verbindung wurde abgelehnt: Etwas im Netzwerk (DNS-Server, Proxy oder Filter) antwortet anstelle der Microsoft-Seiten. Versuche es ohne und dann erneut.",
    "Datum und Uhrzeit der PS5 scheinen falsch zu sein (%s): Einige Inhalte laden nicht. Stelle sie unter Einstellungen > System > Datum und Uhrzeit ein.",
    "Die Anmeldung konnte nicht auf der Konsole gespeichert werden und wird beim nächsten Mal erneut abgefragt. Bitte melde das (Einstellungen > △).",
    "Geschätzte Wartezeit: %s s", // QueueSeconds
    "Geschätzte Wartezeit: %s Min.", // QueueMinutes
    "120-Hz-Ausgabe (VRR)", // HighRefresh
    "An (in Spielen)", // HighRefreshOn
    "Instabile Verbindung", // ConnectionUnstable
    "Sitzung: %s", // SessionSummary
    "Maximale Bitrate", // BitrateCap
    "Automatisch", // BitrateAuto
    "Spiellautstärke", // GameVolume
    "Tastatur: F12 dieses Menü · Pos1 Xbox-Taste", // MenuKeyHint
    "Maus & Tastatur", // FilterMouseKeyboard
    "Maus und Tastatur", // MouseKeyboardSupport
    "Tastatur und Maus: Spieler %s", // KeyboardPlayer
    "Du bist in der Warteschlange.", // InQueueNoEstimate
    "Tastaturbelegung", // KeyboardKeys
    "Steuerkreuz", // DPad
    "Drücke eine Taste oder Maustaste", // PressKey
    "%s ist für die App reserviert", // KeyReserved
    "Ändern", // ChangeKey
    "Löschen", // ClearKey
    "Die Maus bewegt den rechten Stick, jede Rasterstufe des Mausrads drückt Dreieck. F12 (Menü), Pos1 (Xbox-Taste) und 1-4 (Spieler) sind für die App reserviert.", // KeysHelp
    "Gyro-Zielen", // Gyro
    "Immer", // GyroAlways
    "Beim Zielen (L2)", // GyroAiming
    "Gyro-Stärke", // GyroIntensity
    "Sichere Verbindungen wurden abgelehnt, weil Datum und Uhrzeit dieses Computers falsch zu sein scheinen (er zeigt %s). Korrigiere sie in den Systemeinstellungen und versuche es erneut.", // ClockWrongPc
    "Datum und Uhrzeit dieses Computers scheinen falsch zu sein (%s): Einige Inhalte laden nicht. Korrigiere sie in den Systemeinstellungen.", // ClockWrongNoticePc
    "Aus (das des Computers)", // LoginCountryOffPc
    "Im Browser öffnen", // OpenInBrowser
    "%s verfügbar: herunterladen", // UpdateReadyPc
    "Menü: START + BACK oder F12 · Xbox-Taste: Guide oder Pos1", // GestureHintButtons
    "START + BACK oder F12: dieses Menü · Guide oder Pos1: Xbox-Taste", // MenuButtonHint
    "Anzeigemodus", // DisplayMode
    "Fenster", // DisplayWindow
    "Vollbild", // DisplayFullscreen
    "PSBox beenden?", // QuitTitle
    "Beenden", // QuitConfirm
    "Controller-Tasten", // ControllerButtons
    "Drücke die Taste für %s", // PadPressButton
    "Ohne Taste: %s. Weise jeder Funktion eine zu, oder setze zurück bzw. verwirf die Änderungen.", // PadMissing
    "Änderungen verwerfen", // DiscardChanges
    "Gilt nur im Spiel: Die Menüs der App nutzen die Tasten, wie sie sind. Eine schon belegte Taste zu wählen tauscht die beiden.", // PadHelp
    "Abo", // FilterPlan
    "Mein Abo", // PlanMine
    "Alle Abos", // PlanAll
    "In Game Pass %s enthalten", // IncludedWithPlan
    "Touchpad als Maus", // TouchpadMouse
    "Favoriten", // Favorites
    "Zu Favoriten hinzufügen", // AddFavorite
    "Aus Favoriten entfernen", // RemoveFavorite
    "Zu Favoriten hinzugefügt", // AddedToFavorites
    "Aus Favoriten entfernt", // RemovedFromFavorites
    "Spiel-Sprache", // GameLanguage
    "Monatliches Cloud-Gaming-Limit erreicht", // MonthlyLimitExceeded
    "Verbleibende Spielzeit", // PlaytimeRemaining
    "%s Std. verbleibend", // HoursRemaining
    "Weniger als 2 Std. verbleibend", // PlaytimeWarning
    "Trigger-Profil", // TriggerProfile
    "Standard", // TriggerProfileStd
    "Shooter (Hair Trigger)", // TriggerProfileShooter
    "Rennen (ABS)", // TriggerProfileRacing
    "Speicher %s ist schreibgeschützt: Anmeldung und Einstellungen werden nicht gespeichert", // StorageNotWritable
    "FTP: 'chmod 777 %s' ausführen oder Ordner löschen und neu starten", // StorageFixHint
    "Indikatoren anpassen",                           // StatsCustomize
    "FPS und Auflösung",                              // StatsIndicatorFps
    "Bitrate",                                        // StatsIndicatorBitrate
    "Latenz (RTT)",                                   // StatsIndicatorLatency
    "Paketverlust",                                   // StatsIndicatorLoss
    "Frametime-Diagramm",                             // StatsIndicatorGraph
    "Dekodier- und Anzeigelatenz",                    // StatsIndicatorDelay
    "Videoqualität (QP)",                             // StatsIndicatorQp
    "Mikrofonstatus",                                 // StatsIndicatorMic
    "Scannen Sie den QR-Code mit dem Smartphone, um dies bei Microsoft zu beheben, und drücken Sie dann Kreuz zum Wiederholen",  // ScanQrToResolve
    "Kostenlos holen",                                // ClaimFree
    "Kostenlose Lizenz wird angefordert...",          // Claiming
    "Spiel erfolgreich zu Ihrem Konto hinzugefügt!", // ClaimSuccess
    "Kostenloses Spiel konnte nicht angefordert werden: %s",  // ClaimFailed
    "Dieses Spiel ist im Store nicht kostenlos",      // ProductNotFree
    "Kostenloses Spiel. Drücken Sie Kreuz, um es zu Ihrem Konto hinzuzufügen und zu spielen",  // FreeAcquireHint
    "DNS-Auflösung für Microsoft-Server fehlgeschlagen. Stelle auf der PS5 sicher, dass NanoDNS läuft und der primäre DNS auf die lokale IP gesetzt ist, um Internetzugriff zu behalten.", // DnsBlockedNanoDns
    "Dieses Microsoft-Konto hat noch kein Xbox-Profil. Melde dich auf xbox.com auf dem Smartphone oder PC an, um deinen Gamertag zu erstellen, und versuche es erneut.", // XboxProfileMissing
    "Warteschlange bereit! Spiel wird gestartet...",  // QueueReadyAlert
    "Signalton bei Warteschlangenende",               // QueueReadyNotification
    "Gespielt: %s",                                   // PlaytimePlayed
    "Aktualisieren",                                  // RefreshLibrary
    "Bibliothek wird aktualisiert...",                // RefreshingLibrary
    "Dieses Xbox-Konto ist gesperrt oder gebannt. Details unter enforcement.xbox.com.", // XboxAccountBanned
    "Xbox Live wird im Land oder in der Region dieses Kontos nicht angeboten.", // XboxRegionNotSupported
    "Altersprüfung oder Zustimmung zu den Bedingungen erforderlich. Melde dich im Browser auf xbox.com an und versuche es erneut.", // XboxAdultVerificationRequired
    "Kinderkonto: Ein Erwachsener muss dieses Konto unter account.microsoft.com/family zu einer Microsoft-Familie hinzufügen.", // XboxChildAccountFamily
    "Anmeldung prüfen",                               // CheckSignIn
    "Prüfen",                                         // RunCheck
    "Netzwerktest",                                   // NetworkTest
    "Testen",                                         // RunTest
    "Erfolge",                                        // Achievements
    "Keine Erfolge",                                  // NoAchievements
    "Gesperrt",                                       // AchievementLocked
    "Geheimer Erfolg",                                // SecretAchievement
    "Wunschliste",                                    // Wishlist
    "Zur Wunschliste hinzufügen",                     // AddWishlist
    "Von Wunschliste entfernen",                      // RemoveWishlist
    "Zur Wunschliste hinzugefügt",                    // AddedToWishlist
    "Von Wunschliste entfernt",                       // RemovedFromWishlist
    "%s ist im Angebot!",                             // WishlistOnSale
    "%s ist jetzt im Game Pass!",                     // WishlistInGamePass
    "Projekt unterstützen",                           // SupportProject
    "PSBox-Entwicklung unterstützen",                 // SupportTitle
    "PSBox ist ein Open-Source-Projekt, das mit viel Engagement entwickelt wird, um das beste Xbox Cloud Gaming-Erlebnis auf die PlayStation 5 zu bringen. Wenn dir die App gefällt und du die Weiterentwicklung unterstützen möchtest, spendiere mir gerne einen Kaffee auf Ko-Fi!", // SupportMessage
    "Ko-fi",                                          // SupportKoFi
    "Mit dem Smartphone scannen",                     // ScanToDonate
    "Ton testen",                                     // TestSound
    "Wunschliste ist leer",                           // WishlistEmpty
    // Loading tips
    "Touchpad: ↓ wischen (oder Touchpad + Options) für das Menü, ↑ für die Xbox-Taste", // TipTouchpadMenu
    "USB-Tastatur und -Maus an der Konsole werden in unterstützten Cloud-Spielen unterstützt", // TipKeyboardMouse
    "Kostenlose Spiele, Demos und Store-Angebote direkt in der App-Bibliothek sichern", // TipFreeStore
    "Problem aufgetreten? Öffnen Sie 'Problem melden', um einen QR-Code mit Konsolenprotokollen zu scannen", // TipBugReportQr
    "Gefällt Ihnen das Projekt? Unterstützen Sie die Weiterentwicklung via Ko-fi in den Einstellungen!", // TipSupportKofi
    "120-Hz-Videoausgabe in den Einstellungen für geringere Anzeigelatenz und flüssigere Steuerung aktivieren", // Tip120HzVideo
    "Stick-Totzone in den Einstellungen kalibrieren, um Stick-Drift zu verhindern und Präzision zu verbessern", // TipAnalogDeadzone
    "Integriertes DualSense-Mikrofon für Party- und Multiplayer-Sprachchat verwenden", // TipDualSenseMic
    // Settings descriptions
    "Ändert die Sprache der Benutzeroberfläche sowie die bevorzugte Audio- und Untertitelsprache in Cloud-Spielen", // SettingDescLanguage
    "Legt die maximale Streaming-Auflösung fest (1080p, 720p oder Beste). Wird beim nächsten Spielstart wirksam", // SettingDescResolution
    "Wählt die Microsoft-Rechenzentrumsregion für das Streaming aus (Automatisch oder bestimmte Regionen)", // SettingDescRegion
    "Passt die innere Totzone der DualSense-Analogsticks an, um Drift und unbeabsichtigte Bewegungen zu vermeiden", // SettingDescDeadzone
    "Aktiviert oder deaktiviert die Impuls-Trigger-Vibration an den adaptiven DualSense-Triggern L2 und R2", // SettingDescTriggerRumble
    "Wechselt die Tastenbelegung für Bestätigen und Zurück zwischen Kreuz (westlich) und Kreis (japanisch)", // SettingDescConfirmButton
    "Konfiguriert Farbe und Verhalten der Lichtleiste des DualSense-Controllers beim Navigieren und Spielen", // SettingDescLightBar
    "Prüft und installiert offizielle App-Updates automatisch direkt auf der Konsole ohne PC", // SettingDescUpdates
    "Ermöglicht die Xbox-Konto-Anmeldung über eine andere Region für alternative Spielkataloge", // SettingDescLoginCountry
    "Aktiviert 120-Hz-Videoausgabe auf Ihrer Konsole für minimale Eingabelatenz und höhere Bildwiederholrate", // SettingDescHighRefresh
    "Legt die maximale Bitrate für das Videostreaming fest, um Bildqualität zu optimieren oder Daten zu sparen", // SettingDescBitrateCap
    "Regelt die Gesamtlautstärke während der Spielsitzungen im Cloud-Streaming", // SettingDescGameVolume
    "Weist Tasten der USB- oder Bluetooth-Tastatur den entsprechenden Xbox-Controller-Tasten beim Spielen zu", // SettingDescKeyboardKeys
    "Wechselt zwischen Fenster- und Vollbildmodus auf unterstützten Desktop-Plattformen", // SettingDescDisplayMode
    "Öffnet den Test- und Kalibrierungsbildschirm für alle Tasten und Achsen des angeschlossenen Controllers", // SettingDescControllerButtons
    "Nutzt die DualSense-Bewegungssensoren zur Unterstützung des analogen Zielens in Shooter- und Actionspielen", // SettingDescGyro
    "Spielt einen Signalton ab, wenn die Wartezeit in der xCloud-Warteschlange endet und das Spiel bereit ist", // SettingDescQueueReady
    "Führt eine Diagnose der Xbox Live-Anmeldung durch und prüft Zugriffstoken, Profil und Abonnements", // SettingDescCheckSignIn
    "Misst Ping (RTT) und Jitter zu Cloud-Gaming-Rechenzentren in mehreren weltweiten Regionen", // SettingDescNetworkTest
    "Zeigt einen QR-Code, um den Entwickler via Ko-fi zu unterstützen und die Pflege des Projekts zu sichern", // SettingDescSupportProject
    "Neuigkeiten",                                    // WhatsNew
    "Neuigkeiten",                                    // WhatsNewTitle
    "Controller-Lautsprecher",                        // PadSpeakerVolume
    "Stumm",                                          // Mute
    "Web-Steuerung",                                  // WebControlTitle
    "Über Smartphone verbinden",                      // WebControlSubtitle
    "Mit demselben WLAN verbinden, um zu suchen, Tasten zu belegen, das Smartphone als Controller zu nutzen, Fehler zu melden oder Logs anzusehen.", // WebControlDesc
    "Videodekodierung",                               // VideoDecoder
    "Hardware (Empfohlen)",                           // VideoDecoderHw
    "Software (CPU)",                                 // VideoDecoderSw
    "Wählt Hardware-Dekodierung (libSceVideodec2 auf PS5, HWAccel auf Desktop) oder Software-Dekodierung (CPU)", // SettingDescVideoDecoder
    "Audio-zu-Haptik",                                // AudioHaptics
    "Wandelt tiefe Audiofrequenzen in DualSense-Haptik-Vibrationen um", // SettingDescAudioHaptics
    "Touchpad-Kurzbefehle",                           // TouchpadShortcuts
    "Konfiguriert Wischgesten sowie Links- und Rechtsklick auf dem DualSense-Touchpad", // SettingDescTouchpadShortcuts
    "Nach oben wischen (↑)",                          // TouchpadSwipeUp
    "Nach unten wischen (↓)",                         // TouchpadSwipeDown
    "Nach links wischen (←)",                         // TouchpadSwipeLeft
    "Nach rechts wischen (→)",                        // TouchpadSwipeRight
    "Linksklick",                                     // TouchpadClickLeft
    "Rechtsklick",                                    // TouchpadClickRight
    "Deaktiviert",                                    // TouchpadActNone
    "Spielmenü",                                      // TouchpadActGameMenu
    "Xbox-Taste",                                     // TouchpadActXboxButton
    "Statistiken umschalten",                         // TouchpadActToggleStats
    "Mikrofon stumm/aktiv",                           // TouchpadActToggleMic
    "Gyro-Modus wechseln",                            // TouchpadActCycleGyro
    "Trigger-Profil wechseln",                        // TouchpadActCycleTrigger
    "Touchpad + Options öffnet immer das Spielmenü",  // TouchpadHelp
    "Standardeinstellungen wiederherstellen",         // ResetSettings
    "Setzt alle Einstellungen auf die Werkseinstellungen zurück. 5 Sekunden ✕ gedrückt halten zur Bestätigung.", // SettingDescResetSettings
    "Einstellungen auf Standardwerte zurückgesetzt.", // SettingsResetNotice
    "Halten zum Zurücksetzen",                        // HoldToReset
    "Bibliothek ist bereits aktuell",                 // LibraryUpToDate
    "Bibliothek aktualisiert (%s neue Spiele)",       // LibraryUpdatedCount
    "Offline-Modus",                                  // OfflineMode
};

constexpr Column kItalian = {
    "PSBox Cloud Gaming",
    "PSBOX CLOUD GAMING",
    "Accesso in corso...",
    "Richiesta di un codice di accesso...",
    "Caricamento dei tuoi giochi...",
    "Accedi per giocare",
    "Sul telefono o sul computer, vai su",
    "e inserisci questo codice",
    "Scansiona per aprire la pagina, poi inserisci il codice",
    "In attesa del tuo accesso...",
    "Riprendi a giocare",
    "Gioca",
    "Indietro",
    "Seleziona",
    "Esci",
    "Annulla",
    "Riprova",
    "Preparazione del gioco",
    "Sei in coda. Attesa stimata: %s",
    "Connessione...",
    "Avvio dello streaming...",
    "Touchpad: ↓ questo menu · ↑ pulsante Xbox",
    "Streaming terminato",
    "Si è verificato un problema",
    "Accesso effettuato come %s",
    "Ancora nessun gioco da mostrare",
    "Di %s",
    "Impostazioni",
    "Lingua",
    "Risoluzione dello streaming",
    "Regione del server",
    "Automatica (%s)",
    "1080p (Full HD)",
    "720p (usa meno dati)",
    "Migliore (Auto)",
    "Risoluzione e regione valgono dal prossimo gioco avviato.",
    "Cambia",
    "Regola",
    "Tieni premuto per uscire",
    "Continua a tenere premuto per uscire (%s)",
    "Accesso non riuscito: %s",
    "L'accesso nel browser non è stato completato. Utilizza un account Microsoft personale, completa le verifiche/2FA e autorizza l'app.",
    "Impossibile caricare l'elenco dei giochi: %s",
    "Impossibile avviare lo streaming: %s",
    "Nessuna attività: verrai disconnesso tra %s secondi",
    "I miei giochi",
    "Non disponibile sul tuo account",
    "Game Pass",
    "Cerca",
    "Scrivi il nome di un gioco",
    "Nessun gioco trovato",
    "%s risultati",
    "Spazio",
    "Cancella",
    "Svuota",
    "%s giochi",
    "Scrivi",
    "Provo un'altra regione: %s",
    "Disponibili per l'acquisto",
    "ACQUISTA",
    "Acquista questo gioco per giocarci nel cloud",
    "Acquistalo su xbox.com o nell'app Xbox; poi apparirà in I miei giochi.",
    "Scansiona per aprire la pagina dello store",
    "Cerca in %s",
    "GRATIS",
    "Nascondi",
    "Mostra",
    "Nascosti",
    "\xE2\x96\xA1 mostra di nuovo un gioco",
    "Nascosto: lo trovi in Nascosti, in fondo a questa scheda",
    "Di nuovo visibile",
    "Ordina: %s",
    "Recenti prima",
    "A-Z",
    "Per console",
    "Ordina",
    "Gratis",
    "Prezzo più basso",
    "Tutte le console",
    "1 gioco",
    "Sezioni",
    "Aggiornamento dell'elenco dei giochi...",
    "Menu di gioco",
    "Statistiche",
    "Attivo",
    "Disattivo",
    "Risoluzione dello stream",
    "Esci dal gioco",
    "Latenza",
    "Bitrate",
    "Fotogrammi al secondo",
    "Perdita di pacchetti",
    "Regione",
    "Decodifica",
    "Il server potrebbe mantenere la qualità attuale",
    "Nitidezza",
    "Disattivata",
    "Bassa",
    "Media",
    "Alta",
    "Zona morta dello stick",
    "Vibrazione dei grilletti",
    "Tasto di conferma",
    "Attivata",
    "Disattivata",
    "Versione %s disponibile",
    "Controller",
    "Controller %s connesso",
    "Controller %s disconnesso",
    "Attenuazione blocchi",
    "Sullo schermo dopo",
    "Connessione persa: riconnessione...",
    "Riconnesso",
    "Barra luminosa",
    "Upscaling",
    "IA (Anime4K)",
    "Free to play",
    "Ottienilo una volta, gratis, su xbox.com o nell'app Xbox (scansiona il codice con il telefono), poi premi Gioca.",
    "Scansiona per ottenerlo gratis",
    "%s non è ancora nel tuo account. È gratis: scansiona il codice nella sua pagina, scegli Ottieni e riprova (può volerci un minuto).",
    "Il tuo account non può giocare a %s: non è nel tuo abbonamento o non è stato acquistato.",
    "Le mie console",
    "Accesa",
    "In standby",
    "Spenta",
    "Ricerca delle tue console...",
    "Nessuna Xbox trovata per ora",
    "Gioca sulla tua Xbox da qui: il suo schermo, i giochi e le app.",
    "Accensione della tua Xbox...",
    "%s non ha risposto. Controlla che le Funzionalità remote siano attive e la modalità sia Sospensione, oppure accendila, e riprova.",
    "Sulla tua Xbox, accedi con questo stesso account Microsoft.",
    "Impostazioni > Dispositivi e connessioni > Funzionalità remote: attiva le funzionalità remote.",
    "Impostazioni > Generale > Opzioni di alimentazione: scegli Sospensione, così si accende da sola.",
    "Scansiona per l'aiuto di Microsoft",
    "Cerca di nuovo",
    "Pulsante Xbox",
    "Termina lo streaming",
    "Lo streaming è stato terminato sulla Xbox",
    "Un altro dispositivo ha preso lo streaming",
    "La Xbox è stata spenta",
    "%s ha ricevuto la richiesta, ma il gioco remoto non è partito. Riavviala: tieni premuto il pulsante di accensione della console per 10 secondi, riaccendila e riprova.",
    "Touchpad: scorri ↓ per il menu, ↑ per il pulsante Xbox",
    "FSR + pulizia",
    "IA + pulizia",
    "Auto (%s)",
    "Aggiornare ora? L'app si riavvia da sola alla fine.",
    "Aggiorna ora",
    "Non ora",
    "Aggiornamenti",
    "Aggiornato (%s)",
    "%s disponibile: aggiorna",
    "Verifica...",
    "Aggiornamento a %s",
    "Download... %s",
    "Verifica della firma...",
    "Installazione...",
    "Aggiornato. Riavvio...",
    "PSBox aggiornato: riapri l'app",
    "Aggiornamento non riuscito: nulla è cambiato",
    "Colore del gioco",
    "Colore personalizzato",
    "Colore della barra luminosa",
    "Levetta sinistra o croce: colore    L2 / R2: luminosità",
    "Leggera",
    "Media",
    "Forte",
    "Massima",
    "Intensità",
    "Frequenza",
    "Bassa",
    "Alta",
    "Premi L2 / R2 per sentirlo: più a fondo, più forte lo chiederebbe il gioco",
    "Livello %s",
    "Levetta sinistra",
    "Levetta destra",
    "Posizione reale",
    "Ciò che riceve il gioco",
    "L1 / R1: scegliere la levetta    Croce ← / →: regolare    Muovi le levette per vederlo",
    "Resistenza",
    "Stile",
    "Vibrazione",
    "Impulsi di forza",
    "Modalità",
    "Genere",
    "Lingua",
    "Tutte le modalità",
    "Giocatore singolo",
    "Multigiocatore online",
    "Cooperativa online",
    "Locale / schermo condiviso",
    "Tutti i generi",
    "Qualsiasi lingua",
    "italiano",
    "Sottotitoli in %s",
    "Doppiato in %s",
    "Impostazioni di gioco",
    "Profilo",
    "Predefinito",
    "Personalizzato",
    "Ripristina predefiniti",
    "Editore",
    "Tutti gli editori",
    "Indie e altri",
    "Amici che giocano ora",
    "Microfono",
    "Disattivato",
    "Attivo",
    "Uscita audio",
    "TV",
    "Altoparlante del controller",
    "Segnala un problema",
    "Inquadralo con il telefono: si apre il modulo dei bug di GitHub già compilato. Lo controlli prima di inviarlo.",
    "Include la versione dell'app, l'errore e righe del log, senza dati personali.",
    "PSBox si è chiuso inaspettatamente l'ultima volta",
    "Paese di accesso",
    "Disattivato (quello della console)",
    "Accedi come un altro paese",
    "A xCloud verrà detto che l'accesso arriva dal paese scelto. Microsoft non lo consente: usalo a tuo rischio. I giochi potrebbero arrivare da server lontani, con più ritardo. Puoi cambiarlo poi in Impostazioni.",
    "xCloud ha rifiutato l'accesso. Il cloud gaming potrebbe non essere offerto nel tuo paese, oppure l'account non ha un abbonamento Game Pass con il cloud gaming.",
    "xCloud ha rifiutato l'accesso, anche scegliendo %s. Probabilmente l'account non ha un abbonamento Game Pass con il cloud gaming.",
    "Brasile",
    "Giappone",
    "Corea del Sud",
    "Polonia",
    "Stati Uniti",
    "Le connessioni sicure sono state rifiutate perché la data e l'ora della PS5 sembrano errate (indica %s). Impostale in Impostazioni > Sistema > Data e ora, poi riprova.",
    "Una connessione sicura è stata rifiutata: qualcosa nella rete (un server DNS, proxy o filtro) risponde al posto dei siti Microsoft. Prova senza e riprova.",
    "La data e l'ora della PS5 sembrano errate (%s): alcuni contenuti non si caricano. Impostale in Impostazioni > Sistema > Data e ora.",
    "Impossibile salvare l'accesso sulla console: verrà richiesto di nuovo la prossima volta. Segnalalo (Impostazioni > △).",
    "Attesa stimata: %s s", // QueueSeconds
    "Attesa stimata: %s min", // QueueMinutes
    "Uscita 120 Hz (VRR)", // HighRefresh
    "Attiva (nei giochi)", // HighRefreshOn
    "Connessione instabile", // ConnectionUnstable
    "Sessione: %s", // SessionSummary
    "Bitrate massimo", // BitrateCap
    "Automatico", // BitrateAuto
    "Volume del gioco", // GameVolume
    "Tastiera: F12 questo menu · Home pulsante Xbox", // MenuKeyHint
    "Tastiera e mouse", // FilterMouseKeyboard
    "Tastiera e mouse", // MouseKeyboardSupport
    "Tastiera e mouse: giocatore %s", // KeyboardPlayer
    "Sei in coda.", // InQueueNoEstimate
    "Tasti della tastiera", // KeyboardKeys
    "Croce direzionale", // DPad
    "Premi un tasto o un pulsante del mouse", // PressKey
    "%s è riservato all'app", // KeyReserved
    "Cambia", // ChangeKey
    "Cancella", // ClearKey
    "Il mouse muove la levetta destra e ogni scatto della rotella preme Triangolo. F12 (menu), Home (tasto Xbox) e 1-4 (giocatore) sono riservati all'app.", // KeysHelp
    "Mira giroscopica", // Gyro
    "Sempre", // GyroAlways
    "Mirando (L2)", // GyroAiming
    "Intensità giroscopio", // GyroIntensity
    "Le connessioni sicure sono state rifiutate perché la data e l'ora di questo computer sembrano errate (indica %s). Correggile nelle impostazioni di sistema, poi riprova.", // ClockWrongPc
    "La data e l'ora di questo computer sembrano errate (%s): alcuni contenuti non si caricano. Correggile nelle impostazioni di sistema.", // ClockWrongNoticePc
    "Disattivato (quello del computer)", // LoginCountryOffPc
    "Apri nel browser", // OpenInBrowser
    "%s disponibile: scarica", // UpdateReadyPc
    "Menu: START + BACK o F12 · Pulsante Xbox: Guide o Home", // GestureHintButtons
    "START + BACK o F12: questo menu · Guide o Home: pulsante Xbox", // MenuButtonHint
    "Modalità schermo", // DisplayMode
    "Finestra", // DisplayWindow
    "Schermo intero", // DisplayFullscreen
    "Uscire da PSBox?", // QuitTitle
    "Esci", // QuitConfirm
    "Pulsanti del controller", // ControllerButtons
    "Premi il pulsante per %s", // PadPressButton
    "Manca un pulsante per: %s. Assegnane uno a ogni funzione, oppure ripristina o annulla le modifiche.", // PadMissing
    "Annulla modifiche", // DiscardChanges
    "Vale solo nel gioco: i menu dell'app usano i pulsanti così come sono. Scegliere un pulsante già usato scambia i due.", // PadHelp
    "Piano", // FilterPlan
    "Il mio piano", // PlanMine
    "Tutti i piani", // PlanAll
    "Incluso con Game Pass %s", // IncludedWithPlan
    "Touchpad come mouse", // TouchpadMouse
    "Preferiti", // Favorites
    "Aggiungi ai preferiti", // AddFavorite
    "Rimuovi dai preferiti", // RemoveFavorite
    "Aggiunto ai preferiti", // AddedToFavorites
    "Rimosso dai preferiti", // RemovedFromFavorites
    "Lingua del gioco", // GameLanguage
    "Limite mensile di gioco su cloud raggiunto", // MonthlyLimitExceeded
    "Tempo di gioco rimanente", // PlaytimeRemaining
    "%s ore rimanenti", // HoursRemaining
    "Meno di 2 ore rimanenti", // PlaytimeWarning
    "Profilo grilletti", // TriggerProfile
    "Standard", // TriggerProfileStd
    "Sparatutto (Hair Trigger)", // TriggerProfileShooter
    "Corsa (ABS)", // TriggerProfileRacing
    "La memoria %s non è scrivibile: accesso e impostazioni non verranno salvati", // StorageNotWritable
    "FTP: esegui 'chmod 777 %s' o elimina la cartella e riavvia", // StorageFixHint
    "Personalizza indicatori",                        // StatsCustomize
    "FPS e risoluzione",                              // StatsIndicatorFps
    "Bitrate",                                        // StatsIndicatorBitrate
    "Latenza (RTT)",                                  // StatsIndicatorLatency
    "Perdita pacchetti",                              // StatsIndicatorLoss
    "Grafico frametime",                              // StatsIndicatorGraph
    "Latenza di decodifica e visualizzazione",        // StatsIndicatorDelay
    "Qualità video (QP)",                             // StatsIndicatorQp
    "Stato microfono",                                // StatsIndicatorMic
    "Inquadra il codice QR con lo smartphone per risolvere con Microsoft, poi premi Croce per riprovare",  // ScanQrToResolve
    "Ottieni gratis",                                 // ClaimFree
    "Acquisizione licenza gratuita in corso...",      // Claiming
    "Gioco aggiunto al tuo account con successo!",    // ClaimSuccess
    "Impossibile ottenere il gioco gratuito: %s",     // ClaimFailed
    "Questo gioco non è gratuito nello store",        // ProductNotFree
    "Gioco gratuito. Premi Croce per aggiungerlo al tuo account e iniziare a giocare",  // FreeAcquireHint
    "Risoluzione DNS non riuscita per i server Microsoft. Su PS5, assicurati di eseguire NanoDNS e impostare il DNS primario sull'IP locale per bloccare Sony mantenendo l'accesso a Internet.", // DnsBlockedNanoDns
    "Questo account Microsoft non ha ancora un profilo Xbox. Accedi a xbox.com su smartphone o PC per creare il tuo Gamertag, quindi riprova.", // XboxProfileMissing
    "Coda pronta! Avvio del gioco...",                // QueueReadyAlert
    "Segnale acustico di fine coda",                  // QueueReadyNotification
    "Giocato: %s",                                    // PlaytimePlayed
    "Aggiorna",                                       // RefreshLibrary
    "Aggiornamento libreria...",                      // RefreshingLibrary
    "Questo account Xbox è sospeso o bloccato. Visita enforcement.xbox.com per i dettagli.", // XboxAccountBanned
    "Xbox Live non è offerto nel paese o nella regione di questo account.", // XboxRegionNotSupported
    "È richiesta la verifica dell'età o l'accettazione dei termini. Accedi a xbox.com in un browser, poi riprova.", // XboxAdultVerificationRequired
    "Account bambino: un adulto deve aggiungere questo account a una famiglia Microsoft su account.microsoft.com/family.", // XboxChildAccountFamily
    "Verifica il mio accesso",                        // CheckSignIn
    "Verifica",                                       // RunCheck
    "Test di rete",                                   // NetworkTest
    "Testa",                                          // RunTest
    "Obiettivi",                                      // Achievements
    "Nessun obiettivo",                               // NoAchievements
    "Bloccato",                                       // AchievementLocked
    "Obiettivo segreto",                              // SecretAchievement
    "Lista dei desideri",                             // Wishlist
    "Aggiungi ai desideri",                           // AddWishlist
    "Rimuovi dai desideri",                           // RemoveWishlist
    "Aggiunto ai desideri",                           // AddedToWishlist
    "Rimosso dai desideri",                           // RemovedFromWishlist
    "%s è in saldo!",                                 // WishlistOnSale
    "%s è arrivato su Game Pass!",                    // WishlistInGamePass
    "Supporta il progetto",                           // SupportProject
    "Supporta lo Sviluppo di PSBox",                  // SupportTitle
    "PSBox è un progetto open source sviluppato con passione per portare la migliore esperienza di Xbox Cloud Gaming su PlayStation 5. Se ti piace l'app e vuoi sostenere lo sviluppo continuo e le nuove funzionalità, considera di offrirmi un caffè su Ko-Fi!", // SupportMessage
    "Ko-fi",                                          // SupportKoFi
    "Inquadra con lo smartphone",                     // ScanToDonate
    "Prova suono",                                    // TestSound
    "La lista dei desideri è vuota",                  // WishlistEmpty
    // Loading tips
    "Touchpad: scorri ↓ (o Touchpad + Options) per il menu, ↑ per il pulsante Xbox", // TipTouchpadMenu
    "Tastiera e mouse USB collegati alla console sono supportati nei giochi cloud compatibili", // TipKeyboardMouse
    "Ottieni giochi gratuiti, demo e promozioni del negozio direttamente dalla libreria dell'app", // TipFreeStore
    "Hai riscontrato un problema? Apri Segnala problema per scansionare un QR code con i log della console", // TipBugReportQr
    "Ti piace il progetto? Sostieni lo sviluppo futuro tramite Ko-fi nella scheda Impostazioni!", // TipSupportKofi
    "Abilita l'uscita video a 120 Hz nelle Impostazioni per una latenza di visualizzazione ridotta", // Tip120HzVideo
    "Calibra la zona morta delle levette nelle Impostazioni per eliminare il drift e migliorare la precisione", // TipAnalogDeadzone
    "Usa il microfono integrato del DualSense per la chat vocale nei gruppi e nelle partite multigiocatore", // TipDualSenseMic
    // Settings descriptions
    "Modifica la lingua dell'interfaccia dell'applicazione e le preferenze di audio e sottotitoli nei giochi cloud", // SettingDescLanguage
    "Imposta la risoluzione video massima (1080p, 720p o Migliore disponibile). Ha effetto al prossimo avvio", // SettingDescResolution
    "Seleziona la regione dei server Microsoft per lo streaming (Automatica o specifica). Ha effetto al prossimo avvio", // SettingDescRegion
    "Regola la zona morta interna delle levette analogiche DualSense per evitare drift e movimenti involontari", // SettingDescDeadzone
    "Abilita o disabilita l'emulazione della vibrazione sui grilletti adattivi L2 e R2 nei giochi compatibili", // SettingDescTriggerRumble
    "Alterna la configurazione dei tasti conferma e indietro tra Croce (occidentale) e Cerchio (giapponese)", // SettingDescConfirmButton
    "Configura il colore e il comportamento della barra luminosa del DualSense nei menu e durante il gioco", // SettingDescLightBar
    "Verifica e installa automaticamente gli aggiornamenti ufficiali dell'app direttamente sulla console", // SettingDescUpdates
    "Consente di accedere al proprio account Xbox tramite un'altra regione per accedere a cataloghi alternativi", // SettingDescLoginCountry
    "Abilita l'uscita video a 120 Hz sulla console per un ritardo di visualizzazione e latenza di input ridotti", // SettingDescHighRefresh
    "Imposta il limite massimo di bitrate per lo streaming video, garantendo alta fedeltà o risparmio dati", // SettingDescBitrateCap
    "Regola il livello del volume audio principale durante le sessioni di gioco in streaming", // SettingDescGameVolume
    "Mappa i tasti della tastiera USB o Bluetooth sui pulsanti corrispondenti del controller Xbox durante il gioco", // SettingDescKeyboardKeys
    "Alterna tra modalità finestra e schermo intero sulle piattaforme desktop supportate", // SettingDescDisplayMode
    "Apre la schermata di test e calibrazione di tutti i pulsanti e gli assi del controller collegato", // SettingDescControllerButtons
    "Abilita i sensori di movimento DualSense per assistere la mira analogica negli sparatutto e giochi d'azione", // SettingDescGyro
    "Riproduce un segnale sonoro caratteristico quando l'attesa nella coda di xCloud termina e il gioco è pronto", // SettingDescQueueReady
    "Esegue una diagnostica completa dell'accesso a Xbox Live, convalidando token di accesso, profilo e abbonamenti", // SettingDescCheckSignIn
    "Misura il ping (RTT) e il jitter verso i data center di gioco in cloud in diverse regioni globali", // SettingDescNetworkTest
    "Mostra un codice QR per supportare lo sviluppatore tramite Ko-fi e contribuire a mantenere il progetto attivo", // SettingDescSupportProject
    "Novità",                                         // WhatsNew
    "Novità",                                         // WhatsNewTitle
    "Altoparlante del controller",                    // PadSpeakerVolume
    "Muto",                                           // Mute
    "Controllo Web",                                  // WebControlTitle
    "Connetti dallo smartphone",                      // WebControlSubtitle
    "Connettiti allo stesso Wi-Fi per cercare, rimappare pulsanti, usare il telefono come controller virtuale, segnalare problemi o visualizzare i log.", // WebControlDesc
    "Decodifica video",                               // VideoDecoder
    "Hardware (Consigliato)",                         // VideoDecoderHw
    "Software (CPU)",                                 // VideoDecoderSw
    "Seleziona decodifica hardware (libSceVideodec2 su PS5, HWAccel su Desktop) o software (CPU)", // SettingDescVideoDecoder
    "Audio a vibrazione aptica",                       // AudioHaptics
    "Converte le basse frequenze audio in vibrazioni aptiche su DualSense", // SettingDescAudioHaptics
    "Scorciatoie del touchpad",                       // TouchpadShortcuts
    "Configura i gesti di scorrimento e i clic sinistro/destro sul touchpad DualSense", // SettingDescTouchpadShortcuts
    "Scorri in alto (↑)",                             // TouchpadSwipeUp
    "Scorri in basso (↓)",                            // TouchpadSwipeDown
    "Scorri a sinistra (←)",                          // TouchpadSwipeLeft
    "Scorri a destra (→)",                            // TouchpadSwipeRight
    "Clic sinistro",                                  // TouchpadClickLeft
    "Clic destro",                                    // TouchpadClickRight
    "Disattivato",                                    // TouchpadActNone
    "Menu di gioco",                                  // TouchpadActGameMenu
    "Pulsante Xbox",                                  // TouchpadActXboxButton
    "Mostra/nascondi statistiche",                    // TouchpadActToggleStats
    "Disattiva/attiva microfono",                     // TouchpadActToggleMic
    "Cambia modalità giroscopio",                     // TouchpadActCycleGyro
    "Cambia profilo grilletto",                       // TouchpadActCycleTrigger
    "Touchpad + Options apre sempre il menu di gioco", // TouchpadHelp
    "Ripristina impostazioni predefinite",            // ResetSettings
    "Ripristina tutte le impostazioni ai valori di fabbrica. Tieni premuto ✕ per 5 secondi per confermare.", // SettingDescResetSettings
    "Impostazioni ripristinate ai valori predefiniti.", // SettingsResetNotice
    "Tieni premuto per ripristinare",                 // HoldToReset
    "La libreria è già aggiornata",                   // LibraryUpToDate
    "Libreria aggiornata (%s nuovi giochi)",          // LibraryUpdatedCount
    "Modalità non in linea",                          // OfflineMode
};

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
