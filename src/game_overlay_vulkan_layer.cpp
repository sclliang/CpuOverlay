#include "game_overlay_vulkan.h"
#include <cstring>

#define CPU_OVERLAY_VK_EXPORT extern "C" __declspec(dllexport) VKAPI_ATTR
CPU_OVERLAY_VK_EXPORT PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char*);
CPU_OVERLAY_VK_EXPORT PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice, const char*);

namespace GameOverlay::Vk {
namespace {
void* Key(void* handle) { return handle ? *reinterpret_cast<void**>(handle) : nullptr; }
auto& instances = *new std::unordered_map<void*, std::shared_ptr<Instance>>;
auto& devices = *new std::unordered_map<void*, std::shared_ptr<Device>>;
std::shared_ptr<Instance> FindInstance(void* handle) { auto it = instances.find(Key(handle)); return it == instances.end() ? nullptr : it->second; }
std::shared_ptr<Device> FindDevice(void* handle) { auto it = devices.find(Key(handle)); return it == devices.end() ? nullptr : it->second; }
void RememberQueue(Device& device, VkQueue queue, uint32_t family, bool protectedQueue) {
    if (!queue || family >= device.families.size()) return;
    if (device.setLoaderData) device.setLoaderData(device.handle, queue);
    device.queues[queue] = {family, device.families[family].queueFlags, protectedQueue};
}
VkLayerInstanceCreateInfo* InstanceInfo(const VkInstanceCreateInfo* info) {
    for (auto* next = reinterpret_cast<const VkBaseInStructure*>(info->pNext); next; next = next->pNext)
        if (next->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO) {
            auto* item = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<VkBaseInStructure*>(next));
            if (item->function == VK_LAYER_LINK_INFO) return item;
        }
    return nullptr;
}
VkLayerDeviceCreateInfo* DeviceInfo(const VkDeviceCreateInfo* info, VkLayerFunction function) {
    for (auto* next = reinterpret_cast<const VkBaseInStructure*>(info->pNext); next; next = next->pNext)
        if (next->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO) {
            auto* item = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<VkBaseInStructure*>(next));
            if (item->function == function) return item;
        }
    return nullptr;
}
}
}
using namespace GameOverlay::Vk;

CPU_OVERLAY_VK_EXPORT VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* info, const VkAllocationCallbacks* allocator, VkInstance* output) {
    auto* link = InstanceInfo(info); if (!link || !link->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    auto gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    auto physicalProc = link->u.pLayerInfo->pfnNextGetPhysicalDeviceProcAddr;
    auto create = reinterpret_cast<PFN_vkCreateInstance>(gipa(VK_NULL_HANDLE, "vkCreateInstance"));
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    if (!create) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = create(info, allocator, output); if (result != VK_SUCCESS) return result;
    auto state = std::make_shared<Instance>(); state->handle = *output; state->gipa = gipa; state->physicalProc = physicalProc;
    state->destroy = reinterpret_cast<PFN_vkDestroyInstance>(gipa(*output, "vkDestroyInstance"));
    state->memoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(*output, "vkGetPhysicalDeviceMemoryProperties"));
    state->queueProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gipa(*output, "vkGetPhysicalDeviceQueueFamilyProperties"));
    state->createSurface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(gipa(*output, "vkCreateWin32SurfaceKHR"));
    state->destroySurface = reinterpret_cast<PFN_vkDestroySurfaceKHR>(gipa(*output, "vkDestroySurfaceKHR"));
    std::lock_guard<std::recursive_mutex> lock(GameOverlay::RenderMutex()); instances[Key(*output)] = state;
    return result;
}
CPU_OVERLAY_VK_EXPORT void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) {
    std::shared_ptr<Instance> state;
    { std::lock_guard<std::recursive_mutex> lock(GameOverlay::RenderMutex()); state = FindInstance(instance); instances.erase(Key(instance)); }
    if (state && state->destroy) state->destroy(instance, allocator);
}
CPU_OVERLAY_VK_EXPORT VkResult VKAPI_CALL vkCreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* info,
    const VkAllocationCallbacks* allocator, VkSurfaceKHR* output) {
    std::lock_guard<std::recursive_mutex> lock(GameOverlay::RenderMutex()); auto state = FindInstance(instance);
    if (!state || !state->createSurface) return VK_ERROR_INITIALIZATION_FAILED;
    const auto result = state->createSurface(instance, info, allocator, output);
    if (result == VK_SUCCESS) state->surfaces[*output] = info->hwnd;
    return result;
}
CPU_OVERLAY_VK_EXPORT void VKAPI_CALL vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* allocator) {
    std::lock_guard<std::recursive_mutex> lock(GameOverlay::RenderMutex()); auto state = FindInstance(instance);
    if (state) { state->surfaces.erase(surface); if (state->destroySurface) state->destroySurface(instance, surface, allocator); }
}
CPU_OVERLAY_VK_EXPORT VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkDevice* output) {
    std::lock_guard<std::recursive_mutex> lock(GameOverlay::RenderMutex()); auto instance = FindInstance(physical);
    auto* link = DeviceInfo(info, VK_LAYER_LINK_INFO); if (!instance || !link || !link->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    auto gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr; auto gdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    auto create = reinterpret_cast<PFN_vkCreateDevice>(gipa(instance->handle, "vkCreateDevice"));
    auto* callback = DeviceInfo(info, VK_LOADER_DATA_CALLBACK);
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    if (!create) return VK_ERROR_INITIALIZATION_FAILED;
    auto result = create(physical, info, allocator, output); if (result != VK_SUCCESS) return result;
    auto state = std::make_shared<Device>(); state->handle = *output; state->physical = physical; state->gdpa = gdpa; state->instance = instance;
    if (callback) state->setLoaderData = callback->u.pfnSetDeviceLoaderData;
#define CPU_OVERLAY_LOAD_VK(name) state->fn.name = reinterpret_cast<PFN_vk##name>(gdpa(*output, "vk" #name));
    CPU_OVERLAY_VK_FUNCTIONS(CPU_OVERLAY_LOAD_VK)
#undef CPU_OVERLAY_LOAD_VK
    instance->memoryProperties(physical, &state->memory);
    uint32_t count = 0; instance->queueProperties(physical, &count, nullptr); state->families.resize(count);
    instance->queueProperties(physical, &count, state->families.data());
    devices[Key(*output)] = state;
    for (uint32_t i = 0; i < info->queueCreateInfoCount; ++i) {
        const auto& q = info->pQueueCreateInfos[i];
        for (uint32_t index = 0; index < q.queueCount; ++index) {
            VkQueue queue = VK_NULL_HANDLE;
            if (!q.flags) state->fn.GetDeviceQueue(*output, q.queueFamilyIndex, index, &queue);
            else if (state->fn.GetDeviceQueue2) {
                VkDeviceQueueInfo2 qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2}; qi.flags = q.flags; qi.queueFamilyIndex = q.queueFamilyIndex; qi.queueIndex = index;
                state->fn.GetDeviceQueue2(*output, &qi, &queue);
            }
            RememberQueue(*state, queue, q.queueFamilyIndex, q.flags != 0);
        }
    }
    return result;
}
CPU_OVERLAY_VK_EXPORT void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) {
    std::lock_guard<std::recursive_mutex> lock(GameOverlay::RenderMutex()); auto state = FindDevice(device);
    if (!state) return;
    state->fn.DeviceWaitIdle(device); state->swapchains.clear(); devices.erase(Key(device));
    state->fn.DestroyDevice(device, allocator);
}
CPU_OVERLAY_VK_EXPORT void VKAPI_CALL vkGetDeviceQueue(VkDevice device, uint32_t family, uint32_t index, VkQueue* queue) {
    std::lock_guard<std::recursive_mutex> lock(GameOverlay::RenderMutex()); auto state = FindDevice(device); if (!state) return;
    state->fn.GetDeviceQueue(device, family, index, queue); RememberQueue(*state, *queue, family, false);
}
CPU_OVERLAY_VK_EXPORT void VKAPI_CALL vkGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* info, VkQueue* queue) {
    std::lock_guard<std::recursive_mutex> lock(GameOverlay::RenderMutex()); auto state = FindDevice(device); if (!state || !state->fn.GetDeviceQueue2) return;
    state->fn.GetDeviceQueue2(device, info, queue); RememberQueue(*state, *queue, info->queueFamilyIndex, info->flags != 0);
}
CPU_OVERLAY_VK_EXPORT VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* info,
    const VkAllocationCallbacks* allocator, VkSwapchainKHR* output) {
    std::lock_guard<std::recursive_mutex> lock(GameOverlay::RenderMutex()); auto state = FindDevice(device);
    if (!state || !state->fn.CreateSwapchainKHR) return VK_ERROR_INITIALIZATION_FAILED;
    auto result = state->fn.CreateSwapchainKHR(device, info, allocator, output);
    if (result == VK_SUCCESS) {
        auto chain = std::make_shared<Swapchain>(); chain->handle = *output; chain->format = info->imageFormat;
        chain->extent = info->imageExtent; chain->usage = info->imageUsage; chain->flags = info->flags;
        auto window = state->instance->surfaces.find(info->surface); if (window != state->instance->surfaces.end()) chain->window = window->second;
        uint32_t count = 0; state->fn.GetSwapchainImagesKHR(device, *output, &count, nullptr); chain->images.resize(count);
        state->fn.GetSwapchainImagesKHR(device, *output, &count, chain->images.data());
        state->swapchains[*output] = chain;
    }
    return result;
}
CPU_OVERLAY_VK_EXPORT void VKAPI_CALL vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR chain, const VkAllocationCallbacks* allocator) {
    std::lock_guard<std::recursive_mutex> lock(GameOverlay::RenderMutex()); auto state = FindDevice(device); if (!state) return;
    state->fn.DeviceWaitIdle(device); state->swapchains.erase(chain); state->fn.DestroySwapchainKHR(device, chain, allocator);
}
CPU_OVERLAY_VK_EXPORT VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
    std::lock_guard<std::recursive_mutex> lock(GameOverlay::RenderMutex()); auto device = FindDevice(queue);
    if (!device || !device->fn.QueuePresentKHR) return VK_ERROR_INITIALIZATION_FAILED;
    const auto* frame = GameOverlay::ReadFrame(GameOverlay::Vulkan);
    const auto q = device->queues.find(queue);
    if (!frame || q == device->queues.end() || !(q->second.flags & VK_QUEUE_GRAPHICS_BIT) || q->second.protectedQueue)
        return device->fn.QueuePresentKHR(queue, info);
    // A submit consumes the original wait semaphores exactly once. Each rendered image
    // gets an image-indexed completion semaphore, which presentation then waits on.
    std::vector<VkCommandBuffer> commands; std::vector<VkSemaphore> finished; std::vector<VkFence> fences;
    for (uint32_t i = 0; i < info->swapchainCount; ++i) {
        auto found = device->swapchains.find(info->pSwapchains[i]); if (found == device->swapchains.end()) continue;
        VkCommandBuffer command = VK_NULL_HANDLE; VkSemaphore semaphore = VK_NULL_HANDLE; VkFence fence = VK_NULL_HANDLE;
        const auto result = Prepare(*device, *found->second, info->pImageIndices[i], q->second.family, *frame, command, semaphore, fence);
        if (result == VK_SUCCESS) { commands.push_back(command); finished.push_back(semaphore); fences.push_back(fence); }
        else if (result != VK_NOT_READY) GameOverlay::ReportError(GameOverlay::Vulkan, result);
    }
    if (commands.empty()) return device->fn.QueuePresentKHR(queue, info);
    // Submit each image separately so its fence accurately protects its private resources.
    // Only the first submit waits on the game's signals; queue ordering protects later submits.
    std::vector<VkPipelineStageFlags> stages(info->waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    for (size_t i = 0; i < commands.size(); ++i) {
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        if (!i) { submit.waitSemaphoreCount = info->waitSemaphoreCount; submit.pWaitSemaphores = info->pWaitSemaphores; submit.pWaitDstStageMask = stages.data(); }
        submit.commandBufferCount = 1; submit.pCommandBuffers = &commands[i]; submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &finished[i];
        device->fn.ResetFences(device->handle, 1, &fences[i]);
        const auto result = device->fn.QueueSubmit(queue, 1, &submit, fences[i]);
        if (result != VK_SUCCESS) { GameOverlay::ReportError(GameOverlay::Vulkan, result); return result; }
    }
    VkPresentInfoKHR modified = *info; modified.waitSemaphoreCount = static_cast<uint32_t>(finished.size()); modified.pWaitSemaphores = finished.data();
    const auto result = device->fn.QueuePresentKHR(queue, &modified);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        for (uint32_t i = 0; i < info->swapchainCount; ++i) {
            auto found = device->swapchains.find(info->pSwapchains[i]);
            if (found != device->swapchains.end()) { GameOverlay::ReportDraw(GameOverlay::Vulkan, found->second->extent.width, found->second->extent.height); break; }
        }
    }
    return result;
}
namespace {
PFN_vkVoidFunction Intercept(const char* name) {
    if (!name) return nullptr;
#define CPU_OVERLAY_INTERCEPT(fn) if (strcmp(name, #fn) == 0) return reinterpret_cast<PFN_vkVoidFunction>(&fn);
    CPU_OVERLAY_INTERCEPT(vkGetInstanceProcAddr) CPU_OVERLAY_INTERCEPT(vkGetDeviceProcAddr)
    CPU_OVERLAY_INTERCEPT(vkCreateInstance) CPU_OVERLAY_INTERCEPT(vkDestroyInstance) CPU_OVERLAY_INTERCEPT(vkCreateDevice)
    CPU_OVERLAY_INTERCEPT(vkCreateWin32SurfaceKHR) CPU_OVERLAY_INTERCEPT(vkDestroySurfaceKHR)
    CPU_OVERLAY_INTERCEPT(vkDestroyDevice) CPU_OVERLAY_INTERCEPT(vkGetDeviceQueue) CPU_OVERLAY_INTERCEPT(vkGetDeviceQueue2)
    CPU_OVERLAY_INTERCEPT(vkCreateSwapchainKHR) CPU_OVERLAY_INTERCEPT(vkDestroySwapchainKHR) CPU_OVERLAY_INTERCEPT(vkQueuePresentKHR)
#undef CPU_OVERLAY_INTERCEPT
    return nullptr;
}
}
CPU_OVERLAY_VK_EXPORT PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    std::lock_guard<std::recursive_mutex> lock(GameOverlay::RenderMutex());
    if (!name) return nullptr;
    if (!strcmp(name, "vkCreateInstance") || !strcmp(name, "vkGetInstanceProcAddr") || !strcmp(name, "vkGetDeviceProcAddr")) return Intercept(name);
    auto state = FindInstance(instance); if (!state) return nullptr;
    auto next = state->gipa(instance, name); if (!next) return nullptr;
    if (auto function = Intercept(name)) return function;
    return next;
}
CPU_OVERLAY_VK_EXPORT PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* name) {
    std::lock_guard<std::recursive_mutex> lock(GameOverlay::RenderMutex()); auto state = FindDevice(device);
    if (!state) return nullptr;
    auto next = state->gdpa(device, name); if (!next) return nullptr;
    if (auto function = Intercept(name)) return function;
    return next;
}
static PFN_vkVoidFunction VKAPI_CALL PhysicalProc(VkInstance instance, const char* name) {
    std::lock_guard<std::recursive_mutex> lock(GameOverlay::RenderMutex()); auto state = FindInstance(instance);
    return state ? (state->physicalProc ? state->physicalProc(instance, name) : state->gipa(instance, name)) : nullptr;
}
VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* info) {
    if (!info || info->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT || info->loaderLayerInterfaceVersion < 1) return VK_ERROR_INITIALIZATION_FAILED;
    info->loaderLayerInterfaceVersion = std::min(info->loaderLayerInterfaceVersion, 2u);
    info->pfnGetInstanceProcAddr = &vkGetInstanceProcAddr; info->pfnGetDeviceProcAddr = &vkGetDeviceProcAddr;
    info->pfnGetPhysicalDeviceProcAddr = &PhysicalProc; return VK_SUCCESS;
}
