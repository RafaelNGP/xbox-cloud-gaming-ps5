// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "platform/platform.h"
#include "util/log.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
int sceSysmoduleLoadModule(uint32_t id);
int sceSysmoduleLoadModuleInternal(uint32_t id);
int sceCommonDialogInitialize(void);
int sceImeDialogInit(const void* param, void* extended);
int sceImeDialogGetStatus(void);
int sceImeDialogGetResult(void* result);
int sceImeDialogTerm(void);
int sceUserServiceGetInitialUser(int32_t* userId);
}

namespace xc::platform {

namespace {

struct ImeDialogParam {
    int32_t userId;
    int32_t type;  // 0 default, 2 URL, 3 e-mail, 4 number
    uint64_t supportedLanguages;
    int32_t enterLabel;
    int32_t inputMethod;
    void* filter;
    uint32_t option;
    uint32_t maxTextLength;
    char16_t* inputTextBuffer;
    float posx, posy;
    int32_t horizontalAlignment, verticalAlignment;
    const char16_t* placeholder;
    const char16_t* title;
    int8_t reserved[16];
};
static_assert(sizeof(ImeDialogParam) == 96);

struct DialogResult {
    int32_t endStatus;  // 0 ok, 1 cancel, 2 abort
    int8_t reserved[60];
};

int g_available = -1;
bool g_open = false;
std::u16string g_title;
std::vector<char16_t> g_buffer;

void utf8ToUtf16(const std::string& in, std::u16string& out) {
    out.clear();
    for (size_t i = 0; i < in.size();) {
        uint32_t cp = 0;
        unsigned char c = in[i];
        if (c < 0x80) { cp = c; i += 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < in.size()) {
            cp = ((c & 0x1F) << 6) | (in[i + 1] & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < in.size()) {
            cp = ((c & 0x0F) << 12) | ((in[i + 1] & 0x3F) << 6) | (in[i + 2] & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < in.size()) {
            cp = ((c & 0x07) << 18) | ((in[i + 1] & 0x3F) << 12) | ((in[i + 2] & 0x3F) << 6) | (in[i + 3] & 0x3F);
            i += 4;
        } else {
            i += 1;
            continue;
        }
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
}

void utf16ToUtf8(const char16_t* in, std::string& out) {
    out.clear();
    if (!in) return;
    for (size_t i = 0; in[i]; ++i) {
        uint32_t cp = in[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF) {
            cp = 0x10000 + (((cp - 0xD800) << 10) | (in[i + 1] - 0xDC00));
            ++i;
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
}

}  // namespace

bool systemKeyboardAvailable() {
    if (g_available >= 0) return g_available == 1;
    sceSysmoduleLoadModuleInternal(0x80000018); // COMMON_DIALOG
    sceSysmoduleLoadModule(0x0096);             // IME_DIALOG
    int rc = sceCommonDialogInitialize();
    g_available = (rc >= 0 || rc == static_cast<int>(0x80610005)) ? 1 : 0;
    return g_available == 1;
}

bool openSystemKeyboard(const std::string& title, const std::string& text, size_t maxLength, KeyboardKind kind) {
    if (!systemKeyboardAvailable() || g_open) return false;
    int32_t user = -1;
    sceUserServiceGetInitialUser(&user);
    if (user < 0) return false;

    if (maxLength == 0 || maxLength > 2048) maxLength = 2048;
    utf8ToUtf16(title, g_title);
    std::u16string initial16;
    utf8ToUtf16(text, initial16);
    if (initial16.size() > maxLength) initial16.resize(maxLength);

    g_buffer.assign(maxLength + 1, 0);
    std::memcpy(g_buffer.data(), initial16.data(), initial16.size() * sizeof(char16_t));

    ImeDialogParam p{};
    p.userId = user;
    p.supportedLanguages = 0;
    switch (kind) {
        case KeyboardKind::Text: p.type = 0; break;
        case KeyboardKind::Password: p.type = 0; p.option = 0x4; break;
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
    if (rc != 0) {
        XC_LOGW("sceImeDialogInit: 0x%08x", static_cast<unsigned>(rc));
        return false;
    }
    g_open = true;
    return true;
}

KeyboardStatus pollSystemKeyboard(std::string& text) {
    if (!g_open) return KeyboardStatus::Closed;
    int status = sceImeDialogGetStatus();
    if (status == 1) return KeyboardStatus::Open;  // running
    DialogResult res{};
    int rc = sceImeDialogGetResult(&res);
    sceImeDialogTerm();
    g_open = false;
    if (status != 2 || rc != 0 || res.endStatus != 0) return KeyboardStatus::Cancelled;
    utf16ToUtf8(g_buffer.data(), text);
    return KeyboardStatus::Accepted;
}

}  // namespace xc::platform
