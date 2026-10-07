// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PSBox Cloud Gaming contributors
/* Host-link only: never packaged or executed. */
int sceMsgDialogInitialize(void) { return -1; }
int sceMsgDialogOpen(const void *param) { (void)param; return -1; }
int sceMsgDialogUpdateStatus(void) { return -1; }
int sceMsgDialogTerminate(void) { return -1; }
int sceMsgDialogClose(void) { return -1; }
int sceMsgDialogGetResult(void *result) { (void)result; return -1; }
int sceMsgDialogProgressBarSetMsg(int target, const char *msg) { (void)target; (void)msg; return -1; }
int sceMsgDialogProgressBarSetValue(int target, unsigned rate) { (void)target; (void)rate; return -1; }
