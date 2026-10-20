/* vkwrap.c - M2.5 探针: 三套 addressMode 的越界语义
 * 2x2 纹理 NEAREST：(0,0)白 (1,0)红 / (0,1)绿 (1,1)蓝
 * 四边形 uv.u 跨 4 个周期 [-1.5, 2.5)，uv.v 固定 0.25（只看第 0 行：白/红）
 * 三条横带各用一个 sampler: CLAMP_TO_EDGE / REPEAT / MIRROR_REPEAT
 * 判据（中间行颜色游程 RLE，每色块 32 或 96/160 像素）：
 *   CLAMP : W x160  R x96            （u<0 与 u>1 各钳到一侧）
 *   REPEAT: W R W R W R W R  各 32   （4 个周期，锯齿交替）
 *   MIRROR: W R R W W R R W  各 32   （4 个周期，镜像，中间两对同色）
 * 着色器: 复用 spirv_set2.h 的 fs_spv_set0（binding 0 隐式 LOD）
 *   gcc -O1 -o vk_wrap vkwrap.c -I/home/umeko/tu5xx -I. -ldl
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>

#include <vulkan/vulkan.h>
#include "spirv_exp.h"

typedef PFN_vkVoidFunction (VKAPI_PTR *PFN_vkGetInstanceProcAddr_fn)(VkInstance, const char*);

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

static const uint32_t W = 256, H = 256;
/* 2x2: (0,0)白 (1,0)红 (0,1)绿 (1,1)蓝 */
static const uint8_t px_col[4][3] = {
    { 255, 255, 255 }, { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 } };

static const char *col_name(uint8_t r, uint8_t g, uint8_t b)
{
    if (r == 255 && g == 255 && b == 255) return "W";
    if (r == 255 && g == 0 && b == 0) return "R";
    if (r == 0 && g == 255 && b == 0) return "G";
    if (r == 0 && g == 0 && b == 255) return "B";
    static char buf[32];
    snprintf(buf, sizeof buf, "(%u,%u,%u)", r, g, b);
    return buf;
}

int main(void)
{
    void *h = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen FAIL: %s\n", dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr_fn vkGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr_fn)dlsym(h, "vkGetInstanceProcAddr");

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                              .pApplicationName = "vkwrap",
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

    VkImageCreateInfo rici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { W, H, 1 }, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VkImage rt; VkDeviceMemory rt_mem; VkImageView rt_view;
    CHK(vkCreateImage(dev, &rici, NULL, &rt), "vkCreateImage(rt)");
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, rt, &mr);
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
        .memoryTypeIndex = find_mem_type(mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &memprops) };
    CHK(vkAllocateMemory(dev, &mai, NULL, &rt_mem), "vkAllocateMemory(rt)");
    CHK(vkBindImageMemory(dev, rt, rt_mem, 0), "vkBindImageMemory(rt)");
    VkSubresourceLayout srl;
    VkImageSubresource isr = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    vkGetImageSubresourceLayout(dev, rt, &isr, &srl);
    void *im;
    CHK(vkMapMemory(dev, rt_mem, 0, VK_WHOLE_SIZE, 0, &im), "vkMapMemory(rt)");
    for (uint32_t y = 0; y < H; y++) {
        uint8_t *row = (uint8_t *)im + srl.offset + y * srl.rowPitch;
        for (uint32_t x = 0; x < W; x++) {
            row[x * 4] = 7; row[x * 4 + 1] = 9; row[x * 4 + 2] = 11; row[x * 4 + 3] = 255;
        }
    }
    vkUnmapMemory(dev, rt_mem);
    VkImageViewCreateInfo rvci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = rt, .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    CHK(vkCreateImageView(dev, &rvci, NULL, &rt_view), "vkCreateImageView(rt)");

    /* 2x2 纹理：线性、pitch 256B */
    VkImageCreateInfo tici = rici;
    tici.extent.width = 2; tici.extent.height = 2;
    tici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    tici.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkImage tex; VkDeviceMemory tex_mem; VkImageView tex_view;
    CHK(vkCreateImage(dev, &tici, NULL, &tex), "vkCreateImage(tex)");
    vkGetImageMemoryRequirements(dev, tex, &mr);
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = find_mem_type(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &memprops);
    CHK(vkAllocateMemory(dev, &mai, NULL, &tex_mem), "vkAllocateMemory(tex)");
    CHK(vkBindImageMemory(dev, tex, tex_mem, 0), "vkBindImageMemory(tex)");
    VkImageSubresource sub = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout lsl;
    vkGetImageSubresourceLayout(dev, tex, &sub, &lsl);
    CHK(vkMapMemory(dev, tex_mem, 0, VK_WHOLE_SIZE, 0, &im), "vkMapMemory(tex)");
    for (unsigned y = 0; y < 2; y++) {
        uint8_t *row = (uint8_t *)im + lsl.offset + y * lsl.rowPitch;
        for (unsigned x = 0; x < 2; x++) {
            unsigned k = y * 2 + x;
            row[x * 4 + 0] = px_col[k][0];
            row[x * 4 + 1] = px_col[k][1];
            row[x * 4 + 2] = px_col[k][2];
            row[x * 4 + 3] = 255;
        }
    }
    vkUnmapMemory(dev, tex_mem);
    VkImageViewCreateInfo tvci = rvci;
    tvci.image = tex;
    CHK(vkCreateImageView(dev, &tvci, NULL, &tex_view), "vkCreateImageView(tex)");
    printf("  tex pitch=%u\n", (unsigned)lsl.rowPitch);

    static const VkSamplerAddressMode modes[3] = {
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT };
    static const char *mode_name[3] = { "CLAMP", "REPEAT", "MIRROR" };
    VkSampler sampler[3];
    for (unsigned m = 0; m < 3; m++) {
        VkSamplerCreateInfo sci = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = modes[m], .addressModeV = modes[m], .addressModeW = modes[m],
            .minLod = 0.0f, .maxLod = 0.0f };
        CHK(vkCreateSampler(dev, &sci, NULL, &sampler[m]), "vkCreateSampler");
    }

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
    for (unsigned m = 0; m < 3; m++) {
        VkDescriptorImageInfo dii = { .sampler = sampler[m], .imageView = tex_view,
                                      .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
        VkWriteDescriptorSet wds = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = dset[m], .dstBinding = 0, .descriptorCount = 1,
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
    VkFramebufferCreateInfo fbci = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = rp, .attachmentCount = 1, .pAttachments = &rt_view,
        .width = W, .height = H, .layers = 1 };
    VkFramebuffer fb;
    CHK(vkCreateFramebuffer(dev, &fbci, NULL, &fb), "vkCreateFramebuffer");

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
    VkViewport vp = { 0, 0, (float)W, (float)H, 0.0f, 1.0f };
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

    /* 三条横带: y 26..93 / 94..161 / 162..229 */
    static const VkRect2D band[3] = {
        { { 26, 26 }, { 204, 68 } }, { { 26, 94 }, { 204, 68 } }, { { 26, 162 }, { 204, 68 } } };
    VkPipeline pipe[3];
    for (unsigned m = 0; m < 3; m++) {
        VkPipelineShaderStageCreateInfo stages[2] = {
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_VERTEX_BIT, .pName = "main", .module = vs_mod },
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .pName = "main", .module = fs_mod } };
        VkPipelineViewportStateCreateInfo vps = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &band[m] };
        VkGraphicsPipelineCreateInfo gpci = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = 2, .pStages = stages,
            .pVertexInputState = &vi, .pInputAssemblyState = &ia,
            .pViewportState = &vps, .pRasterizationState = &rs,
            .pMultisampleState = &ms, .pColorBlendState = &cbs,
            .layout = playout, .renderPass = rp, .subpass = 0 };
        CHK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe[m]),
            "vkCreateGraphicsPipelines");
    }

    /* 顶点: uv.u 跨 [-1.5, 2.5)，uv.v 固定 0.25 */
    static const float verts[6][6] = {
        { -0.8f, -0.8f, 0.0f, -1.5f, 0.25f, 0.0f },
        {  0.8f, -0.8f, 0.0f,  2.5f, 0.25f, 0.0f },
        { -0.8f,  0.8f, 0.0f, -1.5f, 0.25f, 0.0f },
        {  0.8f, -0.8f, 0.0f,  2.5f, 0.25f, 0.0f },
        {  0.8f,  0.8f, 0.0f,  2.5f, 0.25f, 0.0f },
        { -0.8f,  0.8f, 0.0f, -1.5f, 0.25f, 0.0f } };
    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                               .size = sizeof verts,
                               .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                               .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VkBuffer vbo;
    CHK(vkCreateBuffer(dev, &bci, NULL, &vbo), "vkCreateBuffer(VBO)");
    vkGetBufferMemoryRequirements(dev, vbo, &mr);
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = find_mem_type(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &memprops);
    VkDeviceMemory vbo_mem;
    CHK(vkAllocateMemory(dev, &mai, NULL, &vbo_mem), "vkAllocateMemory(VBO)");
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
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHK(vkBeginCommandBuffer(cb, &bi), "vkBeginCommandBuffer");
    VkClearValue clear = { .color = { { 0, 0, 0, 1 } } };
    VkRenderPassBeginInfo rpbi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = rp, .framebuffer = fb,
        .renderArea = { { 0, 0 }, { W, H } },
        .clearValueCount = 1, .pClearValues = &clear };
    vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &vbo, &off);
    for (unsigned m = 0; m < 3; m++) {
        /* 每条带重新绑 descriptor set：顺带验证 set 重绑后状态会刷新 */
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, playout,
                                0, 1, &dset[m], 0, NULL);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe[m]);
        vkCmdDraw(cb, 6, 1, 0, 0);
    }
    vkCmdEndRenderPass(cb);
    CHK(vkEndCommandBuffer(cb), "vkEndCommandBuffer");

    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    CHK(vkCreateFence(dev, &fci, NULL, &fence), "vkCreateFence");
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                        .commandBufferCount = 1, .pCommandBuffers = &cb };
    VkResult r = vkQueueSubmit(queue, 1, &si, fence);
    if (r != VK_SUCCESS) { printf("  FAIL: vkQueueSubmit 0x%x\n", (unsigned)r); return 3; }
    r = vkWaitForFences(dev, 1, &fence, VK_TRUE, 10000000000ull);
    printf("  waitForFences = 0x%x\n", (unsigned)r);
    vkQueueWaitIdle(queue);

    CHK(vkMapMemory(dev, rt_mem, 0, VK_WHOLE_SIZE, 0, &im), "vkMapMemory(rb)");
    unsigned nonbg = 0, hist_n = 0;
    struct { uint8_t r, g, b; unsigned cnt; } hist[16];
    memset(hist, 0, sizeof hist);
    for (uint32_t y = 0; y < H; y++) {
        uint8_t *row = (uint8_t *)im + srl.offset + y * srl.rowPitch;
        for (uint32_t x = 0; x < W; x++) {
            uint8_t *px = row + x * 4;
            if (px[0] == 7 && px[1] == 9 && px[2] == 11) continue;
            nonbg++;
            unsigned k;
            for (k = 0; k < hist_n; k++)
                if (hist[k].r == px[0] && hist[k].g == px[1] && hist[k].b == px[2]) break;
            if (k == hist_n && hist_n < 16) {
                hist[k].r = px[0]; hist[k].g = px[1]; hist[k].b = px[2]; hist_n++;
            }
            if (k < 16) hist[k].cnt++;
        }
    }
    printf("  nonbg=%u  不同颜色数=%u\n", nonbg, hist_n);
    for (unsigned k = 0; k < hist_n; k++)
        printf("    RGB(%3u,%3u,%3u) x %u\n", hist[k].r, hist[k].g, hist[k].b, hist[k].cnt);

    /* 每条带中间行做颜色游程 */
    for (unsigned m = 0; m < 3; m++) {
        int y = 26 + 34 + m * 68;
        uint8_t *row = (uint8_t *)im + srl.offset + y * srl.rowPitch;
        printf("  %-6s y=%3d RLE:", mode_name[m], y);
        int run = 0; const char *prev = NULL;
        for (int x = 26; x <= 229; x++) {
            uint8_t *px = row + x * 4;
            const char *c = col_name(px[0], px[1], px[2]);
            if (prev && strcmp(prev, c) == 0) { run++; continue; }
            if (prev) printf(" %s x%d", prev, run);
            prev = c; run = 1;
        }
        if (prev) printf(" %s x%d", prev, run);
        printf("\n");
    }
    vkUnmapMemory(dev, rt_mem);
    printf("  DONE\n");
    return 0;
}
