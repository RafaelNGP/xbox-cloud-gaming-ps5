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
    "Press OPTIONS + TOUCHPAD for the game menu",      // LeaveHint
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
    "1440p (experimental, where available)",          // Res1440
    "Resolution and region apply to the next game you start.",  // SettingsNote
    "Change",                                         // Change
    "Hold to sign out",                               // HoldSignOut
    "Keep holding to sign out (%s)",                  // SigningOutIn
    "Sign-in failed: %s",                             // SignInFailed
    "Could not load the game list: %s",               // LibraryFailed
    "Could not start the stream: %s",                 // StreamFailed
    "No input for a while: you will be disconnected in %s seconds",  // IdleWarning
    "Your games",                                     // YourGames
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
    "Buy it on xbox.com or in the Xbox app; it then shows up in Your games.", // BuyHint
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
    "On sale",                                       // FilterSale
    "All consoles",                                  // FilterAllConsoles
    "1 game",                                        // OneGame
    "Sections",                                      // Sections
    "Updating the game list...",                     // UpdatingList
    "Game menu",                                     // MenuTitle
    "Resume",                                        // MenuResume
    "Statistics",                                    // MenuStats
    "On",                                            // On
    "Off",                                           // Off
    "Stream resolution",                             // MenuResolution
    "Refresh the picture",                           // MenuRefresh
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
    "Cross",                                     // ButtonCross
    "Circle",                                    // ButtonCircle
    "PSBox Cloud Gaming %s is out: get it on GitHub (RafaelNGP/xbox-cloud-gaming-ps5)",// UpdateAvailable
    "Controllers",                                   // Controllers
    "Controller %s connected",                       // PadConnected
    "Controller %s disconnected",                    // PadDisconnected
    "Block smoothing",                               // MenuDeband
    "On screen after",                               // StatOnScreen
    "Connection lost: reconnecting...",              // Reconnecting
    "Reconnected",                                   // Reconnected
    "Light bar in the game's colour",                // LightBar
    "Upscaling",                                     // MenuUpscaler
    "AI (Anime4K)",                                  // UpscalerAi
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
    "Pressione OPTIONS + TOUCHPAD para abrir o menu do jogo",
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
    "1440p (experimental, onde disponível)",
    "A resolução e a região valem a partir do próximo jogo.",
    "Alterar",
    "Segure para sair da conta",
    "Continue segurando para sair da conta (%s)",
    "Falha ao entrar: %s",
    "Não foi possível carregar a lista de jogos: %s",
    "Não foi possível iniciar o streaming: %s",
    "Sem atividade: você será desconectado em %s segundos",
    "Seus jogos",
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
    "Compre em xbox.com ou no app Xbox; depois ele aparece em Seus jogos.",
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
    "Em promoção",
    "Todos os consoles",
    "1 jogo",
    "Seções",
    "Atualizando a lista de jogos...",
    "Menu do jogo",
    "Continuar",
    "Estatísticas",
    "Ligado",
    "Desligado",
    "Resolução do stream",
    "Atualizar a imagem",
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
    "Xis",
    "Círculo",
    "Saiu o PSBox Cloud Gaming %s: baixe no GitHub (RafaelNGP/xbox-cloud-gaming-ps5)",
    "Controles",
    "Controle %s conectado",
    "Controle %s desconectado",
    "Suavização de blocos",
    "Na tela após",
    "Conexão perdida: reconectando...",
    "Reconectado",
    "Barra de luz com a cor do jogo",
    "Ampliação",
    "IA (Anime4K)",
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
    "Pulsa OPTIONS + TOUCHPAD para abrir el menú del juego",
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
    "1440p (experimental, donde esté disponible)",
    "La resolución y la región se aplican al próximo juego que inicies.",
    "Cambiar",
    "Mantén para cerrar sesión",
    "Sigue manteniendo para cerrar sesión (%s)",
    "Error al iniciar sesión: %s",
    "No se pudo cargar la lista de juegos: %s",
    "No se pudo iniciar el streaming: %s",
    "Sin actividad: se te desconectará en %s segundos",
    "Tus juegos",
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
    "Cómpralo en xbox.com o en la app de Xbox; luego aparecerá en Tus juegos.",
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
    "En oferta",
    "Todas las consolas",
    "1 juego",
    "Secciones",
    "Actualizando la lista de juegos...",
    "Menú del juego",
    "Continuar",
    "Estadísticas",
    "Activado",
    "Desactivado",
    "Resolución del stream",
    "Actualizar la imagen",
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
    "Equis",
    "Círculo",
    "Ya está disponible PSBox Cloud Gaming %s: descárgalo en GitHub (RafaelNGP/xbox-cloud-gaming-ps5)",
    "Mandos",
    "Mando %s conectado",
    "Mando %s desconectado",
    "Suavizado de bloques",
    "En pantalla tras",
    "Conexión perdida: reconectando...",
    "Reconectado",
    "Barra de luz con el color del juego",
    "Escalado",
    "IA (Anime4K)",
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
    "Appuyez sur OPTIONS + PAVÉ TACTILE pour le menu du jeu",
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
    "1440p (expérimental, si disponible)",
    "La résolution et la région s'appliquent au prochain jeu lancé.",
    "Modifier",
    "Maintenez pour vous déconnecter",
    "Continuez à maintenir pour vous déconnecter (%s)",
    "Échec de la connexion : %s",
    "Impossible de charger la liste des jeux : %s",
    "Impossible de démarrer le streaming : %s",
    "Aucune activité : vous serez déconnecté dans %s secondes",
    "Vos jeux",
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
    "Achetez-le sur xbox.com ou dans l'app Xbox ; il apparaîtra ensuite dans Vos jeux.",
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
    "En promotion",
    "Toutes les consoles",
    "1 jeu",
    "Sections",
    "Mise \xC3\xA0 jour de la liste des jeux...",
    "Menu du jeu",
    "Reprendre",
    "Statistiques",
    "Activé",
    "Désactivé",
    "Résolution du stream",
    "Actualiser l'image",
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
    "Croix",
    "Rond",
    "PSBox Cloud Gaming %s est disponible : téléchargez-le sur GitHub (RafaelNGP/xbox-cloud-gaming-ps5)",
    "Manettes",
    "Manette %s connectée",
    "Manette %s déconnectée",
    "Lissage des blocs",
    "À l'écran après",
    "Connexion perdue : reconnexion...",
    "Reconnecté",
    "Barre lumineuse aux couleurs du jeu",
    "Mise à l'échelle",
    "IA (Anime4K)",
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
    "OPTIONS + TOUCHPAD drücken, um das Spielmenü zu öffnen",
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
    "1440p (experimentell, wo verfügbar)",
    "Auflösung und Region gelten ab dem nächsten gestarteten Spiel.",
    "Ändern",
    "Halten zum Abmelden",
    "Weiter halten zum Abmelden (%s)",
    "Anmeldung fehlgeschlagen: %s",
    "Die Spieleliste konnte nicht geladen werden: %s",
    "Das Streaming konnte nicht gestartet werden: %s",
    "Keine Aktivität: Die Verbindung wird in %s Sekunden getrennt",
    "Deine Spiele",
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
    "Kaufe es auf xbox.com oder in der Xbox-App; danach erscheint es unter Deine Spiele.",
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
    "Im Angebot",
    "Alle Konsolen",
    "1 Spiel",
    "Abschnitte",
    "Spieleliste wird aktualisiert...",
    "Spielmenü",
    "Fortsetzen",
    "Statistiken",
    "An",
    "Aus",
    "Stream-Auflösung",
    "Bild aktualisieren",
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
    "Kreuz",
    "Kreis",
    "PSBox Cloud Gaming %s ist da: auf GitHub herunterladen (RafaelNGP/xbox-cloud-gaming-ps5)",
    "Controller",
    "Controller %s verbunden",
    "Controller %s getrennt",
    "Blockglättung",
    "Auf dem Bildschirm nach",
    "Verbindung verloren: neu verbinden...",
    "Wieder verbunden",
    "Lichtleiste in der Farbe des Spiels",
    "Hochskalierung",
    "KI (Anime4K)",
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
    "Premi OPTIONS + TOUCHPAD per il menu di gioco",
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
    "1440p (sperimentale, dove disponibile)",
    "Risoluzione e regione valgono dal prossimo gioco avviato.",
    "Cambia",
    "Tieni premuto per uscire",
    "Continua a tenere premuto per uscire (%s)",
    "Accesso non riuscito: %s",
    "Impossibile caricare l'elenco dei giochi: %s",
    "Impossibile avviare lo streaming: %s",
    "Nessuna attività: verrai disconnesso tra %s secondi",
    "I tuoi giochi",
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
    "Acquistalo su xbox.com o nell'app Xbox; poi apparirà in I tuoi giochi.",
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
    "In offerta",
    "Tutte le console",
    "1 gioco",
    "Sezioni",
    "Aggiornamento dell'elenco dei giochi...",
    "Menu di gioco",
    "Riprendi",
    "Statistiche",
    "Attivo",
    "Disattivo",
    "Risoluzione dello stream",
    "Aggiorna l'immagine",
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
    "Croce",
    "Cerchio",
    "È disponibile PSBox Cloud Gaming %s: scaricalo da GitHub (RafaelNGP/xbox-cloud-gaming-ps5)",
    "Controller",
    "Controller %s connesso",
    "Controller %s disconnesso",
    "Attenuazione blocchi",
    "Sullo schermo dopo",
    "Connessione persa: riconnessione...",
    "Riconnesso",
    "Barra luminosa con il colore del gioco",
    "Upscaling",
    "IA (Anime4K)",
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
