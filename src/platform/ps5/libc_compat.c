// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
/* libc functions the console's libc does not export but deps use. */
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/syscall.h>

/* usrsctp: constant-time comparison of authentication data. */
int timingsafe_bcmp(const void *b1, const void *b2, size_t n) {
    const unsigned char *p1 = b1, *p2 = b2;
    unsigned char diff = 0;
    while (n--) diff |= *p1++ ^ *p2++;
    return diff != 0;
}

/* plog (libdatachannel's logger) only asks for the thread id this way. */
long syscall(long number, ...) {
    if (number == SYS_thr_self) {
        va_list ap;
        va_start(ap, number);
        long *tid = va_arg(ap, long *);
        va_end(ap);
        if (tid) *tid = (long)(uintptr_t)pthread_self();
        return 0;
    }
    errno = ENOSYS;
    return -1;
}

/* --- Name resolution ----------------------------------------------------------
 * The console libc's getaddrinfo fails inside the app sandbox even for numeric
 * addresses (no resolv.conf; EAI_FAIL / errno 13). These replacements, which
 * take precedence over libc's for everything linked into the eboot (libjuice,
 * libdatachannel, our HTTP client), parse numeric addresses themselves and send
 * host names to sceNetResolver through xc_resolve_ipv4 (platform_ps5.cpp).
 * Only IPv4 is returned for host names and wildcard binds. */
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

int xc_resolve_ipv4(const char *host, uint32_t *addr);

static struct addrinfo *make_ai(int family, const void *addr, unsigned scope, int port, const struct addrinfo *hints) {
    size_t salen = family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    struct addrinfo *ai = calloc(1, sizeof *ai + salen);
    if (!ai) return NULL;
    struct sockaddr *sa = (struct sockaddr *)(ai + 1);
    if (family == AF_INET) {
        struct sockaddr_in *s4 = (struct sockaddr_in *)sa;
        s4->sin_len = sizeof *s4;
        s4->sin_family = AF_INET;
        s4->sin_port = htons((uint16_t)port);
        memcpy(&s4->sin_addr, addr, 4);
    } else {
        struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)sa;
        s6->sin6_len = sizeof *s6;
        s6->sin6_family = AF_INET6;
        s6->sin6_port = htons((uint16_t)port);
        s6->sin6_scope_id = scope;
        memcpy(&s6->sin6_addr, addr, 16);
    }
    ai->ai_family = family;
    ai->ai_socktype = hints ? hints->ai_socktype : 0;
    ai->ai_protocol = hints ? hints->ai_protocol : 0;
    ai->ai_flags = hints ? hints->ai_flags : 0;
    ai->ai_addrlen = (socklen_t)salen;
    ai->ai_addr = sa;
    return ai;
}

int getaddrinfo(const char *host, const char *serv, const struct addrinfo *hints, struct addrinfo **res) {
    int family = hints ? hints->ai_family : AF_UNSPEC;
    int flags = hints ? hints->ai_flags : 0;
    int port = 0;
    if (!res) return EAI_FAIL;
    *res = NULL;
    if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6) return EAI_FAMILY;
    if (serv && *serv) {
        char *end;
        long p = strtol(serv, &end, 10);
        if (*end || p < 0 || p > 65535) {
            if (!strcmp(serv, "https")) p = 443;
            else if (!strcmp(serv, "http")) p = 80;
            else return EAI_SERVICE;
        }
        port = (int)p;
    }

    unsigned char a[16];
    unsigned scope = 0;
    if (!host) {
        /* Wildcard (passive) or loopback. */
        if (family == AF_INET6) {
            memcpy(a, (flags & AI_PASSIVE) ? &in6addr_any : &in6addr_loopback, 16);
            return (*res = make_ai(AF_INET6, a, 0, port, hints)) ? 0 : EAI_MEMORY;
        }
        uint32_t v4 = htonl((flags & AI_PASSIVE) ? INADDR_ANY : INADDR_LOOPBACK);
        return (*res = make_ai(AF_INET, &v4, 0, port, hints)) ? 0 : EAI_MEMORY;
    }
    if (family != AF_INET6 && inet_pton(AF_INET, host, a) == 1)
        return (*res = make_ai(AF_INET, a, 0, port, hints)) ? 0 : EAI_MEMORY;
    if (family != AF_INET) {
        char tmp[64];
        snprintf(tmp, sizeof tmp, "%s", host);
        char *pct = strchr(tmp, '%');
        if (pct) {
            *pct = 0;
            scope = (unsigned)strtoul(pct + 1, NULL, 10);
        }
        if (inet_pton(AF_INET6, tmp, a) == 1)
            return (*res = make_ai(AF_INET6, a, scope, port, hints)) ? 0 : EAI_MEMORY;
    }
    if (flags & AI_NUMERICHOST) return EAI_NONAME;
    if (family == AF_INET6) return EAI_NONAME;
    uint32_t v4;
    if (xc_resolve_ipv4(host, &v4) != 0) return EAI_NONAME;
    return (*res = make_ai(AF_INET, &v4, 0, port, hints)) ? 0 : EAI_MEMORY;
}

void freeaddrinfo(struct addrinfo *ai) {
    while (ai) {
        struct addrinfo *next = ai->ai_next;
        free(ai);
        ai = next;
    }
}

int getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, size_t hostlen, char *serv, size_t servlen,
                int flags) {
    (void)salen;
    (void)flags;  /* always numeric */
    int port;
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *s4 = (const struct sockaddr_in *)sa;
        if (host && hostlen && !inet_ntop(AF_INET, &s4->sin_addr, host, (socklen_t)hostlen)) return EAI_OVERFLOW;
        port = ntohs(s4->sin_port);
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *s6 = (const struct sockaddr_in6 *)sa;
        if (host && hostlen && !inet_ntop(AF_INET6, &s6->sin6_addr, host, (socklen_t)hostlen)) return EAI_OVERFLOW;
        port = ntohs(s6->sin6_port);
    } else {
        return EAI_FAMILY;
    }
    if (serv && servlen) snprintf(serv, servlen, "%d", port);
    return 0;
}

/* --- Non-blocking sockets (see ps5/compat/ps5_lfs.h) ------------------------ */
#include <sys/ioctl.h>
#undef ioctl
/* sys/ioctl.h declared it as xc_ioctl (the forced compat header). */
int ioctl(int fd, unsigned long request, ...);

#define XC_SO_NBIO 0x1200

int xc_ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    if (request == FIONBIO && arg) {
        int on = *(const int *)arg ? 1 : 0;
        if (setsockopt(fd, SOL_SOCKET, XC_SO_NBIO, &on, sizeof on) == 0) return 0;
    }
    return ioctl(fd, request, arg);
}
