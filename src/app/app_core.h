// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The app every platform runs (app_core.cpp), on the main thread: the
// start-up, then one pass of the main loop at a time. Each platform's main()
// calls runApp(); what differs between them is behind app/shell.h and the
// backends it links (docs/multiplatform.md).
#pragma once

namespace xc::app {

// Everything up to the main loop, the worker thread started; false when the
// app is to close at once.
bool coreStartup();
// One pass: input, the UI or the game's stream, drawing. False once the app
// is closed (never on the PS5: the system ends it).
bool coreFrame();
// After the last pass.
void coreShutdown();
// The three in turn; the process's exit code.
int runApp();

}  // namespace xc::app
