// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once
/*
 * Forced into every PS5 translation unit. The PS4 toolchain's musl offered the
 * Linux large-file names (stat64, lseek64, O_LARGEFILE, ...) that StormLib and
 * friends use on "Linux-like" platforms. FreeBSD has one 64-bit API, so the
 * *64 names are aliases here.
 */
#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
#define stat64 stat
#define fstat64 fstat
#define lstat64 lstat
#define lseek64 lseek
#define ftruncate64 ftruncate
#define pread64 pread
#define pwrite64 pwrite
#define off64_t off_t

/*
 * The app sandbox refuses ioctl(FIONBIO) and fcntl(O_NONBLOCK) on sockets
 * (EACCES); setsockopt(SO_NBIO) works. Every ioctl call goes through
 * xc_ioctl (src/platform/ps5/libc_compat.c), which translates FIONBIO.
 */
#define ioctl xc_ioctl
