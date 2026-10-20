/* vk_clear: 验证 a5xx sysmem 的 RB_CLEAR_CNTL 清除路径。
 *
 * 判据（64x64 R8G8B8A8_UNORM 线性 RT，host-visible 映射回读）：
 *   1) 整幅 loadOp=CLEAR (0.2,0.4,0.6,1.0)  -> 4096 像素全 == (51,102,153,255)
 *   2) 再跑一次，renderArea 限定 (16,16)-(32,32) 清成 (1,0,0,1)
 *      -> 只有该 16x16 区域变 (255,0,0,255)，其余仍是 (51,102,153,255)
 * 无 draw call，只测 clear 引擎。
 */
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef PFN_vkVoidFunction (VKAPI_CALL *PFN_vkGetInstanceProcAddr_fn)(VkInstance, const char*);

#define CHK(r, what) do { VkResult _r = (r); \
    if (_r != VK_SUCCESS) { printf("  FAIL: %s -> %d\n", what, (int)_r); exit(3); } } while (0)
#define GIPA(name) PFN_##name name = (PFN_##name)vkGetInstanceProcAddr(inst, #name)

static PFN_vkCreateImage gCreateImage;
static PFN_vkGetImageMemoryRequirements gGetImageMemoryRequirements;
static PFN_vkAllocateMemory gAllocateMemory;
static PFN_vkBindImageMemory gBindImageMemory;
static PFN_vkCreateImageView gCreateImageView;

static int find_mem_type(uint32_t bits, VkMemoryPropertyFlags want, VkPhysicalDeviceMemoryProperties *mp)
{
    for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp->memoryTypes[i].propertyFlags & want) == want) return (int)i;
    return -1;
}

static VkImage new_img(VkDevice dev, VkPhysicalDeviceMemoryProperties *mp, VkDeviceMemory *mem,
                       VkImageView *view, uint32_t w, uint32_t h, VkImageTiling tiling,
                       VkImageUsageFlags usage)
{
    VkImageCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { w, h, 1 }, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = tiling, .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
    VkImage img;
    CHK(gCreateImage(dev, &ci, NULL, &img), "vkCreateImage");
    VkMemoryRequirements mr;
    gGetImageMemoryRequirements(dev, img, &mr);
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size, .memoryTypeIndex = find_mem_type(mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mp) };
    CHK(gAllocateMemory(dev, &mai, NULL, mem), "vkAllocateMemory");
    gBindImageMemory(dev, img, *mem, 0);
    VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    CHK(gCreateImageView(dev, &vci, NULL, view), "vkCreateImageView");
    printf("  img %ux%u %s mem=%u\n", w, h,
           tiling == VK_IMAGE_TILING_OPTIMAL ? "OPTIMAL" : "LINEAR ", (unsigned)mr.size);
    return img;
}

int main(void)
{
    void *h = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen FAIL: %s\n", dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr_fn vkGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr_fn)dlsym(h, "vkGetInstanceProcAddr");

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "vkclear", .apiVersion = VK_MAKE_API_VERSION(0, 1, 1, 0) };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
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
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qci };
    VkDevice dev;
    GIPA(vkCreateDevice);
    CHK(vkCreateDevice(pd, &dci, NULL, &dev), "vkCreateDevice");
    GIPA(vkGetDeviceQueue); GIPA(vkCreateImage); GIPA(vkBindImageMemory); GIPA(vkCreateImageView);
    GIPA(vkGetImageMemoryRequirements);
    GIPA(vkAllocateMemory); GIPA(vkMapMemory); GIPA(vkUnmapMemory);
    GIPA(vkCreateRenderPass); GIPA(vkCreateFramebuffer);
    GIPA(vkCreateCommandPool); GIPA(vkAllocateCommandBuffers);
    GIPA(vkBeginCommandBuffer); GIPA(vkEndCommandBuffer);
    GIPA(vkCmdBeginRenderPass); GIPA(vkCmdEndRenderPass);
    GIPA(vkQueueSubmit); GIPA(vkQueueWaitIdle);

    gCreateImage = vkCreateImage; gGetImageMemoryRequirements = vkGetImageMemoryRequirements;
    gAllocateMemory = vkAllocateMemory; gBindImageMemory = vkBindImageMemory;
    gCreateImageView = vkCreateImageView;

    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    VkDeviceMemory rtmem;
    VkImageView rtview;
    const uint32_t pitch = 64 * 4;   /* 线性 RGBA8：(预)行跨度 = 宽 * 4 */
    VkImage rt = new_img(dev, &mp, &rtmem, &rtview, 64, 64, VK_IMAGE_TILING_LINEAR,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

    VkAttachmentDescription ad = { .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_GENERAL };
    VkAttachmentReference ar = { 0, VK_IMAGE_LAYOUT_GENERAL };
    VkSubpassDescription sp = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &ar };
    VkRenderPassCreateInfo rpci = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &ad, .subpassCount = 1, .pSubpasses = &sp };
    VkRenderPass rp;
    CHK(vkCreateRenderPass(dev, &rpci, NULL, &rp), "vkCreateRenderPass");
    VkFramebufferCreateInfo fbci = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = rp, .attachmentCount = 1, .pAttachments = &rtview,
        .width = 64, .height = 64, .layers = 1 };
    VkFramebuffer fb;
    CHK(vkCreateFramebuffer(dev, &fbci, NULL, &fb), "vkCreateFramebuffer");

    VkCommandPoolCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0 };
    VkCommandPool cpool;
    CHK(vkCreateCommandPool(dev, &cpci, NULL, &cpool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cpool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cb;
    CHK(vkAllocateCommandBuffers(dev, &cbai, &cb), "vkAllocateCommandBuffers");

    /* ---- pass 1: 整幅 clear ---- */
    VkClearValue clear;
    clear.color.float32[0] = 0.2f; clear.color.float32[1] = 0.4f;
    clear.color.float32[2] = 0.6f; clear.color.float32[3] = 1.0f;
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHK(vkBeginCommandBuffer(cb, &bi), "vkBeginCommandBuffer(1)");
    VkRenderPassBeginInfo rpbi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = rp, .framebuffer = fb,
        .renderArea = { { 0, 0 }, { 64, 64 } }, .clearValueCount = 1, .pClearValues = &clear };
    vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(cb);
    CHK(vkEndCommandBuffer(cb), "vkEndCommandBuffer(1)");
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &cb };
    CHK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit(1)");
    CHK(vkQueueWaitIdle(queue), "vkQueueWaitIdle(1)");
    printf("  pass1 done\n");

    void *m = NULL;
    CHK(vkMapMemory(dev, rtmem, 0, VK_WHOLE_SIZE, 0, &m), "vkMapMemory");
    uint32_t dist[5] = { 0 };  /* 期望色 / 旧内容 / 其它 / 总计 / 未用 */
    uint32_t expect0 = (uint32_t)51 | (102u << 8) | (153u << 16) | (255u << 24);
    const uint8_t *base = (const uint8_t *)m;
    for (uint32_t y = 0; y < 64; y++) {
        const uint32_t *row = (const uint32_t *)(base + y * pitch);
        for (uint32_t x = 0; x < 64; x++) {
            if (row[x] == expect0) dist[0]++;
            else if (row[x] == 0) dist[1]++;
            else dist[2]++;
            dist[3]++;
        }
    }
    printf("  pass1 像素: 期望(51,102,153,255)=%u 其它=%u 总=%u\n", dist[0], dist[2], dist[3]);
    printf("  pass1 首个像素 = %08x（期望 %08x）\n",
           dist[3] ? *(const uint32_t *)base : 0u, expect0);
    vkUnmapMemory(dev, rtmem);

    /* ---- pass 2: 子区域 clear 到红色 ---- */
    clear.color.float32[0] = 1.0f; clear.color.float32[1] = 0.0f;
    clear.color.float32[2] = 0.0f; clear.color.float32[3] = 1.0f;
    CHK(vkBeginCommandBuffer(cb, &bi), "vkBeginCommandBuffer(2)");
    VkRenderPassBeginInfo rpbi2 = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = rp, .framebuffer = fb,
        .renderArea = { { 16, 16 }, { 16, 16 } }, .clearValueCount = 1, .pClearValues = &clear };
    vkCmdBeginRenderPass(cb, &rpbi2, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(cb);
    CHK(vkEndCommandBuffer(cb), "vkEndCommandBuffer(2)");
    VkSubmitInfo si2 = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &cb };
    CHK(vkQueueSubmit(queue, 1, &si2, VK_NULL_HANDLE), "vkQueueSubmit(2)");
    CHK(vkQueueWaitIdle(queue), "vkQueueWaitIdle(2)");
    printf("  pass2 done\n");

    CHK(vkMapMemory(dev, rtmem, 0, VK_WHOLE_SIZE, 0, &m), "vkMapMemory(2)");
    base = (const uint8_t *)m;
    uint32_t inside = 0, outside = 0, red = 0, other = 0;
    uint32_t expect_red = 255u | (255u << 24);
    for (uint32_t y = 0; y < 64; y++) {
        const uint32_t *row = (const uint32_t *)(base + y * pitch);
        for (uint32_t x = 0; x < 64; x++) {
            bool in = (x >= 16 && x < 32 && y >= 16 && y < 32);
            if (in) {
                inside++;
                if (row[x] == expect_red) red++;
                else other++;
            } else {
                outside++;
                if (row[x] == expect0) { /* ok */ } else other++;
            }
        }
    }
    printf("  pass2 子区域: 内 %u (红 %u / 其它 %u)  外 %u\n", inside, red, other, outside);
    printf("  pass2 边界像素 (16,16)=%08x (15,15)=%08x (32,32)=%08x\n",
           *(const uint32_t *)(base + 16 * pitch + 16 * 4),
           *(const uint32_t *)(base + 15 * pitch + 15 * 4),
           *(const uint32_t *)(base + 32 * pitch + 32 * 4));
    vkUnmapMemory(dev, rtmem);

    printf("DONE\n");
    return 0;
}
