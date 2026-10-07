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
    "Hold OPTIONS + TOUCHPAD to leave the game",      // LeaveHint
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
    "Resolution and region apply to the next game you start.",  // SettingsNote
    "Change",                                         // Change
    "Sign-in failed: %s",                             // SignInFailed
    "Could not load the game list: %s",               // LibraryFailed
    "Could not start the stream: %s",                 // StreamFailed
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
    "Segure OPTIONS + TOUCHPAD para sair do jogo",
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
    "A resolução e a região valem a partir do próximo jogo.",
    "Alterar",
    "Falha ao entrar: %s",
    "Não foi possível carregar a lista de jogos: %s",
    "Não foi possível iniciar o streaming: %s",
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
    "Mantén OPTIONS + TOUCHPAD para salir del juego",
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
    "La resolución y la región se aplican al próximo juego que inicies.",
    "Cambiar",
    "Error al iniciar sesión: %s",
    "No se pudo cargar la lista de juegos: %s",
    "No se pudo iniciar el streaming: %s",
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
    "Maintenez OPTIONS + PAVÉ TACTILE pour quitter le jeu",
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
    "La résolution et la région s'appliquent au prochain jeu lancé.",
    "Modifier",
    "Échec de la connexion : %s",
    "Impossible de charger la liste des jeux : %s",
    "Impossible de démarrer le streaming : %s",
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
    "OPTIONS + TOUCHPAD gedrückt halten, um das Spiel zu verlassen",
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
    "Auflösung und Region gelten ab dem nächsten gestarteten Spiel.",
    "Ändern",
    "Anmeldung fehlgeschlagen: %s",
    "Die Spieleliste konnte nicht geladen werden: %s",
    "Das Streaming konnte nicht gestartet werden: %s",
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
    "Tieni premuti OPTIONS + TOUCHPAD per uscire dal gioco",
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
    "Risoluzione e regione valgono dal prossimo gioco avviato.",
    "Cambia",
    "Accesso non riuscito: %s",
    "Impossibile caricare l'elenco dei giochi: %s",
    "Impossibile avviare lo streaming: %s",
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
