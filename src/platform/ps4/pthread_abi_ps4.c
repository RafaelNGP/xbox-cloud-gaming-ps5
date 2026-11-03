/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from XCloud4's src/streaming/rtc_pthread.c
 * (https://github.com/asp11edgr/XCloud4, GPL-3.0-only, reused with its
 * author's agreement: https://github.com/asp11edgr/XCloud4/issues/8).
 *
 * The OpenOrbis headers and the precompiled libc++/libc++abi carry musl's
 * object sizes, while the functions that run are libkernel's native ones:
 *  - pthread_once_t is a 4-byte int, but the native pthread_once writes a
 *    16-byte {state, mutex}: every once object (FFmpeg, usrsctp, libjuice,
 *    libc++abi...) overwrote the 12 bytes after it.
 *  - libc++ was compiled with a 4-byte pthread_mutexattr_t; the native
 *    attribute is an 8-byte pointer written into the caller's object.
 *  - libc++abi's cxa_guard asks syscall(20) (gettid), which musl answers
 *    with ENOSYS.
 * The native objects live here instead, keyed by the caller's address, and
 * the caller's storage is never written. */
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

extern int32_t scePthreadMutexattrInit(void** attr);
extern int32_t scePthreadMutexattrDestroy(void** attr);
extern int32_t scePthreadMutexattrSettype(void** attr, int type);
extern int32_t scePthreadMutexInit(void* mutex, const void* attr, const char* name);
typedef struct {
    uint32_t state;
    void* mutex;
} NativeOnce;
_Static_assert(sizeof(NativeOnce) == 16 && offsetof(NativeOnce, mutex) == 8,
               "native pthread_once layout (PS4 x86_64 ABI)");
extern int32_t scePthreadOnce(NativeOnce* once, void (*init)(void));
extern int32_t scePthreadGetthreadid(void);

long syscall(long number, ...) {
    if (number == 20) return (long)scePthreadGetthreadid();  // SYS_gettid for cxa_guard
    errno = ENOSYS;
    return -1;
}

enum { kOnceSlots = 512, kAttrSlots = 64 };

typedef struct {
    const void* key;
    NativeOnce once;
} OnceBinding;
static OnceBinding g_once[kOnceSlots];  // once objects live as long as the process

typedef struct {
    const void* key;
    void* attribute;
} Attribute;
static Attribute g_attrs[kAttrSlots];  // attributes live from init to destroy

static atomic_flag g_lock = ATOMIC_FLAG_INIT;
static void lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_lock, memory_order_acquire)) {
    }
}
static void unlock(void) { atomic_flag_clear_explicit(&g_lock, memory_order_release); }
static int posixError(int32_t error) { return error < 0 ? (int)((uint32_t)error & 0xffffu) : error; }

static Attribute* lookup(const void* key) {
    for (size_t i = 0; i < kAttrSlots; ++i)
        if (g_attrs[i].key == key) return &g_attrs[i];
    return NULL;
}

int pthread_mutexattr_init(pthread_mutexattr_t* key) {
    if (!key) return EINVAL;
    lock();
    Attribute* slot = lookup(key);
    if (slot) {  // initialised again without a destroy: start over
        scePthreadMutexattrDestroy(&slot->attribute);
        *slot = (Attribute){0};
    }
    for (size_t i = 0; i < kAttrSlots && !slot; ++i)
        if (!g_attrs[i].key) slot = &g_attrs[i];
    if (!slot) {
        unlock();
        return ENOMEM;
    }
    int rc = posixError(scePthreadMutexattrInit(&slot->attribute));
    if (!rc) slot->key = key;
    unlock();
    return rc;
}

int pthread_mutexattr_settype(pthread_mutexattr_t* key, int type) {
    // The SDK headers already use Sony's numbering (errorcheck 1, recursive 2, normal 3).
    if (type != PTHREAD_MUTEX_ERRORCHECK && type != PTHREAD_MUTEX_RECURSIVE && type != PTHREAD_MUTEX_NORMAL)
        return EINVAL;
    lock();
    Attribute* slot = lookup(key);
    int rc = slot ? posixError(scePthreadMutexattrSettype(&slot->attribute, type)) : EINVAL;
    unlock();
    return rc;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t* key) {
    lock();
    Attribute* slot = lookup(key);
    int rc = slot ? posixError(scePthreadMutexattrDestroy(&slot->attribute)) : EINVAL;
    if (slot && !rc) *slot = (Attribute){0};
    unlock();
    return rc;
}

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* key) {
    if (!mutex) return EINVAL;
    lock();
    Attribute* slot = key ? lookup(key) : NULL;
    int rc = key && !slot ? EINVAL : posixError(scePthreadMutexInit(mutex, slot ? &slot->attribute : NULL, "psbox"));
    unlock();
    return rc;
}

int pthread_once(pthread_once_t* key, void (*init)(void)) {
    if (!key || !init) return EINVAL;
    lock();
    OnceBinding* slot = NULL;
    for (size_t i = 0; i < kOnceSlots && !slot; ++i)
        if (g_once[i].key == key) slot = &g_once[i];
    for (size_t i = 0; i < kOnceSlots && !slot; ++i)
        if (!g_once[i].key) {
            slot = &g_once[i];
            slot->key = key;
        }
    unlock();
    if (!slot) return ENOMEM;
    // Not under the lock: init may use another once object. The native call
    // provides the waiting and the single run.
    return posixError(scePthreadOnce(&slot->once, init));
}
