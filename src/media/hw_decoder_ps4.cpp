// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "media/hw_decoder.h"
#include "util/log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <unistd.h>
#include <unordered_map>

extern "C" {
int sceSysmoduleLoadModule(uint32_t id);
int sceSysmoduleLoadModuleInternal(uint32_t id);
int sceKernelLoadStartModule(const char* path, size_t argc, const void* argv, uint32_t flags, void* opt, int* res);
size_t sceKernelGetDirectMemorySize();
int sceKernelAllocateDirectMemory(int64_t searchStart, int64_t searchEnd, size_t length, size_t alignment,
                                  int memoryType, int64_t* physicalAddress);
int sceKernelReleaseDirectMemory(int64_t physicalAddress, size_t length);
int sceKernelMapDirectMemory(void** address, size_t length, int protection, int flags, int64_t physicalAddress,
                             size_t alignment);
int sceKernelMapNamedFlexibleMemory(void** address, size_t length, int protection, int flags, const char* name);
int sceKernelMunmap(void* address, size_t length);

// PS4 libSceVideodec2 ABI structures (verified against Orbis/Videodec2.h & XCloud4)
struct Vdec2DecoderConfig {
    uint64_t thisSize;
    uint32_t resourceType;
    uint32_t codecType;
    uint32_t profile;
    uint32_t maxLevel;
    int32_t maxFrameWidth;
    int32_t maxFrameHeight;
    int32_t maxDpbFrameCount;
    uint32_t decodePipelineDepth;
    void* computeQueue;
    uint64_t cpuAffinityMask;
    int32_t cpuThreadPriority;
    bool optimizeProgressiveVideo;
    bool checkMemoryType;
    uint8_t reserved0;
    uint8_t reserved1;
    void* extraConfigInfo;
};

struct Vdec2DecoderMemory {
    uint64_t thisSize;
    uint64_t cpuMemorySize;
    void* cpuMemory;
    uint64_t gpuMemorySize;
    void* gpuMemory;
    uint64_t cpuGpuMemorySize;
    void* cpuGpuMemory;
    uint64_t maxFrameBufferSize;
    uint32_t frameBufferAlignment;
    uint32_t reserved0;
};

struct Vdec2ComputeConfig {
    uint64_t thisSize;
    uint16_t computePipeId;
    uint16_t computeQueueId;
    bool checkMemoryType;
    uint8_t reserved0;
    uint16_t reserved1;
};

struct Vdec2ComputeMemory {
    uint64_t thisSize;
    uint64_t cpuGpuMemorySize;
    void* cpuGpuMemory;
};

struct Vdec2Input {
    uint64_t thisSize;
    const void* auData;
    uint64_t auSize;
    uint64_t ptsData;
    uint64_t dtsData;
    uint64_t attachedData;
};

struct Vdec2Frame {
    uint64_t thisSize;
    void* frameBuffer;
    uint64_t frameBufferSize;
    bool isAccepted;
    uint8_t reserved0[7];
};

struct Vdec2Output {
    uint64_t thisSize;
    bool isValid;
    bool isErrorFrame;
    uint8_t pictureCount;
    uint8_t reserved0;
    uint32_t codecType;
    uint32_t frameWidth;
    uint32_t framePitch;
    uint32_t frameHeight;
    uint32_t reserved1;
    void* frameBuffer;
    uint64_t frameBufferSize;
    uint32_t frameFormat;
    uint32_t pitchBytes;
};

static_assert(sizeof(Vdec2DecoderConfig) == 72, "PS4 OrbisVideodec2DecoderConfigInfo must be 72 bytes");
static_assert(sizeof(Vdec2DecoderMemory) == 72, "PS4 OrbisVideodec2DecoderMemoryInfo must be 72 bytes");
static_assert(sizeof(Vdec2ComputeConfig) == 16, "PS4 OrbisVideodec2ComputeConfigInfo must be 16 bytes");
static_assert(sizeof(Vdec2ComputeMemory) == 24, "PS4 OrbisVideodec2ComputeMemoryInfo must be 24 bytes");
static_assert(sizeof(Vdec2Input) == 48, "PS4 OrbisVideodec2InputData must be 48 bytes");
static_assert(sizeof(Vdec2Frame) == 32, "PS4 OrbisVideodec2FrameBuffer must be 32 bytes");
static_assert(sizeof(Vdec2Output) == 56, "PS4 OrbisVideodec2OutputInfo must be 56 bytes");

// PS4 libSceVideodec2 native functions linked via -lSceVideodec2
int sceVideodec2QueryComputeMemoryInfo(Vdec2ComputeMemory* mem);
int sceVideodec2AllocateComputeQueue(const Vdec2ComputeConfig* cfg, const Vdec2ComputeMemory* mem, void** queue);
int sceVideodec2ReleaseComputeQueue(void* queue);
int sceVideodec2QueryDecoderMemoryInfo(const Vdec2DecoderConfig* cfg, Vdec2DecoderMemory* mem);
int sceVideodec2CreateDecoder(const Vdec2DecoderConfig* cfg, const Vdec2DecoderMemory* mem, void** decoder);
int sceVideodec2DeleteDecoder(void* decoder);
int sceVideodec2Decode(void* decoder, const Vdec2Input* input, Vdec2Frame* frame, Vdec2Output* output);
int sceVideodec2Flush(void* decoder, Vdec2Frame* frame, Vdec2Output* output);
int sceVideodec2Reset(void* decoder);
}

namespace xc::media {

namespace {

constexpr uint32_t kVideodecModule = 0x00CF; // SCE_SYSMODULE_VIDEODEC2
constexpr int kMemoryTypeOnion = 0;          // Coherent CPU+GPU (WB_ONION)
constexpr int kMemoryTypeGarlic = 3;         // GPU Direct Memory (WC_GARLIC)
constexpr int kProtectionRw = 0x33;          // CPU RW | GPU RW
constexpr size_t kInputRingSlots = 8;
constexpr size_t kInputSlotBytes = 2u << 20; // 2 MB per input slot
constexpr size_t kOutputRingSlots = 6;

inline size_t alignUp(size_t n, size_t align) {
    if (align < 0x4000) align = 0x4000;
    return (n + align - 1) & ~(align - 1);
}

inline size_t align16k(size_t n) {
    return alignUp(n, 0x4000);
}

struct DirectMem {
    void* addr = nullptr;
    int64_t phys = 0;
    size_t size = 0;
};

struct FlexibleMem {
    void* addr = nullptr;
    size_t size = 0;
};

DirectMem allocDirect(size_t size, int memoryType, const char* label, size_t alignment = 0x4000) {
    DirectMem mem{};
    if (size == 0) return mem;
    if (alignment < 0x4000) alignment = 0x4000;
    mem.size = alignUp(size, alignment);
    int rc = sceKernelAllocateDirectMemory(0, static_cast<int64_t>(sceKernelGetDirectMemorySize()),
                                           mem.size, alignment, memoryType, &mem.phys);
    if (rc != 0) {
        XC_LOGW("hwdec: %s: allocate %zu bytes failed: 0x%08x", label, mem.size, static_cast<unsigned>(rc));
        mem.size = 0;
        return mem;
    }
    rc = sceKernelMapDirectMemory(&mem.addr, mem.size, kProtectionRw, 0, mem.phys, alignment);
    if (rc != 0 || !mem.addr) {
        XC_LOGW("hwdec: %s: map %zu bytes failed: 0x%08x", label, mem.size, static_cast<unsigned>(rc));
        sceKernelReleaseDirectMemory(mem.phys, mem.size);
        mem.addr = nullptr;
        mem.phys = 0;
        mem.size = 0;
        return mem;
    }
    std::memset(mem.addr, 0, mem.size);
    return mem;
}

void freeDirect(DirectMem& mem) {
    if (mem.addr && mem.size > 0) {
        sceKernelMunmap(mem.addr, mem.size);
    }
    if (mem.size > 0) {
        sceKernelReleaseDirectMemory(mem.phys, mem.size);
    }
    mem.addr = nullptr;
    mem.phys = 0;
    mem.size = 0;
}

void freeFlexible(FlexibleMem& mem) {
    if (mem.addr && mem.size > 0) {
        sceKernelMunmap(mem.addr, mem.size);
    }
    mem.addr = nullptr;
    mem.size = 0;
}

struct DecoderAllocations {
    DirectMem computeBuf;
    DirectMem decCpuDirect;
    FlexibleMem decCpuFlex;
    DirectMem decGpu;
    DirectMem decCpuGpu;
    DirectMem inputBufs[kInputRingSlots];
    DirectMem frameBufs[kOutputRingSlots];
    size_t inputSlot = 0;
    size_t outputSlot = 0;
};

static std::mutex s_allocMutex;
static std::unordered_map<const HwDecoder*, DecoderAllocations> s_allocations;

void freeAllAllocations(DecoderAllocations& a) {
    for (auto& fb : a.frameBufs) freeDirect(fb);
    for (auto& ib : a.inputBufs) freeDirect(ib);
    freeDirect(a.decCpuGpu);
    freeDirect(a.decGpu);
    freeFlexible(a.decCpuFlex);
    freeDirect(a.decCpuDirect);
    freeDirect(a.computeBuf);
}

} // namespace

HwDecoder::~HwDecoder() {
    if (decoder_) {
        sceVideodec2DeleteDecoder(decoder_);
        decoder_ = nullptr;
    }
    if (computeQueue_) {
        sceVideodec2ReleaseComputeQueue(computeQueue_);
        computeQueue_ = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(s_allocMutex);
        auto it = s_allocations.find(this);
        if (it != s_allocations.end()) {
            freeAllAllocations(it->second);
            s_allocations.erase(it);
        }
    }
    frameBuffer_ = nullptr;
    frameBufferSize_ = 0;
}

bool HwDecoder::init(int maxWidth, int maxHeight) {
    if (decoder_) return true;

    int rc = sceSysmoduleLoadModule(kVideodecModule);
    XC_LOGI("hwdec: sceSysmoduleLoadModule(VIDEODEC2) -> 0x%08x", static_cast<unsigned>(rc));
    if (rc < 0 && rc != static_cast<int>(0x80020002) && rc != static_cast<int>(0x8002000e)) {
        return false;
    }

    if (rc != 0) {
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
    }

    sceSysmoduleLoadModuleInternal(0x80000015);

    DecoderAllocations allocs{};
    auto rollback = [&]() {
        if (decoder_) {
            sceVideodec2DeleteDecoder(decoder_);
            decoder_ = nullptr;
        }
        if (computeQueue_) {
            sceVideodec2ReleaseComputeQueue(computeQueue_);
            computeQueue_ = nullptr;
        }
        freeAllAllocations(allocs);
        frameBuffer_ = nullptr;
        frameBufferSize_ = 0;
    };

    // Query and allocate compute queue memory
    Vdec2ComputeMemory computeMem{};
    computeMem.thisSize = sizeof(computeMem);
    rc = sceVideodec2QueryComputeMemoryInfo(&computeMem);
    XC_LOGI("hwdec: compute memory query: rc=0x%08x, %llu bytes", static_cast<unsigned>(rc),
            static_cast<unsigned long long>(computeMem.cpuGpuMemorySize));
    if (rc != 0 || computeMem.cpuGpuMemorySize == 0) {
        return false;
    }

    size_t computeBytes = align16k(computeMem.cpuGpuMemorySize);
    allocs.computeBuf = allocDirect(computeBytes, kMemoryTypeOnion, "compute queue");
    if (!allocs.computeBuf.addr) {
        return false;
    }
    computeMem.cpuGpuMemory = allocs.computeBuf.addr;

    // Allocate compute queue across pipes 0..4 (checkMemoryType = false per XCloud4)
    void* queue = nullptr;
    for (uint16_t pipe = 0; pipe < 5; ++pipe) {
        Vdec2ComputeConfig compCfg{};
        compCfg.thisSize = sizeof(compCfg);
        compCfg.computePipeId = pipe;
        compCfg.computeQueueId = 0;
        compCfg.checkMemoryType = false;

        rc = sceVideodec2AllocateComputeQueue(&compCfg, &computeMem, &queue);
        XC_LOGI("hwdec: compute queue pipe %u queue 0: rc=0x%08x, queue=%p", pipe, static_cast<unsigned>(rc), queue);
        if (rc == 0 && queue) break;
    }
    if (!queue) {
        XC_LOGW("hwdec: failed to allocate compute queue on all pipes");
        rollback();
        return false;
    }
    computeQueue_ = queue;

    // Query decoder memory requirements with fallback profile/level presets
    struct Preset {
        uint32_t profile;
        uint32_t maxLevel;
        int32_t dpbFrames;
        uint32_t height;
    };
    const uint32_t codedHeight = static_cast<uint32_t>(maxHeight);
    const uint32_t altHeight = (maxHeight == 1088) ? 1080u : codedHeight;
    const Preset presets[] = {
        {100, 42, 4, codedHeight}, // High Profile, Level 4.2 (1920x1088 coded height)
        {100, 51, 4, codedHeight}, // High Profile, Level 5.1 (1920x1088 coded height)
        {100, 42, 4, altHeight},   // Fallback if firmware rejects 1088 at Level 4.2
        {66,  31, 4, altHeight},   // Baseline Profile, Level 3.1
    };

    Vdec2DecoderConfig decCfg{};
    Vdec2DecoderMemory decMem{};
    bool queryOk = false;
    for (const auto& p : presets) {
        std::memset(&decCfg, 0, sizeof(decCfg));
        decCfg.thisSize = sizeof(decCfg);
        decCfg.resourceType = 1; // standard
        decCfg.codecType = 1;    // H.264 (AVC)
        decCfg.profile = p.profile;
        decCfg.maxLevel = p.maxLevel;
        decCfg.maxFrameWidth = maxWidth;
        decCfg.maxFrameHeight = p.height;
        decCfg.maxDpbFrameCount = p.dpbFrames;
        decCfg.decodePipelineDepth = 1;
        decCfg.computeQueue = computeQueue_;
        decCfg.cpuAffinityMask = 0x20;
        decCfg.cpuThreadPriority = 700;
        decCfg.optimizeProgressiveVideo = true;
        decCfg.checkMemoryType = false;

        std::memset(&decMem, 0, sizeof(decMem));
        decMem.thisSize = sizeof(decMem);
        rc = sceVideodec2QueryDecoderMemoryInfo(&decCfg, &decMem);
        if (rc == 0) {
            queryOk = true;
            break;
        }
        XC_LOGW("hwdec: QueryDecoderMemoryInfo(profile=%u, level=%u, h=%u) failed: 0x%08x",
                p.profile, p.maxLevel, p.height, static_cast<unsigned>(rc));
    }
    if (!queryOk) {
        rollback();
        return false;
    }
    XC_LOGI("hwdec: decoder memory query: cpu=%llu gpu=%llu cpuGpu=%llu maxFrame=%llu align=%u",
            static_cast<unsigned long long>(decMem.cpuMemorySize),
            static_cast<unsigned long long>(decMem.gpuMemorySize),
            static_cast<unsigned long long>(decMem.cpuGpuMemorySize),
            static_cast<unsigned long long>(decMem.maxFrameBufferSize),
            decMem.frameBufferAlignment);

    if (decMem.cpuMemorySize > 0) {
        size_t cpuBytes = align16k(decMem.cpuMemorySize);
        void* cpuMem = nullptr;
        rc = sceKernelMapNamedFlexibleMemory(&cpuMem, cpuBytes, kProtectionRw, 0, "PSBoxVdecCpu");
        if (rc == 0 && cpuMem) {
            std::memset(cpuMem, 0, cpuBytes);
            decMem.cpuMemory = cpuMem;
            decMem.cpuMemorySize = cpuBytes;
            allocs.decCpuFlex = {cpuMem, cpuBytes};
            XC_LOGI("hwdec: decoder cpu %zu bytes allocated in flexible memory", cpuBytes);
        } else {
            allocs.decCpuDirect = allocDirect(cpuBytes, kMemoryTypeOnion, "decoder cpu");
            if (!allocs.decCpuDirect.addr) {
                rollback();
                return false;
            }
            decMem.cpuMemory = allocs.decCpuDirect.addr;
            decMem.cpuMemorySize = allocs.decCpuDirect.size;
            XC_LOGI("hwdec: decoder cpu %zu bytes allocated in direct Onion memory", cpuBytes);
        }
    }

    if (decMem.gpuMemorySize > 0) {
        allocs.decGpu = allocDirect(decMem.gpuMemorySize, kMemoryTypeGarlic, "decoder gpu");
        if (!allocs.decGpu.addr) {
            rollback();
            return false;
        }
        decMem.gpuMemory = allocs.decGpu.addr;
        decMem.gpuMemorySize = allocs.decGpu.size;
    }

    if (decMem.cpuGpuMemorySize > 0) {
        allocs.decCpuGpu = allocDirect(decMem.cpuGpuMemorySize, kMemoryTypeOnion, "decoder cpu+gpu");
        if (!allocs.decCpuGpu.addr) {
            rollback();
            return false;
        }
        decMem.cpuGpuMemory = allocs.decCpuGpu.addr;
        decMem.cpuGpuMemorySize = allocs.decCpuGpu.size;
    }

    for (size_t i = 0; i < kInputRingSlots; ++i) {
        allocs.inputBufs[i] = allocDirect(kInputSlotBytes, kMemoryTypeOnion, "input au ring");
        if (!allocs.inputBufs[i].addr) {
            rollback();
            return false;
        }
    }

    const size_t minCodedFrameBytes = static_cast<size_t>(maxWidth) * maxHeight * 3 / 2 + 64;
    size_t rawFrameSize = std::max<size_t>(static_cast<size_t>(decMem.maxFrameBufferSize), minCodedFrameBytes);
    size_t fbAlign = std::max<size_t>(decMem.frameBufferAlignment, 0x4000);
    frameBufferSize_ = alignUp(rawFrameSize, fbAlign);
    for (size_t i = 0; i < kOutputRingSlots; ++i) {
        allocs.frameBufs[i] = allocDirect(frameBufferSize_, kMemoryTypeOnion, "frame buffer ring", fbAlign);
        if (!allocs.frameBufs[i].addr) {
            rollback();
            return false;
        }
    }
    frameBuffer_ = static_cast<uint8_t*>(allocs.frameBufs[0].addr);

    rc = sceVideodec2CreateDecoder(&decCfg, &decMem, &decoder_);
    XC_LOGI("hwdec: sceVideodec2CreateDecoder -> 0x%08x", static_cast<unsigned>(rc));
    if (rc != 0 || !decoder_) {
        decoder_ = nullptr;
        rollback();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(s_allocMutex);
        s_allocations[this] = allocs;
    }

    XC_LOGI("hwdec: PS4 decoder running (%zu input slots, %zu output slots of %zu bytes)",
            kInputRingSlots, kOutputRingSlots, frameBufferSize_);
    return true;
}

bool HwDecoder::decode(const uint8_t* data, size_t len, uint64_t pts, Picture& out) {
    out = Picture{};
    if (!decoder_ || !data || len == 0 || !frameBuffer_) return false;

    void* inBuf = nullptr;
    void* outBuf = nullptr;
    size_t outBufSize = frameBufferSize_;
    size_t outSlotUsed = 0;
    {
        std::lock_guard<std::mutex> lock(s_allocMutex);
        auto it = s_allocations.find(this);
        if (it == s_allocations.end() || len > it->second.inputBufs[0].size) {
            return false;
        }
        size_t inSlot = it->second.inputSlot;
        it->second.inputSlot = (inSlot + 1) % kInputRingSlots;
        inBuf = it->second.inputBufs[inSlot].addr;
        std::memcpy(inBuf, data, len);

        outSlotUsed = it->second.outputSlot;
        outBuf = it->second.frameBufs[outSlotUsed].addr;
        outBufSize = it->second.frameBufs[outSlotUsed].size;
    }

    Vdec2Input input{};
    input.thisSize = sizeof(input);
    input.auData = inBuf;
    input.auSize = len;
    input.ptsData = pts;
    input.dtsData = pts;

    Vdec2Frame frame{};
    frame.thisSize = sizeof(frame);
    frame.frameBuffer = outBuf;
    frame.frameBufferSize = outBufSize;

    Vdec2Output output{};
    output.thisSize = sizeof(output);

    int decodeRc = sceVideodec2Decode(decoder_, &input, &frame, &output);
    if (decodeRc == 0 && frame.isAccepted) {
        std::lock_guard<std::mutex> lock(s_allocMutex);
        auto it = s_allocations.find(this);
        if (it != s_allocations.end() && it->second.outputSlot == outSlotUsed) {
            it->second.outputSlot = (outSlotUsed + 1) % kOutputRingSlots;
        }
    }
    if (decodeRc != 0 || output.isErrorFrame) {
        out.error = true;
        return false;
    }

    bool flushed = false;
    void* flushBuf = nullptr;
    size_t flushBufSize = 0;
    int flushRc = 0;
    if (decodeRc == 0 && (!output.isValid || output.pictureCount == 0)) {
        flushed = true;
        output = Vdec2Output{};
        output.thisSize = sizeof(output);

        Vdec2Frame flushFrame{};
        flushFrame.thisSize = sizeof(flushFrame);
        size_t flushSlot = 0;
        {
            std::lock_guard<std::mutex> lock(s_allocMutex);
            auto it = s_allocations.find(this);
            if (it != s_allocations.end()) {
                flushSlot = it->second.outputSlot;
                flushBuf = it->second.frameBufs[flushSlot].addr;
                flushBufSize = it->second.frameBufs[flushSlot].size;
                flushFrame.frameBuffer = flushBuf;
                flushFrame.frameBufferSize = flushBufSize;
            }
        }

        flushRc = sceVideodec2Flush(decoder_, &flushFrame, &output);
        if (flushRc == 0 && flushFrame.isAccepted) {
            std::lock_guard<std::mutex> lock(s_allocMutex);
            auto it = s_allocations.find(this);
            if (it != s_allocations.end() && it->second.outputSlot == flushSlot) {
                it->second.outputSlot = (flushSlot + 1) % kOutputRingSlots;
            }
        }
    }

    static int logged = 0;
    if ((decodeRc != 0 || flushRc != 0 || output.isValid) && logged < 12) {
        ++logged;
        XC_LOGI("hwdec: PS4 decode 0x%08x flush 0x%08x (flushed=%d): valid %d errFrame %d pictures %u, %ux%u pitch %u (%u bytes), format %u, buf %p",
                static_cast<unsigned>(decodeRc), static_cast<unsigned>(flushRc), flushed ? 1 : 0,
                static_cast<int>(output.isValid), static_cast<int>(output.isErrorFrame),
                static_cast<unsigned>(output.pictureCount),
                output.frameWidth, output.frameHeight, output.framePitch, output.pitchBytes,
                output.frameFormat, output.frameBuffer);
    }

    if (output.isErrorFrame) {
        out.error = true;
        return false;
    }

    if (output.isValid && output.pictureCount > 0) {
        const void* chosenBuf = output.frameBuffer ? output.frameBuffer : (flushed && flushBuf ? flushBuf : outBuf);
        size_t chosenSize = output.frameBufferSize ? output.frameBufferSize : (flushed && flushBufSize ? flushBufSize : outBufSize);
        out.data = static_cast<const uint8_t*>(chosenBuf);
        out.size = chosenSize;
        out.width = output.frameWidth;
        out.height = output.frameHeight;
        out.pitch = output.framePitch ? output.framePitch : output.frameWidth;
        out.pitchBytes = output.pitchBytes ? output.pitchBytes : out.pitch;
        out.format = output.frameFormat;
        out.pts = pts;
        out.error = false;
        return true;
    }

    // Pipeline priming frame (rc == 0, !isValid, !isErrorFrame): not an error
    out.pending = true;
    return false;
}

} // namespace xc::media
