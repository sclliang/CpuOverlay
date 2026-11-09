#include <windows.h>
#include <vulkan/vulkan.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>

namespace {
void Check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) { std::cerr << operation << " VkResult=" << result << '\n'; throw std::runtime_error(operation); }
}
#define INSTANCE_FUNCTIONS(X) X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceQueueFamilyProperties) \
    X(GetPhysicalDeviceMemoryProperties) X(CreateWin32SurfaceKHR) X(DestroySurfaceKHR) X(GetPhysicalDeviceSurfaceSupportKHR) \
    X(GetPhysicalDeviceSurfaceCapabilitiesKHR) X(GetPhysicalDeviceSurfaceFormatsKHR) X(CreateDevice) X(GetDeviceProcAddr)
#define DEVICE_FUNCTIONS(X) X(DestroyDevice) X(GetDeviceQueue) X(CreateSwapchainKHR) X(DestroySwapchainKHR) X(GetSwapchainImagesKHR) X(AcquireNextImageKHR) \
    X(QueueSubmit) X(QueuePresentKHR) X(QueueWaitIdle) X(DeviceWaitIdle) X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(ResetCommandPool) \
    X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdPipelineBarrier) X(CmdClearColorImage) X(CmdCopyImageToBuffer) \
    X(CreateSemaphore) X(DestroySemaphore) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) \
    X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(InvalidateMappedMemoryRanges)
struct Api {
#define FIELD(name) PFN_vk##name name = nullptr;
    INSTANCE_FUNCTIONS(FIELD) DEVICE_FUNCTIONS(FIELD)
#undef FIELD
};
struct State {
    HMODULE module = nullptr;
    Api a;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSwapchainKHR chain = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE, rendered = VK_NULL_HANDLE;
    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    ~State() {
        if (device) {
            if (a.DeviceWaitIdle) a.DeviceWaitIdle(device);
            if (readback) a.DestroyBuffer(device, readback, nullptr);
            if (memory) a.FreeMemory(device, memory, nullptr);
            if (pool) a.DestroyCommandPool(device, pool, nullptr);
            if (acquired) a.DestroySemaphore(device, acquired, nullptr);
            if (rendered) a.DestroySemaphore(device, rendered, nullptr);
            if (chain) a.DestroySwapchainKHR(device, chain, nullptr);
            a.DestroyDevice(device, nullptr);
        }
        if (surface) a.DestroySurfaceKHR(instance, surface, nullptr);
        if (instance) a.DestroyInstance(instance, nullptr);
        if (module) FreeLibrary(module);
    }
};
}
bool VulkanOverlayProbe(HWND window, const std::filesystem::path& directory, bool expected) {
    State s;
    SetEnvironmentVariableW(L"VK_LAYER_PATH", directory.c_str());
    s.module = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!s.module) throw std::runtime_error("Vulkan loader unavailable");
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(s.module, "vkGetInstanceProcAddr"));
    if (!gipa) throw std::runtime_error("Vulkan get-proc unavailable");
    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(gipa(VK_NULL_HANDLE, "vkCreateInstance"));
    const char* extensions[]{VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    const char* layers[]{"VK_LAYER_SIXUNITED_CpuOverlay"};
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO}; application.pApplicationName = "CpuOverlay rendering probe"; application.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo instance{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; instance.pApplicationInfo = &application;
    instance.enabledExtensionCount = ARRAYSIZE(extensions); instance.ppEnabledExtensionNames = extensions;
    instance.enabledLayerCount = 1; instance.ppEnabledLayerNames = layers;
    Check(createInstance(&instance, nullptr, &s.instance), "Vulkan create instance with built-in layer");
#define LOAD_INSTANCE(name) s.a.name = reinterpret_cast<PFN_vk##name>(gipa(s.instance, "vk" #name)); if (!s.a.name) throw std::runtime_error("Missing vk" #name);
    INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
    VkWin32SurfaceCreateInfoKHR surface{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR}; surface.hinstance = GetModuleHandleW(nullptr); surface.hwnd = window;
    Check(s.a.CreateWin32SurfaceKHR(s.instance, &surface, nullptr, &s.surface), "Vulkan surface");
    uint32_t count = 0; Check(s.a.EnumeratePhysicalDevices(s.instance, &count, nullptr), "Vulkan adapters");
    std::vector<VkPhysicalDevice> physicals(count); Check(s.a.EnumeratePhysicalDevices(s.instance, &count, physicals.data()), "Vulkan adapter list");
    VkPhysicalDevice physical = VK_NULL_HANDLE; uint32_t family = UINT32_MAX;
    for (auto adapter : physicals) {
        uint32_t n = 0; s.a.GetPhysicalDeviceQueueFamilyProperties(adapter, &n, nullptr); std::vector<VkQueueFamilyProperties> families(n);
        s.a.GetPhysicalDeviceQueueFamilyProperties(adapter, &n, families.data());
        for (uint32_t i = 0; i < n; ++i) {
            VkBool32 supported = VK_FALSE; Check(s.a.GetPhysicalDeviceSurfaceSupportKHR(adapter, i, s.surface, &supported), "Vulkan present support");
            if (supported && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { physical = adapter; family = i; break; }
        }
        if (physical) break;
    }
    if (!physical) throw std::runtime_error("No Vulkan graphics/present queue");
    const float priority = 1;
    VkDeviceQueueCreateInfo queueCreate{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; queueCreate.queueFamilyIndex = family; queueCreate.queueCount = 1; queueCreate.pQueuePriorities = &priority;
    const char* deviceExtensions[]{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; device.queueCreateInfoCount = 1; device.pQueueCreateInfos = &queueCreate;
    device.enabledExtensionCount = 1; device.ppEnabledExtensionNames = deviceExtensions;
    Check(s.a.CreateDevice(physical, &device, nullptr, &s.device), "Vulkan device");
#define LOAD_DEVICE(name) s.a.name = reinterpret_cast<PFN_vk##name>(s.a.GetDeviceProcAddr(s.device, "vk" #name)); if (!s.a.name) throw std::runtime_error("Missing vk" #name);
    DEVICE_FUNCTIONS(LOAD_DEVICE)
#undef LOAD_DEVICE
    VkQueue queue = VK_NULL_HANDLE; s.a.GetDeviceQueue(s.device, family, 0, &queue);
    VkSurfaceCapabilitiesKHR capabilities{}; Check(s.a.GetPhysicalDeviceSurfaceCapabilitiesKHR(physical, s.surface, &capabilities), "Vulkan capabilities");
    Check(s.a.GetPhysicalDeviceSurfaceFormatsKHR(physical, s.surface, &count, nullptr), "Vulkan formats");
    std::vector<VkSurfaceFormatKHR> formats(count); Check(s.a.GetPhysicalDeviceSurfaceFormatsKHR(physical, s.surface, &count, formats.data()), "Vulkan format list");
    auto format = formats.front();
    for (const auto& candidate : formats) if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM) { format = candidate; break; }
    if (format.format == VK_FORMAT_UNDEFINED) format.format = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX) extent = {320, 180};
    if (!extent.width || !extent.height) throw std::runtime_error("Vulkan surface has zero extent");
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if ((capabilities.supportedUsageFlags & usage) != usage) throw std::runtime_error("Vulkan readback usage not supported");
    VkSwapchainCreateInfoKHR swapchain{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; swapchain.surface = s.surface;
    swapchain.minImageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount && swapchain.minImageCount > capabilities.maxImageCount) swapchain.minImageCount = capabilities.maxImageCount;
    swapchain.imageFormat = format.format; swapchain.imageColorSpace = format.colorSpace; swapchain.imageExtent = extent; swapchain.imageArrayLayers = 1;
    swapchain.imageUsage = usage; swapchain.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; swapchain.preTransform = capabilities.currentTransform;
    swapchain.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; swapchain.presentMode = VK_PRESENT_MODE_FIFO_KHR; swapchain.clipped = VK_TRUE;
    Check(s.a.CreateSwapchainKHR(s.device, &swapchain, nullptr, &s.chain), "Vulkan swapchain");
    Check(s.a.GetSwapchainImagesKHR(s.device, s.chain, &count, nullptr), "Vulkan images");
    std::vector<VkImage> images(count); Check(s.a.GetSwapchainImagesKHR(s.device, s.chain, &count, images.data()), "Vulkan image list");
    VkCommandPoolCreateInfo commandPool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; commandPool.queueFamilyIndex = family; commandPool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    Check(s.a.CreateCommandPool(s.device, &commandPool, nullptr, &s.pool), "Vulkan command pool");
    VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; allocation.commandPool = s.pool; allocation.commandBufferCount = 1; allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    VkCommandBuffer command = VK_NULL_HANDLE; Check(s.a.AllocateCommandBuffers(s.device, &allocation, &command), "Vulkan command buffer");
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    Check(s.a.CreateSemaphore(s.device, &semaphore, nullptr, &s.acquired), "Vulkan acquire semaphore");
    Check(s.a.CreateSemaphore(s.device, &semaphore, nullptr, &s.rendered), "Vulkan render semaphore");
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; buffer.size = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
    buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT; buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    Check(s.a.CreateBuffer(s.device, &buffer, nullptr, &s.readback), "Vulkan readback buffer");
    VkMemoryRequirements requirements{}; s.a.GetBufferMemoryRequirements(s.device, s.readback, &requirements);
    VkPhysicalDeviceMemoryProperties memory{}; s.a.GetPhysicalDeviceMemoryProperties(physical, &memory);
    uint32_t memoryType = UINT32_MAX;
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if ((requirements.memoryTypeBits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) { memoryType = i; break; }
    if (memoryType == UINT32_MAX) throw std::runtime_error("Vulkan host memory unavailable");
    VkMemoryAllocateInfo memoryAllocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; memoryAllocation.allocationSize = requirements.size; memoryAllocation.memoryTypeIndex = memoryType;
    Check(s.a.AllocateMemory(s.device, &memoryAllocation, nullptr, &s.memory), "Vulkan readback allocation");
    Check(s.a.BindBufferMemory(s.device, s.readback, s.memory, 0), "Vulkan bind readback");
    uint32_t firstImage = UINT32_MAX;
    for (unsigned iteration = 0; iteration < images.size() * 4 + 4; ++iteration) {
        uint32_t index = 0; Check(s.a.AcquireNextImageKHR(s.device, s.chain, 5000000000ull, s.acquired, VK_NULL_HANDLE, &index), "Vulkan acquire");
        const bool read = firstImage == index;
        Check(s.a.ResetCommandPool(s.device, s.pool, 0), "Vulkan reset pool");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Check(s.a.BeginCommandBuffer(command, &begin), "Vulkan begin");
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.image = images[index];
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; barrier.subresourceRange.levelCount = barrier.subresourceRange.layerCount = 1;
        barrier.oldLayout = read ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = read ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask = read ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
        s.a.CmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        if (read) {
            VkBufferImageCopy copy{}; copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; copy.imageSubresource.layerCount = 1;
            copy.imageExtent = {extent.width, extent.height, 1};
            s.a.CmdCopyImageToBuffer(command, images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s.readback, 1, &copy);
        } else {
            VkClearColorValue clear{{0.9f, 0.93f, 0.96f, 1}};
            s.a.CmdClearColorImage(command, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &barrier.subresourceRange);
        }
        barrier.oldLayout = barrier.newLayout; barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcAccessMask = read ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = 0;
        s.a.CmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        Check(s.a.EndCommandBuffer(command), "Vulkan end");
        const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &s.acquired; submit.pWaitDstStageMask = &stage;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
        if (!read) { submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &s.rendered; }
        Check(s.a.QueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "Vulkan submit");
        if (read) {
            Check(s.a.QueueWaitIdle(queue), "Vulkan readback wait");
            void* data = nullptr; Check(s.a.MapMemory(s.device, s.memory, 0, VK_WHOLE_SIZE, 0, &data), "Vulkan map readback");
            if (!(memory.memoryTypes[memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE}; range.memory = s.memory; range.size = VK_WHOLE_SIZE;
                Check(s.a.InvalidateMappedMemoryRanges(s.device, 1, &range), "Vulkan invalidate readback");
            }
            const float dpi = static_cast<float>(GetDpiForWindow(window)) / 96;
            const uint32_t x = static_cast<uint32_t>(20 * dpi), y = static_cast<uint32_t>(20 * dpi);
            const auto* pixel = static_cast<const unsigned char*>(data) + (static_cast<size_t>(y) * extent.width + x) * 4;
            const bool bgra = format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_B8G8R8A8_SRGB;
            const int red = pixel[bgra ? 2 : 0], green = pixel[1], blue = pixel[bgra ? 0 : 2];
            std::cout << "Vulkan pixel RGB=" << red << ',' << green << ',' << blue << " format=" << format.format << '\n';
            const bool drawn = abs(red - 220) <= 5 && abs(green - 40) <= 5 && abs(blue - 60) <= 5;
            s.a.UnmapMemory(s.device, s.memory); return drawn == expected;
        }
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; present.waitSemaphoreCount = 1; present.pWaitSemaphores = &s.rendered;
        present.swapchainCount = 1; present.pSwapchains = &s.chain; present.pImageIndices = &index;
        Check(s.a.QueuePresentKHR(queue, &present), "Vulkan present through native layer"); Check(s.a.QueueWaitIdle(queue), "Vulkan present wait");
        if (firstImage == UINT32_MAX) firstImage = index;
    }
    throw std::runtime_error("Vulkan could not reacquire the rendered image");
}
