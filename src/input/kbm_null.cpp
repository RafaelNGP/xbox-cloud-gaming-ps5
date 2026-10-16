// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// No USB keyboard or mouse read (a build without a kbm backend).
#include "input/kbm.h"

namespace xc::input {

bool kbmInit(int32_t) { return false; }
KbmInput kbmPoll() { return {}; }

}  // namespace xc::input
