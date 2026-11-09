#include "game_overlay_vulkan.h"
#include "vulkan_shaders.h"
#include <cstring>
#include <limits>

namespace GameOverlay::Vk {
namespace {
struct Vertex { float x, y, u, v; uint32_t color; };
struct ImageFrame {
    VkImageView targetView = VK_NULL_HANDLE, textureView = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore finished = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    VkImage texture = VK_NULL_HANDLE;
    VkDeviceMemory textureMemory = VK_NULL_HANDLE, uploadMemory = VK_NULL_HANDLE;
    VkBuffer upload = VK_NULL_HANDLE;
    VkMemoryPropertyFlags uploadFlags = 0;
    uint32_t width = 0, height = 0, owner = 0;
    uint64_t generation = 0;
    bool initialized = false;
};
}
struct Renderer {
    Device& d;
    Swapchain& s;
    uint32_t family;
    VkRenderPass pass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptors = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    std::vector<ImageFrame> frames;
    Renderer(Device& device, Swapchain& chain, uint32_t queueFamily) : d(device), s(chain), family(queueFamily) {}
    void DropTexture(ImageFrame& f) {
        if (f.textureView) d.fn.DestroyImageView(d.handle, f.textureView, nullptr);
        if (f.texture) d.fn.DestroyImage(d.handle, f.texture, nullptr);
        if (f.textureMemory) d.fn.FreeMemory(d.handle, f.textureMemory, nullptr);
        if (f.upload) d.fn.DestroyBuffer(d.handle, f.upload, nullptr);
        if (f.uploadMemory) d.fn.FreeMemory(d.handle, f.uploadMemory, nullptr);
        f.textureView = VK_NULL_HANDLE; f.texture = VK_NULL_HANDLE; f.textureMemory = VK_NULL_HANDLE;
        f.upload = VK_NULL_HANDLE; f.uploadMemory = VK_NULL_HANDLE; f.width = f.height = 0; f.initialized = false; f.generation = 0;
    }
    ~Renderer() {
        // The layer drains device work before deleting swapchains or devices.
        for (auto& f : frames) {
            DropTexture(f);
            if (f.framebuffer) d.fn.DestroyFramebuffer(d.handle, f.framebuffer, nullptr);
            if (f.targetView) d.fn.DestroyImageView(d.handle, f.targetView, nullptr);
            if (f.pool) d.fn.DestroyCommandPool(d.handle, f.pool, nullptr);
            if (f.fence) d.fn.DestroyFence(d.handle, f.fence, nullptr);
            if (f.finished) d.fn.DestroySemaphore(d.handle, f.finished, nullptr);
        }
        if (pipeline) d.fn.DestroyPipeline(d.handle, pipeline, nullptr);
        if (pipelineLayout) d.fn.DestroyPipelineLayout(d.handle, pipelineLayout, nullptr);
        if (descriptors) d.fn.DestroyDescriptorPool(d.handle, descriptors, nullptr);
        if (descriptorLayout) d.fn.DestroyDescriptorSetLayout(d.handle, descriptorLayout, nullptr);
        if (sampler) d.fn.DestroySampler(d.handle, sampler, nullptr);
        if (pass) d.fn.DestroyRenderPass(d.handle, pass, nullptr);
    }
    uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags flags) {
        for (uint32_t i = 0; i < d.memory.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (d.memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
        return UINT32_MAX;
    }
    VkResult Allocate(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags flags, VkDeviceMemory& output, uint32_t* selected = nullptr) {
        uint32_t type = MemoryType(requirements.memoryTypeBits, flags);
        if (type == UINT32_MAX && flags == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            type = MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        if (type == UINT32_MAX && flags == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) type = MemoryType(requirements.memoryTypeBits, 0);
        if (type == UINT32_MAX) return VK_ERROR_FEATURE_NOT_PRESENT;
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
        if (selected) *selected = type;
        return d.fn.AllocateMemory(d.handle, &allocation, nullptr, &output);
    }
    VkResult Init() {
        if (!d.fn.CreateRenderPass || !d.fn.CreateGraphicsPipelines || !(s.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
            (s.flags & VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR) || s.images.empty()) return VK_ERROR_FEATURE_NOT_PRESENT;
        VkAttachmentDescription attachment{}; attachment.format = s.format; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &color;
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL; dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0; dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dependencies[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        VkRenderPassCreateInfo renderPass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; renderPass.attachmentCount = 1; renderPass.pAttachments = &attachment;
        renderPass.subpassCount = 1; renderPass.pSubpasses = &subpass; renderPass.dependencyCount = 2; renderPass.pDependencies = dependencies;
        VkResult result = d.fn.CreateRenderPass(d.handle, &renderPass, nullptr, &pass); if (result != VK_SUCCESS) return result;
        VkDescriptorSetLayoutBinding binding{}; binding.binding = 0; binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1; binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; layout.bindingCount = 1; layout.pBindings = &binding;
        if ((result = d.fn.CreateDescriptorSetLayout(d.handle, &layout, nullptr, &descriptorLayout)) != VK_SUCCESS) return result;
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, 16};
        VkPipelineLayoutCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pipelineInfo.setLayoutCount = 1;
        pipelineInfo.pSetLayouts = &descriptorLayout; pipelineInfo.pushConstantRangeCount = 1; pipelineInfo.pPushConstantRanges = &push;
        if ((result = d.fn.CreatePipelineLayout(d.handle, &pipelineInfo, nullptr, &pipelineLayout)) != VK_SUCCESS) return result;
        VkSamplerCreateInfo sampling{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sampling.magFilter = sampling.minFilter = VK_FILTER_LINEAR;
        sampling.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; sampling.addressModeU = sampling.addressModeV = sampling.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampling.maxLod = 1; sampling.maxAnisotropy = 1;
        if ((result = d.fn.CreateSampler(d.handle, &sampling, nullptr, &sampler)) != VK_SUCCESS) return result;
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(s.images.size())};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pool.maxSets = poolSize.descriptorCount; pool.poolSizeCount = 1; pool.pPoolSizes = &poolSize;
        if ((result = d.fn.CreateDescriptorPool(d.handle, &pool, nullptr, &descriptors)) != VK_SUCCESS) return result;
        VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
        VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; shader.codeSize = sizeof(NativeOverlayShaders::vert_spv); shader.pCode = NativeOverlayShaders::vert_spv;
        if ((result = d.fn.CreateShaderModule(d.handle, &shader, nullptr, &vertex)) != VK_SUCCESS) return result;
        shader.codeSize = sizeof(NativeOverlayShaders::frag_spv); shader.pCode = NativeOverlayShaders::frag_spv;
        result = d.fn.CreateShaderModule(d.handle, &shader, nullptr, &fragment);
        if (result != VK_SUCCESS) { d.fn.DestroyShaderModule(d.handle, vertex, nullptr); return result; }
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (auto& stage : stages) { stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stage.pName = "main"; }
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertex;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragment;
        VkVertexInputBindingDescription vertexBinding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attributes[]{ {0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 8}, {2, 0, VK_FORMAT_R8G8B8A8_UNORM, 16} };
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; input.vertexBindingDescriptionCount = 1;
        input.pVertexBindingDescriptions = &vertexBinding; input.vertexAttributeDescriptionCount = ARRAYSIZE(attributes); input.pVertexAttributeDescriptions = attributes;
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE; raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState blend{}; blend.blendEnable = VK_TRUE; blend.srcColorBlendFactor = blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstColorBlendFactor = blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD; blend.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blending.attachmentCount = 1; blending.pAttachments = &blend;
        const VkDynamicState dynamicStates[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = dynamicStates;
        VkGraphicsPipelineCreateInfo pipelineCreate{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; pipelineCreate.stageCount = 2; pipelineCreate.pStages = stages;
        pipelineCreate.pVertexInputState = &input; pipelineCreate.pInputAssemblyState = &assembly; pipelineCreate.pViewportState = &viewport;
        pipelineCreate.pRasterizationState = &raster; pipelineCreate.pMultisampleState = &multisample; pipelineCreate.pDepthStencilState = &depth;
        pipelineCreate.pColorBlendState = &blending; pipelineCreate.pDynamicState = &dynamic; pipelineCreate.layout = pipelineLayout; pipelineCreate.renderPass = pass;
        result = d.fn.CreateGraphicsPipelines(d.handle, VK_NULL_HANDLE, 1, &pipelineCreate, nullptr, &pipeline);
        d.fn.DestroyShaderModule(d.handle, vertex, nullptr); d.fn.DestroyShaderModule(d.handle, fragment, nullptr);
        if (result != VK_SUCCESS) return result;
        frames.resize(s.images.size());
        for (size_t i = 0; i < frames.size(); ++i) {
            auto& f = frames[i];
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image = s.images[i]; view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = s.format; view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; view.subresourceRange.levelCount = view.subresourceRange.layerCount = 1;
            if ((result = d.fn.CreateImageView(d.handle, &view, nullptr, &f.targetView)) != VK_SUCCESS) return result;
            VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fb.renderPass = pass; fb.attachmentCount = 1; fb.pAttachments = &f.targetView;
            fb.width = s.extent.width; fb.height = s.extent.height; fb.layers = 1;
            if ((result = d.fn.CreateFramebuffer(d.handle, &fb, nullptr, &f.framebuffer)) != VK_SUCCESS) return result;
            VkCommandPoolCreateInfo commands{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; commands.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; commands.queueFamilyIndex = family;
            if ((result = d.fn.CreateCommandPool(d.handle, &commands, nullptr, &f.pool)) != VK_SUCCESS) return result;
            VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; command.commandPool = f.pool;
            command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; command.commandBufferCount = 1;
            if ((result = d.fn.AllocateCommandBuffers(d.handle, &command, &f.command)) != VK_SUCCESS) return result;
            if (d.setLoaderData && (result = d.setLoaderData(d.handle, f.command)) != VK_SUCCESS) return result;
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            if ((result = d.fn.CreateFence(d.handle, &fence, nullptr, &f.fence)) != VK_SUCCESS) return result;
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            if ((result = d.fn.CreateSemaphore(d.handle, &semaphore, nullptr, &f.finished)) != VK_SUCCESS) return result;
            VkDescriptorSetAllocateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; set.descriptorPool = descriptors;
            set.descriptorSetCount = 1; set.pSetLayouts = &descriptorLayout;
            if ((result = d.fn.AllocateDescriptorSets(d.handle, &set, &f.descriptor)) != VK_SUCCESS) return result;
        }
        return VK_SUCCESS;
    }
    VkResult Texture(ImageFrame& f, const Frame& source) {
        if (f.texture && f.width == source.header.width && f.height == source.header.height) return VK_SUCCESS;
        DropTexture(f);
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; image.imageType = VK_IMAGE_TYPE_2D; image.format = VK_FORMAT_B8G8R8A8_UNORM;
        image.extent = {source.header.width, source.header.height, 1}; image.mipLevels = image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT; image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult result = d.fn.CreateImage(d.handle, &image, nullptr, &f.texture); if (result != VK_SUCCESS) return result;
        VkMemoryRequirements requirements{}; d.fn.GetImageMemoryRequirements(d.handle, f.texture, &requirements);
        if ((result = Allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, f.textureMemory)) != VK_SUCCESS) return result;
        if ((result = d.fn.BindImageMemory(d.handle, f.texture, f.textureMemory, 0)) != VK_SUCCESS) return result;
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image = f.texture; view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = image.format; view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; view.subresourceRange.levelCount = view.subresourceRange.layerCount = 1;
        if ((result = d.fn.CreateImageView(d.handle, &view, nullptr, &f.textureView)) != VK_SUCCESS) return result;
        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; buffer.size = source.header.pixelBytes + 256ull;
        buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if ((result = d.fn.CreateBuffer(d.handle, &buffer, nullptr, &f.upload)) != VK_SUCCESS) return result;
        d.fn.GetBufferMemoryRequirements(d.handle, f.upload, &requirements); uint32_t type = 0;
        if ((result = Allocate(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, f.uploadMemory, &type)) != VK_SUCCESS) return result;
        f.uploadFlags = d.memory.memoryTypes[type].propertyFlags;
        if ((result = d.fn.BindBufferMemory(d.handle, f.upload, f.uploadMemory, 0)) != VK_SUCCESS) return result;
        VkDescriptorImageInfo imageInfo{sampler, f.textureView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; write.dstSet = f.descriptor; write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &imageInfo;
        d.fn.UpdateDescriptorSets(d.handle, 1, &write, 0, nullptr);
        f.width = source.header.width; f.height = source.header.height; return VK_SUCCESS;
    }
    VkResult Record(uint32_t index, const Frame& source) {
        if (index >= frames.size()) return VK_ERROR_OUT_OF_DATE_KHR;
        auto& f = frames[index];
        const auto status = d.fn.GetFenceStatus(d.handle, f.fence); if (status != VK_SUCCESS) return status;
        auto result = Texture(f, source); if (result != VK_SUCCESS) return result;
        if ((result = d.fn.ResetCommandPool(d.handle, f.pool, 0)) != VK_SUCCESS) return result;
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if ((result = d.fn.BeginCommandBuffer(f.command, &begin)) != VK_SUCCESS) return result;
        const auto rect = WindowPlacement(source, s.window, s.extent.width, s.extent.height);
        const Vertex vertices[]{ {rect.x, rect.y, 0, 0, 0xffffffff}, {rect.x + rect.width, rect.y, 1, 0, 0xffffffff},
            {rect.x, rect.y + rect.height, 0, 1, 0xffffffff}, {rect.x + rect.width, rect.y + rect.height, 1, 1, 0xffffffff} };
        void* mapped = nullptr;
        if ((result = d.fn.MapMemory(d.handle, f.uploadMemory, 0, VK_WHOLE_SIZE, 0, &mapped)) != VK_SUCCESS) return result;
        memcpy(mapped, vertices, sizeof(vertices));
        const bool upload = !f.initialized || f.owner != source.header.ownerPid || f.generation != source.header.generation;
        if (upload) memcpy(static_cast<uint8_t*>(mapped) + 256, source.pixels.data(), source.header.pixelBytes);
        if (!(f.uploadFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE}; range.memory = f.uploadMemory; range.size = VK_WHOLE_SIZE;
            result = d.fn.FlushMappedMemoryRanges(d.handle, 1, &range);
        }
        d.fn.UnmapMemory(d.handle, f.uploadMemory); if (result != VK_SUCCESS) return result;
        if (upload) {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.image = f.texture;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; barrier.subresourceRange.levelCount = barrier.subresourceRange.layerCount = 1;
            barrier.oldLayout = f.initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.srcAccessMask = f.initialized ? VK_ACCESS_SHADER_READ_BIT : 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            d.fn.CmdPipelineBarrier(f.command, f.initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            VkBufferImageCopy copy{}; copy.bufferOffset = 256; copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1; copy.imageExtent = {f.width, f.height, 1};
            d.fn.CmdCopyBufferToImage(f.command, f.upload, f.texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            d.fn.CmdPipelineBarrier(f.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                0, nullptr, 0, nullptr, 1, &barrier);
        }
        VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; render.renderPass = pass;
        render.framebuffer = f.framebuffer; render.renderArea.extent = s.extent;
        d.fn.CmdBeginRenderPass(f.command, &render, VK_SUBPASS_CONTENTS_INLINE);
        d.fn.CmdBindPipeline(f.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkViewport viewport{0, 0, static_cast<float>(s.extent.width), static_cast<float>(s.extent.height), 0, 1};
        VkRect2D scissor{{0, 0}, s.extent}; d.fn.CmdSetViewport(f.command, 0, 1, &viewport); d.fn.CmdSetScissor(f.command, 0, 1, &scissor);
        d.fn.CmdBindDescriptorSets(f.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &f.descriptor, 0, nullptr);
        const float push[]{2.0f / s.extent.width, 2.0f / s.extent.height, -1, -1};
        d.fn.CmdPushConstants(f.command, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), push);
        const VkDeviceSize offset = 0; d.fn.CmdBindVertexBuffers(f.command, 0, 1, &f.upload, &offset);
        d.fn.CmdDraw(f.command, 4, 1, 0, 0); d.fn.CmdEndRenderPass(f.command);
        result = d.fn.EndCommandBuffer(f.command);
        if (result == VK_SUCCESS) { f.generation = source.header.generation; f.owner = source.header.ownerPid; f.initialized = true; }
        return result;
    }
};
std::shared_ptr<Renderer> CreateRenderer(Device& device, Swapchain& chain, uint32_t family) {
    auto renderer = std::make_shared<Renderer>(device, chain, family);
    const auto result = renderer->Init(); if (result != VK_SUCCESS) { ReportError(Vulkan, result); return {}; }
    return renderer;
}
VkResult Prepare(Device& device, Swapchain& chain, uint32_t image, uint32_t family, const Frame& source,
    VkCommandBuffer& command, VkSemaphore& finished, VkFence& fence) {
    if (!chain.renderer) chain.renderer = CreateRenderer(device, chain, family);
    if (!chain.renderer) return VK_ERROR_INITIALIZATION_FAILED;
    if (chain.renderer->family != family) return VK_ERROR_FEATURE_NOT_PRESENT;
    const auto result = chain.renderer->Record(image, source);
    if (result != VK_SUCCESS) return result;
    const auto& f = chain.renderer->frames[image]; command = f.command; finished = f.finished; fence = f.fence; return VK_SUCCESS;
}
}
