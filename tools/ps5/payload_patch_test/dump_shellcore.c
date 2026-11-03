// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only RE dump of SceShellCore for the user's own jailbroken 13.60 console.
// No patching, no unjail — pure kernel_proc_copyout of mapped ranges to /data,
// so the trophy daemon's checks/strings can be located offline (PHU-style port).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sysctl.h>
#include <ps5/kernel.h>

extern void klog_printf(const char *fmt, ...);

static pid_t find_proc(const char* name) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t sz = 0;
    if (sysctl(mib, 4, NULL, &sz, NULL, 0) != 0 || !sz) return 0;
    void* buf = malloc(sz);
    if (!buf) return 0;
    pid_t found = 0;
    if (sysctl(mib, 4, buf, &sz, NULL, 0) == 0) {
        for (void* p = buf; p < buf + sz; ) {
            struct kinfo_proc* ki = (struct kinfo_proc*)p;
            p += ki->ki_structsize;
            if (strstr(ki->ki_comm, name)) { found = ki->ki_pid; break; }
        }
    }
    free(buf);
    return found;
}

static intptr_t mapbase_of(pid_t pid) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, pid};
    size_t sz = 0;
    if (sysctl(mib, 4, NULL, &sz, NULL, 0) != 0 || !sz) return 0;
    char* buf = malloc(sz);
    if (!buf) return 0;
    intptr_t mb = 0;
    if (sysctl(mib, 4, buf, &sz, NULL, 0) == 0) {
        for (char* p = buf; p < buf + sz; ) {
            struct kinfo_vmentry* kve = (struct kinfo_vmentry*)p;
            if (kve->kve_structsize <= 0) break;
            p += kve->kve_structsize;
            if ((kve->kve_protection & 4) && kve->kve_offset == 0 && kve->kve_start < 0x200000000ULL) {
                mb = kve->kve_start; break;
            }
        }
    }
    free(buf);
    return mb;
}

static void dump_range(pid_t pid, intptr_t mb, const char* path, uintptr_t off, size_t len) {
    FILE* f = fopen(path, "wb");
    if (!f) { klog_printf("[SCDUMP] open fail %s\n", path); return; }
    uint64_t hdr[4] = {0x504d554443530ULL, (uint64_t)mb, (uint64_t)(mb + off), (uint64_t)len};
    fwrite(hdr, 1, sizeof(hdr), f);
    unsigned char* c = malloc(0x10000);
    size_t ok = 0;
    if (c) {
        for (size_t i = 0; i < len; i += 0x10000) {
            size_t n = (len - i) < 0x10000 ? (len - i) : 0x10000;
            if (kernel_proc_copyout(pid, mb + off + i, c, n) != 0) memset(c, 0, n);
            else ok += n;
            fwrite(c, 1, n, f);
        }
        free(c);
    }
    fclose(f);
    klog_printf("[SCDUMP] %s: base 0x%lx len 0x%zx readable ~0x%zx\n", path, mb + off, len, ok);
}

int main(void) {
    pid_t pid = find_proc("SceShellCore");
    intptr_t mb = mapbase_of(pid);
    klog_printf("[SCDUMP] SceShellCore pid=%d mapbase=0x%lx\n", pid, mb);
    if (pid <= 0 || mb == 0) return 1;
    // .text covering the trophy daemon + app-inventory/NP regions.
    dump_range(pid, mb, "/data/sc_text.bin", 0xb00000, 0x500000);   // 0xb00000..0x1000000
    // rodata/data covering B1 strings ("AAAA00000"), vtables, dispatch tables.
    dump_range(pid, mb, "/data/sc_rodata.bin", 0x1a00000, 0x500000); // 0x1a00000..0x1f00000
    dump_range(pid, mb, "/data/sc_data.bin", 0x1f00000, 0x500000);   // 0x1f00000..0x2400000
    klog_printf("[SCDUMP] done\n");
    return 0;
}
