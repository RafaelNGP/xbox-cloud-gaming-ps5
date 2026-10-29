// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

// OpenOrbis musl headers declare sockaddr_storage with ss_family at offset 0 without ss_len.
// However, the FreeBSD / PS4 Orbis kernel and libSceNet expect ss_len at offset 0 and ss_family at offset 1,
// matching struct sockaddr (sa_len, sa_family) and struct sockaddr_in (sin_len, sin_family).
// We redefine sockaddr_storage so that any sockaddr_storage object has ss_family at offset 1.
#define sockaddr_storage __broken_musl_sockaddr_storage
#include_next <sys/socket.h>
#undef sockaddr_storage

struct sockaddr_storage {
    unsigned char ss_len;
    sa_family_t ss_family;
    char __ss_padding[128 - sizeof(long) - 2];
    unsigned long __ss_align;
};

#ifndef SO_NBIO
#define SO_NBIO 0x1200
#endif
