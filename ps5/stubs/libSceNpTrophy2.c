// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
/* Host-link only: names the module's exports so the native-app converter
 * records the imports. Never packaged or executed. */
#include <stdint.h>

int32_t sceNpTrophy2CreateContext(int32_t *context, int32_t userId, uint32_t serviceLabel, uint64_t options) {
    (void)context; (void)userId; (void)serviceLabel; (void)options;
    return -1;
}

int32_t sceNpTrophy2DestroyContext(int32_t context) {
    (void)context;
    return -1;
}

int32_t sceNpTrophy2CreateHandle(int32_t *handle) {
    (void)handle;
    return -1;
}

int32_t sceNpTrophy2DestroyHandle(int32_t handle) {
    (void)handle;
    return -1;
}

int32_t sceNpTrophy2AbortHandle(int32_t handle) {
    (void)handle;
    return -1;
}

int32_t sceNpTrophy2RegisterContext(int32_t context, int32_t handle, uint64_t options) {
    (void)context; (void)handle; (void)options;
    return -1;
}

int32_t sceNpTrophy2SystemDebugUnlockTrophy(int32_t handle, int32_t context, const void *param) {
    (void)handle; (void)context; (void)param;
    return -1;
}

int32_t sceNpTrophy2ShowTrophyList(int32_t handle, uint64_t options) {
    (void)handle; (void)options;
    return -1;
}
