// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
/* Host-link only: names the module's exports so the native-app converter
 * records the imports. Never packaged or executed. */
int sceVideodec2QueryComputeMemoryInfo(void *info) { (void)info; return -1; }
int sceVideodec2AllocateComputeQueue(const void *cfg, const void *mem, void **queue) { (void)cfg; (void)mem; (void)queue; return -1; }
int sceVideodec2ReleaseComputeQueue(void *queue) { (void)queue; return -1; }
int sceVideodec2QueryDecoderMemoryInfo(const void *cfg, void *mem) { (void)cfg; (void)mem; return -1; }
int sceVideodec2CreateDecoder(const void *cfg, const void *mem, void **decoder) { (void)cfg; (void)mem; (void)decoder; return -1; }
int sceVideodec2DeleteDecoder(void *decoder) { (void)decoder; return -1; }
int sceVideodec2Decode(void *decoder, const void *in, void *fb, void *out) { (void)decoder; (void)in; (void)fb; (void)out; return -1; }
int sceVideodec2Flush(void *decoder, void *fb, void *out) { (void)decoder; (void)fb; (void)out; return -1; }
int sceVideodec2Reset(void *decoder) { (void)decoder; return -1; }
