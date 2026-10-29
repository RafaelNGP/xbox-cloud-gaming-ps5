// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once
#include_next <signal.h>

#ifdef sa_sigaction
#undef sa_sigaction
#define sa_sigaction __sa_handler.__sa_sigaction
#endif
