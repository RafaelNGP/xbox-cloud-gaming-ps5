// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
/* Appended to deps/mbedtls's default config (MBEDTLS_USER_CONFIG_FILE).
 * WebRTC needs the DTLS-SRTP extension (use_srtp), off by default. */
#define MBEDTLS_SSL_DTLS_SRTP

/* The app makes HTTPS requests from several threads at once (lists, prices,
 * images) while WebRTC runs DTLS: the shared random generator and the PSA
 * crypto state (TLS 1.3) need Mbed TLS's own locking, or handshakes fail
 * with "This is a bug in the library" (-0x006E, CORRUPTION_DETECTED). */
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_PTHREAD
