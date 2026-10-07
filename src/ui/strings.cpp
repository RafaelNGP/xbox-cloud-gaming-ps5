#include "ui/strings.h"

#include <array>

namespace xc::ui {

namespace {

Language g_language = Language::English;

constexpr std::array<const char*, static_cast<size_t>(Str::Count)> kEnglish = {
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
};

}  // namespace

void setLanguage(Language lang) { g_language = lang; }
Language language() { return g_language; }

const char* catalogLanguage() {
    switch (g_language) {
        case Language::English: break;
    }
    return "en-us";
}

const char* tr(Str id) {
    auto i = static_cast<size_t>(id);
    return i < kEnglish.size() ? kEnglish[i] : "";
}

std::string trf(Str id, const std::string& arg) {
    std::string s = tr(id);
    size_t at = s.find("%s");
    if (at != std::string::npos) s.replace(at, 2, arg);
    return s;
}

}  // namespace xc::ui
