/* vktilemap.c - M3.1 第二段取证: 实测 TILE5_3 的像素→内存偏移映射（推导 swizzle）
 *
 * 做法：把 128x128 LINEAR 纹理填成「坐标指纹」r=x, g=y, b=x^y, a=255，
 * 用 NEAREST 采样渲染进 128x128 TILING_OPTIMAL 图像（RB 按 TILE5_3 写内存），
 * 再把 tiled 图像的内存整块 dump 到 /tmp/tilemap.bin。
 * 主机侧按指纹还原每个内存字属于哪个像素，即可反推 tile 尺寸与块内 swizzle。
 *
 *   gcc -O1 -o vk_tilemap vktilemap.c -I/home/umeko/tu5xx -I. -ldl
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

static PFN_vkCreateImage gCreateImage;
static PFN_vkGetImageMemoryRequirements gGetImageMemoryRequirements;
static PFN_vkAllocateMemory gAllocateMemory;
static PFN_vkBindImageMemory gBindImageMemory;
static PFN_vkCreateImageView gCreateImageView;
static PFN_vkGetImageSubresourceLayout gGetImageSubresourceLayout;

static VkImage make_img(VkDevice dev, VkPhysicalDeviceMemoryProperties *mp,
                        VkDeviceMemory *mem, VkImageView *view,
                        uint32_t w, uint32_t h, VkImageTiling tiling, VkImageUsageFlags usage,
                        VkSubresourceLayout *out_sl)
{
    VkImage img;
    VkImageCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { w, h, 1 }, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = tiling, .usage = usage,
        .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    if (gCreateImage(dev, &ci, NULL, &img) != VK_SUCCESS) { printf("  FAIL: vkCreateImage\n"); exit(3); }
    VkMemoryRequirements mr;
    gGetImageMemoryRequirements(dev, img, &mr);
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
        .memoryTypeIndex = find_mem_type(mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mp) };
    if (gAllocateMemory(dev, &mai, NULL, mem) != VK_SUCCESS) { printf("  FAIL: vkAllocateMemory\n"); exit(3); }
    gBindImageMemory(dev, img, *mem, 0);
    VkImageViewCreateInfo vci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    if (gCreateImageView(dev, &vci, NULL, view) != VK_SUCCESS) { printf("  FAIL: vkCreateImageView\n"); exit(3); }
    if (out_sl) {
        VkImageSubresource isr = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
        gGetImageSubresourceLayout(dev, img, &isr, out_sl);
    }
    printf("  img %ux%u %s mem=%u\n", w, h, tiling == VK_IMAGE_TILING_OPTIMAL ? "TILED " : "LINEAR",
           (unsigned)mr.size);
    return img;
}

static uint32_t W = 512, H = 512;

int main(int argc, char **argv)
{
    if (argc > 1) { W = (uint32_t)atoi(argv[1]); H = W; }
    if (argc > 2) H = (uint32_t)atoi(argv[2]);
    void *h = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen FAIL: %s\n", dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr_fn vkGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr_fn)dlsym(h, "vkGetInstanceProcAddr");

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                              .pApplicationName = "vktilemap",
                              .apiVersion = VK_MAKE_API_VERSION(0, 1, 1, 0) };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                 .pApplicationInfo = &app };
    VkInstance inst;
    CHK(((PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance"))(&ici, NULL, &inst),
        "vkCreateInstance");

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
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                               .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci };
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
    GIPA(vkCmdBeginRenderPass); GIPA(vkCmdBindPipeline); GIPA(vkCmdBindDescriptorSets);
    GIPA(vkCmdBindVertexBuffers); GIPA(vkCmdDraw); GIPA(vkCmdEndRenderPass);
    GIPA(vkQueueSubmit); GIPA(vkQueueWaitIdle);

    gCreateImage = vkCreateImage; gGetImageMemoryRequirements = vkGetImageMemoryRequirements;
    gAllocateMemory = vkAllocateMemory; gBindImageMemory = vkBindImageMemory;
    gCreateImageView = vkCreateImageView; gGetImageSubresourceLayout = vkGetImageSubresourceLayout;

    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    VkDeviceMemory tmem, pmem;
    VkImageView tview, pview;
    VkSubresourceLayout tsl, psl;
    VkImage tile = make_img(dev, &mp, &tmem, &tview, W, H, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, &tsl);
    VkImage pat = make_img(dev, &mp, &pmem, &pview, W, H, VK_IMAGE_TILING_LINEAR,
                           VK_IMAGE_USAGE_SAMPLED_BIT, &psl);
    printf("  tiled: offset=%u pitch=%u size=%u ; pattern: offset=%u pitch=%u\n",
           (unsigned)tsl.offset, (unsigned)tsl.rowPitch, (unsigned)tsl.size,
           (unsigned)psl.offset, (unsigned)psl.rowPitch);

    /* 坐标指纹纹理：r=x, g=y, b=x^y, a=255 */
    void *m;
    CHK(vkMapMemory(dev, pmem, 0, VK_WHOLE_SIZE, 0, &m), "vkMapMemory(pat)");
    /* 指纹：r=x&255, g=y&255, b=bit0(x>>8)|bit1(y>>8) => 唯一解出 512 内的 (x,y) */
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            uint8_t *px = (uint8_t *)m + psl.offset + y * psl.rowPitch + x * 4;
            px[0] = x & 255; px[1] = y & 255;
            px[2] = ((x >> 8) & 1) | (((y >> 8) & 1) << 1); px[3] = 255;
        }
    vkUnmapMemory(dev, pmem);

    VkSamplerCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .minLod = 0.0f, .maxLod = 0.0f };
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
    VkDescriptorImageInfo dii = { .sampler = sampler, .imageView = pview,
                                  .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
    VkWriteDescriptorSet wds = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = dset, .dstBinding = 0, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &dii };
    vkUpdateDescriptorSets(dev, 1, &wds, 0, NULL);

    VkAttachmentDescription att = { .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_GENERAL };
    VkAttachmentReference aref = { 0, VK_IMAGE_LAYOUT_GENERAL };
    VkSubpassDescription subd = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &aref };
    VkRenderPassCreateInfo rpci = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &att, .subpassCount = 1, .pSubpasses = &subd };
    VkRenderPass rp;
    CHK(vkCreateRenderPass(dev, &rpci, NULL, &rp), "vkCreateRenderPass");
    VkFramebufferCreateInfo fbci = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = rp, .attachmentCount = 1, .pAttachments = &tview,
        .width = W, .height = H, .layers = 1 };
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

    VkViewport vp = { 0, 0, (float)W, (float)H, 0.0f, 1.0f };
    VkRect2D sc = { { 0, 0 }, { W, H } };
    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .pName = "main", .module = vs_mod },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .pName = "main", .module = fs_mod } };
    VkPipelineViewportStateCreateInfo vps = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc };
    VkGraphicsPipelineCreateInfo gpci = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vi,
        .pInputAssemblyState = &ia, .pViewportState = &vps, .pRasterizationState = &rs,
        .pMultisampleState = &ms, .pColorBlendState = &cbs, .layout = playout,
        .renderPass = rp, .subpass = 0 };
    VkPipeline pipe;
    CHK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe), "vkCreateGraphicsPipelines");

    static const float verts[6][6] = {
        { -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f }, {  1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f },
        { -1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 0.0f }, {  1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f },
        {  1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 0.0f }, { -1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 0.0f } };
    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof verts,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VkBuffer vbo;
    CHK(vkCreateBuffer(dev, &bci, NULL, &vbo), "vkCreateBuffer");
    VkMemoryRequirements bmr;
    vkGetBufferMemoryRequirements(dev, vbo, &bmr);
    VkMemoryAllocateInfo bmai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = bmr.size,
        .memoryTypeIndex = find_mem_type(bmr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &mp) };
    VkDeviceMemory vbo_mem;
    CHK(vkAllocateMemory(dev, &bmai, NULL, &vbo_mem), "vkAllocateMemory(VBO)");
    vkBindBufferMemory(dev, vbo, vbo_mem, 0);
    void *bp;
    CHK(vkMapMemory(dev, vbo_mem, 0, sizeof verts, 0, &bp), "vkMapMemory(VBO)");
    memcpy(bp, verts, sizeof verts);
    vkUnmapMemory(dev, vbo_mem);

    VkCommandPoolCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0 };
    VkCommandPool cpool;
    CHK(vkCreateCommandPool(dev, &cpci, NULL, &cpool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cpool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cb;
    CHK(vkAllocateCommandBuffers(dev, &cbai, &cb), "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHK(vkBeginCommandBuffer(cb, &bi), "vkBeginCommandBuffer");
    VkClearValue clear = { .color = { { 1, 2, 3, 255 } } };
    VkRenderPassBeginInfo rpbi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = rp, .framebuffer = fb, .renderArea = { { 0, 0 }, { W, H } },
        .clearValueCount = 1, .pClearValues = &clear };
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

    /* dump tiled 内存 */
    CHK(vkMapMemory(dev, tmem, 0, VK_WHOLE_SIZE, 0, &m), "vkMapMemory(tile)");
    FILE *f = fopen("/tmp/tilemap.bin", "wb");
    if (!f) { printf("  FAIL: fopen /tmp/tilemap.bin\n"); return 3; }
    const uint32_t bytes = W * H * 4;
    fwrite((uint8_t *)m + tsl.offset, 1, bytes, f);
    fclose(f);

    /* 自检：每个字应解出唯一 (x,y)，坐标完全覆盖 [0,SZ) */
    uint32_t *own = calloc((size_t)W * H, sizeof(uint32_t));
    unsigned dup = 0, bad = 0, zero = 0;
    for (uint32_t o = 0; o < bytes; o += 4) {
        uint8_t *px = (uint8_t *)m + tsl.offset + o;
        uint32_t x = px[0] | ((px[2] & 1) << 8);
        uint32_t y = px[1] | (((px[2] >> 1) & 1) << 8);
        if (x >= W || y >= H || px[3] != 255) { zero++; continue; }
        if (own[y * W + x]++) dup++;
    }
    unsigned missing = 0;
    for (uint32_t i2 = 0; i2 < W * H; i2++) { if (!own[i2]) missing++; else if (!bad && own[i2] > 2) bad++; }
    printf("  dump %u 字节 (SZ=%u)；越界=%u 重复=%u 缺失=%u\n", bytes, W, zero, dup, missing);
    printf("  采样 off/4: (x0y0)=%u (x1)=%u (x2)=%u (x4)=%u (x8)=%u (x16)=%u (y1)=%u (y2)=%u (y4)=%u (y8)=%u\n",
           own[0], own[1], own[2], own[4], own[8], own[16], own[W], own[2 * W], own[4 * W], own[8 * W]);
    free(own);
    vkUnmapMemory(dev, tmem);
    printf("  DONE\n");
    return 0;
}
