/* vkup.c - 真实应用纹理上传路径: vkCmdCopyBufferToImage -> 采样 -> 逐像素验证
 *
 * 目的：M3.1 第二段。真实应用把 PNG 解到 buffer 后用 vkCmdCopyBufferToImage 上传到
 * VK_IMAGE_TILING_OPTIMAL 纹理，再采样。这里用 4x4 已知图案走完整路径：
 *   buffer(64 字节, 4x4 RGBA8) --vkCmdCopyBufferToImage--> image(OPTIMAL) --采样--> RT(16x16 LINEAR)
 * 判据：RT 四象限各 64 像素，颜色 W/R/G/B。
 *
 *   gcc -O1 -o vk_up vkup.c -I/home/umeko/tu5xx -I. -ldl
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdbool.h>

#include <vulkan/vulkan.h>
#include "spirv_exp.h"

typedef PFN_vkVoidFunction (VKAPI_CALL *PFN_vkGetInstanceProcAddr_fn)(VkInstance, const char*);

#define CHK(r, what) do { \
    if ((r) != VK_SUCCESS) { \
        printf("  FAIL: %s => 0x%x\n", what, (unsigned)(r)); \
        return 3; \
    } \
} while (0)

#define GIPA(name) PFN_##name name = (PFN_##name)vkGetInstanceProcAddr(inst, #name); \
    if (!name) { printf("  no proc: %s\n", #name); return 2; }

static PFN_vkCreateImage gCreateImage;
static PFN_vkGetImageMemoryRequirements gGetImageMemoryRequirements;
static PFN_vkAllocateMemory gAllocateMemory;
static PFN_vkBindImageMemory gBindImageMemory;
static PFN_vkCreateImageView gCreateImageView;
static PFN_vkGetImageSubresourceLayout gGetImageSubresourceLayout;

static uint32_t find_mem_type(uint32_t bits, VkMemoryPropertyFlags props, VkPhysicalDeviceMemoryProperties *mp)
{
    for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp->memoryTypes[i].propertyFlags & props) == props) return i;
    return 0xffffffffu;
}

static VkImage new_img(VkDevice dev, VkPhysicalDeviceMemoryProperties *mp, VkDeviceMemory *mem,
                       VkImageView *view, uint32_t w, uint32_t h, VkImageTiling tiling,
                       VkImageUsageFlags usage, VkSubresourceLayout *sl)
{
    VkImage img;
    VkImageCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = { w, h, 1 }, .mipLevels = 1,
        .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = tiling, .usage = usage,
        .initialLayout = VK_IMAGE_LAYOUT_GENERAL, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    if (gCreateImage(dev, &ci, NULL, &img) != VK_SUCCESS) { printf("  FAIL: vkCreateImage\n"); exit(3); }
    VkMemoryRequirements mr;
    gGetImageMemoryRequirements(dev, img, &mr);
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size, .memoryTypeIndex = find_mem_type(mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mp) };
    if (gAllocateMemory(dev, &mai, NULL, mem) != VK_SUCCESS) { printf("  FAIL: vkAllocateMemory\n"); exit(3); }
    gBindImageMemory(dev, img, *mem, 0);
    VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    if (gCreateImageView(dev, &vci, NULL, view) != VK_SUCCESS) { printf("  FAIL: vkCreateImageView\n"); exit(3); }
    if (sl) {
        VkImageSubresource isr = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
        gGetImageSubresourceLayout(dev, img, &isr, sl);
    }
    printf("  img %ux%u %s mem=%u%s\n", w, h,
           tiling == VK_IMAGE_TILING_OPTIMAL ? "OPTIMAL" : "LINEAR ", (unsigned)mr.size,
           sl ? "" : "");
    return img;
}

int main(void)
{
    void *h = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen FAIL: %s\n", dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr_fn vkGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr_fn)dlsym(h, "vkGetInstanceProcAddr");

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "vkup", .apiVersion = VK_MAKE_API_VERSION(0, 1, 1, 0) };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst;
    CHK(((PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance"))(&ici, NULL, &inst), "vkCreateInstance");

    GIPA(vkEnumeratePhysicalDevices); GIPA(vkGetPhysicalDeviceMemoryProperties);
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, NULL);
    VkPhysicalDevice pd;
    vkEnumeratePhysicalDevices(inst, &n, &pd);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qci };
    VkDevice dev;
    GIPA(vkCreateDevice);
    CHK(vkCreateDevice(pd, &dci, NULL, &dev), "vkCreateDevice");
    GIPA(vkGetDeviceQueue); GIPA(vkCreateImage); GIPA(vkBindImageMemory); GIPA(vkCreateImageView);
    GIPA(vkGetImageSubresourceLayout); GIPA(vkGetImageMemoryRequirements);
    GIPA(vkAllocateMemory); GIPA(vkMapMemory); GIPA(vkUnmapMemory);
    GIPA(vkCreateSampler); GIPA(vkCreateDescriptorSetLayout); GIPA(vkCreateDescriptorPool);
    GIPA(vkAllocateDescriptorSets); GIPA(vkUpdateDescriptorSets);
    GIPA(vkCreateRenderPass); GIPA(vkCreateFramebuffer);
    GIPA(vkCreateShaderModule); GIPA(vkCreatePipelineLayout); GIPA(vkCreateGraphicsPipelines);
    GIPA(vkCreateBuffer); GIPA(vkBindBufferMemory); GIPA(vkGetBufferMemoryRequirements);
    GIPA(vkCreateCommandPool); GIPA(vkAllocateCommandBuffers);
    GIPA(vkBeginCommandBuffer); GIPA(vkEndCommandBuffer);
    GIPA(vkCmdCopyBufferToImage); GIPA(vkCmdPipelineBarrier);
    GIPA(vkCmdBeginRenderPass); GIPA(vkCmdBindPipeline); GIPA(vkCmdBindDescriptorSets);
    GIPA(vkCmdBindVertexBuffers); GIPA(vkCmdDraw); GIPA(vkCmdEndRenderPass);
    GIPA(vkQueueSubmit); GIPA(vkQueueWaitIdle);

    gCreateImage = vkCreateImage; gGetImageMemoryRequirements = vkGetImageMemoryRequirements;
    gAllocateMemory = vkAllocateMemory; gBindImageMemory = vkBindImageMemory;
    gCreateImageView = vkCreateImageView; gGetImageSubresourceLayout = vkGetImageSubresourceLayout;

    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    VkDeviceMemory texmem, rtmem, bufmem;
    VkImageView texview, rtview;
    VkImage tex = new_img(dev, &mp, &texmem, &texview, 4, 4, VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, NULL);
    VkImage rt = new_img(dev, &mp, &rtmem, &rtview, 16, 16, VK_IMAGE_TILING_LINEAR,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, NULL);

    /* 4x4 图案：左上白 右上红 左下绿 右下蓝（每格 2x2 texel） */
    static const uint8_t pat[4][4][4] = {
        { {255,255,255,255}, {255,255,255,255}, {255,0,0,255},   {255,0,0,255} },
        { {255,255,255,255}, {255,255,255,255}, {255,0,0,255},   {255,0,0,255} },
        { {0,255,0,255},     {0,255,0,255},     {0,0,255,255},   {0,0,255,255} },
        { {0,255,0,255},     {0,255,0,255},     {0,0,255,255},   {0,0,255,255} },
    };

    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 64,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VkBuffer buf;
    CHK(vkCreateBuffer(dev, &bci, NULL, &buf), "vkCreateBuffer");
    VkMemoryRequirements bmr;
    vkGetBufferMemoryRequirements(dev, buf, &bmr);
    VkMemoryAllocateInfo bmai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = bmr.size,
        .memoryTypeIndex = find_mem_type(bmr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &mp) };
    CHK(vkAllocateMemory(dev, &bmai, NULL, &bufmem), "vkAllocateMemory(buf)");
    vkBindBufferMemory(dev, buf, bufmem, 0);
    void *m;
    CHK(vkMapMemory(dev, bufmem, 0, 64, 0, &m), "vkMapMemory(buf)");
    memcpy(m, pat, 64);
    vkUnmapMemory(dev, bufmem);

    VkSamplerCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .maxLod = 0.0f };
    VkSampler sampler;
    CHK(vkCreateSampler(dev, &sci, NULL, &sampler), "vkCreateSampler");
    VkDescriptorSetLayoutBinding dslb = { .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT };
    VkDescriptorSetLayoutCreateInfo dslci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &dslb };
    VkDescriptorSetLayout dsl;
    CHK(vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl), "vkCreateDescriptorSetLayout");
    VkDescriptorPoolSize dps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    VkDescriptorPoolCreateInfo dpci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &dps };
    VkDescriptorPool dpool;
    CHK(vkCreateDescriptorPool(dev, &dpci, NULL, &dpool), "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo dsai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = dpool, .descriptorSetCount = 1, .pSetLayouts = &dsl };
    VkDescriptorSet dset;
    CHK(vkAllocateDescriptorSets(dev, &dsai, &dset), "vkAllocateDescriptorSets");
    VkDescriptorImageInfo dii = { .sampler = sampler, .imageView = texview,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
    VkWriteDescriptorSet wds = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset,
        .dstBinding = 0, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &dii };
    vkUpdateDescriptorSets(dev, 1, &wds, 0, NULL);

    VkAttachmentDescription att = { .format = VK_FORMAT_R8G8B8A8_UNORM, .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_GENERAL };
    VkAttachmentReference aref = { 0, VK_IMAGE_LAYOUT_GENERAL };
    VkSubpassDescription subd = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &aref };
    VkRenderPassCreateInfo rpci = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1,
        .pAttachments = &att, .subpassCount = 1, .pSubpasses = &subd };
    VkRenderPass rp;
    CHK(vkCreateRenderPass(dev, &rpci, NULL, &rp), "vkCreateRenderPass");
    VkFramebufferCreateInfo fbci = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = rp,
        .attachmentCount = 1, .pAttachments = &rtview, .width = 16, .height = 16, .layers = 1 };
    VkFramebuffer fb;
    CHK(vkCreateFramebuffer(dev, &fbci, NULL, &fb), "vkCreateFramebuffer");

    VkShaderModuleCreateInfo smci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkShaderModule vs_mod, fs_mod;
    smci.codeSize = sizeof vs_spv; smci.pCode = vs_spv;
    CHK(vkCreateShaderModule(dev, &smci, NULL, &vs_mod), "vkCreateShaderModule(VS)");
    smci.codeSize = sizeof fs_spv_set0; smci.pCode = fs_spv_set0;
    CHK(vkCreateShaderModule(dev, &smci, NULL, &fs_mod), "vkCreateShaderModule(FS)");

    VkVertexInputBindingDescription vib = { .binding = 0, .stride = 6 * sizeof(float),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription via[2] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0 },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 3 * sizeof(float) } };
    VkPipelineVertexInputStateCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vib,
        .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = via };
    VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState cbatt = { .blendEnable = VK_FALSE, .colorWriteMask = 0xf };
    VkPipelineColorBlendStateCreateInfo cbs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &cbatt };
    VkPipelineLayoutCreateInfo plci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &dsl };
    VkPipelineLayout playout;
    CHK(vkCreatePipelineLayout(dev, &plci, NULL, &playout), "vkCreatePipelineLayout");
    VkViewport vp = { 0, 0, 16.0f, 16.0f, 0.0f, 1.0f };
    VkRect2D sc = { { 0, 0 }, { 16, 16 } };
    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .pName = "main", .module = vs_mod },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .pName = "main", .module = fs_mod } };
    VkPipelineViewportStateCreateInfo vps = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc };
    VkGraphicsPipelineCreateInfo gpci = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vi, .pInputAssemblyState = &ia,
        .pViewportState = &vps, .pRasterizationState = &rs, .pMultisampleState = &ms,
        .pColorBlendState = &cbs, .layout = playout, .renderPass = rp, .subpass = 0 };
    VkPipeline pipe;
    CHK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe), "vkCreateGraphicsPipelines");

    static const float verts[6][6] = {
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f }, {  1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f },
        { -1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 0.0f }, {  1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f },
        {  1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 0.0f }, { -1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 0.0f } };
    VkBufferCreateInfo vbci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof verts,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VkBuffer vbo;
    CHK(vkCreateBuffer(dev, &vbci, NULL, &vbo), "vkCreateBuffer(vbo)");
    VkMemoryRequirements vmr;
    vkGetBufferMemoryRequirements(dev, vbo, &vmr);
    VkMemoryAllocateInfo vmai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = vmr.size,
        .memoryTypeIndex = find_mem_type(vmr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &mp) };
    VkDeviceMemory vbomem;
    CHK(vkAllocateMemory(dev, &vmai, NULL, &vbomem), "vkAllocateMemory(vbo)");
    vkBindBufferMemory(dev, vbo, vbomem, 0);
    CHK(vkMapMemory(dev, vbomem, 0, sizeof verts, 0, &m), "vkMapMemory(vbo)");
    memcpy(m, verts, sizeof verts);
    vkUnmapMemory(dev, vbomem);

    VkCommandPoolCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0 };
    VkCommandPool cpool;
    CHK(vkCreateCommandPool(dev, &cpci, NULL, &cpool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cpool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cb;
    CHK(vkAllocateCommandBuffers(dev, &cbai, &cb), "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHK(vkBeginCommandBuffer(cb, &bi), "vkBeginCommandBuffer");

    VkBufferImageCopy bic = { .bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = { 0, 0, 0 }, .imageExtent = { 4, 4, 1 } };
    vkCmdCopyBufferToImage(cb, buf, tex, VK_IMAGE_LAYOUT_GENERAL, 1, &bic);

    VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 1, &mb, 0, NULL, 0, NULL);

    VkClearValue clear = { .color = { { 9, 9, 9, 255 } } };
    VkRenderPassBeginInfo rpbi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = rp,
        .framebuffer = fb, .renderArea = { { 0, 0 }, { 16, 16 } }, .clearValueCount = 1, .pClearValues = &clear };
    vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, playout, 0, 1, &dset, 0, NULL);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &vbo, &off);
    vkCmdDraw(cb, 6, 1, 0, 0);
    vkCmdEndRenderPass(cb);
    CHK(vkEndCommandBuffer(cb), "vkEndCommandBuffer");
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
    CHK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit");
    CHK(vkQueueWaitIdle(queue), "vkQueueWaitIdle");

    /* 读回 16x16 并统计 */
    VkImageSubresource isr = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout sl;
    vkGetImageSubresourceLayout(dev, rt, &isr, &sl);
    CHK(vkMapMemory(dev, rtmem, 0, VK_WHOLE_SIZE, 0, &m), "vkMapMemory(rt)");
    unsigned cnt[4] = { 0, 0, 0, 0 };
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            uint8_t *px = (uint8_t *)m + sl.offset + y * sl.rowPitch + x * 4;
            int v = (px[0] > 128 ? 1 : 0) | (px[1] > 128 ? 2 : 0) | (px[2] > 128 ? 4 : 0);
            cnt[v]++;
        }
    printf("  分布: W=%u R=%u G=%u B=%u 其它=%u (总 %u)\n",
           cnt[7], cnt[1], cnt[2], cnt[4], 256 - cnt[7] - cnt[1] - cnt[2] - cnt[4], 256);
    printf("  quad0 ( 3, 3)=RGBA(%u,%u,%u,%u) 期望 W\n",
           ((uint8_t*)m)[sl.offset + 3*sl.rowPitch + 3*4 + 0], ((uint8_t*)m)[sl.offset + 3*sl.rowPitch + 3*4 + 1],
           ((uint8_t*)m)[sl.offset + 3*sl.rowPitch + 3*4 + 2], ((uint8_t*)m)[sl.offset + 3*sl.rowPitch + 3*4 + 3]);
    printf("  quad1 (12, 3)=RGBA(%u,%u,%u,%u) 期望 R\n",
           ((uint8_t*)m)[sl.offset + 3*sl.rowPitch + 12*4 + 0], ((uint8_t*)m)[sl.offset + 3*sl.rowPitch + 12*4 + 1],
           ((uint8_t*)m)[sl.offset + 3*sl.rowPitch + 12*4 + 2], ((uint8_t*)m)[sl.offset + 3*sl.rowPitch + 12*4 + 3]);
    printf("  quad2 ( 3,12)=RGBA(%u,%u,%u,%u) 期望 G\n",
           ((uint8_t*)m)[sl.offset + 12*sl.rowPitch + 3*4 + 0], ((uint8_t*)m)[sl.offset + 12*sl.rowPitch + 3*4 + 1],
           ((uint8_t*)m)[sl.offset + 12*sl.rowPitch + 3*4 + 2], ((uint8_t*)m)[sl.offset + 12*sl.rowPitch + 3*4 + 3]);
    printf("  quad3 (12,12)=RGBA(%u,%u,%u,%u) 期望 B\n",
           ((uint8_t*)m)[sl.offset + 12*sl.rowPitch + 12*4 + 0], ((uint8_t*)m)[sl.offset + 12*sl.rowPitch + 12*4 + 1],
           ((uint8_t*)m)[sl.offset + 12*sl.rowPitch + 12*4 + 2], ((uint8_t*)m)[sl.offset + 12*sl.rowPitch + 12*4 + 3]);
    vkUnmapMemory(dev, rtmem);
    printf("  DONE\n");
    return 0;
}
