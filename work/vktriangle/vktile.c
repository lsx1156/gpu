/* vktile.c - M3.1 探针: tiled(TILE5_3) 渲染目标 + tiled 采样 与 linear 路径逐像素一致
 *
 * 流程（三次提交，每次 vkQueueWaitIdle 全同步，避开缺失的 barrier）：
 *   s1: 用 2x2 LINEAR 纹理(白/红/绿/蓝) NEAREST 采样 → 渲染进 128x128 TILING_OPTIMAL 图像 T
 *   s2: 把 T 当 tiled 纹理 NEAREST 采样 → 渲染进 256x256 LINEAR 图像 A（tiled 往返）
 *   s3: 把 2x2 LINEAR 纹理直接采样 → 渲染进 256x256 LINEAR 图像 B（纯 linear 参考）
 * 判据：
 *   - A/B 逐像素完全一致（不一致计数 = 0，最大差 = 0）⇒ tiled 写 + tiled 读 的像素排布与
 *     linear 一致，即 tile swizzle 在两侧对称、pitch/尺寸字段正确；
 *   - A 的四个象限各 16384 像素、颜色分别为 白/红/绿/蓝。
 * 反证手段：把 MRT 或纹理描述的 TILE_MODE 置回 LINEAR（只改一侧），A/B 会出现大量差异。
 *
 *   gcc -O1 -o vk_tile vktile.c -I/home/umeko/tu5xx -I. -ldl
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

static uint32_t find_mem_type(uint32_t type_bits, VkMemoryPropertyFlags props,
                              VkPhysicalDeviceMemoryProperties *mp)
{
    for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
        if ((type_bits & (1u << i)) &&
            (mp->memoryTypes[i].propertyFlags & props) == props)
            return i;
    return 0xffffffffu;
}

static const uint8_t tex_px[4][3] = {
    { 255, 255, 255 }, { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 } };
static const char *col_name(uint8_t r, uint8_t g, uint8_t b)
{
    if (r == 255 && g == 255 && b == 255) return "W";
    if (r == 255 && g == 0 && b == 0) return "R";
    if (r == 0 && g == 255 && b == 0) return "G";
    if (r == 0 && g == 0 && b == 255) return "B";
    static char b1[32];
    snprintf(b1, sizeof b1, "(%u,%u,%u)", r, g, b);
    return b1;
}

int main(void)
{
    void *h = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen FAIL: %s\n", dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr_fn vkGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr_fn)dlsym(h, "vkGetInstanceProcAddr");

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                              .pApplicationName = "vktile",
                              .apiVersion = VK_MAKE_API_VERSION(0, 1, 1, 0) };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                 .pApplicationInfo = &app };
    VkInstance inst;
    CHK(((PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance"))
            (&ici, NULL, &inst), "vkCreateInstance");

    GIPA(vkEnumeratePhysicalDevices);
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, NULL);
    if (n == 0) { printf("no device\n"); return 3; }
    VkPhysicalDevice pd;
    vkEnumeratePhysicalDevices(inst, &n, &pd);
    GIPA(vkGetPhysicalDeviceMemoryProperties);
    VkPhysicalDeviceMemoryProperties memprops;
    vkGetPhysicalDeviceMemoryProperties(pd, &memprops);

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci };
    VkDevice dev;
    GIPA(vkCreateDevice);
    CHK(vkCreateDevice(pd, &dci, NULL, &dev), "vkCreateDevice");

    GIPA(vkGetDeviceQueue);
    GIPA(vkCreateImage); GIPA(vkBindImageMemory); GIPA(vkCreateImageView);
    GIPA(vkGetImageSubresourceLayout); GIPA(vkGetImageMemoryRequirements);
    GIPA(vkAllocateMemory); GIPA(vkMapMemory); GIPA(vkUnmapMemory);
    GIPA(vkCreateSampler);
    GIPA(vkCreateDescriptorSetLayout); GIPA(vkCreateDescriptorPool);
    GIPA(vkAllocateDescriptorSets); GIPA(vkUpdateDescriptorSets);
    GIPA(vkCreateRenderPass); GIPA(vkCreateFramebuffer);
    GIPA(vkCreateShaderModule); GIPA(vkCreatePipelineLayout);
    GIPA(vkCreateGraphicsPipelines);
    GIPA(vkCreateBuffer); GIPA(vkBindBufferMemory); GIPA(vkGetBufferMemoryRequirements);
    GIPA(vkCreateCommandPool); GIPA(vkAllocateCommandBuffers);
    GIPA(vkBeginCommandBuffer); GIPA(vkEndCommandBuffer);
    GIPA(vkCmdBeginRenderPass); GIPA(vkCmdBindPipeline);
    GIPA(vkCmdBindDescriptorSets);
    GIPA(vkCmdBindVertexBuffers); GIPA(vkCmdDraw); GIPA(vkCmdEndRenderPass);
    GIPA(vkQueueSubmit); GIPA(vkQueueWaitIdle);
    GIPA(vkCreateFence); GIPA(vkWaitForFences);

    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    /* ---- 通用图像创建 ---- */
    VkSubresourceLayout srl_lin[2];
    VkImage img[3];        /* 0 = tiled 128, 1 = linear A 256, 2 = linear B 256 */
    VkDeviceMemory img_mem[3];
    VkImageView img_view[3];
    VkImageSubresource isr = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    for (unsigned i = 0; i < 3; i++) {
        const bool tiled = (i == 0);
        const uint32_t sz = tiled ? 128 : 256;
        VkImageCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = { sz, sz, 1 }, .mipLevels = 1, .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = tiled ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_LINEAR,
            .usage = tiled ? (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
                           : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
        CHK(vkCreateImage(dev, &ci, NULL, &img[i]), "vkCreateImage");
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(dev, img[i], &mr);
        VkMemoryAllocateInfo mai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
            .memoryTypeIndex = find_mem_type(mr.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &memprops) };
        CHK(vkAllocateMemory(dev, &mai, NULL, &img_mem[i]), "vkAllocateMemory");
        CHK(vkBindImageMemory(dev, img[i], img_mem[i], 0), "vkBindImageMemory");
        VkImageViewCreateInfo vci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = img[i], .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        CHK(vkCreateImageView(dev, &vci, NULL, &img_view[i]), "vkCreateImageView");
        VkSubresourceLayout sl;
        vkGetImageSubresourceLayout(dev, img[i], &isr, &sl);
        printf("  img[%u] %s %ux%u mem=%u offset=%u pitch=%u size=%u\n", i,
               tiled ? "TILED " : "LINEAR", sz, sz, (unsigned)mr.size,
               (unsigned)sl.offset, (unsigned)sl.rowPitch, (unsigned)sl.size);
        if (!tiled) srl_lin[i - 1] = sl;
    }

    /* ---- 2x2 纯色 LINEAR 纹理 ---- */
    VkImage tex; VkDeviceMemory tex_mem; VkImageView tex_view;
    {
        VkImageCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = { 2, 2, 1 }, .mipLevels = 1, .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_LINEAR,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
        CHK(vkCreateImage(dev, &ci, NULL, &tex), "vkCreateImage(tex)");
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(dev, tex, &mr);
        VkMemoryAllocateInfo mai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
            .memoryTypeIndex = find_mem_type(mr.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &memprops) };
        CHK(vkAllocateMemory(dev, &mai, NULL, &tex_mem), "vkAllocateMemory(tex)");
        CHK(vkBindImageMemory(dev, tex, tex_mem, 0), "vkBindImageMemory(tex)");
        VkSubresourceLayout sl;
        vkGetImageSubresourceLayout(dev, tex, &isr, &sl);
        void *m;
        CHK(vkMapMemory(dev, tex_mem, 0, VK_WHOLE_SIZE, 0, &m), "vkMapMemory(tex)");
        for (unsigned y = 0; y < 2; y++) {
            uint8_t *row = (uint8_t *)m + sl.offset + y * sl.rowPitch;
            for (unsigned x = 0; x < 2; x++) {
                unsigned k = y * 2 + x;
                row[x * 4 + 0] = tex_px[k][0];
                row[x * 4 + 1] = tex_px[k][1];
                row[x * 4 + 2] = tex_px[k][2];
                row[x * 4 + 3] = 255;
            }
        }
        vkUnmapMemory(dev, tex_mem);
        VkImageViewCreateInfo vci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = tex, .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        CHK(vkCreateImageView(dev, &vci, NULL, &tex_view), "vkCreateImageView(tex)");
    }

    VkSamplerCreateInfo sci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .minLod = 0.0f, .maxLod = 0.0f };
    VkSampler sampler;
    CHK(vkCreateSampler(dev, &sci, NULL, &sampler), "vkCreateSampler");

    /* ---- 三个 descriptor set：s1 采样 2x2；s2 采样 tiled T；s3 采样 2x2 ---- */
    VkDescriptorSetLayoutBinding dslb = {
        .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT };
    VkDescriptorSetLayoutCreateInfo dslci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &dslb };
    VkDescriptorSetLayout dsl;
    CHK(vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl), "vkCreateDescriptorSetLayout");
    VkDescriptorPoolSize dps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 };
    VkDescriptorPoolCreateInfo dpci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 3, .poolSizeCount = 1, .pPoolSizes = &dps };
    VkDescriptorPool dpool;
    CHK(vkCreateDescriptorPool(dev, &dpci, NULL, &dpool), "vkCreateDescriptorPool");
    VkDescriptorSet dset[3];
    VkDescriptorSetLayout dsl3[3] = { dsl, dsl, dsl };
    VkDescriptorSetAllocateInfo dsai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = dpool, .descriptorSetCount = 3, .pSetLayouts = dsl3 };
    CHK(vkAllocateDescriptorSets(dev, &dsai, dset), "vkAllocateDescriptorSets");
    for (unsigned k = 0; k < 3; k++) {
        VkDescriptorImageInfo dii = {
            .sampler = sampler,
            .imageView = (k == 1) ? img_view[0] : tex_view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
        VkWriteDescriptorSet wds = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = dset[k], .dstBinding = 0, .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &dii };
        vkUpdateDescriptorSets(dev, 1, &wds, 0, NULL);
    }

    VkAttachmentDescription att = {
        .format = VK_FORMAT_R8G8B8A8_UNORM, .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_GENERAL };
    VkAttachmentReference aref = { 0, VK_IMAGE_LAYOUT_GENERAL };
    VkSubpassDescription subd = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &aref };
    VkRenderPassCreateInfo rpci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &att,
        .subpassCount = 1, .pSubpasses = &subd };
    VkRenderPass rp;
    CHK(vkCreateRenderPass(dev, &rpci, NULL, &rp), "vkCreateRenderPass");
    VkFramebuffer fb[3];
    for (unsigned i = 0; i < 3; i++) {
        const uint32_t sz = (i == 0) ? 128 : 256;
        VkFramebufferCreateInfo fbci = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = rp, .attachmentCount = 1, .pAttachments = &img_view[i],
            .width = sz, .height = sz, .layers = 1 };
        CHK(vkCreateFramebuffer(dev, &fbci, NULL, &fb[i]), "vkCreateFramebuffer");
    }

    VkShaderModuleCreateInfo smci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkShaderModule vs_mod, fs_mod;
    smci.codeSize = sizeof vs_spv; smci.pCode = vs_spv;
    CHK(vkCreateShaderModule(dev, &smci, NULL, &vs_mod), "vkCreateShaderModule(VS)");
    smci.codeSize = sizeof fs_spv_set0; smci.pCode = fs_spv_set0;
    CHK(vkCreateShaderModule(dev, &smci, NULL, &fs_mod), "vkCreateShaderModule(FS)");

    VkVertexInputBindingDescription vib = {
        .binding = 0, .stride = 6 * sizeof(float),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription via[2] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0 },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
          .offset = 3 * sizeof(float) } };
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vib,
        .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = via };
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState cbatt = { .blendEnable = VK_FALSE, .colorWriteMask = 0xf };
    VkPipelineColorBlendStateCreateInfo cbs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &cbatt };
    VkPipelineLayoutCreateInfo plci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &dsl };
    VkPipelineLayout playout;
    CHK(vkCreatePipelineLayout(dev, &plci, NULL, &playout), "vkCreatePipelineLayout");

    VkPipeline pipe[3];
    for (unsigned i = 0; i < 3; i++) {
        const uint32_t sz = (i == 0) ? 128 : 256;
        VkViewport vp = { 0, 0, (float)sz, (float)sz, 0.0f, 1.0f };
        VkRect2D sc = { { 0, 0 }, { sz, sz } };
        VkPipelineShaderStageCreateInfo stages[2] = {
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_VERTEX_BIT, .pName = "main", .module = vs_mod },
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .pName = "main", .module = fs_mod } };
        VkPipelineViewportStateCreateInfo vps = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc };
        VkGraphicsPipelineCreateInfo gpci = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = 2, .pStages = stages,
            .pVertexInputState = &vi, .pInputAssemblyState = &ia,
            .pViewportState = &vps, .pRasterizationState = &rs,
            .pMultisampleState = &ms, .pColorBlendState = &cbs,
            .layout = playout, .renderPass = rp, .subpass = 0 };
        CHK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe[i]),
            "vkCreateGraphicsPipelines");
    }

    /* 顶点：整屏（±1.0），uv 0..1 */
    static const float verts[6][6] = {
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        {  1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f },
        { -1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 0.0f },
        {  1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f },
        {  1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 0.0f },
        { -1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 0.0f } };
    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                               .size = sizeof verts,
                               .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                               .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VkBuffer vbo;
    CHK(vkCreateBuffer(dev, &bci, NULL, &vbo), "vkCreateBuffer(VBO)");
    VkMemoryRequirements bmr;
    vkGetBufferMemoryRequirements(dev, vbo, &bmr);
    VkMemoryAllocateInfo bmai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = bmr.size,
        .memoryTypeIndex = find_mem_type(bmr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &memprops) };
    VkDeviceMemory vbo_mem;
    CHK(vkAllocateMemory(dev, &bmai, NULL, &vbo_mem), "vkAllocateMemory(VBO)");
    CHK(vkBindBufferMemory(dev, vbo, vbo_mem, 0), "vkBindBufferMemory");
    void *mp;
    CHK(vkMapMemory(dev, vbo_mem, 0, sizeof verts, 0, &mp), "vkMapMemory(VBO)");
    memcpy(mp, verts, sizeof verts);
    vkUnmapMemory(dev, vbo_mem);

    VkCommandPoolCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                     .queueFamilyIndex = 0 };
    VkCommandPool cpool;
    CHK(vkCreateCommandPool(dev, &cpci, NULL, &cpool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cpool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1 };
    VkCommandBuffer cb;
    CHK(vkAllocateCommandBuffers(dev, &cbai, &cb), "vkAllocateCommandBuffers");
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    CHK(vkCreateFence(dev, &fci, NULL, &fence), "vkCreateFence");

    /* 三次提交：s1 → T(tiled), s2 → A(linear, 采 T), s3 → B(linear, 采 2x2) */
    for (unsigned s = 0; s < 3; s++) {
        VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        CHK(vkBeginCommandBuffer(cb, &bi), "vkBeginCommandBuffer");
        VkClearValue clear = { .color = { { 3, 5, 7, 255 } } };
        VkRenderPassBeginInfo rpbi = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = rp, .framebuffer = fb[s],
            .renderArea = { { 0, 0 }, { (s == 0) ? 128u : 256u, (s == 0) ? 128u : 256u } },
            .clearValueCount = 1, .pClearValues = &clear };
        vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, playout,
                                0, 1, &dset[s], 0, NULL);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe[s]);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &vbo, &off);
        vkCmdDraw(cb, 6, 1, 0, 0);
        vkCmdEndRenderPass(cb);
        CHK(vkEndCommandBuffer(cb), "vkEndCommandBuffer");
        VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                            .commandBufferCount = 1, .pCommandBuffers = &cb };
        VkResult r = vkQueueSubmit(queue, 1, &si, fence);
        if (r != VK_SUCCESS) { printf("  FAIL: vkQueueSubmit 0x%x\n", (unsigned)r); return 3; }
        r = vkWaitForFences(dev, 1, &fence, VK_TRUE, 10000000000ull);
        if (r != VK_SUCCESS) { printf("  FAIL: waitForFences 0x%x\n", (unsigned)r); return 3; }
    }

    /* ---- 读回 A / B 并逐像素比较 ---- */
    static uint8_t bufA[256 * 256 * 4], bufB[256 * 256 * 4];
    for (unsigned k = 0; k < 2; k++) {
        void *m;
        CHK(vkMapMemory(dev, img_mem[k + 1], 0, VK_WHOLE_SIZE, 0, &m), "vkMapMemory(read)");
        for (unsigned y = 0; y < 256; y++)
            memcpy((k ? bufB : bufA) + y * 1024,
                   (uint8_t *)m + srl_lin[k].offset + y * srl_lin[k].rowPitch, 1024);
        vkUnmapMemory(dev, img_mem[k + 1]);
    }
    unsigned diff = 0, maxd = 0;
    long sum = 0;
    unsigned chdiff[4] = { 0, 0, 0, 0 };
    unsigned x0 = 999, x1 = 0, y0 = 999, y1 = 0;
    for (unsigned i = 0; i < 256 * 256 * 4; i++) {
        int d = (int)bufA[i] - (int)bufB[i];
        if (d < 0) d = -d;
        if (d) {
            diff++; sum += d; chdiff[i & 3]++;
            if ((unsigned)d > maxd) maxd = (unsigned)d;
            unsigned p = i / 4, x = p & 255, y = p >> 8;
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    }
    printf("  A(tiled 往返) vs B(linear 直采): 差异字节=%u 最大差=%u 平均差(仅差异)=%.2f\n",
           diff, maxd, diff ? (double)sum / diff : 0.0);
    printf("    分通道 R=%u G=%u B=%u A=%u  bbox x[%u..%u] y[%u..%u]\n",
           chdiff[0], chdiff[1], chdiff[2], chdiff[3], x0, x1, y0, y1);

    /* A 的四象限颜色与像素数 */
    for (unsigned q = 0; q < 4; q++) {
        int cx = (q & 1) ? 192 : 64, cy = (q & 2) ? 192 : 64;
        uint8_t *px = bufA + cy * 1024 + cx * 4;
        printf("  A quad%u (%3u,%3u) = (%3u,%3u,%3u)  期望 %s\n", q, cx, cy,
               px[0], px[1], px[2], col_name(tex_px[(q & 2 ? 2 : 0) + (q & 1)][0],
               tex_px[(q & 2 ? 2 : 0) + (q & 1)][1], tex_px[(q & 2 ? 2 : 0) + (q & 1)][2]));
    }
    unsigned hist_n = 0;
    struct { uint8_t r, g, b; unsigned cnt; } hist[16];
    memset(hist, 0, sizeof hist);
    for (unsigned i = 0; i < 256 * 256; i++) {
        uint8_t *px = bufA + i * 4;
        unsigned k;
        for (k = 0; k < hist_n; k++)
            if (hist[k].r == px[0] && hist[k].g == px[1] && hist[k].b == px[2]) break;
        if (k == hist_n && hist_n < 16) {
            hist[k].r = px[0]; hist[k].g = px[1]; hist[k].b = px[2]; hist_n++;
        }
        if (k < 16) hist[k].cnt++;
    }
    printf("  A 不同颜色数=%u\n", hist_n);
    for (unsigned k = 0; k < hist_n; k++)
        printf("    RGB(%3u,%3u,%3u) x %u\n", hist[k].r, hist[k].g, hist[k].b, hist[k].cnt);
    printf("  DONE\n");
    return 0;
}
