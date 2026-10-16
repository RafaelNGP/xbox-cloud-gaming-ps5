// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// display.h on SDL3's renderer (the desktop app): a resizable window showing
// the app's 1920x1080 picture letterboxed. SDL's render calls are only made
// on the main thread, so the threads that draw (the menu on the main thread,
// the video thread while streaming) hand their picture over instead: draw
// into a back frame under frameMutex(), present() swaps it into a one-slot
// mailbox (the newest wins, nobody waits for the screen), and pump(), each
// pass of the main loop, uploads the mailbox's frame (YUV straight into an
// IYUV texture, the GPU converts it) with the overlay and presents it,
// paced by vsync. No upscalers, sharpening or block smoothing here: caps()
// says so and their setters do nothing.
#include "display/display.h"
#include "display/display_sdl.h"

#include "platform/platform.h"
#include "util/log.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <utility>

namespace xc::display {

namespace {

enum class Kind { None, Menu, Video };

// A picture handed from a drawing thread to the main thread.
struct Frame {
    Kind kind = Kind::None;
    std::vector<uint32_t> rgba;    // Menu: kWidth x kHeight, 0xAABBGGRR
    std::vector<uint8_t> y, u, v;  // Video: the planes, rows packed
    int width = 0, height = 0;     // Video
    uint64_t id = 0;               // present()'s count when handed over
};

struct Overlay {
    std::vector<uint32_t> pixels;
    int x = 0, y = 0, w = 0, h = 0;
    uint8_t opacity = 0;
    bool on = false;
};

// Main thread only.
SDL_Window* g_window = nullptr;
SDL_Renderer* g_renderer = nullptr;
SDL_Texture* g_uiTex = nullptr;
SDL_Texture* g_videoTex = nullptr;
int g_videoW = 0, g_videoH = 0;
SDL_Texture* g_overlayTex = nullptr;
int g_overlayW = 0, g_overlayH = 0;
SDL_Texture* g_readTex = nullptr;  // readBackRgb()'s target
SDL_ThreadID g_mainThread = 0;
bool g_startFullscreen = false;
bool g_shown = false;  // the window shows from its first picture on
Frame g_front;         // the picture on screen
Overlay g_overlayShown;
bool g_frontPending = false, g_overlayPending = false;  // not uploaded yet

std::mutex g_frameMutex;  // display.h's: held around a draw and its present()
Frame g_back;             // being drawn (under g_frameMutex)

std::mutex g_mailboxMutex;
std::condition_variable g_mailboxCv;
Frame g_mailbox;  // the newest picture handed over
bool g_mailboxFull = false;
uint64_t g_handed = 0;
std::atomic<uint64_t> g_lastHanded{0};

std::mutex g_overlayMutex;
Overlay g_overlay;
bool g_overlayChanged = false;

std::atomic<bool> g_repaint{false};
std::mutex g_shownMutex;
uint64_t g_shownId = 0, g_shownAtUs = 0;  // the last video frame presented

bool onMainThread() { return g_mainThread != 0 && SDL_GetCurrentThreadID() == g_mainThread; }

// The video texture at the stream's size: BT.709 limited range, what the
// decoder gives (the GPU converts it).
bool videoTexture(int w, int h) {
    if (g_videoTex && g_videoW == w && g_videoH == h) return true;
    if (g_videoTex) SDL_DestroyTexture(g_videoTex);
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_IYUV);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STREAMING);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_BT709_LIMITED);
    g_videoTex = SDL_CreateTextureWithProperties(g_renderer, p);
    SDL_DestroyProperties(p);
    if (!g_videoTex) {
        g_videoW = g_videoH = 0;
        XC_LOGE("display: video texture %dx%d: %s", w, h, SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(g_videoTex, SDL_SCALEMODE_LINEAR);
    g_videoW = w, g_videoH = h;
    XC_LOGI("display: video texture %dx%d (BT.709 limited)", w, h);
    return true;
}

// What pump() took from the mailbox and the overlay, into the textures.
void upload() {
    if (g_frontPending) {
        g_frontPending = false;
        if (g_front.kind == Kind::Menu) {
            SDL_UpdateTexture(g_uiTex, nullptr, g_front.rgba.data(), static_cast<int>(kWidth * 4));
        } else if (g_front.kind == Kind::Video && videoTexture(g_front.width, g_front.height)) {
            int cw = (g_front.width + 1) / 2;
            SDL_UpdateYUVTexture(g_videoTex, nullptr, g_front.y.data(), g_front.width, g_front.u.data(), cw,
                                 g_front.v.data(), cw);
        }
    }
    if (g_overlayPending) {
        g_overlayPending = false;
        const Overlay& o = g_overlayShown;
        if (o.on) {
            if (!g_overlayTex || g_overlayW != o.w || g_overlayH != o.h) {
                if (g_overlayTex) SDL_DestroyTexture(g_overlayTex);
                g_overlayTex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, o.w, o.h);
                g_overlayW = g_overlayTex ? o.w : 0, g_overlayH = g_overlayTex ? o.h : 0;
                if (g_overlayTex) SDL_SetTextureBlendMode(g_overlayTex, SDL_BLENDMODE_BLEND);
            }
            if (g_overlayTex) {
                SDL_UpdateTexture(g_overlayTex, nullptr, o.pixels.data(), o.w * 4);
                SDL_SetTextureAlphaMod(g_overlayTex, o.opacity);
            }
        }
    }
}

// The picture on the current target, in the 1920x1080 logical space.
void renderScene() {
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    const SDL_FRect full{0, 0, static_cast<float>(kWidth), static_cast<float>(kHeight)};
    if (g_front.kind == Kind::Menu) {
        SDL_RenderTexture(g_renderer, g_uiTex, nullptr, &full);
    } else if (g_front.kind == Kind::Video && g_videoTex) {
        SDL_RenderTexture(g_renderer, g_videoTex, nullptr, &full);
        const Overlay& o = g_overlayShown;
        if (o.on && g_overlayTex) {
            SDL_FRect d{static_cast<float>(o.x), static_cast<float>(o.y), static_cast<float>(o.w), static_cast<float>(o.h)};
            SDL_RenderTexture(g_renderer, g_overlayTex, nullptr, &d);
        }
    }
}

}  // namespace

void sdlStartFullscreen(bool on) { g_startFullscreen = on; }
SDL_Window* sdlWindow() { return g_window; }
const char* sdlRendererName() { return g_renderer ? SDL_GetRendererName(g_renderer) : ""; }
void sdlRepaint() { g_repaint = true; }

void sdlToggleFullscreen() {
    if (!g_window) return;
    bool full = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (!SDL_SetWindowFullscreen(g_window, !full)) XC_LOGW("display: fullscreen: %s", SDL_GetError());
    XC_LOGI("display: %s", full ? "windowed" : "fullscreen");
    g_repaint = true;
}

std::mutex& frameMutex() { return g_frameMutex; }

bool init(bool) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        XC_LOGE("display: SDL video: %s", SDL_GetError());
        return false;
    }
    g_mainThread = SDL_GetCurrentThreadID();
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    if (g_startFullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    g_window = SDL_CreateWindow("PSBox Cloud Gaming", 1600, 900, flags);
    if (!g_window) {
        XC_LOGE("display: window: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    SDL_SetWindowMinimumSize(g_window, 640, 360);
    g_renderer = SDL_CreateRenderer(g_window, nullptr);
    if (!g_renderer) {
        XC_LOGE("display: renderer: %s", SDL_GetError());
        shutdown();
        return false;
    }
    if (!SDL_SetRenderVSync(g_renderer, 1)) XC_LOGW("display: no vsync (%s)", SDL_GetError());
    SDL_SetRenderLogicalPresentation(g_renderer, kWidth, kHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    g_uiTex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);
    if (!g_uiTex) {
        XC_LOGE("display: UI texture: %s", SDL_GetError());
        shutdown();
        return false;
    }
    SDL_SetTextureScaleMode(g_uiTex, SDL_SCALEMODE_LINEAR);
    XC_LOGI("display: SDL %d.%d.%d, video driver %s, renderer %s%s", SDL_VERSIONNUM_MAJOR(SDL_GetVersion()),
            SDL_VERSIONNUM_MINOR(SDL_GetVersion()), SDL_VERSIONNUM_MICRO(SDL_GetVersion()), SDL_GetCurrentVideoDriver(),
            SDL_GetRendererName(g_renderer), g_startFullscreen ? ", fullscreen" : "");
    return true;
}

void shutdown() {
    if (!g_window) return;
    for (SDL_Texture** t : {&g_uiTex, &g_videoTex, &g_overlayTex, &g_readTex})
        if (*t) SDL_DestroyTexture(*t), *t = nullptr;
    if (g_renderer) SDL_DestroyRenderer(g_renderer), g_renderer = nullptr;
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
    g_videoW = g_videoH = g_overlayW = g_overlayH = 0;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void drawRgba(const uint32_t* pixels) {
    g_back.kind = Kind::Menu;
    g_back.rgba.assign(pixels, pixels + static_cast<size_t>(kWidth) * kHeight);
}

bool drawYuv420(const uint8_t* y, const uint8_t* u, const uint8_t* v, int strideY, int strideU, int strideV,
                int width, int height, bool) {
    // Never waits for the screen: the mailbox keeps the newest frame.
    if (width <= 0 || height <= 0) return false;
    const int cw = (width + 1) / 2, ch = (height + 1) / 2;
    g_back.kind = Kind::Video;
    g_back.width = width, g_back.height = height;
    g_back.y.resize(static_cast<size_t>(width) * height);
    g_back.u.resize(static_cast<size_t>(cw) * ch);
    g_back.v.resize(static_cast<size_t>(cw) * ch);
    for (int r = 0; r < height; ++r)
        std::memcpy(&g_back.y[static_cast<size_t>(r) * width], y + static_cast<size_t>(r) * strideY, width);
    for (int r = 0; r < ch; ++r) {
        std::memcpy(&g_back.u[static_cast<size_t>(r) * cw], u + static_cast<size_t>(r) * strideU, cw);
        std::memcpy(&g_back.v[static_cast<size_t>(r) * cw], v + static_cast<size_t>(r) * strideV, cw);
    }
    return true;
}

void present() {
    std::lock_guard<std::mutex> lock(g_mailboxMutex);
    if (g_back.kind == Kind::None) return;
    g_back.id = ++g_handed;
    g_lastHanded = g_handed;
    std::swap(g_back, g_mailbox);  // a frame pump() didn't take is dropped
    g_back.kind = Kind::None;      // its buffers kept for the next draw
    g_mailboxFull = true;
    g_mailboxCv.notify_all();
}

void pump() {
    if (!g_renderer || !onMainThread()) return;
    {
        std::lock_guard<std::mutex> lock(g_mailboxMutex);
        if (g_mailboxFull) {
            std::swap(g_front, g_mailbox);
            g_mailboxFull = false;
            g_frontPending = true;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_overlayMutex);
        if (g_overlayChanged) {
            std::swap(g_overlayShown, g_overlay);
            g_overlayChanged = false;
            g_overlayPending = true;
        }
    }
    // Minimized or covered: nothing drawn, the mailbox still emptied (the
    // newest frame goes up once the window shows again).
    if (g_shown && (SDL_GetWindowFlags(g_window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED))) return;
    bool repaint = g_repaint.exchange(false);
    if (!g_frontPending && !g_overlayPending && !repaint) return;
    if (g_front.kind == Kind::None) return;
    upload();
    renderScene();
    if (!g_shown) {
        SDL_ShowWindow(g_window);
        g_shown = true;
    }
    SDL_RenderPresent(g_renderer);
    if (g_front.kind == Kind::Video) {
        std::lock_guard<std::mutex> lock(g_shownMutex);
        g_shownId = g_front.id;
        g_shownAtUs = platform::nowUs();
    }
}

void idleWait(uint32_t ms) {
    std::unique_lock<std::mutex> lock(g_mailboxMutex);
    g_mailboxCv.wait_for(lock, std::chrono::milliseconds(ms), [] { return g_mailboxFull; });
}

void setOverlay(const uint32_t* pixels, int x, int y, int w, int h, uint8_t opacity) {
    Overlay next;
    if (pixels && w > 0 && h > 0) {
        next.pixels.assign(pixels, pixels + static_cast<size_t>(w) * h);
        next.x = x, next.y = y, next.w = w, next.h = h;
        next.opacity = opacity;
        next.on = true;
    }
    std::lock_guard<std::mutex> lock(g_overlayMutex);
    g_overlay = std::move(next);
    g_overlayChanged = true;
    g_mailboxCv.notify_all();
}

void setSharpness(int) {}
void setDeband(int) {}
void setUpscaler(int) {}
void setRestore(bool) {}
DisplayCaps caps() {
    DisplayCaps c;
    c.presentTiming = true;  // the frame handed over, and when pump() presented it
    return c;
}
void setSwapImages(int) {}
void setPresentWait(bool) {}
bool setHighRefresh(bool) { return false; }
bool canSwitchRefresh() { return false; }
bool bringUpTest(int) {
    XC_LOGW("display: vktest is the PS5's GPU check");
    return false;
}

uint64_t lastPresentId() { return g_lastHanded; }

bool lastShown(uint64_t& id, uint64_t& atUs) {
    std::lock_guard<std::mutex> lock(g_shownMutex);
    if (!g_shownId) return false;
    id = g_shownId, atUs = g_shownAtUs;
    return true;
}

bool readBackRgb(std::vector<uint8_t>& rgb, int& width, int& height) {
    // The renderer is the main thread's: the video thread's snapshots get nothing.
    if (!g_renderer || !onMainThread() || g_front.kind == Kind::None) return false;
    upload();
    if (!g_readTex)
        g_readTex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, kWidth, kHeight);
    if (!g_readTex || !SDL_SetRenderTarget(g_renderer, g_readTex)) {
        XC_LOGW("display: read back: %s", SDL_GetError());
        return false;
    }
    renderScene();
    SDL_Surface* read = SDL_RenderReadPixels(g_renderer, nullptr);
    SDL_SetRenderTarget(g_renderer, nullptr);
    g_repaint = true;  // the window's picture drawn again at the next pump()
    if (!read) {
        XC_LOGW("display: read back: %s", SDL_GetError());
        return false;
    }
    SDL_Surface* conv = SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGB24);
    SDL_DestroySurface(read);
    if (!conv) return false;
    width = conv->w, height = conv->h;
    rgb.resize(static_cast<size_t>(width) * height * 3);
    for (int r = 0; r < height; ++r)
        std::memcpy(&rgb[static_cast<size_t>(r) * width * 3], static_cast<const uint8_t*>(conv->pixels) + r * conv->pitch,
                    static_cast<size_t>(width) * 3);
    SDL_DestroySurface(conv);
    return true;
}

}  // namespace xc::display
