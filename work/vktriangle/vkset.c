/* vkset.c - M2.4 探针: 两个 descriptor set 各有一个 binding 0
 * set0.binding0 -> 纹理 A（纯色 10,200,90）  左半屏
 * set1.binding0 -> 纹理 B（纯色 200,60,10）  右半屏
 * 两条管线：FS 分别引用 set0/binding0 与 set1/binding0，各自裁剪一半。
 * 判据：左半 = A、右半 = B 各 20808 像素。若仍按 binding 号编号，两边会显示同一张
 *      （同一个单元被后绑的 set 覆盖）——正是 M2.4 要排除的故障。
 * 着色器: gen_spirv_exp.py set2 spirv_set2.h
 *   gcc -O1 -o vk_set vkset.c -I/home/umeko/tu5xx -I. -ldl
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
static const uint8_t tex_col[2][4] = {
    {  10, 200,  90, 255 },   /* A: set 0 */
    { 200,  60,  10, 255 },   /* B: set 1 */
};

int main(void)
{
    void *h = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen FAIL: %s\n", dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr_fn vkGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr_fn)dlsym(h, "vkGetInstanceProcAddr");

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                              .pApplicationName = "vkset",
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

    /* ---- 颜色附件 ---- */
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

    /* ---- 两张 2x2 纯色纹理 ---- */
    VkImageCreateInfo tici = rici;
    tici.extent.width = 2; tici.extent.height = 2;
    tici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    tici.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkImage tex[2]; VkDeviceMemory tex_mem[2]; VkImageView tex_view[2];
    for (unsigned t = 0; t < 2; t++) {
        CHK(vkCreateImage(dev, &tici, NULL, &tex[t]), "vkCreateImage(tex)");
        vkGetImageMemoryRequirements(dev, tex[t], &mr);
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = find_mem_type(mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &memprops);
        CHK(vkAllocateMemory(dev, &mai, NULL, &tex_mem[t]), "vkAllocateMemory(tex)");
        CHK(vkBindImageMemory(dev, tex[t], tex_mem[t], 0), "vkBindImageMemory(tex)");
        VkImageSubresource sub = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
        VkSubresourceLayout lsl;
        vkGetImageSubresourceLayout(dev, tex[t], &sub, &lsl);
        CHK(vkMapMemory(dev, tex_mem[t], 0, VK_WHOLE_SIZE, 0, &im), "vkMapMemory(tex)");
        for (unsigned y = 0; y < 2; y++) {
            uint8_t *row = (uint8_t *)im + lsl.offset + y * lsl.rowPitch;
            for (unsigned x = 0; x < 2; x++) {
                row[x * 4 + 0] = tex_col[t][0];
                row[x * 4 + 1] = tex_col[t][1];
                row[x * 4 + 2] = tex_col[t][2];
                row[x * 4 + 3] = 255;
            }
        }
        vkUnmapMemory(dev, tex_mem[t]);
        VkImageViewCreateInfo tvci = rvci;
        tvci.image = tex[t];
        CHK(vkCreateImageView(dev, &tvci, NULL, &tex_view[t]), "vkCreateImageView(tex)");
        printf("  tex%c = (%u,%u,%u) pitch=%u\n", 'A' + t, tex_col[t][0], tex_col[t][1],
               tex_col[t][2], (unsigned)lsl.rowPitch);
    }

    VkSamplerCreateInfo sci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .minLod = 0.0f, .maxLod = 0.0f };
    VkSampler sampler[2];
    for (unsigned t = 0; t < 2; t++)
        CHK(vkCreateSampler(dev, &sci, NULL, &sampler[t]), "vkCreateSampler");

    /* ---- 两个 set layout（内容相同），pipeline layout 两个 set ---- */
    VkDescriptorSetLayoutBinding dslb = {
        .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT };
    VkDescriptorSetLayoutCreateInfo dslci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &dslb };
    VkDescriptorSetLayout dsl[2];
    for (unsigned s = 0; s < 2; s++)
        CHK(vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl[s]), "vkCreateDescriptorSetLayout");

    VkDescriptorPoolSize dps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 };
    VkDescriptorPoolCreateInfo dpci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 2, .poolSizeCount = 1, .pPoolSizes = &dps };
    VkDescriptorPool dpool;
    CHK(vkCreateDescriptorPool(dev, &dpci, NULL, &dpool), "vkCreateDescriptorPool");

    VkDescriptorSet dset[2];
    VkDescriptorSetAllocateInfo dsai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = dpool, .descriptorSetCount = 2, .pSetLayouts = dsl };
    CHK(vkAllocateDescriptorSets(dev, &dsai, dset), "vkAllocateDescriptorSets");

    for (unsigned s = 0; s < 2; s++) {
        VkDescriptorImageInfo dii = { .sampler = sampler[s], .imageView = tex_view[s],
                                      .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
        VkWriteDescriptorSet wds = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = dset[s], .dstBinding = 0, .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &dii };
        vkUpdateDescriptorSets(dev, 1, &wds, 0, NULL);
    }

    /* ---- renderpass / framebuffer ---- */
    VkAttachmentDescription att = {
        .format = VK_FORMAT_R8G8B8A8_UNORM, .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_GENERAL };
    VkAttachmentReference aref = { 0, VK_IMAGE_LAYOUT_GENERAL };
    VkSubpassDescription sub = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &aref };
    VkRenderPassCreateInfo rpci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &att,
        .subpassCount = 1, .pSubpasses = &sub };
    VkRenderPass rp;
    CHK(vkCreateRenderPass(dev, &rpci, NULL, &rp), "vkCreateRenderPass");
    VkFramebufferCreateInfo fbci = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = rp, .attachmentCount = 1, .pAttachments = &rt_view,
        .width = W, .height = H, .layers = 1 };
    VkFramebuffer fb;
    CHK(vkCreateFramebuffer(dev, &fbci, NULL, &fb), "vkCreateFramebuffer");

    /* ---- 两条管线：FS 分别用 set0 / set1 的 binding 0 ---- */
    VkShaderModuleCreateInfo smci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkShaderModule vs_mod;
    smci.codeSize = sizeof vs_spv; smci.pCode = vs_spv;
    CHK(vkCreateShaderModule(dev, &smci, NULL, &vs_mod), "vkCreateShaderModule(VS)");
    VkShaderModule fs_mod[2];
    smci.codeSize = sizeof fs_spv_set0; smci.pCode = fs_spv_set0;
    CHK(vkCreateShaderModule(dev, &smci, NULL, &fs_mod[0]), "vkCreateShaderModule(FS0)");
    smci.codeSize = sizeof fs_spv_set1; smci.pCode = fs_spv_set1;
    CHK(vkCreateShaderModule(dev, &smci, NULL, &fs_mod[1]), "vkCreateShaderModule(FS1)");

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
        .setLayoutCount = 2, .pSetLayouts = dsl };
    VkPipelineLayout playout;
    CHK(vkCreatePipelineLayout(dev, &plci, NULL, &playout), "vkCreatePipelineLayout");

    static const VkRect2D half[2] = {
        { { 26, 26 }, { 102, 204 } },     /* 左半：set0 */
        { { 128, 26 }, { 102, 204 } } };  /* 右半：set1 */
    VkPipeline pipe[2];
    for (unsigned s = 0; s < 2; s++) {
        VkPipelineShaderStageCreateInfo stages[2] = {
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_VERTEX_BIT, .pName = "main", .module = vs_mod },
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .pName = "main", .module = fs_mod[s] } };
        VkPipelineViewportStateCreateInfo vps = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &half[s] };
        VkGraphicsPipelineCreateInfo gpci = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = 2, .pStages = stages,
            .pVertexInputState = &vi, .pInputAssemblyState = &ia,
            .pViewportState = &vps, .pRasterizationState = &rs,
            .pMultisampleState = &ms, .pColorBlendState = &cbs,
            .layout = playout, .renderPass = rp, .subpass = 0 };
        CHK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe[s]),
            "vkCreateGraphicsPipelines");
    }

    static const float verts[6][6] = {
        { -0.8f, -0.8f, 0.0f,  0.0f, 0.0f, 0.0f },
        {  0.8f, -0.8f, 0.0f,  1.0f, 0.0f, 0.0f },
        { -0.8f,  0.8f, 0.0f,  0.0f, 1.0f, 0.0f },
        {  0.8f, -0.8f, 0.0f,  1.0f, 0.0f, 0.0f },
        {  0.8f,  0.8f, 0.0f,  1.0f, 1.0f, 0.0f },
        { -0.8f,  0.8f, 0.0f,  0.0f, 1.0f, 0.0f } };
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
    void *m;
    CHK(vkMapMemory(dev, vbo_mem, 0, sizeof verts, 0, &m), "vkMapMemory(VBO)");
    memcpy(m, verts, sizeof verts);
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

    /* 关键：一次绑定两个 set（firstSet=0, count=2） */
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, playout,
                            0, 2, dset, 0, NULL);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &vbo, &off);
    for (unsigned s = 0; s < 2; s++) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe[s]);
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
    struct { uint8_t r, g, b; unsigned cnt; int x0, x1, y0, y1; } hist[64];
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
            if (k == hist_n && hist_n < 64) {
                hist[k].r = px[0]; hist[k].g = px[1]; hist[k].b = px[2];
                hist[k].x0 = hist[k].x1 = (int)x;
                hist[k].y0 = hist[k].y1 = (int)y;
                hist_n++;
            }
            if (k < 64) {
                hist[k].cnt++;
                if ((int)x < hist[k].x0) hist[k].x0 = (int)x;
                if ((int)x > hist[k].x1) hist[k].x1 = (int)x;
                if ((int)y < hist[k].y0) hist[k].y0 = (int)y;
                if ((int)y > hist[k].y1) hist[k].y1 = (int)y;
            }
        }
    }
    printf("  nonbg=%u  不同颜色数=%u\n", nonbg, hist_n);
    for (unsigned k = 0; k < hist_n && k < 8; k++)
        printf("    RGB(%3u,%3u,%3u) x %-5u bbox x[%d..%d] y[%d..%d]\n",
               hist[k].r, hist[k].g, hist[k].b, hist[k].cnt,
               hist[k].x0, hist[k].x1, hist[k].y0, hist[k].y1);
    static const struct { int x, y; const char *tag; unsigned t; } q[2] = {
        { 76, 128, "left(set0)", 0 }, { 178, 128, "right(set1)", 1 } };
    for (unsigned i = 0; i < 2; i++) {
        uint8_t *px = (uint8_t *)im + srl.offset + q[i].y * srl.rowPitch + q[i].x * 4;
        printf("  %-11s (%3d,%3d) = RGBA(%3u,%3u,%3u,%u)  期望(%3u,%3u,%3u)\n",
               q[i].tag, q[i].x, q[i].y, px[0], px[1], px[2], px[3],
               tex_col[q[i].t][0], tex_col[q[i].t][1], tex_col[q[i].t][2]);
    }
    vkUnmapMemory(dev, rt_mem);
    printf("  DONE\n");
    return 0;
}
