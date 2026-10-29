// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#ifndef _SYS_MUTEX_H_
#define _SYS_MUTEX_H_

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

#ifndef _ALIGNBYTES
#define _ALIGNBYTES (sizeof(uintptr_t) - 1)
#endif
#ifndef _ALIGN
#define _ALIGN(p) (((uintptr_t)(p) + _ALIGNBYTES) & ~_ALIGNBYTES)
#endif

struct lock_object {
    const char *lo_name;
    uint32_t lo_flags;
    uint32_t lo_data;
    void *lo_witness;
};

struct mtx {
    struct lock_object lock_object;
    volatile uintptr_t mtx_lock;
};

#ifdef __cplusplus
extern "C" {
#endif

void arc4random_buf(void *buf, size_t size);

static inline int timingsafe_bcmp(const void *b1, const void *b2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)b1;
    const unsigned char *p2 = (const unsigned char *)b2;
    int ret = 0;
    for (size_t i = 0; i < n; i++) {
        ret |= (p1[i] ^ p2[i]);
    }
    return ret;
}

#ifdef __cplusplus
}
#endif

#endif /* _SYS_MUTEX_H_ */
