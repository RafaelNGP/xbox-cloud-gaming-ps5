// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "display/gpu.h"

#include "platform/platform.h"
#include "util/log.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

#include "display/shaders/easu.spv.h"
#include "display/shaders/rcas.spv.h"
#include "display/shaders/yuv2rgb.spv.h"

// RADV is linked in whole (no loader): its ICD entry point resolves the rest.
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* name);

namespace xc::display::gpu {

namespace {

#define XC_VK_GLOBAL(X) X(CreateInstance) X(EnumerateInstanceExtensionProperties)

#define XC_VK_INSTANCE(X)                                                                                    \
    X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) X(GetPhysicalDeviceQueueFamilyProperties)     \
    X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceFeatures) X(CreateDevice) X(GetDeviceProcAddr)   \
    X(EnumerateDeviceExtensionProperties)                                                                    \
    X(GetPhysicalDeviceDisplayPropertiesKHR) X(GetDisplayModePropertiesKHR) X(CreateDisplayPlaneSurfaceKHR)  \
    X(GetPhysicalDeviceSurfaceSupportKHR) X(GetPhysicalDeviceSurfaceCapabilitiesKHR)                         \
    X(GetPhysicalDeviceSurfacePresentModesKHR)

#define XC_VK_DEVICE(X)                                                                                      \
    X(GetDeviceQueue) X(DeviceWaitIdle) X(CreateSwapchainKHR) X(GetSwapchainImagesKHR)                       \
    X(AcquireNextImageKHR) X(QueuePresentKHR) X(CreateSemaphore) X(CreateFence) X(WaitForFences)             \
    X(ResetFences) X(CreateCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer)  \
    X(ResetCommandBuffer) X(QueueSubmit) X(CmdPipelineBarrier2) X(CmdClearColorImage)                        \
    X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory) X(CreateImage)       \
    X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory) X(AllocateMemory) X(FreeMemory)         \
    X(MapMemory) X(CreateImageView) X(DestroyImageView) X(CreateSampler) X(CreateShaderModule)               \
    X(CreateDescriptorSetLayout) X(CreatePipelineLayout) X(CreateComputePipelines)                           \
    X(CreateDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CmdBindPipeline)            \
    X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch) X(CmdCopyBufferToImage)                     \
    X(CmdCopyImageToBuffer)

#define XC_VK_DECLARE(name) PFN_vk##name vk##name = nullptr;
XC_VK_GLOBAL(XC_VK_DECLARE)
XC_VK_INSTANCE(XC_VK_DECLARE)
XC_VK_DEVICE(XC_VK_DECLARE)

constexpr uint32_t kUiW = 1920, kUiH = 1080;  // the menus' canvas, and the overlay's

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint32_t w = 0, h = 0;
};

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint8_t* map = nullptr;
    size_t size = 0;
};

struct Pipeline {
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

struct State {
    bool ready = false;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory{};
    VkDevice device = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkExtent2D extent{3840, 2160};
    uint32_t refreshMilliHz = 60000;
    VkImage images[8]{};
    VkImageView views[8]{};
    uint32_t imageCount = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE, rendered = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool fencePending = false;
    bool presentId = false, presentWait = false;
    uint64_t presented = 0;  // the last present id

    VkSampler nearest = VK_NULL_HANDLE, linear = VK_NULL_HANDLE;
    Pipeline yuv, easu, rcas;
    VkDescriptorPool descriptors = VK_NULL_HANDLE;
    VkDescriptorSet yuvSet = VK_NULL_HANDLE, easuVideoSet = VK_NULL_HANDLE, easuUiSet = VK_NULL_HANDLE;
    VkDescriptorSet rcasSets[8]{};

    Image luma, chromaU, chromaV, rgb;  // the video picture, at its size
    Image ui, upscaled, overlay;
    Buffer staging;         // this frame's planes or canvas
    Buffer overlayStaging;  // the overlay, when it changed
    Buffer readback;

    // The frame being drawn: acquired image, recorded command buffer.
    bool frameOpen = false;
    uint32_t frameIndex = 0;
    uint32_t lastPresented = UINT32_MAX;
};
State g;

// The overlay, as setOverlay() left it (any thread).
std::mutex g_overlayMutex;
std::vector<uint32_t> g_overlayPixels(kUiW* kUiH, 0);
bool g_overlayDirty = false, g_overlayShown = false;
std::atomic<int> g_sharpness{0};
std::atomic<int> g_deband{1};
uint32_t g_frame = 0;
uint32_t g_swapImages = 2;
bool g_noPresentWait = false;
PFN_vkWaitForPresentKHR vkWaitForPresentKHR = nullptr;
// The last frame known on the screen: its present id and when (us).
std::atomic<uint64_t> g_shownId{0}, g_shownAtUs{0};
// How long acquiring waited for the display to free an image (FIFO: the
// frame before went on screen), logged every 600 frames.
uint64_t g_acquireWaitUs = 0, g_acquireWaitMaxUs = 0, g_acquires = 0;

bool fail(const char* step, VkResult r) {
    XC_LOGE("gpu: %s failed (%d)", step, static_cast<int>(r));
    return false;
}

// --- device -------------------------------------------------------------------

bool createInstance() {
#define XC_VK_LOAD_GLOBAL(name) vk##name = reinterpret_cast<PFN_vk##name>(vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vk" #name));
    XC_VK_GLOBAL(XC_VK_LOAD_GLOBAL)
    if (!vkCreateInstance || !vkEnumerateInstanceExtensionProperties) {
        XC_LOGE("gpu: the RADV ICD has no vkCreateInstance");
        return false;
    }
    VkExtensionProperties ext[64];
    uint32_t n = 64;
    bool display = false;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &n, ext) >= VK_SUCCESS)
        for (uint32_t i = 0; i < n; ++i) display |= std::strcmp(ext[i].extensionName, VK_KHR_DISPLAY_EXTENSION_NAME) == 0;
    if (!display) {
        XC_LOGE("gpu: VK_KHR_display is not reported");
        return false;
    }
    const char* const names[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME};
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "PSBox Cloud Gaming";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = 2;
    info.ppEnabledExtensionNames = names;
    VkResult r = vkCreateInstance(&info, nullptr, &g.instance);
    if (r != VK_SUCCESS) return fail("vkCreateInstance", r);
#define XC_VK_LOAD_INSTANCE(name) vk##name = reinterpret_cast<PFN_vk##name>(vk_icdGetInstanceProcAddr(g.instance, "vk" #name));
    XC_VK_INSTANCE(XC_VK_LOAD_INSTANCE)
    return vkEnumeratePhysicalDevices && vkCreateDevice && vkCreateDisplayPlaneSurfaceKHR;
}

bool createDevice() {
    uint32_t n = 1;
    VkResult r = vkEnumeratePhysicalDevices(g.instance, &n, &g.physical);
    if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || n == 0) return fail("vkEnumeratePhysicalDevices", r);
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(g.physical, &props);
    XC_LOGI("gpu: %s, Vulkan %u.%u.%u", props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion),
            VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion));
    vkGetPhysicalDeviceMemoryProperties(g.physical, &g.memory);
    VkQueueFamilyProperties families[8];
    n = 8;
    vkGetPhysicalDeviceQueueFamilyProperties(g.physical, &n, families);
    // What the driver offers for hardware video decoding (Vulkan Video).
    for (uint32_t i = 0; i < n; ++i)
        XC_LOGI("gpu: queue family %u: flags 0x%x, %u queue(s)%s", i, families[i].queueFlags, families[i].queueCount,
                (families[i].queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) ? " (video decode)" : "");
    {
        static VkExtensionProperties ext[512];
        uint32_t count = 512;
        std::string video;
        if (vkEnumerateDeviceExtensionProperties(g.physical, nullptr, &count, ext) < VK_SUCCESS) count = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const char* name = ext[i].extensionName;
            if (std::strcmp(name, VK_KHR_PRESENT_WAIT_EXTENSION_NAME) == 0) g.presentWait = true;
            if (std::strcmp(name, VK_KHR_PRESENT_ID_EXTENSION_NAME) == 0) g.presentId = true;
            if (std::strstr(name, "video") || std::strstr(name, "present") || std::strstr(name, "swapchain") ||
                std::strstr(name, "display"))
                video += std::string(" ") + name;
        }
        XC_LOGI("gpu: %u device extensions; video/present/swapchain/display:%s", count,
                video.empty() ? " none" : video.c_str());
    }
    g.family = UINT32_MAX;
    for (uint32_t i = 0; i < n && g.family == UINT32_MAX; ++i)
        if ((families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
            (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
            g.family = i;
    if (g.family == UINT32_MAX) {
        XC_LOGE("gpu: no graphics queue");
        return false;
    }
    VkPhysicalDeviceFeatures supported;
    vkGetPhysicalDeviceFeatures(g.physical, &supported);
    if (!supported.shaderStorageImageWriteWithoutFormat) {
        XC_LOGE("gpu: no shaderStorageImageWriteWithoutFormat");
        return false;
    }
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = g.family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    v13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures features{};
    features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
    // Present ids and waiting for them: frames are submitted only once the
    // one before is on the screen (FIFO would otherwise queue two), and that
    // moment is the frame's display time.
    g.presentWait = g.presentWait && g.presentId;
    VkPhysicalDevicePresentIdFeaturesKHR presentIdFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    presentIdFeature.presentId = VK_TRUE;
    VkPhysicalDevicePresentWaitFeaturesKHR presentWaitFeature{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    presentWaitFeature.presentWait = VK_TRUE;
    presentWaitFeature.pNext = &presentIdFeature;
    if (g.presentWait) v13.pNext = &presentWaitFeature;
    const char* const ext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PRESENT_ID_EXTENSION_NAME,
                               VK_KHR_PRESENT_WAIT_EXTENSION_NAME};
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &v13;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = g.presentWait ? 3 : 1;
    info.ppEnabledExtensionNames = ext;
    info.pEnabledFeatures = &features;
    r = vkCreateDevice(g.physical, &info, nullptr, &g.device);
    if (r != VK_SUCCESS) return fail("vkCreateDevice", r);
#define XC_VK_LOAD_DEVICE(name) vk##name = reinterpret_cast<PFN_vk##name>(vkGetDeviceProcAddr(g.device, "vk" #name));
    XC_VK_DEVICE(XC_VK_LOAD_DEVICE)
    vkGetDeviceQueue(g.device, g.family, 0, &g.queue);
    if (g.presentWait) {
        vkWaitForPresentKHR = reinterpret_cast<PFN_vkWaitForPresentKHR>(vkGetDeviceProcAddr(g.device, "vkWaitForPresentKHR"));
        g.presentWait = vkWaitForPresentKHR != nullptr;
    }
    XC_LOGI("gpu: present wait %s%s", g.presentWait ? "on" : "off",
            g.presentWait && g_noPresentWait ? " (measuring only)" : "");
    return true;
}

bool createSwapchain() {
    VkDisplayPropertiesKHR display;
    uint32_t n = 1;
    VkResult r = vkGetPhysicalDeviceDisplayPropertiesKHR(g.physical, &n, &display);
    if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || n == 0) return fail("vkGetPhysicalDeviceDisplayPropertiesKHR", r);
    VkDisplayModePropertiesKHR modes[4];
    n = 4;
    r = vkGetDisplayModePropertiesKHR(g.physical, display.display, &n, modes);
    if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || n == 0) return fail("vkGetDisplayModePropertiesKHR", r);
    g.extent = display.physicalResolution;
    g.refreshMilliHz = modes[0].parameters.refreshRate;
    XC_LOGI("gpu: display %ux%u at %.3f Hz", g.extent.width, g.extent.height, g.refreshMilliHz / 1000.0);

    VkDisplaySurfaceCreateInfoKHR surface{VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR};
    surface.displayMode = modes[0].displayMode;
    surface.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    surface.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    surface.imageExtent = g.extent;
    r = vkCreateDisplayPlaneSurfaceKHR(g.instance, &surface, nullptr, &g.surface);
    if (r != VK_SUCCESS) return fail("vkCreateDisplayPlaneSurfaceKHR", r);
    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(g.physical, g.family, g.surface, &supported);
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g.physical, g.surface, &caps);
    {
        VkPresentModeKHR modes[8];
        uint32_t count = 8;
        vkGetPhysicalDeviceSurfacePresentModesKHR(g.physical, g.surface, &count, modes);
        std::string list;
        for (uint32_t i = 0; i < count; ++i) list += " " + std::to_string(static_cast<int>(modes[i]));
        XC_LOGI("gpu: present modes:%s (0 immediate, 1 mailbox, 2 fifo, 3 fifo relaxed); %u..%u images",
                list.c_str(), caps.minImageCount, caps.maxImageCount);
    }
    const VkImageUsageFlags usage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!supported || (caps.supportedUsageFlags & usage) != usage) {
        XC_LOGE("gpu: the display can't take storage writes (usage 0x%x)", caps.supportedUsageFlags);
        return false;
    }

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = g.surface;
    // Two images: one on the screen, one being drawn. With three, FIFO kept
    // two finished frames queued and the one shown was ~33 ms old.
    info.minImageCount = std::max(caps.minImageCount, g_swapImages);
    info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    info.imageExtent = g.extent;
    info.imageArrayLayers = 1;
    info.imageUsage = usage;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    r = vkCreateSwapchainKHR(g.device, &info, nullptr, &g.swapchain);
    if (r != VK_SUCCESS) return fail("vkCreateSwapchainKHR", r);
    g.imageCount = 8;
    r = vkGetSwapchainImagesKHR(g.device, g.swapchain, &g.imageCount, g.images);
    if (r != VK_SUCCESS) return fail("vkGetSwapchainImagesKHR", r);
    for (uint32_t i = 0; i < g.imageCount; ++i) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = g.images[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = VK_FORMAT_B8G8R8A8_UNORM;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        r = vkCreateImageView(g.device, &view, nullptr, &g.views[i]);
        if (r != VK_SUCCESS) return fail("swapchain image view", r);
    }
    return true;
}

bool createCommands() {
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = g.family;
    VkResult r = vkCreateCommandPool(g.device, &pool, nullptr, &g.pool);
    if (r != VK_SUCCESS) return fail("vkCreateCommandPool", r);
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = g.pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    r = vkAllocateCommandBuffers(g.device, &alloc, &g.cmd);
    if (r != VK_SUCCESS) return fail("vkAllocateCommandBuffers", r);
    VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateSemaphore(g.device, &sem, nullptr, &g.acquired) != VK_SUCCESS ||
        vkCreateSemaphore(g.device, &sem, nullptr, &g.rendered) != VK_SUCCESS ||
        vkCreateFence(g.device, &fence, nullptr, &g.fence) != VK_SUCCESS) {
        XC_LOGE("gpu: semaphores / fence");
        return false;
    }
    return true;
}

// --- resources ----------------------------------------------------------------

bool allocate(const VkMemoryRequirements& req, VkMemoryPropertyFlags want, VkDeviceMemory& out) {
    for (uint32_t i = 0; i < g.memory.memoryTypeCount; ++i) {
        if (!(req.memoryTypeBits & (1u << i)) || (g.memory.memoryTypes[i].propertyFlags & want) != want) continue;
        VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        info.allocationSize = req.size;
        info.memoryTypeIndex = i;
        return vkAllocateMemory(g.device, &info, nullptr, &out) == VK_SUCCESS;
    }
    return false;
}

bool createBuffer(Buffer& b, size_t size, VkBufferUsageFlags usage) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    if (vkCreateBuffer(g.device, &info, nullptr, &b.buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g.device, b.buffer, &req);
    if (!allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, b.memory)) return false;
    vkBindBufferMemory(g.device, b.buffer, b.memory, 0);
    void* map = nullptr;
    if (vkMapMemory(g.device, b.memory, 0, VK_WHOLE_SIZE, 0, &map) != VK_SUCCESS) return false;
    b.map = static_cast<uint8_t*>(map);
    b.size = size;
    return true;
}

bool createImage(Image& img, VkFormat format, uint32_t w, uint32_t h, VkImageUsageFlags usage) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {w, h, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(g.device, &info, nullptr, &img.image) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g.device, img.image, &req);
    if (!allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, img.memory)) return false;
    vkBindImageMemory(g.device, img.image, img.memory, 0);
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = img.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(g.device, &view, nullptr, &img.view) != VK_SUCCESS) return false;
    img.w = w;
    img.h = h;
    return true;
}

void destroyImage(Image& img) {
    if (img.view) vkDestroyImageView(g.device, img.view, nullptr);
    if (img.image) vkDestroyImage(g.device, img.image, nullptr);
    if (img.memory) vkFreeMemory(g.device, img.memory, nullptr);
    img = Image{};
}

bool createPipeline(Pipeline& p, const uint32_t* code, size_t bytes, const VkDescriptorSetLayoutBinding* bindings,
                    uint32_t bindingCount, uint32_t pushBytes) {
    VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set.bindingCount = bindingCount;
    set.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(g.device, &set, nullptr, &p.setLayout) != VK_SUCCESS) return false;
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, pushBytes};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &p.setLayout;
    layout.pushConstantRangeCount = pushBytes ? 1 : 0;
    layout.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(g.device, &layout, nullptr, &p.layout) != VK_SUCCESS) return false;
    VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    module.codeSize = bytes;
    module.pCode = code;
    VkShaderModule shader;
    if (vkCreateShaderModule(g.device, &module, nullptr, &shader) != VK_SUCCESS) return false;
    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = shader;
    info.stage.pName = "main";
    info.layout = p.layout;
    return vkCreateComputePipelines(g.device, VK_NULL_HANDLE, 1, &info, nullptr, &p.pipeline) == VK_SUCCESS;
}

VkDescriptorSetLayoutBinding binding(uint32_t index, VkDescriptorType type) {
    return {index, type, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
}

VkDescriptorSet allocateSet(const Pipeline& p) {
    VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorPool = g.descriptors;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &p.setLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    vkAllocateDescriptorSets(g.device, &info, &set);
    return set;
}

void writeSampled(VkDescriptorSet set, uint32_t index, VkImageView view, VkSampler sampler) {
    VkDescriptorImageInfo image{sampler, view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set;
    w.dstBinding = index;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &image;
    vkUpdateDescriptorSets(g.device, 1, &w, 0, nullptr);
}

void writeStorage(VkDescriptorSet set, uint32_t index, VkImageView view) {
    VkDescriptorImageInfo image{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set;
    w.dstBinding = index;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w.pImageInfo = &image;
    vkUpdateDescriptorSets(g.device, 1, &w, 0, nullptr);
}

bool createPipelines() {
    VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.magFilter = s.minFilter = VK_FILTER_NEAREST;
    if (vkCreateSampler(g.device, &s, nullptr, &g.nearest) != VK_SUCCESS) return false;
    s.magFilter = s.minFilter = VK_FILTER_LINEAR;
    if (vkCreateSampler(g.device, &s, nullptr, &g.linear) != VK_SUCCESS) return false;

    const VkDescriptorType sampled = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, storage = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    const VkDescriptorSetLayoutBinding yuv[] = {binding(0, sampled), binding(1, sampled), binding(2, sampled),
                                                binding(3, storage)};
    const VkDescriptorSetLayoutBinding easu[] = {binding(0, sampled), binding(1, storage)};
    const VkDescriptorSetLayoutBinding rcas[] = {binding(0, storage), binding(1, sampled), binding(2, storage)};
    if (!createPipeline(g.yuv, kShader_yuv2rgb, sizeof kShader_yuv2rgb, yuv, 4, 20) ||
        !createPipeline(g.easu, kShader_easu, sizeof kShader_easu, easu, 2, 64) ||
        !createPipeline(g.rcas, kShader_rcas, sizeof kShader_rcas, rcas, 3, 24)) {
        XC_LOGE("gpu: the FSR pipelines");
        return false;
    }
    const VkDescriptorPoolSize sizes[] = {{sampled, 32}, {storage, 32}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 16;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(g.device, &pool, nullptr, &g.descriptors) != VK_SUCCESS) return false;
    g.yuvSet = allocateSet(g.yuv);
    g.easuVideoSet = allocateSet(g.easu);
    g.easuUiSet = allocateSet(g.easu);
    for (uint32_t i = 0; i < g.imageCount; ++i) g.rcasSets[i] = allocateSet(g.rcas);

    // The images every frame uses; the video's come with its first picture.
    const VkImageUsageFlags upload = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!createImage(g.ui, VK_FORMAT_R8G8B8A8_UNORM, kUiW, kUiH, upload) ||
        !createImage(g.overlay, VK_FORMAT_R8G8B8A8_UNORM, kUiW, kUiH, upload) ||
        !createImage(g.upscaled, VK_FORMAT_R8G8B8A8_UNORM, g.extent.width, g.extent.height,
                     VK_IMAGE_USAGE_STORAGE_BIT) ||
        !createBuffer(g.staging, kUiW * kUiH * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT) ||
        !createBuffer(g.overlayStaging, kUiW * kUiH * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) {
        XC_LOGE("gpu: images and buffers");
        return false;
    }
    writeSampled(g.easuUiSet, 0, g.ui.view, g.linear);
    writeStorage(g.easuUiSet, 1, g.upscaled.view);
    writeStorage(g.easuVideoSet, 1, g.upscaled.view);
    for (uint32_t i = 0; i < g.imageCount; ++i) {
        writeStorage(g.rcasSets[i], 0, g.upscaled.view);
        writeSampled(g.rcasSets[i], 1, g.overlay.view, g.linear);
        writeStorage(g.rcasSets[i], 2, g.views[i]);
    }
    std::memset(g.overlayStaging.map, 0, g.overlayStaging.size);
    g_overlayDirty = true;  // the first frame clears the overlay image
    return true;
}

// The video's images, at the picture's size (made again when it changes).
bool videoImages(uint32_t w, uint32_t h) {
    if (g.luma.w == w && g.luma.h == h) return true;
    vkDeviceWaitIdle(g.device);
    destroyImage(g.luma);
    destroyImage(g.chromaU);
    destroyImage(g.chromaV);
    destroyImage(g.rgb);
    const VkImageUsageFlags upload = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    uint32_t cw = (w + 1) / 2, ch = (h + 1) / 2;
    if (!createImage(g.luma, VK_FORMAT_R8_UNORM, w, h, upload) ||
        !createImage(g.chromaU, VK_FORMAT_R8_UNORM, cw, ch, upload) ||
        !createImage(g.chromaV, VK_FORMAT_R8_UNORM, cw, ch, upload) ||
        !createImage(g.rgb, VK_FORMAT_R8G8B8A8_UNORM, w, h, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        XC_LOGE("gpu: video images %ux%u", w, h);
        return false;
    }
    writeSampled(g.yuvSet, 0, g.luma.view, g.linear);  // deband samples between pixels
    writeSampled(g.yuvSet, 1, g.chromaU.view, g.linear);
    writeSampled(g.yuvSet, 2, g.chromaV.view, g.linear);
    writeStorage(g.yuvSet, 3, g.rgb.view);
    writeSampled(g.easuVideoSet, 0, g.rgb.view, g.linear);
    XC_LOGI("gpu: video images %ux%u", w, h);
    return true;
}

// --- frames -------------------------------------------------------------------

// Every image stays in GENERAL; the barriers only order the work.
void barrier(VkImage image, VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(g.cmd, &dep);
}

// Everything before (copies, dispatches) done and visible to what follows.
void memoryBarrier() {
    VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &b;
    vkCmdPipelineBarrier2(g.cmd, &dep);
}

void copyToImage(const Buffer& src, size_t offset, uint32_t rowTexels, const Image& dst) {
    barrier(dst.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    VkBufferImageCopy copy{};
    copy.bufferOffset = offset;
    copy.bufferRowLength = rowTexels;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {dst.w, dst.h, 1};
    vkCmdCopyBufferToImage(g.cmd, src.buffer, dst.image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
}

uint32_t asBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// Waits for the previous frame, acquires an image and starts recording.
bool beginFrame(bool wait) {
    if (!g.ready) return false;
    if (g.fencePending) {
        vkWaitForFences(g.device, 1, &g.fence, VK_TRUE, UINT64_MAX);
        g.fencePending = false;
    }
    if (g.frameOpen) return true;  // drawn twice without a present: draw over it
    if (g.presentWait && g.presented) {
        if (!g_noPresentWait) {
            // The frame before on the screen first (at most 50 ms, then go on).
            if (vkWaitForPresentKHR(g.device, g.swapchain, g.presented, 50'000'000ull) == VK_SUCCESS) {
                g_shownAtUs = platform::nowUs();
                g_shownId = g.presented;
            }
        } else {
            // Not pacing (for comparison): the newest frame already shown.
            for (uint64_t id = g.presented; id > g_shownId; --id)
                if (vkWaitForPresentKHR(g.device, g.swapchain, id, 0) == VK_SUCCESS) {
                    g_shownAtUs = platform::nowUs();
                    g_shownId = id;
                    break;
                }
        }
    }
    uint64_t t0 = platform::nowUs();
    VkResult r = vkAcquireNextImageKHR(g.device, g.swapchain, wait ? UINT64_MAX : 0, g.acquired, VK_NULL_HANDLE,
                                       &g.frameIndex);
    if (r == VK_NOT_READY || r == VK_TIMEOUT) return false;
    uint64_t waited = platform::nowUs() - t0;
    g_acquireWaitUs += waited;
    g_acquireWaitMaxUs = std::max(g_acquireWaitMaxUs, waited);
    if (++g_acquires == 600) {
        XC_LOGI("gpu: %u-image swapchain, last 600 frames waited %.2f ms on average (max %.2f) for a free image",
                g.imageCount, g_acquireWaitUs / 600000.0, g_acquireWaitMaxUs / 1000.0);
        g_acquires = g_acquireWaitUs = g_acquireWaitMaxUs = 0;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return fail("vkAcquireNextImageKHR", r);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(g.cmd, 0);
    vkBeginCommandBuffer(g.cmd, &begin);
    g.frameOpen = true;
    return true;
}

// `input` (the RGB picture, w x h) upscaled with EASU, sharpened with RCAS,
// the overlay on top, into the acquired image.
void recordUpscale(VkDescriptorSet easuSet, uint32_t w, uint32_t h, int sharpness, bool overlay) {
    // EASU constants (FsrEasuCon), the picture filling the display.
    float ow = static_cast<float>(g.extent.width), oh = static_cast<float>(g.extent.height);
    float iw = static_cast<float>(w), ih = static_cast<float>(h);
    uint32_t easu[16] = {asBits(iw / ow), asBits(ih / oh), asBits(0.5f * iw / ow - 0.5f), asBits(0.5f * ih / oh - 0.5f),
                         asBits(1.0f / iw), asBits(1.0f / ih), asBits(1.0f / iw), asBits(-1.0f / ih),
                         asBits(-1.0f / iw), asBits(2.0f / ih), asBits(1.0f / iw), asBits(2.0f / ih),
                         asBits(0.0f / iw), asBits(4.0f / ih), 0, 0};
    barrier(g.upscaled.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.easu.pipeline);
    vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.easu.layout, 0, 1, &easuSet, 0, nullptr);
    vkCmdPushConstants(g.cmd, g.easu.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof easu, easu);
    vkCmdDispatch(g.cmd, (g.extent.width + 7) / 8, (g.extent.height + 7) / 8, 1);
    memoryBarrier();

    // RCAS (FsrRcasCon): sharpness in stops, 0 = the most.
    float stops = sharpness >= 256 ? 0.2f : sharpness >= 176 ? 0.5f : sharpness > 0 ? 1.0f : 2.0f;
    float linear = std::exp2(-stops);
    struct {
        uint32_t con[4];
        uint32_t sharpen;
        float opacity;
    } rcas{{asBits(linear), 0, 0, 0}, sharpness > 0 ? 1u : 0u, overlay ? 1.0f : 0.0f};
    barrier(g.images[g.frameIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.rcas.pipeline);
    vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.rcas.layout, 0, 1, &g.rcasSets[g.frameIndex], 0,
                            nullptr);
    vkCmdPushConstants(g.cmd, g.rcas.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof rcas, &rcas);
    vkCmdDispatch(g.cmd, (g.extent.width + 7) / 8, (g.extent.height + 7) / 8, 1);
    barrier(g.images[g.frameIndex], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

// The overlay image, when setOverlay() changed it.
void recordOverlay(bool& shown) {
    std::lock_guard<std::mutex> lock(g_overlayMutex);
    shown = g_overlayShown;
    if (!g_overlayDirty) return;
    std::memcpy(g.overlayStaging.map, g_overlayPixels.data(), g.overlayStaging.size);
    copyToImage(g.overlayStaging, 0, kUiW, g.overlay);
    g_overlayDirty = false;
}

}  // namespace

bool init() {
    if (g.ready) return true;
    uint64_t t0 = platform::nowMs();
    if (!createInstance() || !createDevice() || !createSwapchain() || !createCommands() || !createPipelines())
        return false;
    g.ready = true;
    XC_LOGI("gpu: ready in %llu ms", static_cast<unsigned long long>(platform::nowMs() - t0));
    return true;
}

bool ready() { return g.ready; }

bool drawYuv420(const uint8_t* y, const uint8_t* u, const uint8_t* v, int strideY, int strideU, int strideV,
                int width, int height, bool wait) {
    if (width <= 0 || height <= 0) return false;
    size_t ySize = static_cast<size_t>(strideY) * height, cSize = static_cast<size_t>(strideU) * ((height + 1) / 2);
    size_t vSize = static_cast<size_t>(strideV) * ((height + 1) / 2);
    if (ySize + cSize + vSize > g.staging.size) return false;
    if (!beginFrame(wait)) return false;
    if (!videoImages(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) return false;
    std::memcpy(g.staging.map, y, ySize);
    std::memcpy(g.staging.map + ySize, u, cSize);
    std::memcpy(g.staging.map + ySize + cSize, v, vSize);
    copyToImage(g.staging, 0, static_cast<uint32_t>(strideY), g.luma);
    copyToImage(g.staging, ySize, static_cast<uint32_t>(strideU), g.chromaU);
    copyToImage(g.staging, ySize + cSize, static_cast<uint32_t>(strideV), g.chromaV);
    bool overlay = false;
    recordOverlay(overlay);
    barrier(g.rgb.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    memoryBarrier();  // the planes copied
    vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.yuv.pipeline);
    vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.yuv.layout, 0, 1, &g.yuvSet, 0, nullptr);
    // Deband: low smooths steps up to ~2 code values in one pass, high up
    // to ~4 in two (libplacebo's defaults are close to low).
    int deband = g_deband;
    struct {
        uint32_t iterations;
        float threshold, radius, grain;
        uint32_t frame;
    } yuvPc{deband >= 2 ? 2u : deband == 1 ? 1u : 0u, deband >= 2 ? 0.016f : 0.008f, 16.0f,
            deband >= 2 ? 0.005f : 0.003f, ++g_frame};
    vkCmdPushConstants(g.cmd, g.yuv.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof yuvPc, &yuvPc);
    vkCmdDispatch(g.cmd, (g.rgb.w + 7) / 8, (g.rgb.h + 7) / 8, 1);
    memoryBarrier();
    recordUpscale(g.easuVideoSet, g.rgb.w, g.rgb.h, g_sharpness, overlay);
    return true;
}

void drawRgba(const uint32_t* pixels) {
    if (!beginFrame(true)) return;
    std::memcpy(g.staging.map, pixels, kUiW * kUiH * 4);
    copyToImage(g.staging, 0, kUiW, g.ui);
    memoryBarrier();
    // The menus' text and edges: a light RCAS after EASU.
    recordUpscale(g.easuUiSet, kUiW, kUiH, 96, false);
}

void present() {
    if (!g.frameOpen) return;
    g.frameOpen = false;
    vkEndCommandBuffer(g.cmd);
    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &g.acquired;
    submit.pWaitDstStageMask = &wait;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &g.cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &g.rendered;
    vkResetFences(g.device, 1, &g.fence);
    VkResult r = vkQueueSubmit(g.queue, 1, &submit, g.fence);
    if (r != VK_SUCCESS) {
        fail("vkQueueSubmit", r);
        return;
    }
    g.fencePending = true;
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    uint64_t id = g.presented + 1;
    VkPresentIdKHR presentId{VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
    presentId.swapchainCount = 1;
    presentId.pPresentIds = &id;
    if (g.presentWait) present.pNext = &presentId;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &g.rendered;
    present.swapchainCount = 1;
    present.pSwapchains = &g.swapchain;
    present.pImageIndices = &g.frameIndex;
    r = vkQueuePresentKHR(g.queue, &present);
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) fail("vkQueuePresentKHR", r);
    g.lastPresented = g.frameIndex;
    g.presented = id;
}

void setOverlay(const uint32_t* pixels, int x, int y, int w, int h, uint8_t opacity) {
    std::lock_guard<std::mutex> lock(g_overlayMutex);
    std::fill(g_overlayPixels.begin(), g_overlayPixels.end(), 0u);
    g_overlayShown = pixels && w > 0 && h > 0;
    if (g_overlayShown) {
        // Alpha carries the opacity: RCAS mixes by it.
        uint32_t alpha = static_cast<uint32_t>(opacity) << 24;
        for (int row = 0; row < h; ++row) {
            int sy = y + row;
            if (sy < 0 || sy >= static_cast<int>(kUiH)) continue;
            for (int col = 0; col < w; ++col) {
                int sx = x + col;
                if (sx < 0 || sx >= static_cast<int>(kUiW)) continue;
                g_overlayPixels[static_cast<size_t>(sy) * kUiW + sx] =
                    (pixels[static_cast<size_t>(row) * w + col] & 0x00FFFFFFu) | alpha;
            }
        }
    }
    g_overlayDirty = true;
}

void setSharpness(int amount) { g_sharpness = std::clamp(amount, 0, 256); }

void setDeband(int level) { g_deband = std::clamp(level, 0, 2); }

void setPresentWait(bool on) { g_noPresentWait = !on; }

uint64_t lastPresentId() { return g.presented; }

bool lastShown(uint64_t& id, uint64_t& atUs) {
    id = g_shownId;
    atUs = g_shownAtUs;
    return id != 0;
}

void setSwapImages(int count) { g_swapImages = static_cast<uint32_t>(std::clamp(count, 2, 5)); }

bool readBack(std::vector<uint8_t>& rgb, int& width, int& height) {
    if (!g.ready || g.lastPresented == UINT32_MAX) return false;
    if (g.fencePending) {
        vkWaitForFences(g.device, 1, &g.fence, VK_TRUE, UINT64_MAX);
        g.fencePending = false;
    }
    size_t bytes = static_cast<size_t>(g.extent.width) * g.extent.height * 4;
    if (!g.readback.buffer && !createBuffer(g.readback, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT)) return false;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(g.cmd, 0);
    vkBeginCommandBuffer(g.cmd, &begin);
    VkImage image = g.images[g.lastPresented];
    barrier(image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_GENERAL);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {g.extent.width, g.extent.height, 1};
    vkCmdCopyImageToBuffer(g.cmd, image, VK_IMAGE_LAYOUT_GENERAL, g.readback.buffer, 1, &copy);
    barrier(image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    vkEndCommandBuffer(g.cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &g.cmd;
    vkResetFences(g.device, 1, &g.fence);
    if (vkQueueSubmit(g.queue, 1, &submit, g.fence) != VK_SUCCESS) return false;
    vkWaitForFences(g.device, 1, &g.fence, VK_TRUE, UINT64_MAX);
    width = static_cast<int>(g.extent.width);
    height = static_cast<int>(g.extent.height);
    rgb.resize(static_cast<size_t>(width) * height * 3);
    const uint8_t* p = g.readback.map;  // B, G, R, A
    for (size_t i = 0, n = static_cast<size_t>(width) * height; i < n; ++i) {
        rgb[i * 3] = p[i * 4 + 2];
        rgb[i * 3 + 1] = p[i * 4 + 1];
        rgb[i * 3 + 2] = p[i * 4];
    }
    return true;
}

void probe(int frames) {
    if (!g.ready) return;
    uint64_t start = 0;
    int shown = 0;
    for (int f = 0; f < frames; ++f) {
        if (f == 10) start = platform::nowUs();
        if (!beginFrame(true)) break;
        barrier(g.images[g.frameIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        // Green, cycling through its shades: the TV shows the GPU is presenting.
        float t = static_cast<float>(f % 60) / 60.0f;
        VkClearColorValue color{{0.05f, 0.25f + 0.5f * t, 0.10f, 1.0f}};
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(g.cmd, g.images[g.frameIndex], VK_IMAGE_LAYOUT_GENERAL, &color, 1, &range);
        barrier(g.images[g.frameIndex], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        present();
        ++shown;
    }
    if (shown > 10) {
        double seconds = (platform::nowUs() - start) / 1e6;
        double expected = (shown - 10) * 1000.0 / g.refreshMilliHz;
        XC_LOGI("gpu: probe presented %d frames; the last %d in %.3f s (%.3f s at the refresh)", shown, shown - 10,
                seconds, expected);
    }
}

}  // namespace xc::display::gpu
