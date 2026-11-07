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

static void patch_client_trophy_module(pid_t pid) {
    if (pid <= 0) return;
    intptr_t trophy_base = 0;
    uint32_t handle = 0;
    if (kernel_dynlib_handle(pid, "libSceNpTrophy2.sprx", &handle) == 0 && handle > 0) {
        trophy_base = kernel_dynlib_mapbase_addr(pid, handle);
        klog_printf("[TROPHY_PATCH] kernel_dynlib_handle found libSceNpTrophy2.sprx handle=%u base=0x%lx\n", handle, trophy_base);
    }
    if (trophy_base == 0) {
        int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, pid};
        size_t size = 0;
        if (sysctl(mib, 4, NULL, &size, NULL, 0) == 0 && size > 0) {
            char *buf = malloc(size);
            if (buf && sysctl(mib, 4, buf, &size, NULL, 0) == 0) {
                for (char *p = buf; p < buf + size; ) {
                    struct kinfo_vmentry *kve = (struct kinfo_vmentry*)p;
                    if (kve->kve_structsize <= 0) break;
                    p += kve->kve_structsize;
                    if (kve->kve_protection & 4) {
                        if (strstr(kve->kve_path, "libSceNpTrophy2") != NULL) {
                            trophy_base = kve->kve_start;
                            break;
                        }
                        // Signature fallback check (0x6d90 in libSceNpTrophy2 text segment)
                        unsigned char sig[8] = {0};
                        if (kernel_proc_copyout(pid, kve->kve_start + 0x6d90, sig, sizeof(sig)) == 0) {
                            const unsigned char expected_sig[8] = {0x83, 0x7f, 0x30, 0x00, 0x0f, 0x94, 0xc0, 0xc3};
                            const unsigned char patched_sig[8] = {0xb0, 0x01, 0xc3, 0x90, 0x90, 0x90, 0x90, 0x90};
                            if (memcmp(sig, expected_sig, 8) == 0 || memcmp(sig, patched_sig, 8) == 0) {
                                trophy_base = kve->kve_start;
                                break;
                            }
                        }
                    }
                }
            }
            free(buf);
        }
    }
    if (trophy_base == 0) {
        klog_printf("[TROPHY_PATCH] libSceNpTrophy2.sprx not found in PID %d vmmap yet\n", pid);
        return;
    }
    klog_printf("[TROPHY_PATCH] Found libSceNpTrophy2.sprx in PID %d at 0x%lx\n", pid, trophy_base);

    // Patch 0x6d90: 83 7f 30 00 0f 94 c0 c3 -> b0 01 c3 90 90 90 90 90 (mov al, 1; ret; 5x NOP)
    const unsigned char orig_6d90[8] = {0x83, 0x7f, 0x30, 0x00, 0x0f, 0x94, 0xc0, 0xc3};
    const unsigned char patch_al_one[8] = {0xb0, 0x01, 0xc3, 0x90, 0x90, 0x90, 0x90, 0x90};
    apply_patch(pid, trophy_base + 0x6d90, patch_al_one, orig_6d90, sizeof(patch_al_one), "Client NpTrophy2 0x6d90 bypass");

    // Patch 0x6da0: 83 7f 30 01 0f 94 c0 c3 -> b0 01 c3 90 90 90 90 90 (mov al, 1; ret; 5x NOP)
    const unsigned char orig_6da0[8] = {0x83, 0x7f, 0x30, 0x01, 0x0f, 0x94, 0xc0, 0xc3};
    apply_patch(pid, trophy_base + 0x6da0, patch_al_one, orig_6da0, sizeof(patch_al_one), "Client NpTrophy2 0x6da0 bypass");

    // [B2] Inject NpAsmClient cache in libSceNpManager.sprx (FW 13.60 DAT_01063178)
    uint32_t npmgr_handle = 0;
    intptr_t npmgr_base = 0;
    if (kernel_dynlib_handle(pid, "libSceNpManager.sprx", &npmgr_handle) == 0 && npmgr_handle > 0) {
        npmgr_base = kernel_dynlib_mapbase_addr(pid, npmgr_handle);
    }
    if (npmgr_base > 0) {
        uint64_t struct_va = 0;
        kernel_proc_copyout(pid, npmgr_base + 0x63178, &struct_va, sizeof(struct_va));
        klog_printf("[TROPHY_PATCH] B2 libSceNpManager base=0x%lx struct_va=0x%lx\n", npmgr_base, struct_va);
        if (struct_va > 0) {
            char fake_title_id[16] = "PPSA01922_00";
            uint8_t fake_secret[128];
            memset(fake_secret, 0x5a, sizeof(fake_secret));
            uint8_t b1_flag = 1;

            kernel_proc_copyin(pid, fake_title_id, struct_va + 0x14, 16);
            kernel_proc_copyin(pid, fake_secret, struct_va + 0x24, 128);
            kernel_proc_copyin(pid, &b1_flag, struct_va + 0xa4, 1);
            kernel_proc_copyin(pid, &b1_flag, struct_va + 0xa5, 1);
            kernel_proc_copyin(pid, &b1_flag, struct_va + 0xa6, 1);
            klog_printf("[TROPHY_PATCH] B2 NpAsmClient injected for PPSA01922_00 (struct_va=0x%lx)\n", struct_va);
        }
    }
}

int main(void) {
    // 1. Immediately unjail eboot.bin from pidfile or process scan
    FILE* f = fopen("/data/psbox/eboot_pid.txt", "r");
    if (f) {
        pid_t eboot_pid = 0;
        if (fscanf(f, "%d", &eboot_pid) == 1 && eboot_pid > 0) {
            unjail_pid(eboot_pid);
            patch_client_trophy_module(eboot_pid);
        }
        fclose(f);
    }
    pid_t scan_pid = find_process_by_name("eboot.bin");
    if (scan_pid > 0) {
        unjail_pid(scan_pid);
        patch_client_trophy_module(scan_pid);
    }

    // 2. Trophy directory permissions
    mkdir("/data/psbox", 0777);
    chmod("/data/psbox", 0777);
    chmod("/user/trophy2", 0777);
    chmod("/user/trophy2/nobackup", 0777);
    chmod("/user/trophy2/nobackup/conf", 0777);
    chmod("/user/trophy2/nobackup/conf/NPWR20561_00", 0777);
    chmod("/user/trophy2/nobackup/conf/NPWR20561_00/TROPHY.UCP", 0666);

    chmod("/user/home/1ea2f4da/trophy2", 0777);
    chmod("/user/home/1ea2f4da/trophy2/nobackup", 0777);
    chmod("/user/home/1ea2f4da/trophy2/nobackup/data", 0777);
    chmod("/user/home/1ea2f4da/trophy2/nobackup/data/NPWR20561_00", 0777);
    chmod("/user/home/1ea2f4da/trophy2/nobackup/data/NPWR20561_00/TRPTITLE.DAT", 0666);
    chmod("/system_data/priv/mms", 0777);
    chmod("/system_data/priv/mms/app.db", 0666);
    chmod("/system_data/priv/mms/app.db-wal", 0666);
    chmod("/system_data/priv/mms/app.db-shm", 0666);

    pid_t pid = find_process_by_name("SceShellCore");
    intptr_t mapbase = get_process_mapbase(pid);
    klog_printf("[TROPHY_PATCH] SceShellCore dynamic pid=%d, mapbase=0x%lx\n", pid, mapbase);

    if (pid <= 0 || mapbase == 0) {
        klog_printf("[TROPHY_PATCH] ERROR: Failed to resolve SceShellCore PID or mapbase\n");
        return 1;
    }

    // =====================================================================
    // SceShellCore trophy patches — VERIFIED 2026-11-04 FW 13.60.
    // Every single offset below is byte-verified against decrypted FW 13.60
    // SceShellCore memory dump (sc_text.bin, sc_rodata.bin, sc_data.bin).
    // =====================================================================

    // [B1] SceShellCore "AAAA00000" string patches -> "ZZZZZZZZZ" for arbitrary commId acceptance (FW 13.60)
    const unsigned char aaaa_orig[10] = "AAAA00000";
    const unsigned char aaaa_patch[10] = "ZZZZZZZZZ";
    const intptr_t b1_offsets[] = {
        0x2070788, 0x2070794, 0x2070ee8, 0x2070ef4, 0x1f32868
    };
    for (size_t i = 0; i < sizeof(b1_offsets)/sizeof(b1_offsets[0]); i++) {
        char name[64];
        snprintf(name, sizeof(name), "B1 commId slot %zu (0x%lx)", i, b1_offsets[i]);
        apply_patch(pid, mapbase + b1_offsets[i], aaaa_patch, aaaa_orig, sizeof(aaaa_patch), name);
    }

    // [B5_LNC] Scan for LncManager::getSceSysDirPath attr gate and patch JZ -> NOP
    intptr_t b5_lnc_addr = 0;
    unsigned char scan_buf[4096];
    for (intptr_t off = 0x1500000; off < 0x1800000; off += 3800) {
        if (kernel_proc_copyout(pid, mapbase + off, scan_buf, sizeof(scan_buf)) != 0) continue;
        for (size_t i = 0; i < sizeof(scan_buf) - 16; i++) {
            if (scan_buf[i] == 0xf6 && scan_buf[i+1] == 0x86 &&
                ((scan_buf[i+2] == 0xd4 && scan_buf[i+3] == 0x00 && scan_buf[i+4] == 0x00 && scan_buf[i+5] == 0x00 && scan_buf[i+6] == 0x06) ||
                 (scan_buf[i+2] == 0x04 && scan_buf[i+3] == 0x01 && scan_buf[i+4] == 0x00 && scan_buf[i+5] == 0x00 && scan_buf[i+6] == 0x06))) {
                intptr_t jz_addr = mapbase + off + i + 7;
                klog_printf("[TROPHY_PATCH] B5_LNC candidate found at mapbase + 0x%lx (jz at 0x%lx): %02x %02x %02x %02x %02x %02x\n",
                            off + i, jz_addr, scan_buf[i+7], scan_buf[i+8], scan_buf[i+9], scan_buf[i+10], scan_buf[i+11], scan_buf[i+12]);
                b5_lnc_addr = jz_addr;
                break;
            }
        }
        if (b5_lnc_addr != 0) break;
    }
    if (b5_lnc_addr != 0) {
        unsigned char jz_bytes[6] = {0};
        kernel_proc_copyout(pid, b5_lnc_addr, jz_bytes, 6);
        if (jz_bytes[0] == 0x74) {
            const unsigned char orig_2b[2] = { jz_bytes[0], jz_bytes[1] };
            const unsigned char p_2nop[2] = { 0x90, 0x90 };
            apply_patch(pid, b5_lnc_addr, p_2nop, orig_2b, 2, "B5_LNC short JZ bypass");
        } else if (jz_bytes[0] == 0x0f && jz_bytes[1] == 0x84) {
            const unsigned char p_6nop[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
            apply_patch(pid, b5_lnc_addr, p_6nop, jz_bytes, 6, "B5_LNC long JZ bypass");
        }
    } else {
        klog_printf("[TROPHY_PATCH] B5_LNC scan completed (not found in 0x1500000..0x1800000)\n");
    }

    // [B4] SceShellCore app category gate in RegisterContextIpcJob (0xe19d74 on FW 13.60)
    // Forces calling process category to 2 (Installed Game) and returns 0 (success)
    const unsigned char b4_orig[28] = {
        0x85, 0xc0, 0x0f, 0x88, 0x66, 0x01, 0x00, 0x00,
        0x8b, 0x85, 0x1c, 0xfb, 0xff, 0xff,
        0x31, 0xdb,
        0x83, 0xc0, 0xfc,
        0x83, 0xf8, 0xfe,
        0x0f, 0x82, 0x54, 0x01, 0x00, 0x00
    };
    const unsigned char b4_patch[28] = {
        0xc7, 0x85, 0x1c, 0xfb, 0xff, 0xff, 0x02, 0x00, 0x00, 0x00, // mov dword ptr [rbp - 0x4e4], 2
        0x31, 0xc0,                                                 // xor eax, eax
        0x31, 0xdb,                                                 // xor ebx, ebx
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90                          // 14x NOP
    };
    apply_patch(pid, mapbase + 0xe19d74, b4_patch, b4_orig, sizeof(b4_patch), "B4 App Category Bypass (0xe19d74)");

    // [K1] Trophy config load/init guard:
    // Address: mapbase + 0xe6b1ff
    // 78 45 (js +0x45) -> 2x NOP
    const unsigned char orig_comm_info[] = {0x78, 0x45};
    const unsigned char patch_comm_info[] = {0x90, 0x90};
    apply_patch(pid, mapbase + 0xe6b1ff, patch_comm_info, orig_comm_info, sizeof(patch_comm_info), "Trophy config load guard (0xe6b1ff)");

    // [K2] Debug-unlock IPC gate:
    // Address: mapbase + 0xeb4716
    // 75 36 (jne +0x36) -> 2x NOP
    const unsigned char orig_ipc_debug[] = {0x75, 0x36};
    const unsigned char patch_ipc_debug[] = {0x90, 0x90};
    apply_patch(pid, mapbase + 0xeb4716, patch_ipc_debug, orig_ipc_debug, sizeof(patch_ipc_debug), "Trophy debug IPC gate (0xeb4716)");

    // [K3] Debug/dev status gate:
    // Address: mapbase + 0xb20050
    // 55 48 89 e5 53 48 -> mov eax, 1; ret
    const unsigned char orig_debug_fn[] = {0x55, 0x48, 0x89, 0xe5, 0x53, 0x48};
    const unsigned char patch_debug_fn[] = {0xb8, 0x01, 0x00, 0x00, 0x00, 0xc3};
    apply_patch(pid, mapbase + 0xb20050, patch_debug_fn, orig_debug_fn, sizeof(patch_debug_fn), "Trophy debug/dev status gate (0xb20050)");

    // [K4] Unlock reader already-unlocked bitmap test:
    // Address: mapbase + 0xe5c1b4
    // 0f 83 b8 00 00 00 (jae +0xb8) -> 6x NOP
    const unsigned char orig_bitmask_check[] = {0x0f, 0x83, 0xb8, 0x00, 0x00, 0x00};
    const unsigned char patch_6nop[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    const unsigned char patch_2nop[] = {0x90, 0x90};
    apply_patch(pid, mapbase + 0xe5c1b4, patch_6nop, orig_bitmask_check, sizeof(patch_6nop), "Unlock already-unlocked bitmap (0xe5c1b4)");

    // [K5] Unlock worker group bounds:
    // Address: mapbase + 0xe65c85
    // 0f 83 02 01 00 00 (jae +0x102) -> 6x NOP
    const unsigned char orig_bounds1[] = {0x0f, 0x83, 0x02, 0x01, 0x00, 0x00};
    apply_patch(pid, mapbase + 0xe65c85, patch_6nop, orig_bounds1, sizeof(patch_6nop), "Trophy Pack Group Check Bypass (0xe65c85)");

    // [K6] Trophy TrophyId Bounds Check:
    // Address: mapbase + 0xe65c9d
    // 0f 83 ea 00 00 00 (jae +0xea) -> 6x NOP
    const unsigned char orig_bounds2[] = {0x0f, 0x83, 0xea, 0x00, 0x00, 0x00};
    apply_patch(pid, mapbase + 0xe65c9d, patch_6nop, orig_bounds2, sizeof(patch_6nop), "Trophy Pack TrophyId Check Bypass (0xe65c9d)");

    // [K7] Trophy Group ID Match Check:
    // Address: mapbase + 0xe65d35
    // 75 56 (jne +0x56) -> 2x NOP
    const unsigned char orig_group_match[] = {0x75, 0x56};
    apply_patch(pid, mapbase + 0xe65d35, patch_2nop, orig_group_match, sizeof(patch_2nop), "Trophy Group ID Match Bypass (0xe65d35)");

    // [K8] Trophy Entry Size Match Check:
    // Address: mapbase + 0xe65d3f
    // 75 4c (jne +0x4c) -> 2x NOP
    const unsigned char orig_size_match[] = {0x75, 0x4c};
    apply_patch(pid, mapbase + 0xe65d3f, patch_2nop, orig_size_match, sizeof(patch_2nop), "Trophy Entry Size Match Bypass (0xe65d3f)");

    // [K9] Pack-register precondition 1:
    // Address: mapbase + 0xbf5a62
    const unsigned char orig_pack_check1[] = {
        0x80, 0x7d, 0x97, 0x01, 0xb8, 0x5b, 0xb7, 0x55, 0x80, 0x0f, 0x85, 0xcd, 0x00, 0x00, 0x00,
        0x80, 0x7d, 0x96, 0x00, 0x0f, 0x85, 0xc3, 0x00, 0x00, 0x00
    };
    const unsigned char patch_pack_check1[] = {
        0xeb, 0x17, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
    };
    apply_patch(pid, mapbase + 0xbf5a62, patch_pack_check1, orig_pack_check1, sizeof(patch_pack_check1), "Trophy Pack Check 1 Bypass (0xbf5a62)");

    // [K10] Pack-register precondition 2:
    // Address: mapbase + 0xbe79a5
    const unsigned char orig_pack_check2[] = {
        0x80, 0x7d, 0xcf, 0x01, 0xb8, 0x5b, 0xb7, 0x55, 0x80, 0x75, 0x56, 0x80, 0x7d, 0xce, 0x00, 0x75, 0x50
    };
    const unsigned char patch_pack_check2[] = {
        0xeb, 0x0f, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
    };
    apply_patch(pid, mapbase + 0xbe79a5, patch_pack_check2, orig_pack_check2, sizeof(patch_pack_check2), "Trophy Pack Check 2 Bypass (0xbe79a5)");

    // [K11] RESTORE the collection ingest/commit function prologue @0xe1a4c0.
    // This fn IS the surfacing path (0xe232e0 -> here -> cache ingest 0xe39310
    // -> fan-out 0xe34e70 -> ShellUI). Write the ORIGINAL prologue back!
    const unsigned char uds_orig[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57}; // real prologue
    const unsigned char uds_stub[] = {0x31, 0xc0, 0xc3, 0x90, 0x90, 0x90}; // former no-op stub
    apply_patch(pid, mapbase + 0xe1a4c0, uds_orig, uds_stub, sizeof(uds_orig), "RESTORE collection-ingest fn (0xe1a4c0)");

    // [K12] Auto-Enumerate Titles Check:
    // Address: mapbase + 0xe2acef
    // 75 09 (jne +0x09) -> 2x NOP
    const unsigned char orig_enum_check[] = {0x75, 0x09};
    apply_patch(pid, mapbase + 0xe2acef, patch_2nop, orig_enum_check, sizeof(patch_2nop), "Trophy Auto-Enumerate Titles Bypass (0xe2acef)");

    // [B12] Bypass commId NULL check JZ -> NOP NOP in LoadNpBindDat callers (0xe6a4fa and 0xe6a9ea on FW 13.60)
    // Fixes RegisterContext error 0x8055391a
    const unsigned char orig_b12[] = {0x74, 0x6a};
    const unsigned char patch_b12[] = {0x90, 0x90};
    apply_patch(pid, mapbase + 0xe6a4fa, patch_b12, orig_b12, sizeof(patch_b12), "B12 commId check bypass (0xe6a4fa)");
    apply_patch(pid, mapbase + 0xe6a9ea, patch_b12, orig_b12, sizeof(patch_b12), "B12 commId check bypass (0xe6a9ea)");

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

    // Cronos /system_data/game/temp dirs (PS5 FW 10.00+ and 13.60)
    chmod("/system_data/game", 0777);
    chmod("/system_data/game/temp", 0777);
    chmod("/system_data/game/temp/PPSA99810", 0777);
    chmod("/system_data/game/temp/PPSA99810/trophy2", 0777);
    chmod("/system_data/game/temp/PPSA99810/npbind.dat", 0666);
    chmod("/system_data/game/temp/PPSA99810/trophy2/npbind.dat", 0666);
    chmod("/system_data/game/temp/PPSA99810/trophy2/trophy00.ucp", 0666);

    chmod("/system_data/game/temp/PPSA01922", 0777);
    chmod("/system_data/game/temp/PPSA01922/trophy2", 0777);
    chmod("/system_data/game/temp/PPSA01922/npbind.dat", 0666);
    chmod("/system_data/game/temp/PPSA01922/trophy2/npbind.dat", 0666);
    chmod("/system_data/game/temp/PPSA01922/trophy2/trophy00.ucp", 0666);

    FILE* done_log = fopen("/data/trophy_patch_log.txt", "w");
    if (done_log) {
        fprintf(done_log, "PSBox_Trophies DONE\n");
        fclose(done_log);
        chmod("/data/trophy_patch_log.txt", 0666);
    }

    return 0;
}
