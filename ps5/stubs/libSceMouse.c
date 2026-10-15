// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
/* Host-link only: names the module's exports so the native-app converter
 * records the imports. Never packaged or executed. */
int sceMouseInit(void) { return -1; }
int sceMouseOpen(int user, int type, int index, const void *param) { (void)user; (void)type; (void)index; (void)param; return -1; }
int sceMouseRead(int handle, void *data, int count) { (void)handle; (void)data; (void)count; return -1; }
int sceMouseClose(int handle) { (void)handle; return -1; }
