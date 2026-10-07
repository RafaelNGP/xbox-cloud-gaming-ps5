// PS5 front end: on-screen device-code sign-in, the streamable title list,
// and playing a title (queue -> /connect -> Provisioned -> StreamPlayer).
//
// Network work runs on one worker thread that owns AuthManager/GssvClient;
// the main thread only reads the pad and draws a snapshot of `Ui`.
#include "app/stream_player.h"
#include "auth/auth_manager.h"
#include "display/display.h"
#include "input/controller.h"
#include "net/http.h"
#include "platform/platform.h"
#include "util/log.h"
#include "xcloud/gssv.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace xc;
using display::Color;

extern "C" int sceSystemServiceHideSplashScreen(void);

namespace {

enum class Screen { Busy, DeviceCode, Titles, Session, Streaming, Error };

struct Ui {
    Screen screen = Screen::Busy;
    std::string status;  // Busy / Session / Error text
    bool sessionRunning = false;
    auth::DeviceCode code;
    std::vector<xcloud::Title> titles;
    std::string gamertag;
    std::string region;
};

std::mutex g_uiMutex;
Ui g_ui;
std::atomic<uint64_t> g_uiVersion{1};

template <class F>
void updateUi(F&& f) {
    {
        std::lock_guard<std::mutex> lock(g_uiMutex);
        f(g_ui);
    }
    ++g_uiVersion;
}

void setBusy(const std::string& text) {
    XC_LOGI("%s", text.c_str());
    updateUi([&](Ui& ui) {
        ui.screen = Screen::Busy;
        ui.status = text;
    });
}

void setError(const std::string& text) {
    XC_LOGE("%s", text.c_str());
    updateUi([&](Ui& ui) {
        ui.screen = Screen::Error;
        ui.status = text;
    });
}

// --- Worker ----------------------------------------------------------------

enum Command { kNone, kSignIn, kProvision, kSignOut };

std::atomic<int> g_command{kSignIn};
std::atomic<bool> g_cancel{false};
std::mutex g_argMutex;
std::string g_provisionTitle;

// The running stream, for the input thread.
std::mutex g_playerMutex;
app::StreamPlayer* g_player = nullptr;

void setSession(const std::string& text, bool running);

void play(xcloud::GssvClient& gssv, std::string& result) {
    setSession("Conectando o stream...", true);
    app::StreamPlayer player(gssv);
    std::string err;
    if (!player.start(err)) {
        result = "ERRO ao conectar o stream: " + err;
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_playerMutex);
        g_player = &player;
    }
    updateUi([](Ui& ui) { ui.screen = Screen::Streaming; });
    platform::notify("Segure OPTIONS + TOUCHPAD para sair do jogo");
    uint64_t nextTick = platform::nowMs();
    while (player.running() && !g_cancel) {
        if (platform::nowMs() >= nextTick) {
            nextTick += 1000;
            player.tick();
            auto st = player.stats();
            XC_LOGD("stream: %llu frames, %llu decoded, %llu skipped, %llu audio",
                    static_cast<unsigned long long>(st.videoFrames), static_cast<unsigned long long>(st.decodedFrames),
                    static_cast<unsigned long long>(st.droppedFrames), static_cast<unsigned long long>(st.audioPackets));
        }
        platform::sleepMs(100);
    }
    {
        std::lock_guard<std::mutex> lock(g_playerMutex);
        g_player = nullptr;
    }
    std::string reason = g_cancel ? "voce saiu do jogo" : player.endReason();
    player.stop();
    auto st = player.stats();
    result = "Stream encerrado (" + reason + "). " + std::to_string(st.decodedFrames) + " quadros exibidos.";
}

void doSignIn(auth::AuthManager& am, xcloud::GssvClient& gssv) {
    setBusy(am.hasStoredAccount() ? "Entrando na conta salva..." : "Pedindo codigo de login...");
    std::string err;
    auto onCode = [](const auth::DeviceCode& dc) {
        updateUi([&](Ui& ui) {
            ui.screen = Screen::DeviceCode;
            ui.code = dc;
        });
    };
    if (!am.signIn(gssv, onCode, err, &g_cancel)) {
        setError("Falha no login: " + err);
        return;
    }
    updateUi([&](Ui& ui) {
        ui.gamertag = am.profile().gamertag;
        ui.region = gssv.region().name;
    });
    platform::notify("Conectado como " + am.profile().gamertag);

    setBusy("Carregando jogos...");
    std::vector<xcloud::Title> titles;
    if (!gssv.listTitles(titles, err, true) || titles.empty()) {
        // The recent list is short and fast; fall back to the full catalog.
        if (!gssv.listTitles(titles, err, false)) {
            setError("Falha ao listar jogos: " + err);
            return;
        }
    }
    std::string market = gssv.session().market.empty() ? "US" : gssv.session().market;
    if (!gssv.hydrateTitles(titles, market, "en-us", err)) XC_LOGW("%s", err.c_str());
    XC_LOGI("%zu titles", titles.size());
    updateUi([&](Ui& ui) {
        ui.titles = std::move(titles);
        ui.screen = Screen::Titles;
    });
}

void setSession(const std::string& text, bool running) {
    XC_LOGI("session: %s", text.c_str());
    updateUi([&](Ui& ui) {
        ui.screen = Screen::Session;
        ui.status = text;
        ui.sessionRunning = running;
    });
}

void doProvision(auth::AuthManager& am, xcloud::GssvClient& gssv, const std::string& titleId) {
    setSession("Iniciando sessao para " + titleId + "...", true);
    std::string err;
    if (!gssv.startSession(titleId, "en-US", err)) {
        setSession("ERRO: " + err, false);
        return;
    }
    bool connected = false;
    std::string result = "Tempo esgotado esperando a sessao";
    for (int i = 0; i < 600 && !g_cancel; ++i) {
        xcloud::SessionStatus st;
        if (!gssv.sessionState(st, err)) {
            result = "ERRO: " + err;
            break;
        }
        if (st.state == xcloud::SessionState::WaitingForResources) {
            setSession("Na fila, espera estimada " + std::to_string(gssv.waitTimeSeconds()) + "s", true);
        } else {
            setSession("Estado da sessao: " + st.raw, true);
        }
        if (st.state == xcloud::SessionState::Failed) {
            result = "Sessao falhou: " + st.errorCode + " " + st.errorMessage;
            break;
        }
        if (st.state == xcloud::SessionState::ReadyToConnect && !connected) {
            std::string transfer;
            if (!am.consoleTransferToken(transfer, err) || !gssv.connect(transfer, err)) {
                result = "ERRO: " + err;
                break;
            }
            connected = true;
        }
        if (st.state == xcloud::SessionState::Provisioned) {
            play(gssv, result);
            break;
        }
        platform::sleepMs(1000);
    }
    if (g_cancel && result.rfind("Stream", 0) != 0) result = "Cancelado";
    gssv.stopSession();
    setSession(result, false);
}

void worker() {
    auth::AuthManager am(platform::dataDir() + "/account.json");
    xcloud::GssvClient gssv;
    for (;;) {
        int cmd = g_command.exchange(kNone);
        g_cancel = false;
        switch (cmd) {
            case kSignIn: doSignIn(am, gssv); break;
            case kSignOut:
                am.signOut();
                doSignIn(am, gssv);
                break;
            case kProvision: {
                std::string id;
                {
                    std::lock_guard<std::mutex> lock(g_argMutex);
                    id = g_provisionTitle;
                }
                doProvision(am, gssv, id);
                break;
            }
            default: platform::sleepMs(50); break;
        }
    }
}

// --- Drawing ---------------------------------------------------------------

constexpr unsigned kMargin = 80;

unsigned textWidth(size_t chars, unsigned scale) { return static_cast<unsigned>(chars) * 6 * scale; }

void drawCentered(display::Canvas& c, unsigned y, const std::string& text, unsigned scale, Color color) {
    unsigned w = textWidth(text.size(), scale);
    c.drawText(w < display::kWidth ? (display::kWidth - w) / 2 : 0, y, text, scale, color);
}

// Word-wraps `text` to `maxChars` per line and draws it; returns the next y.
unsigned drawWrapped(display::Canvas& c, unsigned x, unsigned y, const std::string& text, unsigned scale,
                     Color color, size_t maxChars) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t len = std::min(maxChars, text.size() - pos);
        if (pos + len < text.size()) {
            size_t sp = text.rfind(' ', pos + len);
            if (sp != std::string::npos && sp > pos) len = sp - pos;
        }
        c.drawText(x, y, text.substr(pos, len), scale, color);
        y += 10 * scale;
        pos += len;
        while (pos < text.size() && text[pos] == ' ') ++pos;
    }
    return y;
}

void drawFrame(const Ui& ui, int selected, int scroll) {
    display::Canvas c = display::getBackBuffer();
    c.clear(Color::DarkSlate);

    c.fillRect(0, 0, display::kWidth, 110, Color::HeaderBar);
    c.fillRect(0, 106, display::kWidth, 4, Color::XboxGreen);
    c.drawText(kMargin, 38, "XBOX CLOUD GAMING", 5, Color::White);
    if (!ui.gamertag.empty()) {
        std::string who = ui.gamertag + (ui.region.empty() ? "" : "  (" + ui.region + ")");
        c.drawText(display::kWidth - kMargin - textWidth(who.size(), 3), 46, who, 3, Color::GrayText);
    }

    const unsigned footerY = display::kHeight - 70;
    std::string footer;
    switch (ui.screen) {
        case Screen::Busy:
            drawCentered(c, 480, ui.status, 4, Color::LightGray);
            break;
        case Screen::DeviceCode:
            drawCentered(c, 260, "No celular ou PC, abra:", 4, Color::GrayText);
            drawCentered(c, 340, ui.code.verificationUri, 6, Color::White);
            drawCentered(c, 470, "e digite o codigo:", 4, Color::GrayText);
            c.fillRect(560, 550, 800, 170, Color::CardBg);
            drawCentered(c, 595, ui.code.userCode, 14, Color::LightGreen);
            drawCentered(c, 800, "Aguardando login...", 3, Color::GrayText);
            footer = "O: cancelar";
            break;
        case Screen::Titles: {
            constexpr unsigned kTop = 150, kRow = 54;
            const int visible = static_cast<int>((footerY - 20 - kTop) / kRow);
            for (int i = 0; i < visible && scroll + i < static_cast<int>(ui.titles.size()); ++i) {
                const auto& t = ui.titles[static_cast<size_t>(scroll + i)];
                unsigned y = kTop + static_cast<unsigned>(i) * kRow;
                bool sel = scroll + i == selected;
                c.fillRect(kMargin, y, display::kWidth - 2 * kMargin, kRow - 6, sel ? Color::CardSelected : Color::CardBg);
                c.drawText(kMargin + 20, y + 13, t.name.empty() ? t.titleId : t.name, 3,
                           sel ? Color::White : Color::LightGray);
            }
            footer = "X: jogar    OPTIONS: sair da conta    " + std::to_string(selected + 1) + "/" +
                     std::to_string(ui.titles.size());
            break;
        }
        case Screen::Session:
            drawCentered(c, 300, "SESSAO", 5, Color::White);
            drawWrapped(c, kMargin + 100, 450, ui.status, 4, Color::LightGray, 60);
            footer = ui.sessionRunning ? "O: cancelar" : "X ou O: voltar";
            break;
        case Screen::Streaming: break;  // the video thread owns the screen
        case Screen::Error:
            drawCentered(c, 300, "ERRO", 6, Color::White);
            drawWrapped(c, kMargin + 40, 420, ui.status, 3, Color::LightGray, 90);
            footer = "X: tentar novamente    OPTIONS: sair da conta";
            break;
    }
    if (!footer.empty()) c.drawText(kMargin, footerY, footer, 3, Color::GrayText);
    display::present();
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    platform::init();
    log::setFile((platform::dataDir() + "/xcloud.log").c_str());
    XC_LOGI("=== xCloud PS5 starting ===");
    sceSystemServiceHideSplashScreen();

    bool haveDisplay = display::init();
    if (!input::init()) XC_LOGE("controller init failed");

    if (!net::initTls(platform::caBundlePath())) {
        platform::notify("xCloud: erro ao carregar certificados TLS");
        setError("Nao foi possivel carregar " + platform::caBundlePath());
    } else {
        std::thread(worker).detach();
    }

    // Never return from main: the app is closed from the home screen.
    input::ControllerState prev{}, pad{};
    int selected = 0, scroll = 0;
    uint64_t drawn = 0;
    uint64_t exitHeldSince = 0;
    for (;;) {
        input::poll(pad);
        auto pressed = [&](bool input::ControllerState::*b) { return pad.*b && !(prev.*b); };

        Screen screen;
        size_t titleCount;
        bool running;
        {
            std::lock_guard<std::mutex> lock(g_uiMutex);
            screen = g_ui.screen;
            titleCount = g_ui.titles.size();
            running = g_ui.sessionRunning;
        }

        bool moved = false;
        switch (screen) {
            case Screen::DeviceCode:
                if (pressed(&input::ControllerState::btnB)) g_cancel = true;
                break;
            case Screen::Titles: {
                int n = static_cast<int>(titleCount);
                if (pressed(&input::ControllerState::dpadDown) && selected + 1 < n) ++selected, moved = true;
                if (pressed(&input::ControllerState::dpadUp) && selected > 0) --selected, moved = true;
                if (pressed(&input::ControllerState::dpadRight)) selected = std::min(n - 1, selected + 10), moved = true;
                if (pressed(&input::ControllerState::dpadLeft)) selected = std::max(0, selected - 10), moved = true;
                if (pressed(&input::ControllerState::btnA) && n > 0) {
                    std::lock_guard<std::mutex> lock(g_uiMutex);
                    std::lock_guard<std::mutex> lock2(g_argMutex);
                    g_provisionTitle = g_ui.titles[static_cast<size_t>(selected)].titleId;
                    g_command = kProvision;
                }
                if (pressed(&input::ControllerState::btnOptions)) g_command = kSignOut;
                break;
            }
            case Screen::Session:
                if (running && pressed(&input::ControllerState::btnB)) g_cancel = true;
                if (!running && (pressed(&input::ControllerState::btnA) || pressed(&input::ControllerState::btnB)))
                    updateUi([](Ui& ui) { ui.screen = Screen::Titles; });
                break;
            case Screen::Error:
                if (pressed(&input::ControllerState::btnA)) g_command = kSignIn;
                if (pressed(&input::ControllerState::btnOptions)) g_command = kSignOut;
                break;
            case Screen::Streaming: {
                {
                    std::lock_guard<std::mutex> lock(g_playerMutex);
                    if (g_player) g_player->sendInput(pad);
                }
                // OPTIONS + TOUCHPAD held for a second leaves the game.
                if (pad.btnOptions && pad.btnTouchpad) {
                    if (!exitHeldSince) exitHeldSince = platform::nowMs();
                    if (platform::nowMs() - exitHeldSince > 1000) g_cancel = true;
                } else {
                    exitHeldSince = 0;
                }
                break;
            }
            case Screen::Busy: break;
        }
        prev = pad;

        if (moved) {
            constexpr int kVisible = 15;
            if (selected < scroll) scroll = selected;
            if (selected >= scroll + kVisible) scroll = selected - kVisible + 1;
        }

        uint64_t version = g_uiVersion.load();
        if (screen == Screen::Streaming) {
            platform::sleepMs(8);  // ~120 Hz input polling
        } else if (haveDisplay && (version != drawn || moved)) {
            Ui snapshot;
            {
                std::lock_guard<std::mutex> lock(g_uiMutex);
                snapshot = g_ui;
            }
            std::lock_guard<std::mutex> lock(display::frameMutex());
            if (snapshot.screen != Screen::Streaming) drawFrame(snapshot, selected, scroll);  // waits for vblank
            drawn = version;
        } else {
            platform::sleepMs(16);
        }
    }
}
