// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The system on-screen keyboard (sceImeDialog).
//
// An app's imports from a module it didn't load stay unresolved, and the
// app can't see /system/common/lib: the modules are loaded on first use from
// the sandbox's view of it (/<random word>/common/lib), then the imports bind
// on their first call. (sceKernelDlsym answers ESRCH for every name here.)
#include "platform/platform.h"
#include "util/log.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
int sceKernelLoadStartModule(const char* path, size_t argc, const void* argv, uint32_t flags, void* opt, int* res);
const char* sceKernelGetFsSandboxRandomWord(void);
// Linked imports, bound on first call once the modules are loaded.
int sceCommonDialogInitialize(void);
int sceImeDialogInit(const void* param, void* extended);
int sceImeDialogGetStatus(void);
int sceImeDialogGetResult(void* result);
int sceImeDialogTerm(void);
int sceUserServiceGetInitialUser(int32_t* userId);
}

namespace xc::platform {

namespace {

// sceImeDialogInit's parameters, as in the PS4 SDK (the console's wchar_t is
// 16 bits).
struct ImeDialogParam {
    int32_t userId;
    int32_t type;  // 0 default, 2 URL, 3 e-mail, 4 number
    uint64_t supportedLanguages;
    int32_t enterLabel;
    int32_t inputMethod;
    void* filter;
    uint32_t option;  // 0x4 password
    uint32_t maxTextLength;
    char16_t* inputTextBuffer;
    float posx, posy;
    int32_t horizontalAlignment, verticalAlignment;  // 1 = centre
    const char16_t* placeholder;
    const char16_t* title;
    int8_t reserved[16];
};
static_assert(sizeof(ImeDialogParam) == 96);

struct DialogResult {
    int32_t endStatus;  // 0 ok, 1 cancel, 2 abort
    int8_t reserved[60];
};

int g_available = -1;  // unknown until the first use
bool g_open = false;
std::u16string g_title;
std::vector<char16_t> g_buffer;

// An app sees the system libraries under /<random word>/common/lib.
std::string systemLib(const char* name) {
    const char* word = sceKernelGetFsSandboxRandomWord();
    return std::string("/") + (word ? word : "") + "/common/lib/" + name;
}

int load(const char* path) {
    int res = 0;
    int handle = sceKernelLoadStartModule(path, 0, nullptr, 0, nullptr, &res);
    XC_LOGI("keyboard: load %s -> 0x%08x", path, static_cast<unsigned>(handle));
    return handle;
}

bool resolve() {
    if (g_available >= 0) return g_available;
    g_available = 0;

    // The dialogs sit on libSceCommonDialog, which wants initialising first.
    int common = load(systemLib("libSceCommonDialog.sprx").c_str());
    if (common < 0) common = load("libSceCommonDialog.sprx");
    if (common >= 0) {
        int rc = sceCommonDialogInitialize();
        XC_LOGI("keyboard: sceCommonDialogInitialize -> 0x%08x", static_cast<unsigned>(rc));
    }

    int ime = load(systemLib("libSceImeDialog.sprx").c_str());
    if (ime < 0) ime = load("libSceImeDialog.sprx");
    g_available = ime >= 0;
    return g_available;
}

std::u16string toUtf16(const std::string& s) {
    std::u16string out;
    for (size_t i = 0; i < s.size();) {
        uint32_t c = static_cast<uint8_t>(s[i]);
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (extra) c &= 0x3F >> extra;
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i) c = (c << 6) | (static_cast<uint8_t>(s[i]) & 0x3F);
        if (c >= 0x10000) {
            c -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (c >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (c & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(c));
        }
    }
    return out;
}

std::string toUtf8(const char16_t* s) {
    std::string out;
    for (; *s; ++s) {
        uint32_t c = *s;
        if (c >= 0xD800 && c < 0xDC00 && s[1] >= 0xDC00 && s[1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[1] - 0xDC00);
            ++s;
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

}  // namespace

bool systemKeyboardAvailable() { return resolve(); }

bool openSystemKeyboard(const std::string& title, const std::string& text, size_t maxLength, KeyboardKind kind) {
    if (g_open || !resolve()) return false;
    if (maxLength == 0 || maxLength > 2048) maxLength = 2048;
    g_title = toUtf16(title);
    std::u16string initial = toUtf16(text);
    if (initial.size() > maxLength) initial.resize(maxLength);
    g_buffer.assign(maxLength + 1, 0);
    std::memcpy(g_buffer.data(), initial.data(), initial.size() * sizeof(char16_t));

    ImeDialogParam p{};
    int32_t user = -1;
    sceUserServiceGetInitialUser(&user);
    p.userId = user;
    p.supportedLanguages = 0;
    switch (kind) {
    case KeyboardKind::Text: break;
    case KeyboardKind::Password: p.option = 0x4; break;
    case KeyboardKind::Number: p.type = 4; break;
    case KeyboardKind::Email: p.type = 3; break;
    case KeyboardKind::Url: p.type = 2; break;
    }
    p.maxTextLength = static_cast<uint32_t>(maxLength);
    p.inputTextBuffer = g_buffer.data();
    p.posx = 1920 / 2.0f;
    p.posy = 1080 / 2.0f;
    p.horizontalAlignment = 1;
    p.verticalAlignment = 1;
    p.title = g_title.c_str();
    int rc = sceImeDialogInit(&p, nullptr);
    XC_LOGI("keyboard: sceImeDialogInit -> 0x%08x", static_cast<unsigned>(rc));
    if (rc != 0) return false;
    g_open = true;
    return true;
}

KeyboardStatus pollSystemKeyboard(std::string& text) {
    if (!g_open) return KeyboardStatus::Closed;
    int status = sceImeDialogGetStatus();
    if (status == 1) return KeyboardStatus::Open;  // running
    DialogResult r{};
    int rc = sceImeDialogGetResult(&r);
    sceImeDialogTerm();
    g_open = false;
    XC_LOGI("keyboard: closed (status %d, result 0x%08x, end %d)", status, static_cast<unsigned>(rc), r.endStatus);
    if (status != 2 || rc != 0 || r.endStatus != 0) return KeyboardStatus::Cancelled;
    text = toUtf8(g_buffer.data());
    return KeyboardStatus::Accepted;
}

}  // namespace xc::platform
