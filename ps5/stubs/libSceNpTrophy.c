// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
/* Host-link only: names the module's exports so the native-app converter
 * records the imports. Never packaged or executed. */
#include <stdint.h>

int32_t sceNpTrophyCreateContext(int32_t *context, int32_t user, uint32_t unk, uint64_t unk2) {
    (void)context; (void)user; (void)unk; (void)unk2;
    return -1;
}

int32_t sceNpTrophyDestroyContext(int32_t context) {
    (void)context;
    return -1;
}

int32_t sceNpTrophyCreateHandle(int32_t *handle) {
    (void)handle;
    return -1;
}

int32_t sceNpTrophyDestroyHandle(int32_t handle) {
    (void)handle;
    return -1;
}

int32_t sceNpTrophyRegisterContext(int32_t context, int32_t handle, uint64_t unk) {
    (void)context; (void)handle; (void)unk;
    return -1;
}

int32_t sceNpTrophyUnlockTrophy(int32_t context, int32_t handle, int32_t TrophyID, int32_t *PLATID) {
    (void)context; (void)handle; (void)TrophyID; (void)PLATID;
    return -1;
}

int32_t sceNpTrophyGetTrophyUnlockState(int32_t context, int32_t handle, uint32_t *flags, uint32_t *count) {
    (void)context; (void)handle; (void)flags; (void)count;
    return -1;
}
