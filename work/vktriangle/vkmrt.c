/* vkmrt.c - M1.7 探针: alpha 写入 + 双颜色附件(MRT) 路由
 * 两个 256x256 R8G8B8A8_UNORM 附件(linear tiling, host visible)，同一个 draw 写入。
 * 着色器由 gen_spirv_exp.py 生成，编译前 cp 成 spirv_exp.h:
 *   mode=mrt2       -> loc0=vec4(col.rgb,1), loc1=vec4(col.b,col.g,col.r,1) : 双侧都要有内容
 *   mode=alpha_half -> loc0=vec4(col.rgb,0.5)                                : 检 alpha=128
 * 用法(设备):
 *   cp spirv_mrt2.h spirv_exp.h && gcc -O1 -o vk_mrt2 vkmrt.c -I/home/umeko/tu5xx -I. -ldl
 *   VK_ICD_FILENAMES=.../freedreno_icd.device.json TU_DEBUG=sysmem ./vk_mrt2
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

static uint32_t find_mem_type(VkPhysicalDevice pd, uint32_t type_bits,
                              VkMemoryPropertyFlags props,
                              VkPhysicalDeviceMemoryProperties *mp)
{
    (void)pd;
    for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
        if ((type_bits & (1u << i)) &&
            (mp->memoryTypes[i].propertyFlags & props) == props)
            return i;
    return 0xffffffffu;
}

#define NIMG 2
static const uint32_t W = 256, H = 256;

int main(void)
{
    void *h = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen FAIL: %s\n", dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr_fn vkGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr_fn)dlsym(h, "vkGetInstanceProcAddr");

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                              .pApplicationName = "vkmrt",
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
    GIPA(vkCreateRenderPass); GIPA(vkCreateFramebuffer);
    GIPA(vkCreateShaderModule); GIPA(vkCreatePipelineLayout);
    GIPA(vkCreateGraphicsPipelines);
    GIPA(vkCreateBuffer); GIPA(vkBindBufferMemory); GIPA(vkGetBufferMemoryRequirements);
    GIPA(vkCreateCommandPool); GIPA(vkAllocateCommandBuffers);
    GIPA(vkBeginCommandBuffer); GIPA(vkEndCommandBuffer);
    GIPA(vkCmdBeginRenderPass); GIPA(vkCmdBindPipeline);
    GIPA(vkCmdBindVertexBuffers); GIPA(vkCmdDraw); GIPA(vkCmdEndRenderPass);
    GIPA(vkQueueSubmit); GIPA(vkQueueWaitIdle);
    GIPA(vkCreateFence); GIPA(vkWaitForFences);

    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    /* ---- NIMG 个颜色附件 ---- */
    VkImage img[NIMG]; VkDeviceMemory imem[NIMG]; VkImageView iview[NIMG];
    VkSubresourceLayout srl[NIMG];
    for (int i = 0; i < NIMG; i++) {
        VkImageCreateInfo ici2 = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = { W, H, 1 },
            .mipLevels = 1, .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_LINEAR,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
        CHK(vkCreateImage(dev, &ici2, NULL, &img[i]), "vkCreateImage");
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(dev, img[i], &mr);
        uint32_t mt = find_mem_type(pd, mr.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &memprops);
        if (mt == 0xffffffffu)
            mt = find_mem_type(pd, mr.memoryTypeBits,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memprops);
        VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                     .allocationSize = mr.size, .memoryTypeIndex = mt };
        CHK(vkAllocateMemory(dev, &mai, NULL, &imem[i]), "vkAllocateMemory(rt)");
        CHK(vkBindImageMemory(dev, img[i], imem[i], 0), "vkBindImageMemory");

        VkImageSubresource isr = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
        vkGetImageSubresourceLayout(dev, img[i], &isr, &srl[i]);
        void *im;
        CHK(vkMapMemory(dev, imem[i], 0, VK_WHOLE_SIZE, 0, &im), "vkMapMemory(bg)");
        for (uint32_t y = 0; y < H; y++) {
            uint8_t *row = (uint8_t *)im + srl[i].offset + y * srl[i].rowPitch;
            for (uint32_t x = 0; x < W; x++) {
                row[x * 4 + 0] = 0; row[x * 4 + 1] = 0;
                row[x * 4 + 2] = 0; row[x * 4 + 3] = 255;
            }
        }
        vkUnmapMemory(dev, imem[i]);

        VkImageViewCreateInfo ivci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = img[i], .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        CHK(vkCreateImageView(dev, &ivci, NULL, &iview[i]), "vkCreateImageView");
        printf("  img[%d] pitch=%u offset=%llu\n", i, (unsigned)srl[i].rowPitch,
               (unsigned long long)srl[i].offset);
    }

    /* ---- renderpass: NIMG 个颜色附件 ---- */
    VkAttachmentDescription att[NIMG];
    VkAttachmentReference aref[NIMG];
    for (int i = 0; i < NIMG; i++) {
        att[i] = (VkAttachmentDescription){
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_GENERAL };
        aref[i] = (VkAttachmentReference){ (uint32_t)i, VK_IMAGE_LAYOUT_GENERAL };
    }
    VkSubpassDescription sub = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = NIMG, .pColorAttachments = aref };
    VkRenderPassCreateInfo rpci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = NIMG, .pAttachments = att,
        .subpassCount = 1, .pSubpasses = &sub };
    VkRenderPass rp;
    CHK(vkCreateRenderPass(dev, &rpci, NULL, &rp), "vkCreateRenderPass(2 att)");

    VkFramebufferCreateInfo fbci = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = rp, .attachmentCount = NIMG, .pAttachments = iview,
        .width = W, .height = H, .layers = 1 };
    VkFramebuffer fb;
    CHK(vkCreateFramebuffer(dev, &fbci, NULL, &fb), "vkCreateFramebuffer");

    /* ---- shaders + pipeline ---- */
    VkShaderModuleCreateInfo smci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkShaderModule vs_mod, fs_mod;
    smci.codeSize = sizeof vs_spv; smci.pCode = vs_spv;
    CHK(vkCreateShaderModule(dev, &smci, NULL, &vs_mod), "vkCreateShaderModule(VS)");
    smci.codeSize = sizeof fs_spv; smci.pCode = fs_spv;
    CHK(vkCreateShaderModule(dev, &smci, NULL, &fs_mod), "vkCreateShaderModule(FS)");

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .pName = "main", .module = vs_mod },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .pName = "main", .module = fs_mod } };

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
    VkRect2D sc = { { 0, 0 }, { W, H } };
    VkPipelineViewportStateCreateInfo vs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &vp,
        .scissorCount = 1, .pScissors = &sc };

    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };

    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };

    VkPipelineColorBlendAttachmentState cbatt[NIMG];
    for (int i = 0; i < NIMG; i++)
        cbatt[i] = (VkPipelineColorBlendAttachmentState){
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
    VkPipelineColorBlendStateCreateInfo cbs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = NIMG, .pAttachments = cbatt };

    VkPipelineLayoutCreateInfo plci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkPipelineLayout playout;
    CHK(vkCreatePipelineLayout(dev, &plci, NULL, &playout), "vkCreatePipelineLayout");

    VkGraphicsPipelineCreateInfo gpci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vi, .pInputAssemblyState = &ia,
        .pViewportState = &vs, .pRasterizationState = &rs,
        .pMultisampleState = &ms, .pColorBlendState = &cbs,
        .layout = playout, .renderPass = rp, .subpass = 0 };
    VkPipeline pipe;
    CHK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe),
        "vkCreateGraphicsPipelines");

    /* ---- vertex buffer ---- */
    static const float verts[3][6] = {
        {  0.0f, -0.8f, 0.0f,  1.0f, 0.0f, 0.0f },
        { -0.8f,  0.8f, 0.0f,  0.0f, 1.0f, 0.0f },
        {  0.8f,  0.8f, 0.0f,  0.0f, 0.0f, 1.0f } };
    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                               .size = sizeof verts,
                               .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                               .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VkBuffer vbo;
    CHK(vkCreateBuffer(dev, &bci, NULL, &vbo), "vkCreateBuffer(VBO)");
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, vbo, &mr);
    uint32_t mt = find_mem_type(pd, mr.memoryTypeBits,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &memprops);
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                 .allocationSize = mr.size, .memoryTypeIndex = mt };
    VkDeviceMemory vbo_mem;
    CHK(vkAllocateMemory(dev, &mai, NULL, &vbo_mem), "vkAllocateMemory(VBO)");
    CHK(vkBindBufferMemory(dev, vbo, vbo_mem, 0), "vkBindBufferMemory");
    void *m;
    CHK(vkMapMemory(dev, vbo_mem, 0, sizeof verts, 0, &m), "vkMapMemory");
    memcpy(m, verts, sizeof verts);
    vkUnmapMemory(dev, vbo_mem);

    /* ---- record ---- */
    VkCommandPoolCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                     .queueFamilyIndex = 0 };
    VkCommandPool cpool;
    CHK(vkCreateCommandPool(dev, &cpci, NULL, &cpool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cpool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cb;
    CHK(vkAllocateCommandBuffers(dev, &cbai, &cb), "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHK(vkBeginCommandBuffer(cb, &bi), "vkBeginCommandBuffer");
    VkClearValue clear[NIMG];
    for (int i = 0; i < NIMG; i++) {
        clear[i].color.float32[0] = 0; clear[i].color.float32[1] = 0;
        clear[i].color.float32[2] = 0; clear[i].color.float32[3] = 1;
    }
    VkRenderPassBeginInfo rpbi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = rp, .framebuffer = fb,
        .renderArea = { { 0, 0 }, { W, H } },
        .clearValueCount = NIMG, .pClearValues = clear };
    vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &vbo, &off);
    vkCmdDraw(cb, 3, 1, 0, 0);
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

    /* ---- readback ---- */
    static const struct { int x, y; const char *tag; } probes[] = {
        { 128, 50, "top" }, { 50, 210, "left" }, { 206, 210, "right" },
        { 128, 160, "mid" } };
    for (int i = 0; i < NIMG; i++) {
        void *im;
        CHK(vkMapMemory(dev, imem[i], 0, VK_WHOLE_SIZE, 0, &im), "vkMapMemory(rb)");
        unsigned nonbg = 0;
        for (uint32_t y = 0; y < H; y++) {
            uint8_t *row = (uint8_t *)im + srl[i].offset + y * srl[i].rowPitch;
            for (uint32_t x = 0; x < W; x++)
                if (row[x * 4] || row[x * 4 + 1] || row[x * 4 + 2]) nonbg++;
        }
        printf("  att%d: nonbg=%u\n", i, nonbg);
        for (unsigned p = 0; p < sizeof probes / sizeof probes[0]; p++) {
            uint8_t *px = (uint8_t *)im + srl[i].offset +
                          probes[p].y * srl[i].rowPitch + probes[p].x * 4;
            printf("  att%d %-5s (%3d,%3d) = RGBA(%u,%u,%u,%u)\n", i, probes[p].tag,
                   probes[p].x, probes[p].y, px[0], px[1], px[2], px[3]);
        }
        vkUnmapMemory(dev, imem[i]);
    }
    printf("  DONE\n");
    return 0;
}
