// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "media/hw_decoder.h"
#include "util/log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>

extern "C" {
int sceSysmoduleLoadModule(uint32_t id);
int sceSysmoduleLoadModuleInternal(uint32_t id);
int sceKernelLoadStartModule(const char* path, size_t argc, const void* argv, uint32_t flags, void* opt, int* res);
size_t sceKernelGetDirectMemorySize();
int sceKernelAllocateDirectMemory(int64_t searchStart, int64_t searchEnd, size_t length, size_t alignment,
                                  int memoryType, int64_t* physicalAddress);
int sceKernelMapDirectMemory(void** address, size_t length, int protection, int flags, int64_t physicalAddress,
                             size_t alignment);
int sceKernelMapNamedFlexibleMemory(void** address, size_t length, int protection, int flags, const char* name);

// PS4 libSceVideodec2 structures
struct Vdec2DecoderConfig {
    uint64_t size;
    uint32_t resourceType, codecType, profile, maxLevel;
    int32_t maxWidth, maxHeight, maxDpbFrames;
    uint32_t pipelineDepth;
    uint64_t computeQueue, cpuAffinity;
    int32_t cpuPriority;
    uint32_t optimizeProgressive, checkMemoryType, reserved;
};

struct Vdec2DecoderMemory {
    uint64_t size, cpuSize;
    void* cpu;
    uint64_t gpuSize;
    void* gpu;
    uint64_t cpuGpuSize;
    void* cpuGpu;
    uint64_t maxFrameSize;
    uint32_t frameAlignment, reserved;
};

struct Vdec2ComputeConfig {
    uint64_t size;
    uint16_t pipeId, queueId;
    uint8_t checkMemoryType, reserved0;
    uint16_t reserved1;
};

struct Vdec2ComputeMemory {
    uint64_t size, cpuGpuSize;
    void* cpuGpu;
};

struct Vdec2Input {
    uint64_t size;
    void* au;
    uint64_t auSize, pts, dts, attached;
};

struct Vdec2Frame {
    uint64_t size;
    void* buffer;
    uint64_t bufferSize;
    uint32_t accepted, reserved;
};

struct Vdec2Output {
    uint64_t size;
    uint8_t valid, error, pictureCount, padding;
    uint32_t codec, width, pitch, height, reserved;
    void* buffer;
    uint64_t bufferSize;
    uint32_t frameFormat, pitchBytes;
};

using FnQueryComputeMem = int (*)(Vdec2ComputeMemory*);
using FnAllocComputeQueue = int (*)(const Vdec2ComputeConfig*, const Vdec2ComputeMemory*, void**);
using FnReleaseComputeQueue = int (*)(void*);
using FnQueryDecoderMem = int (*)(const Vdec2DecoderConfig*, Vdec2DecoderMemory*);
using FnCreateDecoder = int (*)(const Vdec2DecoderConfig*, const Vdec2DecoderMemory*, void**);
using FnDeleteDecoder = int (*)(void*);
using FnDecode = int (*)(void*, Vdec2Input*, Vdec2Frame*, Vdec2Output*);
using FnFlush = int (*)(void*, Vdec2Frame*, Vdec2Output*);

struct Videodec2Api {
    FnQueryComputeMem queryComputeMem = nullptr;
    FnAllocComputeQueue allocComputeQueue = nullptr;
    FnReleaseComputeQueue releaseComputeQueue = nullptr;
    FnQueryDecoderMem queryDecoderMem = nullptr;
    FnCreateDecoder createDecoder = nullptr;
    FnDeleteDecoder deleteDecoder = nullptr;
    FnDecode decode = nullptr;
    FnFlush flush = nullptr;
    bool loaded = false;
};
static Videodec2Api g_vdec;
}

namespace xc::media {

namespace {

constexpr uint32_t kVideodecModule = 0x00CF; // SCE_SYSMODULE_VIDEODEC2
constexpr int kMemoryTypeOnion = 0;          // Coherent CPU+GPU
constexpr int kMemoryTypeGarlic = 3;         // GPU Direct Memory
constexpr int kProtectionRw = 0x33;
constexpr const char* kProbeFile = "/data/psbox/hwdec.probe";

inline size_t align16k(size_t n) {
    return (n + 0x3FFF) & ~static_cast<size_t>(0x3FFF);
}

void* allocDirect(size_t size, int memoryType, const char* label) {
    if (size == 0) return nullptr;
    size = align16k(size);
    int64_t phys = 0;
    int rc = sceKernelAllocateDirectMemory(0, static_cast<int64_t>(sceKernelGetDirectMemorySize()),
                                           size, 0x4000, memoryType, &phys);
    if (rc != 0) {
        XC_LOGW("hwdec: %s: allocate %zu bytes failed: 0x%08x", label, size, static_cast<unsigned>(rc));
        return nullptr;
    }
    void* addr = nullptr;
    rc = sceKernelMapDirectMemory(&addr, size, kProtectionRw, 0, phys, 0x4000);
    if (rc != 0 || !addr) {
        XC_LOGW("hwdec: %s: map %zu bytes failed: 0x%08x", label, size, static_cast<unsigned>(rc));
        return nullptr;
    }
    return addr;
}

void writeProbe(const char* state) {
    int fd = ::open(kProbeFile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        (void)::write(fd, state, std::strlen(state));
        ::close(fd);
    }
}

void removeProbe() {
    ::unlink(kProbeFile);
}

bool hasProbe() {
    return ::access(kProbeFile, F_OK) == 0;
}

} // namespace

HwDecoder::~HwDecoder() {
    if (decoder_) {
        Vdec2Frame frm{};
        frm.size = sizeof(frm);
        Vdec2Output out{};
        out.size = sizeof(out);
        if (g_vdec.flush) g_vdec.flush(decoder_, &frm, &out);
        if (g_vdec.deleteDecoder) g_vdec.deleteDecoder(decoder_);
        decoder_ = nullptr;
    }
    if (computeQueue_) {
        if (g_vdec.releaseComputeQueue) g_vdec.releaseComputeQueue(computeQueue_);
        computeQueue_ = nullptr;
    }
    frameBuffer_ = nullptr;
    frameBufferSize_ = 0;
}

bool HwDecoder::init(int maxWidth, int maxHeight) {
    if (hasProbe()) {
        XC_LOGW("hwdec: previous crash probe file found (%s); disabling hardware decoding", kProbeFile);
        return false;
    }

    writeProbe("starting");

    int rc = sceSysmoduleLoadModule(kVideodecModule);
    XC_LOGI("hwdec: sceSysmoduleLoadModule(VIDEODEC2) -> 0x%08x", static_cast<unsigned>(rc));
    if (rc < 0 && rc != static_cast<int>(0x80020002) && rc != static_cast<int>(0x8002000e)) {
        removeProbe();
        return false;
    }

    if (!g_vdec.loaded) {
        g_vdec.queryComputeMem = reinterpret_cast<FnQueryComputeMem>(dlsym(RTLD_DEFAULT, "sceVideodec2QueryComputeMemoryInfo"));
        g_vdec.allocComputeQueue = reinterpret_cast<FnAllocComputeQueue>(dlsym(RTLD_DEFAULT, "sceVideodec2AllocateComputeQueue"));
        g_vdec.releaseComputeQueue = reinterpret_cast<FnReleaseComputeQueue>(dlsym(RTLD_DEFAULT, "sceVideodec2ReleaseComputeQueue"));
        g_vdec.queryDecoderMem = reinterpret_cast<FnQueryDecoderMem>(dlsym(RTLD_DEFAULT, "sceVideodec2QueryDecoderMemoryInfo"));
        g_vdec.createDecoder = reinterpret_cast<FnCreateDecoder>(dlsym(RTLD_DEFAULT, "sceVideodec2CreateDecoder"));
        g_vdec.deleteDecoder = reinterpret_cast<FnDeleteDecoder>(dlsym(RTLD_DEFAULT, "sceVideodec2DeleteDecoder"));
        g_vdec.decode = reinterpret_cast<FnDecode>(dlsym(RTLD_DEFAULT, "sceVideodec2Decode"));
        g_vdec.flush = reinterpret_cast<FnFlush>(dlsym(RTLD_DEFAULT, "sceVideodec2Flush"));
        if (!g_vdec.queryComputeMem || !g_vdec.allocComputeQueue || !g_vdec.createDecoder || !g_vdec.decode) {
            XC_LOGW("hwdec: could not resolve all libSceVideodec2 symbols via dlsym; falling back to software decoding");
            removeProbe();
            return false;
        }
        g_vdec.loaded = true;
    }

    // Load firmware helper modules (available on retail PS4 firmware)
    for (const char* sprx : {
        "/system/common/lib/libSceVdecCore.sprx",
        "/system/common/lib/libSceVdecSavc.sprx",
        "/system/common/lib/libSceVdecSavc2.sprx",
        "/system/common/lib/libSceVdecwrap.sprx"
    }) {
        int res = 0;
        int mrc = sceKernelLoadStartModule(sprx, 0, nullptr, 0, nullptr, &res);
        XC_LOGI("hwdec: load %s -> 0x%08x", sprx, static_cast<unsigned>(mrc));
    }
    sceSysmoduleLoadModuleInternal(0x80000015);

    // Query and allocate compute queue memory
    Vdec2ComputeMemory computeMem{};
    computeMem.size = sizeof(computeMem);
    rc = g_vdec.queryComputeMem(&computeMem);
    XC_LOGI("hwdec: compute memory query: rc=0x%08x, %llu bytes", static_cast<unsigned>(rc),
            static_cast<unsigned long long>(computeMem.cpuGpuSize));
    if (rc != 0 || computeMem.cpuGpuSize == 0) {
        removeProbe();
        return false;
    }

    size_t computeBytes = align16k(computeMem.cpuGpuSize);
    void* computeBuf = allocDirect(computeBytes, kMemoryTypeOnion, "compute queue");
    if (!computeBuf) {
        removeProbe();
        return false;
    }
    computeMem.cpuGpu = computeBuf;

    // Allocate compute queue across pipes 0..4
    void* queue = nullptr;
    for (uint16_t pipe = 0; pipe < 5; ++pipe) {
        Vdec2ComputeConfig compCfg{};
        compCfg.size = sizeof(compCfg);
        compCfg.pipeId = pipe;
        compCfg.queueId = 0;
        compCfg.checkMemoryType = 1;

        writeProbe("compute queue");
        rc = g_vdec.allocComputeQueue(&compCfg, &computeMem, &queue);
        removeProbe();

        XC_LOGI("hwdec: compute queue pipe %u queue 0: rc=0x%08x, queue=%p", pipe, static_cast<unsigned>(rc), queue);
        if (rc == 0 && queue) break;
    }
    if (!queue) {
        XC_LOGW("hwdec: failed to allocate compute queue on all pipes");
        removeProbe();
        return false;
    }
    computeQueue_ = queue;

    // Query decoder memory requirements
    Vdec2DecoderConfig decCfg{};
    decCfg.size = sizeof(decCfg);
    decCfg.resourceType = 0; // standard
    decCfg.codecType = 0;    // H.264
    decCfg.profile = 2;      // High profile
    decCfg.maxLevel = 42;    // Level 4.2
    decCfg.maxWidth = maxWidth;
    decCfg.maxHeight = maxHeight;
    decCfg.maxDpbFrames = 4;
    decCfg.pipelineDepth = 2;
    decCfg.computeQueue = reinterpret_cast<uint64_t>(computeQueue_);

    Vdec2DecoderMemory decMem{};
    decMem.size = sizeof(decMem);
    rc = g_vdec.queryDecoderMem(&decCfg, &decMem);
    if (rc != 0) {
        XC_LOGW("hwdec: sceVideodec2QueryDecoderMemoryInfo failed: 0x%08x", static_cast<unsigned>(rc));
        removeProbe();
        return false;
    }

    if (decMem.cpuSize > 0) {
        size_t cpuBytes = align16k(decMem.cpuSize);
        void* cpuMem = nullptr;
        rc = sceKernelMapNamedFlexibleMemory(&cpuMem, cpuBytes, kProtectionRw, 0, "PSBoxVdecCpu");
        if (rc != 0 || !cpuMem) {
            XC_LOGW("hwdec: allocate flexible memory %zu failed: 0x%08x", cpuBytes, static_cast<unsigned>(rc));
            removeProbe();
            return false;
        }
        decMem.cpu = cpuMem;
        decMem.cpuSize = cpuBytes;
    }

    if (decMem.gpuSize > 0) {
        decMem.gpu = allocDirect(decMem.gpuSize, kMemoryTypeGarlic, "decoder gpu");
        if (!decMem.gpu) { removeProbe(); return false; }
    }

    if (decMem.cpuGpuSize > 0) {
        decMem.cpuGpu = allocDirect(decMem.cpuGpuSize, kMemoryTypeOnion, "decoder cpu+gpu");
        if (!decMem.cpuGpu) { removeProbe(); return false; }
    }

    frameBufferSize_ = align16k(decMem.maxFrameSize > 0 ? decMem.maxFrameSize : (static_cast<size_t>(maxWidth) * maxHeight * 3 / 2 + 64));
    frameBuffer_ = static_cast<uint8_t*>(allocDirect(frameBufferSize_, kMemoryTypeGarlic, "frame buffer"));
    if (!frameBuffer_) {
        removeProbe();
        return false;
    }

    writeProbe("create decoder");
    rc = g_vdec.createDecoder(&decCfg, &decMem, &decoder_);
    removeProbe();

    XC_LOGI("hwdec: sceVideodec2CreateDecoder -> 0x%08x", static_cast<unsigned>(rc));
    if (rc != 0 || !decoder_) {
        decoder_ = nullptr;
        return false;
    }

    XC_LOGI("hwdec: PS4 decoder running, frame buffer %zu bytes", frameBufferSize_);
    return true;
}

bool HwDecoder::decode(const uint8_t* data, size_t len, uint64_t pts, Picture& out) {
    if (!decoder_ || !data || len == 0 || !frameBuffer_) return false;

    Vdec2Input input{};
    input.size = sizeof(input);
    input.au = const_cast<uint8_t*>(data);
    input.auSize = len;
    input.pts = pts;

    Vdec2Frame frame{};
    frame.size = sizeof(frame);
    frame.buffer = frameBuffer_;
    frame.bufferSize = frameBufferSize_;

    Vdec2Output output{};
    output.size = sizeof(output);

    int rc = g_vdec.decode ? g_vdec.decode(decoder_, &input, &frame, &output) : -1;
    if (rc != 0) {
        return false;
    }

    if (output.valid && output.pictureCount > 0) {
        out.data = static_cast<const uint8_t*>(output.buffer ? output.buffer : frameBuffer_);
        out.size = output.bufferSize ? output.bufferSize : frameBufferSize_;
        out.width = output.width;
        out.height = output.height;
        out.pitch = output.pitch;
        out.pitchBytes = output.pitchBytes ? output.pitchBytes : (output.width);
        out.format = output.frameFormat;
        out.pts = pts;
        out.error = (output.error != 0);
        return true;
    }

    return false;
}

} // namespace xc::media
