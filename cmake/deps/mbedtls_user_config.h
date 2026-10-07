// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
/* Appended to deps/mbedtls's default config (MBEDTLS_USER_CONFIG_FILE).
 * WebRTC needs the DTLS-SRTP extension (use_srtp), off by default. */
#define MBEDTLS_SSL_DTLS_SRTP
