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

// PS4 libSceVideodec2 native functions linked via -lSceVideodec2
int sceVideodec2QueryComputeMemoryInfo(Vdec2ComputeMemory* mem);
int sceVideodec2AllocateComputeQueue(const Vdec2ComputeConfig* cfg, const Vdec2ComputeMemory* mem, void** queue);
int sceVideodec2ReleaseComputeQueue(void* queue);
int sceVideodec2QueryDecoderMemoryInfo(const Vdec2DecoderConfig* cfg, Vdec2DecoderMemory* mem);
int sceVideodec2CreateDecoder(const Vdec2DecoderConfig* cfg, const Vdec2DecoderMemory* mem, void** decoder);
int sceVideodec2DeleteDecoder(void* decoder);
int sceVideodec2Decode(void* decoder, Vdec2Input* input, Vdec2Frame* frame, Vdec2Output* output);
int sceVideodec2Flush(void* decoder, Vdec2Frame* frame, Vdec2Output* output);
}

namespace xc::media {

namespace {

constexpr uint32_t kVideodecModule = 0x00CF; // SCE_SYSMODULE_VIDEODEC2
constexpr int kMemoryTypeOnion = 0;          // Coherent CPU+GPU
constexpr int kMemoryTypeGarlic = 3;         // GPU Direct Memory
constexpr int kProtectionRw = 0x33;

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

} // namespace

HwDecoder::~HwDecoder() {
    if (decoder_) {
        Vdec2Frame frm{};
        frm.size = sizeof(frm);
        Vdec2Output out{};
        out.size = sizeof(out);
        sceVideodec2Flush(decoder_, &frm, &out);
        sceVideodec2DeleteDecoder(decoder_);
        decoder_ = nullptr;
    }
    if (computeQueue_) {
        sceVideodec2ReleaseComputeQueue(computeQueue_);
        computeQueue_ = nullptr;
    }
    frameBuffer_ = nullptr;
    frameBufferSize_ = 0;
}

bool HwDecoder::init(int maxWidth, int maxHeight) {
    int rc = sceSysmoduleLoadModule(kVideodecModule);
    XC_LOGI("hwdec: sceSysmoduleLoadModule(VIDEODEC2) -> 0x%08x", static_cast<unsigned>(rc));
    if (rc < 0 && rc != static_cast<int>(0x80020002) && rc != static_cast<int>(0x8002000e)) {
        return false;
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
    rc = sceVideodec2QueryComputeMemoryInfo(&computeMem);
    XC_LOGI("hwdec: compute memory query: rc=0x%08x, %llu bytes", static_cast<unsigned>(rc),
            static_cast<unsigned long long>(computeMem.cpuGpuSize));
    if (rc != 0 || computeMem.cpuGpuSize == 0) {
        return false;
    }

    size_t computeBytes = align16k(computeMem.cpuGpuSize);
    void* computeBuf = allocDirect(computeBytes, kMemoryTypeOnion, "compute queue");
    if (!computeBuf) {
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

        rc = sceVideodec2AllocateComputeQueue(&compCfg, &computeMem, &queue);
        XC_LOGI("hwdec: compute queue pipe %u queue 0: rc=0x%08x, queue=%p", pipe, static_cast<unsigned>(rc), queue);
        if (rc == 0 && queue) break;
    }
    if (!queue) {
        XC_LOGW("hwdec: failed to allocate compute queue on all pipes");
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
    rc = sceVideodec2QueryDecoderMemoryInfo(&decCfg, &decMem);
    if (rc != 0) {
        XC_LOGW("hwdec: sceVideodec2QueryDecoderMemoryInfo failed: 0x%08x", static_cast<unsigned>(rc));
        return false;
    }

    if (decMem.cpuSize > 0) {
        size_t cpuBytes = align16k(decMem.cpuSize);
        void* cpuMem = nullptr;
        rc = sceKernelMapNamedFlexibleMemory(&cpuMem, cpuBytes, kProtectionRw, 0, "PSBoxVdecCpu");
        if (rc != 0 || !cpuMem) {
            XC_LOGW("hwdec: allocate flexible memory %zu failed: 0x%08x", cpuBytes, static_cast<unsigned>(rc));
            return false;
        }
        decMem.cpu = cpuMem;
        decMem.cpuSize = cpuBytes;
    }

    if (decMem.gpuSize > 0) {
        decMem.gpu = allocDirect(decMem.gpuSize, kMemoryTypeGarlic, "decoder gpu");
        if (!decMem.gpu) return false;
    }

    if (decMem.cpuGpuSize > 0) {
        decMem.cpuGpu = allocDirect(decMem.cpuGpuSize, kMemoryTypeOnion, "decoder cpu+gpu");
        if (!decMem.cpuGpu) return false;
    }

    frameBufferSize_ = align16k(decMem.maxFrameSize > 0 ? decMem.maxFrameSize : (static_cast<size_t>(maxWidth) * maxHeight * 3 / 2 + 64));
    frameBuffer_ = static_cast<uint8_t*>(allocDirect(frameBufferSize_, kMemoryTypeGarlic, "frame buffer"));
    if (!frameBuffer_) {
        return false;
    }

    rc = sceVideodec2CreateDecoder(&decCfg, &decMem, &decoder_);
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

    int rc = sceVideodec2Decode(decoder_, &input, &frame, &output);
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
