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

static pid_t find_process_by_name(const char* name) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t buf_size = 0;
    if (sysctl(mib, 4, NULL, &buf_size, NULL, 0) != 0 || buf_size == 0) return 0;
    void *buf = malloc(buf_size);
    if (!buf) return 0;
    if (sysctl(mib, 4, buf, &buf_size, NULL, 0) != 0) {
        free(buf);
        return 0;
    }
    pid_t found = 0;
    for (void *ptr = buf; ptr < (buf + buf_size); ) {
        struct kinfo_proc *ki = (struct kinfo_proc*)ptr;
        ptr += ki->ki_structsize;
        if (strstr(ki->ki_comm, name) != NULL) {
            found = ki->ki_pid;
            break;
        }
    }
    free(buf);
    return found;
}

static intptr_t get_process_mapbase(pid_t pid) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, pid};
    size_t size = 0;
    if (sysctl(mib, 4, NULL, &size, NULL, 0) != 0 || size == 0) return 0;
    char *buf = malloc(size);
    if (!buf) return 0;
    intptr_t mapbase = 0;
    if (sysctl(mib, 4, buf, &size, NULL, 0) == 0) {
        for (char *p = buf; p < buf + size; ) {
            struct kinfo_vmentry *kve = (struct kinfo_vmentry*)p;
            if (kve->kve_structsize <= 0) break;
            p += kve->kve_structsize;
            if ((kve->kve_protection & 4) && kve->kve_offset == 0 && kve->kve_start < 0x200000000ULL) {
                mapbase = kve->kve_start;
                break;
            }
        }
    }
    free(buf);
    return mapbase;
}

static void unjail_pid(pid_t pid) {
    if (pid <= 0) return;
    intptr_t rootvnode = kernel_get_root_vnode();
    int r1 = kernel_set_proc_rootdir(pid, rootvnode);
    int r2 = kernel_set_proc_jaildir(pid, rootvnode);
    int r3 = kernel_set_ucred_uid(pid, 0);
    int r4 = kernel_set_ucred_ruid(pid, 0);
    int r5 = kernel_set_ucred_prison(pid, KERNEL_ADDRESS_PRISON0);
    uint8_t caps[16];
    memset(caps, 0xff, sizeof(caps));
    int r6 = kernel_set_ucred_caps(pid, caps);
    klog_printf("[TROPHY_PATCH] Unjailed PID %d: root=%d jail=%d uid=%d ruid=%d prison=%d caps=%d\n",
                pid, r1, r2, r3, r4, r5, r6);
}

static void apply_patch(pid_t pid, intptr_t addr, const unsigned char *patch, const unsigned char *expected_orig, size_t size, const char *name) {
    unsigned char orig[64] = {0};
    size_t check_len = size < 64 ? size : 64;
    kernel_proc_copyout(pid, addr, orig, check_len);
    klog_printf("[TROPHY_PATCH] %s at 0x%lx orig: %02x %02x %02x %02x %02x %02x\n",
                name, addr, orig[0], orig[1], orig[2], orig[3], orig[4], orig[5]);

    if (expected_orig && memcmp(orig, expected_orig, size) != 0) {
        // If already patched, verify it
        if (memcmp(orig, patch, size) == 0) {
            klog_printf("[TROPHY_PATCH] %s at 0x%lx already patched\n", name, addr);
            return;
        }
        klog_printf("[TROPHY_PATCH] ERROR: %s at 0x%lx orig bytes mismatch, skipping!\n", name, addr);
        return;
    }

    kernel_mprotect(pid, addr & ~0x3fff, 0x4000, PROT_READ | PROT_WRITE | PROT_EXEC);
    kernel_proc_copyin(pid, patch, addr, size);

    unsigned char verify[64] = {0};
    kernel_proc_copyout(pid, addr, verify, check_len);
    klog_printf("[TROPHY_PATCH] %s at 0x%lx patched: %02x %02x %02x %02x %02x %02x\n",
                name, addr, verify[0], verify[1], verify[2], verify[3], verify[4], verify[5]);
}

int main(void) {
    pid_t pid = find_process_by_name("SceShellCore");
    intptr_t mapbase = get_process_mapbase(pid);
    klog_printf("[TROPHY_PATCH] SceShellCore dynamic pid=%d, mapbase=0x%lx\n", pid, mapbase);

    if (pid <= 0 || mapbase == 0) {
        klog_printf("[TROPHY_PATCH] ERROR: Failed to resolve SceShellCore PID or mapbase\n");
        return 1;
    }

    // =====================================================================
    // SceShellCore trophy patches — CLEANED 2026-11-03.
    // From disassembling the live SceShellCore .text (RE workflows): the
    // collection/surfacing path is handler 0xe232e0 -> 0xe1a4c0 -> cache
    // ingest [cache_vtable+0x30]=0xe39310 -> fan-out 0xe34e70 (ShellUI). The
    // unlock subsystem (~0xe65xxx) is separate. REMOVED 7 past-agent patches
    // that targeted off-path / non-trophy / inert / counterproductive code:
    //   old #1  0xc3ca78  account/NP gate (errors 0x8055001x), not trophy
    //   old #3  0xe32600  wrong ctx-state predicate (==1; ingest uses ==0/==2)
    //   old #7  0xe2175b  inert null/validity guard (non-null args pass anyway)
    //   old #12 0xe17a60  inert serviceLabel validator (we use label 1)
    //   old #13 0xbcd5b8  generic app-manager find-by-id, not the trophy service
    //   old #14 0xe17b80  inert context-param validator (valid params pass)
    //   old #15 0xe4c980  serviceLabel==0 validator; bypass re-allowed label 0
    //                     (counterproductive vs AGENTS.md 8.3)
    // What remains below is trophy-mechanism only; final trim pends the chosen
    // surfacing approach (in-process funnel vs external service).
    // =====================================================================

    // [K1] Trophy config load/init guard (old #2):
    // Address: mapbase + 0xe6b1ff (file offset 0xe6f1ff)
    // 78 45 (js +0x45) -> 2x NOP
    const unsigned char orig_comm_info[] = {0x78, 0x45};
    const unsigned char patch_comm_info[] = {0x90, 0x90};
    apply_patch(pid, mapbase + 0xe6b1ff, patch_comm_info, orig_comm_info, sizeof(patch_comm_info), "Trophy config load guard (0xe6b1ff)");

    // [K2] Debug-unlock IPC gate (old #4):
    // Address: mapbase + 0xeb4716 (file offset 0xeb8716)
    // 75 36 (jne +0x36) -> 2x NOP
    const unsigned char orig_ipc_debug[] = {0x75, 0x36};
    const unsigned char patch_ipc_debug[] = {0x90, 0x90};
    apply_patch(pid, mapbase + 0xeb4716, patch_ipc_debug, orig_ipc_debug, sizeof(patch_ipc_debug), "Trophy debug IPC gate (0xeb4716)");

    // [K3] Debug/dev status gate a trophy handler checks; keep until the chosen path proves it unneeded.
    const unsigned char orig_debug_fn[] = {0x55, 0x48, 0x89, 0xe5, 0x53, 0x48};
    const unsigned char patch_debug_fn[] = {0xb8, 0x01, 0x00, 0x00, 0x00, 0xc3};
    apply_patch(pid, mapbase + 0xb20050, patch_debug_fn, orig_debug_fn, sizeof(patch_debug_fn), "Trophy debug/dev status gate (0xb20050)");

    // [K4] Unlock reader already-unlocked bitmap test (old #6):
    // Address: mapbase + 0xe5c1b4 (file offset 0xe601b4)
    // 0f 83 b8 00 00 00 (jae +0xb8) -> 6x NOP
    const unsigned char orig_bitmask_check[] = {0x0f, 0x83, 0xb8, 0x00, 0x00, 0x00};
    const unsigned char patch_bitmask_check[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    apply_patch(pid, mapbase + 0xe5c1b4, patch_bitmask_check, orig_bitmask_check, sizeof(patch_bitmask_check), "Unlock already-unlocked bitmap (0xe5c1b4)");

    // [K5] Unlock worker group bounds (old #8):
    // Address: mapbase + 0xe65c85 (file offset 0xe69c85)
    // 0f 83 02 01 00 00 (jae +0x102) -> 6x NOP
    const unsigned char orig_bounds1[] = {0x0f, 0x83, 0x02, 0x01, 0x00, 0x00};
    const unsigned char patch_6nop[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    apply_patch(pid, mapbase + 0xe65c85, patch_6nop, orig_bounds1, sizeof(patch_6nop), "Trophy Pack Group Check Bypass (0xe69c85)");

    // 9. Patch Trophy TrophyId Bounds Check (bypasses 0x80553904 check in 0xe69c9d):
    // Address: mapbase + 0xe65c9d (file offset 0xe69c9d)
    // 0f 83 ea 00 00 00 (jae +0xea) -> 6x NOP
    const unsigned char orig_bounds2[] = {0x0f, 0x83, 0xea, 0x00, 0x00, 0x00};
    apply_patch(pid, mapbase + 0xe65c9d, patch_6nop, orig_bounds2, sizeof(patch_6nop), "Trophy Pack TrophyId Check Bypass (0xe69c9d)");

    // 10. Patch Trophy Group ID Match Check (bypasses 0x80553999 check in 0xe69d35):
    // Address: mapbase + 0xe65d35 (file offset 0xe69d35)
    // 75 56 (jne +0x56) -> 2x NOP
    const unsigned char orig_group_match[] = {0x75, 0x56};
    const unsigned char patch_2nop[] = {0x90, 0x90};
    apply_patch(pid, mapbase + 0xe65d35, patch_2nop, orig_group_match, sizeof(patch_2nop), "Trophy Group ID Match Bypass (0xe69d35)");

    // 11. Patch Trophy Entry Size Match Check (bypasses 0x80553999 check in 0xe69d3f):
    // Address: mapbase + 0xe65d3f (file offset 0xe69d3f)
    // 75 4c (jne +0x4c) -> 2x NOP
    const unsigned char orig_size_match[] = {0x75, 0x4c};
    apply_patch(pid, mapbase + 0xe65d3f, patch_2nop, orig_size_match, sizeof(patch_2nop), "Trophy Entry Size Match Bypass (0xe69d3f)");

    // (removed old #12-#15: inert serviceLabel/param validators + the
    //  counterproductive serviceLabel==0 bypass — see cleanup header above)

    // [K9] Pack-register precondition 1 (old #16):
    // Address: mapbase + 0xbf5a62 (file offset 0xbf9a62)
    // 80 7d 97 01 b8 5b b7 55 80 0f 85 cd 00 00 00 80 7d 96 00 0f 85 c3 00 00 00 -> jmp +0x17 (eb 17); 23x NOP
    const unsigned char orig_pack_check1[] = {
        0x80, 0x7d, 0x97, 0x01, 0xb8, 0x5b, 0xb7, 0x55, 0x80, 0x0f, 0x85, 0xcd, 0x00, 0x00, 0x00,
        0x80, 0x7d, 0x96, 0x00, 0x0f, 0x85, 0xc3, 0x00, 0x00, 0x00
    };
    const unsigned char patch_pack_check1[] = {
        0xeb, 0x17, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
    };
    apply_patch(pid, mapbase + 0xbf5a62, patch_pack_check1, orig_pack_check1, sizeof(patch_pack_check1), "Trophy Pack Check 1 Bypass (0xbf9a62)");

    // 17. Patch Trophy Context Register Pack Check 2 (bypasses 0x8055b75b in 0xbeb9a5):
    // Address: mapbase + 0xbe79a5 (file offset 0xbeb9a5)
    // 80 7d cf 01 b8 5b b7 55 80 75 56 80 7d ce 00 75 50 -> jmp +0x0f (eb 0f); 15x NOP
    const unsigned char orig_pack_check2[] = {
        0x80, 0x7d, 0xcf, 0x01, 0xb8, 0x5b, 0xb7, 0x55, 0x80, 0x75, 0x56, 0x80, 0x7d, 0xce, 0x00, 0x75, 0x50
    };
    const unsigned char patch_pack_check2[] = {
        0xeb, 0x0f, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
    };
    apply_patch(pid, mapbase + 0xbe79a5, patch_pack_check2, orig_pack_check2, sizeof(patch_pack_check2), "Trophy Pack Check 2 Bypass (0xbeb9a5)");

    // [K11] RESTORE the collection ingest/commit function (old #18) @0xe1a4c0.
    // This fn IS the surfacing path (0xe232e0 -> here -> cache ingest 0xe39310
    // -> fan-out 0xe34e70 -> ShellUI). A past agent had stubbed it to
    // `xor eax,eax; ret`, disabling the only real surfacing function while
    // doing nothing for the debug-unlock path (which never reaches it). Write
    // the ORIGINAL prologue back so it stays intact. Idempotent: restores when
    // the old stub is present, and reports "already patched" once original.
    const unsigned char uds_orig[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57}; // real prologue
    const unsigned char uds_stub[] = {0x31, 0xc0, 0xc3, 0x90, 0x90, 0x90}; // the former no-op stub
    apply_patch(pid, mapbase + 0xe1a4c0, uds_orig, uds_stub, sizeof(uds_orig), "RESTORE collection-ingest fn (0xe1a4c0)");

    // 19. Patch Trophy Cache Auto-Enumerate Titles Check (bypasses suppression in 0xe2acea):
    // Address: mapbase + 0xe2acef (file offset 0xe2ecef)
    // 75 09 (jne +0x09) -> 2x NOP
    const unsigned char orig_enum_check[] = {0x75, 0x09};
    apply_patch(pid, mapbase + 0xe2acef, patch_2nop, orig_enum_check, sizeof(patch_2nop), "Trophy Auto-Enumerate Titles Bypass (0xe2acef)");

    // Cache Diagnostic: Inspect NpTrophy2CSCache live state
    intptr_t svc_ptr = 0;
    kernel_proc_copyout(pid, mapbase + 0x22269a0, &svc_ptr, sizeof(svc_ptr));
    klog_printf("[TROPHY_DIAG] SceShellCore NpTrophy2Service* at 0x%lx = 0x%lx\n", mapbase + 0x22269a0, svc_ptr);
    if (svc_ptr > 0) {
        intptr_t cache_ptr = 0;
        kernel_proc_copyout(pid, svc_ptr + 0x80, &cache_ptr, sizeof(cache_ptr));
        klog_printf("[TROPHY_DIAG] NpTrophy2CSCache* at 0x%lx+0x80 = 0x%lx\n", svc_ptr, cache_ptr);
        if (cache_ptr > 0) {
            uint32_t state = 0;
            kernel_proc_copyout(pid, cache_ptr + 0x30, &state, sizeof(state));
            uint8_t flags[16] = {0};
            kernel_proc_copyout(pid, cache_ptr + 0x08, flags, sizeof(flags));
            klog_printf("[TROPHY_DIAG] Cache flags: 0xc=%d, 0xd=%d, 0xe=%d, 0x14=%d, state=0x%x\n",
                        flags[0x0c - 8], flags[0x0d - 8], flags[0x0e - 8], flags[0x14 - 8], state);

            unsigned char cbytes[128] = {0};
            kernel_proc_copyout(pid, cache_ptr, cbytes, sizeof(cbytes));
            klog_printf("[TROPHY_DIAG] Cache raw 00..1f: %016lx %016lx %016lx %016lx\n",
                        *(uint64_t*)(cbytes+0), *(uint64_t*)(cbytes+8), *(uint64_t*)(cbytes+16), *(uint64_t*)(cbytes+24));
            klog_printf("[TROPHY_DIAG] Cache raw 20..3f: %016lx %016lx %016lx %016lx\n",
                        *(uint64_t*)(cbytes+32), *(uint64_t*)(cbytes+40), *(uint64_t*)(cbytes+48), *(uint64_t*)(cbytes+56));
            klog_printf("[TROPHY_DIAG] Cache raw 40..5f: %016lx %016lx %016lx %016lx\n",
                        *(uint64_t*)(cbytes+64), *(uint64_t*)(cbytes+72), *(uint64_t*)(cbytes+80), *(uint64_t*)(cbytes+88));
            klog_printf("[TROPHY_DIAG] Cache raw 60..7f: %016lx %016lx %016lx %016lx\n",
                        *(uint64_t*)(cbytes+96), *(uint64_t*)(cbytes+104), *(uint64_t*)(cbytes+112), *(uint64_t*)(cbytes+120));
        }
    }

    // Scan SceShellCore heap to verify title presence
    {
        int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, pid};
        size_t size = 0;
        if (sysctl(mib, 4, NULL, &size, NULL, 0) == 0 && size > 0) {
            char *vmbuf = malloc(size);
            if (vmbuf && sysctl(mib, 4, vmbuf, &size, NULL, 0) == 0) {
                int found_aw = 0, found_fn = 0;
                for (char *p = vmbuf; p < vmbuf + size; ) {
                    struct kinfo_vmentry *kve = (struct kinfo_vmentry*)p;
                    if (kve->kve_structsize <= 0) break;
                    p += kve->kve_structsize;
                    if ((kve->kve_protection & 3) == 3 && kve->kve_start >= 0x800000000ULL && (kve->kve_end - kve->kve_start) <= 32*1024*1024) {
                        size_t seg_sz = kve->kve_end - kve->kve_start;
                        char *chunk = malloc(seg_sz);
                        if (chunk) {
                            if (kernel_proc_copyout(pid, kve->kve_start, chunk, seg_sz) == 0) {
                                for (size_t i = 0; i + 12 <= seg_sz; i += 4) {
                                    if (!found_aw && memcmp(chunk + i, "NPWR36759_00", 12) == 0) {
                                        found_aw = 1;
                                        klog_printf("[TROPHY_DIAG] Animal Well (NPWR36759_00) found in SceShellCore heap at 0x%lx\n", kve->kve_start + i);
                                    }
                                    if (!found_fn && memcmp(chunk + i, "NPWR20561_00", 12) == 0) {
                                        found_fn = 1;
                                        klog_printf("[TROPHY_DIAG] Fortnite (NPWR20561_00) found in SceShellCore heap at 0x%lx\n", kve->kve_start + i);
                                        unsigned char node[128] = {0};
                                        kernel_proc_copyout(pid, kve->kve_start + i - 32, node, sizeof(node));
                                        klog_printf("[TROPHY_DIAG] Node -32..+0: %016lx %016lx %016lx %016lx\n",
                                                    *(uint64_t*)(node+0), *(uint64_t*)(node+8), *(uint64_t*)(node+16), *(uint64_t*)(node+24));
                                        klog_printf("[TROPHY_DIAG] Node +0..+32: %016lx %016lx %016lx %016lx\n",
                                                    *(uint64_t*)(node+32), *(uint64_t*)(node+40), *(uint64_t*)(node+48), *(uint64_t*)(node+56));
                                        klog_printf("[TROPHY_DIAG] Node +32..+64: %016lx %016lx %016lx %016lx\n",
                                                    *(uint64_t*)(node+64), *(uint64_t*)(node+72), *(uint64_t*)(node+80), *(uint64_t*)(node+88));
                                    }
                                }
                            }
                            free(chunk);
                        }
                    }
                }
                klog_printf("[TROPHY_DIAG] SceShellCore heap scan result: AnimalWell=%d, Fortnite=%d\n", found_aw, found_fn);
            }
            if (vmbuf) free(vmbuf);
        }
    }

    // Permissions for homebrew trophy dirs
    chmod("/user/app", 0777);
    chmod("/user/app/PPSA99810", 0777);
    chmod("/user/app/PPSA99810/sce_sys", 0777);
    chmod("/user/app/PPSA99810/sce_sys/trophy2", 0777);
    chmod("/user/app/PPSA99810/sce_sys/trophy2/npbind.dat", 0666);
    chmod("/user/app/PPSA99810/sce_sys/trophy2/trophy00.ucp", 0666);
    chmod("/user/appmeta/PPSA99810", 0777);
    chmod("/user/appmeta/PPSA99810/trophy2", 0777);
    chmod("/user/appmeta/PPSA99810/trophy2/npbind.dat", 0666);
    chmod("/user/appmeta/PPSA99810/trophy2/trophy00.ucp", 0666);
    chmod("/system_data/priv", 0777);
    chmod("/system_data/priv/appmeta", 0777);
    chmod("/system_data/priv/appmeta/PPSA99810", 0777);
    chmod("/system_data/priv/appmeta/PPSA99810/trophy2", 0777);
    chmod("/system_data/priv/appmeta/PPSA99810/trophy2/npbind.dat", 0666);
    chmod("/system_data/priv/appmeta/PPSA99810/trophy2/trophy00.ucp", 0666);

    // Permissions for Fortnite PPSA01922 trophy dirs
    chmod("/user/app/PPSA01922", 0777);
    chmod("/user/app/PPSA01922/sce_sys", 0777);
    chmod("/user/app/PPSA01922/sce_sys/param.json", 0666);
    chmod("/user/app/PPSA01922/sce_sys/trophy2", 0777);
    chmod("/user/app/PPSA01922/sce_sys/trophy2/npbind.dat", 0666);
    chmod("/user/app/PPSA01922/sce_sys/trophy2/trophy00.ucp", 0666);
    chmod("/user/appmeta/PPSA01922", 0777);
    chmod("/user/appmeta/PPSA01922/param.json", 0666);
    chmod("/user/appmeta/PPSA01922/trophy2", 0777);
    chmod("/user/appmeta/PPSA01922/trophy2/npbind.dat", 0666);
    chmod("/user/appmeta/PPSA01922/trophy2/trophy00.ucp", 0666);
    chmod("/system_data/priv/appmeta/PPSA01922", 0777);
    chmod("/system_data/priv/appmeta/PPSA01922/param.json", 0666);
    chmod("/system_data/priv/appmeta/PPSA01922/trophy2", 0777);
    chmod("/system_data/priv/appmeta/PPSA01922/trophy2/npbind.dat", 0666);
    chmod("/system_data/priv/appmeta/PPSA01922/trophy2/trophy00.ucp", 0666);

    // Unjail eboot.bin from pidfile
    FILE* f = fopen("/data/psbox/eboot_pid.txt", "r");
    if (f) {
        pid_t eboot_pid = 0;
        if (fscanf(f, "%d", &eboot_pid) == 1 && eboot_pid > 0) {
            unjail_pid(eboot_pid);
        }
        fclose(f);
    }

    // Also search for running eboot.bin if any
    pid_t scan_pid = find_process_by_name("eboot.bin");
    if (scan_pid > 0) {
        unjail_pid(scan_pid);
    }

    return 0;
}
