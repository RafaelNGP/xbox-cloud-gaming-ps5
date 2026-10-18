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

}  // namespace xc::ui
