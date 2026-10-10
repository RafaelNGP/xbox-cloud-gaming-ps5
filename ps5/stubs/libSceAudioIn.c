// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
/* Host-link only: names the module's exports so the native-app converter
 * records the imports. Never packaged or executed. */

int sceAudioInInit(void) { return -1; }

int sceAudioInOpen(int userId, int type, int index, unsigned int len, unsigned int freq, unsigned int param) {
    (void)userId; (void)type; (void)index; (void)len; (void)freq; (void)param;
    return -1;
}

int sceAudioInInput(int handle, void *dest) {
    (void)handle; (void)dest;
    return -1;
}

int sceAudioInClose(int handle) {
    (void)handle;
    return -1;
}

int sceAudioInGetStatus(int handle, unsigned int *status) {
    (void)handle; (void)status;
    return -1;
}

int sceAudioInSetGain(int handle, int gain) {
    (void)handle; (void)gain;
    return -1;
}
