/* vktriangle.c - M1.3 探针：renderpass + pipeline + draw 全套
 * 目标: A506 上完成第一个 vkCmdDraw（sysmem 模式），fence 不 hang。
 * 验收: 全链路 VK_SUCCESS + vkWaitForFences OK + vkQueueWaitIdle OK。
 *
 * 用法(设备):
 *   VK_ICD_FILENAMES=/path/a506_icd.x86_64.json TU_DEBUG=sysmem ./vktriangle
 *
 * 约定(简化，专为探针):
 *   - renderpass: 1 颜色附件 RGBA8, loadOp/storeOp=DONT_CARE(不触发 clear/blit 路径)
 *   - 不做拷回验证像素(M1.4)
 *   - 静态 viewport/scissor; 无 push consts/descriptors(绕开未适配的 const 路径)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>

#include <vulkan/vulkan.h>
#include "spirv_tri.h"

typedef PFN_vkVoidFunction (VKAPI_PTR *PFN_vkGetInstanceProcAddr_fn)(VkInstance, const char*);

static VKAPI_ATTR VkBool32 VKAPI_CALL dbg_cb(
    VkDebugUtilsMessageSeverityFlagBitsEXT sev,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
{
    (void)type; (void)user;
    fprintf(stderr, "[drv:%d] %s\n", (int)sev, data->pMessage);
    return VK_FALSE;
}

#define CHK(r, what) do { \
    if ((r) != VK_SUCCESS) { \
        printf("  FAIL: %s => 0x%x\n", what, (unsigned)(r)); \
        return 3; \
    } \
    printf("  OK: %s\n", what); \
} while (0)

#define GIPA(name) PFN_##name name = (PFN_##name)vkGetInstanceProcAddr(inst, #name); \
    if (!name) { printf("  no proc: %s\n", #name); return 2; }

static uint32_t find_mem_type(VkPhysicalDevice pd, uint32_t type_bits,
                              VkMemoryPropertyFlags props,
                              VkPhysicalDeviceMemoryProperties *mp)
{
    for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
        if ((type_bits & (1u << i)) &&
            (mp->memoryTypes[i].propertyFlags & props) == props)
            return i;
    return 0xffffffffu;
}

int main(void)
{
    void* h = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen FAIL: %s\n", dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr_fn vkGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr_fn)dlsym(h, "vkGetInstanceProcAddr");
    PFN_vkCreateInstance vkCreateInstance =
        (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                              .pApplicationName = "vktriangle",
                              .apiVersion = VK_MAKE_API_VERSION(0, 1, 1, 0) };
    VkDebugUtilsMessengerCreateInfoEXT dmci = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,
        .pfnUserCallback = dbg_cb };
    const char* inst_exts[] = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                 .pNext = &dmci,
                                 .pApplicationInfo = &app,
                                 .enabledExtensionCount = 1,
                                 .ppEnabledExtensionNames = inst_exts };
    VkInstance inst;
    CHK(vkCreateInstance(&ici, NULL, &inst), "vkCreateInstance");

    /* ---- device ---- */
    GIPA(vkEnumeratePhysicalDevices);
    uint32_t n = 0;
    CHK(vkEnumeratePhysicalDevices(inst, &n, NULL), "EnumeratePhysicalDevices");
    if (n == 0) { printf("no device\n"); return 3; }
    VkPhysicalDevice pd;
    CHK(vkEnumeratePhysicalDevices(inst, &n, &pd), "EnumeratePhysicalDevices(2)");
    GIPA(vkGetPhysicalDeviceProperties);
    GIPA(vkGetPhysicalDeviceMemoryProperties);
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(pd, &props);
    printf("  device: %s\n", props.deviceName);

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

#define DPA(name) PFN_##name name = (PFN_##name)vkGetInstanceProcAddr(inst, #name); \
    if (!name) { printf("  no proc: %s\n", #name); return 2; }

    DPA(vkGetDeviceQueue);
    DPA(vkCreateBuffer); DPA(vkAllocateMemory); DPA(vkBindBufferMemory);
    DPA(vkMapMemory); DPA(vkUnmapMemory);
    DPA(vkCreateImage); DPA(vkBindImageMemory); DPA(vkCreateImageView);
    DPA(vkGetImageSubresourceLayout);
    DPA(vkGetBufferMemoryRequirements); DPA(vkGetImageMemoryRequirements);
    DPA(vkCreateRenderPass); DPA(vkCreateFramebuffer);
    DPA(vkCreateShaderModule); DPA(vkCreatePipelineLayout);
    DPA(vkCreateGraphicsPipelines);
    DPA(vkCreateCommandPool); DPA(vkAllocateCommandBuffers);
    DPA(vkBeginCommandBuffer); DPA(vkEndCommandBuffer);
    DPA(vkCmdBeginRenderPass); DPA(vkCmdBindPipeline);
    DPA(vkCmdFillBuffer);
    DPA(vkCmdBindVertexBuffers); DPA(vkCmdDraw); DPA(vkCmdEndRenderPass);
    DPA(vkQueueSubmit); DPA(vkWaitForFences); DPA(vkQueueWaitIdle);
    DPA(vkCreateFence); DPA(vkDestroyFence);
    DPA(vkDestroyBuffer); DPA(vkFreeMemory); DPA(vkDestroyImage);
    DPA(vkDestroyImageView); DPA(vkDestroyRenderPass);
    DPA(vkDestroyFramebuffer); DPA(vkDestroyShaderModule);
    DPA(vkDestroyPipeline); DPA(vkDestroyPipelineLayout);
    DPA(vkDestroyCommandPool); DPA(vkFreeCommandBuffers);
    DPA(vkDestroyDevice);

    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    /* ---- image + view (颜色附件, 256x256 RGBA8) ----
     * M1.4: LINEAR tiling + HOST_VISIBLE 内存，draw 后 CPU 直接读回验证。 */
    const uint32_t W = 256, H = 256;
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
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkImage image;
    CHK(vkCreateImage(dev, &ici2, NULL, &image), "vkCreateImage");

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, image, &mr);
    uint32_t mt = find_mem_type(pd, mr.memoryTypeBits,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &memprops);
    if (mt == 0xffffffffu)
        mt = find_mem_type(pd, mr.memoryTypeBits,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memprops);
    printf("  image mem type = %u (bits 0x%x, size %llu)\n",
           mt, mr.memoryTypeBits, (unsigned long long)mr.size);
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size, .memoryTypeIndex = mt };
    VkDeviceMemory img_mem;
    CHK(vkAllocateMemory(dev, &mai, NULL, &img_mem), "vkAllocateMemory(image)");
    CHK(vkBindImageMemory(dev, image, img_mem, 0), "vkBindImageMemory");

    /* 预填充背景: 黑色不透明 (0,0,0,255)——loadOp=DONT_CARE 下驱动不清屏,
     * draw 后非背景像素即三角形覆盖。 */
    VkImageSubresource isr = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout srl;
    vkGetImageSubresourceLayout(dev, image, &isr, &srl);
    printf("  image row pitch = %u (offset %llu)\n",
           (unsigned)srl.rowPitch, (unsigned long long)srl.offset);
    void* im;
    CHK(vkMapMemory(dev, img_mem, 0, VK_WHOLE_SIZE, 0, &im), "vkMapMemory(bg)");
    for (uint32_t y = 0; y < H; y++) {
        uint8_t* row = (uint8_t*)im + srl.offset + y * srl.rowPitch;
        for (uint32_t x = 0; x < W; x++) {
            row[x * 4 + 0] = 0; row[x * 4 + 1] = 0;
            row[x * 4 + 2] = 0; row[x * 4 + 3] = 255;
        }
    }
    vkUnmapMemory(dev, img_mem);

    VkImageViewCreateInfo ivci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    VkImageView view;
    CHK(vkCreateImageView(dev, &ivci, NULL, &view), "vkCreateImageView");

    /* ---- renderpass (DONT_CARE, 不走 clear/blit) ---- */
    VkAttachmentDescription att = {
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,  /* M1.4: DONT_CARE 不写回附件 */
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
        .renderPass = rp, .attachmentCount = 1, .pAttachments = &view,
        .width = W, .height = H, .layers = 1 };
    VkFramebuffer fb;
    CHK(vkCreateFramebuffer(dev, &fbci, NULL, &fb), "vkCreateFramebuffer");

    /* ---- shaders + pipeline ---- */
    VkShaderModuleCreateInfo smci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkShaderModule vs_mod, fs_mod;
    smci.codeSize = sizeof vs_spv; smci.pCode = vs_spv;
    CHK(vkCreateShaderModule(dev, &smci, NULL, &vs_mod), "vkCreateShaderModule(VS)");
    smci.codeSize = sizeof fs_spv; smci.pCode = fs_spv;
    CHK(vkCreateShaderModule(dev, &smci, NULL, &fs_mod), "vkCreateShaderModule(FS)");

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .pName = "main", .module = vs_mod },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .pName = "main", .module = fs_mod },
    };

    VkVertexInputBindingDescription vib = {
        .binding = 0, .stride = 3 * sizeof(float),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription via = {
        .location = 0, .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0 };
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vib,
        .vertexAttributeDescriptionCount = 1, .pVertexAttributeDescriptions = &via };

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
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f };

    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };

    VkPipelineColorBlendAttachmentState cbatt = {
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
    VkPipelineColorBlendStateCreateInfo cbs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &cbatt };

    VkPipelineLayoutCreateInfo plci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkPipelineLayout playout;
    CHK(vkCreatePipelineLayout(dev, &plci, NULL, &playout),
        "vkCreatePipelineLayout");

    VkGraphicsPipelineCreateInfo gpci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pViewportState = &vs,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pColorBlendState = &cbs,
        .layout = playout,
        .renderPass = rp, .subpass = 0 };
    VkPipeline pipe;
    CHK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe),
        "vkCreateGraphicsPipelines");

    /* ---- vertex buffer (host visible) ---- */
    static const float verts[3][3] = {
        {  0.0f, -0.8f, 0.0f },
        { -0.8f,  0.8f, 0.0f },
        {  0.8f,  0.8f, 0.0f },
    };
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof verts,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VkBuffer vbo;
    CHK(vkCreateBuffer(dev, &bci, NULL, &vbo), "vkCreateBuffer(VBO)");
    vkGetBufferMemoryRequirements(dev, vbo, &mr);
    mt = find_mem_type(pd, mr.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &memprops);
    printf("  vbo mem type = %u\n", mt);
    mai.allocationSize = mr.size; mai.memoryTypeIndex = mt;
    VkDeviceMemory vbo_mem;
    CHK(vkAllocateMemory(dev, &mai, NULL, &vbo_mem), "vkAllocateMemory(VBO)");
    CHK(vkBindBufferMemory(dev, vbo, vbo_mem, 0), "vkBindBufferMemory");
    void* m;
    CHK(vkMapMemory(dev, vbo_mem, 0, sizeof verts, 0, &m), "vkMapMemory");
    memcpy(m, verts, sizeof verts);
    vkUnmapMemory(dev, vbo_mem);

    /* ---- M1.4 隔离实验: GPU 直接写 host-visible 内存通路 ---- */
    VkBufferCreateInfo fbci2 = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 4096,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VkBuffer fbuf;
    CHK(vkCreateBuffer(dev, &fbci2, NULL, &fbuf), "vkCreateBuffer(fill)");
    vkGetBufferMemoryRequirements(dev, fbuf, &mr);
    mt = find_mem_type(pd, mr.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &memprops);
    mai.allocationSize = mr.size; mai.memoryTypeIndex = mt;
    VkDeviceMemory fbuf_mem;
    CHK(vkAllocateMemory(dev, &mai, NULL, &fbuf_mem), "vkAllocateMemory(fill)");
    CHK(vkBindBufferMemory(dev, fbuf, fbuf_mem, 0), "vkBindBufferMemory(fill)");

    /* ---- command buffer: renderpass + draw ---- */
    VkCommandPoolCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0 };
    VkCommandPool cpool;
    CHK(vkCreateCommandPool(dev, &cpci, NULL, &cpool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cpool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1 };
    VkCommandBuffer cb;
    CHK(vkAllocateCommandBuffers(dev, &cbai, &cb), "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHK(vkBeginCommandBuffer(cb, &bi), "vkBeginCommandBuffer");

    VkClearValue clear = { .color = { { 0, 0, 0, 1 } } };  /* loadOp=DONT_CARE 不使用 */
    VkRenderPassBeginInfo rpbi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = rp, .framebuffer = fb,
        .renderArea = { { 0, 0 }, { W, H } },
        .clearValueCount = 1, .pClearValues = &clear };
    vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    printf("  OK: vkCmdBeginRenderPass (recorded)\n");

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &vbo, &off);
    vkCmdDraw(cb, 3, 1, 0, 0);
    printf("  OK: vkCmdDraw(3) (recorded)\n");
    vkCmdEndRenderPass(cb);
    /* 隔离实验: renderpass 外 fill buffer，验证 GPU 写 host 内存 */
    vkCmdFillBuffer(cb, fbuf, 0, VK_WHOLE_SIZE, 0xAABBCCDD);
    CHK(vkEndCommandBuffer(cb), "vkEndCommandBuffer");

    /* ---- submit + fence ---- */
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    CHK(vkCreateFence(dev, &fci, NULL, &fence), "vkCreateFence");
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                        .commandBufferCount = 1, .pCommandBuffers = &cb };
    VkResult r = vkQueueSubmit(queue, 1, &si, fence);
    if (r != VK_SUCCESS) {
        printf("  FAIL: vkQueueSubmit => 0x%x\n", (unsigned)r);
        return 3;
    }
    printf("  OK: vkQueueSubmit\n");

    r = vkWaitForFences(dev, 1, &fence, VK_TRUE, 30ull * 1000 * 1000 * 1000);
    printf("== vkWaitForFences(draw) => %s (0x%x) ==\n",
           r == VK_SUCCESS ? "OK(不 hang)" : "TIMEOUT/ERR", (unsigned)r);

    r = vkQueueWaitIdle(queue);
    printf("== vkQueueWaitIdle => %s (0x%x) ==\n",
           r == VK_SUCCESS ? "OK" : "ERR", (unsigned)r);

    /* ---- M1.4: 像素读回验证 ---- */
    CHK(vkMapMemory(dev, img_mem, 0, VK_WHOLE_SIZE, 0, &im), "vkMapMemory(readback)");
    const uint8_t bg[4] = { 0, 0, 0, 255 };
    const uint8_t exp[4] = { 0, 255, 0, 255 };  /* FS 输出绿色 */
    uint32_t hit = 0, green = 0;
    for (uint32_t y = 0; y < H; y++) {
        const uint8_t* row = (const uint8_t*)im + srl.offset + y * srl.rowPitch;
        for (uint32_t x = 0; x < W; x++) {
            const uint8_t* p = row + x * 4;
            if (memcmp(p, bg, 4) != 0)
                hit++;
            if (memcmp(p, exp, 4) == 0)
                green++;
        }
    }
    const uint8_t* c = (const uint8_t*)im + srl.offset + (H / 2) * srl.rowPitch + (W / 2) * 4;
    printf("== readback: center(%u,%u) = RGBA(%u,%u,%u,%u)  nonbg=%u  green=%u ==\n",
           W / 2, H / 2, c[0], c[1], c[2], c[3], hit, green);
    /* 三角形顶点 (0,-0.8) (-0.8,0.8) (0.8,0.8)，NDC 中心在内部；
     * 面积比 ~0.64*0.5 → 期望 green > 2 万像素 */
    printf("== verify: %s ==\n",
           (c[1] == 255 && c[0] == 0 && c[2] == 0 && c[3] == 255 && green > 20000)
              ? "TRIANGLE RASTERIZED OK" : "FAIL (center not green or no coverage)");
    vkUnmapMemory(dev, img_mem);

    /* 隔离实验结果 */
    void* fm;
    CHK(vkMapMemory(dev, fbuf_mem, 0, 4096, 0, &fm), "vkMapMemory(fill)");
    uint32_t fill_ok = 1;
    const uint32_t* fw = (const uint32_t*)fm;
    for (int i = 0; i < 1024; i++) {
        if (fw[i] != 0xAABBCCDD) { fill_ok = 0; break; }
    }
    printf("== fill buffer: %s (first=0x%08x) ==\n",
           fill_ok ? "GPU WRITE OK" : "GPU WRITE FAIL", fw[0]);
    vkUnmapMemory(dev, fbuf_mem);

    /* ---- cleanup ---- */
    vkDestroyFence(dev, fence, NULL);
    vkFreeCommandBuffers(dev, cpool, 1, &cb);
    vkDestroyCommandPool(dev, cpool, NULL);
    vkDestroyBuffer(dev, vbo, NULL);
    vkFreeMemory(dev, vbo_mem, NULL);
    vkDestroyPipeline(dev, pipe, NULL);
    vkDestroyPipelineLayout(dev, playout, NULL);
    vkDestroyShaderModule(dev, vs_mod, NULL);
    vkDestroyShaderModule(dev, fs_mod, NULL);
    vkDestroyFramebuffer(dev, fb, NULL);
    vkDestroyRenderPass(dev, rp, NULL);
    vkDestroyImageView(dev, view, NULL);
    vkDestroyImage(dev, image, NULL);
    vkFreeMemory(dev, img_mem, NULL);
    vkDestroyDevice(dev, NULL);
    printf("vktriangle clean exit\n");
    return r == VK_SUCCESS ? 0 : 4;
}
