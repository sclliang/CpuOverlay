#pragma once
#include "game_overlay_runtime.h"
#include <vulkan/vulkan.h>
extern "C" {
#include <vulkan/vk_layer.h>
}
#include <memory>
#include <unordered_map>

namespace GameOverlay::Vk {
#define CPU_OVERLAY_VK_FUNCTIONS(X) \
    X(DestroyDevice) X(GetDeviceQueue) X(GetDeviceQueue2) X(CreateSwapchainKHR) X(DestroySwapchainKHR) X(GetSwapchainImagesKHR) X(QueuePresentKHR) \
    X(DeviceWaitIdle) X(QueueSubmit) X(CreateImageView) X(DestroyImageView) X(CreateRenderPass) X(DestroyRenderPass) \
    X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(DestroyPipelineLayout) \
    X(CreateShaderModule) X(DestroyShaderModule) X(CreateGraphicsPipelines) X(DestroyPipeline) \
    X(CreateSampler) X(DestroySampler) X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
    X(CreateFramebuffer) X(DestroyFramebuffer) X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(ResetCommandPool) \
    X(BeginCommandBuffer) X(EndCommandBuffer) X(CreateFence) X(DestroyFence) X(GetFenceStatus) X(ResetFences) \
    X(CreateSemaphore) X(DestroySemaphore) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory) \
    X(AllocateMemory) X(FreeMemory) X(MapMemory) X(UnmapMemory) X(FlushMappedMemoryRanges) \
    X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory) \
    X(CmdPipelineBarrier) X(CmdCopyBufferToImage) X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdBindPipeline) \
    X(CmdSetViewport) X(CmdSetScissor) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdBindVertexBuffers) X(CmdDraw)
struct Dispatch {
#define CPU_OVERLAY_VK_FIELD(name) PFN_vk##name name = nullptr;
    CPU_OVERLAY_VK_FUNCTIONS(CPU_OVERLAY_VK_FIELD)
#undef CPU_OVERLAY_VK_FIELD
};
struct Instance {
    VkInstance handle = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr gipa = nullptr;
    PFN_GetPhysicalDeviceProcAddr physicalProc = nullptr;
    PFN_vkDestroyInstance destroy = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties memoryProperties = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties queueProperties = nullptr;
    PFN_vkCreateWin32SurfaceKHR createSurface = nullptr;
    PFN_vkDestroySurfaceKHR destroySurface = nullptr;
    std::unordered_map<VkSurfaceKHR, HWND> surfaces;
};
struct Renderer;
struct Swapchain {
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkFormat format{};
    VkExtent2D extent{};
    VkImageUsageFlags usage = 0;
    VkSwapchainCreateFlagsKHR flags = 0;
    HWND window = nullptr;
    std::vector<VkImage> images;
    std::shared_ptr<Renderer> renderer;
};
struct Queue { uint32_t family = 0; VkQueueFlags flags = 0; bool protectedQueue = false; };
struct Device {
    VkDevice handle = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    std::shared_ptr<Instance> instance;
    PFN_vkGetDeviceProcAddr gdpa = nullptr;
    PFN_vkSetDeviceLoaderData setLoaderData = nullptr;
    Dispatch fn;
    VkPhysicalDeviceMemoryProperties memory{};
    std::vector<VkQueueFamilyProperties> families;
    std::unordered_map<VkQueue, Queue> queues;
    std::unordered_map<VkSwapchainKHR, std::shared_ptr<Swapchain>> swapchains;
};
std::shared_ptr<Renderer> CreateRenderer(Device& device, Swapchain& chain, uint32_t family);
VkResult Prepare(Device& device, Swapchain& chain, uint32_t image, uint32_t family, const Frame& source,
    VkCommandBuffer& command, VkSemaphore& finished, VkFence& fence);
}
