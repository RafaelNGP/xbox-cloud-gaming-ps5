// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once
#include_next <sys/ioctl.h>

#ifndef FIONBIO
#define FIONBIO 0x8004667eUL
#endif
#ifndef FIONREAD
#define FIONREAD 0x4004667fUL
#endif
