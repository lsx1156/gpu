/* vkenum.c - Vulkan 物理设备枚举器（dlopen，使用真实 Vulkan 头文件）
 * 用法: VK_ICD_FILENAMES=<icd.json> ./vkenum
 * 头文件来源: Mesa include/vulkan, 编译加 -I<mesa>/include/vulkan
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>

#include <vulkan/vulkan.h>

typedef PFN_vkVoidFunction (VKAPI_PTR *PFN_vkGetInstanceProcAddr_fn)(VkInstance, const char*);

static const char* devTypeName(VkPhysicalDeviceType t) {
    switch (t) {
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:         return "OTHER";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:return "INTEGRATED_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:  return "DISCRETE_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:   return "VIRTUAL_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:           return "CPU";
        default:                                    return "?";
    }
}

int main(int argc, char** argv) {
    printf("env VK_ICD_FILENAMES = '%s'\n", getenv("VK_ICD_FILENAMES") ?: "(default)");

    void* h = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen libvulkan.so.1 FAIL: %s\n", dlerror()); return 1; }

    PFN_vkGetInstanceProcAddr_fn vkGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr_fn)dlsym(h, "vkGetInstanceProcAddr");
    if (!vkGetInstanceProcAddr) { printf("no vkGetInstanceProcAddr: %s\n", dlerror()); return 1; }

    PFN_vkCreateInstance vkCreateInstance =
        (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");
    if (!vkCreateInstance) { printf("no vkCreateInstance (global)\n"); return 1; }

    VkApplicationInfo appInfo = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = "vkenum",
        .applicationVersion = 1,
        .pEngineName        = "vkenum",
        .engineVersion      = 1,
        .apiVersion         = VK_MAKE_API_VERSION(0, 1, 1, 0),
    };
    VkInstanceCreateInfo instInfo = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo        = &appInfo,
        .enabledLayerCount       = 0,
        .ppEnabledLayerNames     = NULL,
        .enabledExtensionCount   = 0,
        .ppEnabledExtensionNames = NULL,
    };

    VkInstance inst = VK_NULL_HANDLE;
    VkResult r = vkCreateInstance(&instInfo, NULL, &inst);
    printf("vkCreateInstance => %s (0x%x)\n",
           r == VK_SUCCESS ? "VK_SUCCESS" : "ERR", (unsigned)r);
    if (r != VK_SUCCESS) { dlclose(h); return 2; }

    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices =
        (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr(inst, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties =
        (PFN_vkGetPhysicalDeviceProperties)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceProperties");
    PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties =
        (PFN_vkEnumerateDeviceExtensionProperties)vkGetInstanceProcAddr(inst, "vkEnumerateDeviceExtensionProperties");
    PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures =
        (PFN_vkGetPhysicalDeviceFeatures)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceFeatures");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkCreateDevice vkCreateDevice =
        (PFN_vkCreateDevice)vkGetInstanceProcAddr(inst, "vkCreateDevice");
    PFN_vkDestroyDevice vkDestroyDevice =
        (PFN_vkDestroyDevice)vkGetInstanceProcAddr(inst, "vkDestroyDevice");
    PFN_vkGetDeviceQueue vkGetDeviceQueue =
        (PFN_vkGetDeviceQueue)vkGetInstanceProcAddr(inst, "vkGetDeviceQueue");
    PFN_vkQueueSubmit vkQueueSubmit =
        (PFN_vkQueueSubmit)vkGetInstanceProcAddr(inst, "vkQueueSubmit");
    PFN_vkQueueWaitIdle vkQueueWaitIdle =
        (PFN_vkQueueWaitIdle)vkGetInstanceProcAddr(inst, "vkQueueWaitIdle");
    PFN_vkCreateFence vkCreateFence =
        (PFN_vkCreateFence)vkGetInstanceProcAddr(inst, "vkCreateFence");
    PFN_vkDestroyFence vkDestroyFence =
        (PFN_vkDestroyFence)vkGetInstanceProcAddr(inst, "vkDestroyFence");
    PFN_vkWaitForFences vkWaitForFences =
        (PFN_vkWaitForFences)vkGetInstanceProcAddr(inst, "vkWaitForFences");
    PFN_vkCreateCommandPool vkCreateCommandPool =
        (PFN_vkCreateCommandPool)vkGetInstanceProcAddr(inst, "vkCreateCommandPool");
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers =
        (PFN_vkAllocateCommandBuffers)vkGetInstanceProcAddr(inst, "vkAllocateCommandBuffers");
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer =
        (PFN_vkBeginCommandBuffer)vkGetInstanceProcAddr(inst, "vkBeginCommandBuffer");
    PFN_vkEndCommandBuffer vkEndCommandBuffer =
        (PFN_vkEndCommandBuffer)vkGetInstanceProcAddr(inst, "vkEndCommandBuffer");
    PFN_vkFreeCommandBuffers vkFreeCommandBuffers =
        (PFN_vkFreeCommandBuffers)vkGetInstanceProcAddr(inst, "vkFreeCommandBuffers");
    PFN_vkDestroyCommandPool vkDestroyCommandPool =
        (PFN_vkDestroyCommandPool)vkGetInstanceProcAddr(inst, "vkDestroyCommandPool");
    if (!vkEnumeratePhysicalDevices || !vkGetPhysicalDeviceProperties ||
        !vkEnumerateDeviceExtensionProperties || !vkGetPhysicalDeviceFeatures ||
        !vkGetPhysicalDeviceQueueFamilyProperties || !vkCreateDevice || !vkDestroyDevice ||
        !vkGetDeviceQueue || !vkQueueSubmit || !vkQueueWaitIdle ||
        !vkCreateFence || !vkDestroyFence || !vkWaitForFences ||
        !vkCreateCommandPool || !vkAllocateCommandBuffers || !vkBeginCommandBuffer ||
        !vkEndCommandBuffer || !vkFreeCommandBuffers || !vkDestroyCommandPool) {
        printf("no instance-level commands resolved\n"); dlclose(h); return 2;
    }

    uint32_t n = 0;
    r = vkEnumeratePhysicalDevices(inst, &n, NULL);
    printf("physical device count = %u (enumerate returned %s)\n", n, r == VK_SUCCESS ? "OK" : "FAIL");
    if (n == 0) { printf("NO physical device enumerated\n"); dlclose(h); return 0; }

    VkPhysicalDevice* devs = calloc(n, sizeof(VkPhysicalDevice));
    if (!devs) { perror("calloc"); dlclose(h); return 2; }
    r = vkEnumeratePhysicalDevices(inst, &n, devs);
    if (r != VK_SUCCESS) { printf("enumerate(2nd) failed 0x%x\n", (unsigned)r); free(devs); dlclose(h); return 2; }

    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devs[i], &props);
        uint32_t v = props.driverVersion;
        printf("---- device[%u] ----\n", i);
        printf("  name        : %s\n", props.deviceName);
        printf("  apiVersion  : Vulkan %u.%u.%u\n",
               VK_API_VERSION_MAJOR(props.apiVersion),
               VK_API_VERSION_MINOR(props.apiVersion),
               VK_API_VERSION_PATCH(props.apiVersion));
        printf("  driverVersion: %u (Vulkan %u.%u.%u)\n", v,
               VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v));
        printf("  vendorID    : 0x%04x  deviceID: 0x%04x\n", props.vendorID, props.deviceID);
        printf("  deviceType  : %s\n", devTypeName(props.deviceType));

        /* 扩展属性 */
        uint32_t extn = 0;
        r = vkEnumerateDeviceExtensionProperties(devs[i], NULL, &extn, NULL);
        printf("  device extensions: %u (r=0x%x)\n", extn, (unsigned)r);
        if (r == VK_SUCCESS && extn > 0) {
            VkExtensionProperties* exts = calloc(extn, sizeof(VkExtensionProperties));
            vkEnumerateDeviceExtensionProperties(devs[i], NULL, &extn, exts);
            uint32_t shown = extn < 10 ? extn : 10;
            for (uint32_t e = 0; e < shown; e++)
                printf("    - %s v%u\n", exts[e].extensionName, exts[e].specVersion);
            if (extn > shown) printf("    ... +%u more\n", extn - shown);
            free(exts);
        }

        /* Features（只列几个关键位） */
        VkPhysicalDeviceFeatures feats;
        memset(&feats, 0, sizeof feats);
        vkGetPhysicalDeviceFeatures(devs[i], &feats);
        printf("  features: geometryShader=%d tessellationShader=%d float64=%d "
               "independentBlend=%d wideLines=%d sparseBinding=%d\n",
               feats.geometryShader, feats.tessellationShader, feats.shaderFloat64,
               feats.independentBlend, feats.wideLines, feats.sparseBinding);

        /* 队列族 */
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qn, NULL);
        printf("  queue families: %u\n", qn);
        if (qn > 0) {
            VkQueueFamilyProperties* q = calloc(qn, sizeof(VkQueueFamilyProperties));
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qn, q);
            for (uint32_t qi = 0; qi < qn; qi++)
                printf("    family[%u]: queues=%u graphics=%d compute=%d transfer=%d\n",
                       qi, q[qi].queueCount,
                       !!(q[qi].queueFlags & VK_QUEUE_GRAPHICS_BIT),
                       !!(q[qi].queueFlags & VK_QUEUE_COMPUTE_BIT),
                       !!(q[qi].queueFlags & VK_QUEUE_TRANSFER_BIT));
            free(q);
        }

        /* vkCreateDevice: 1 个 graphics 队列 */
        if (qn > 0) {
            float prio = 1.0f;
            VkDeviceQueueCreateInfo qci = {
                .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = 0,
                .queueCount       = 1,
                .pQueuePriorities = &prio,
            };
            VkDeviceCreateInfo dci = {
                .sType             = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .queueCreateInfoCount = 1,
                .pQueueCreateInfos = &qci,
                .enabledExtensionCount = 0,
                .ppEnabledExtensionNames = NULL,
                .pEnabledFeatures  = NULL,
            };
            VkDevice dev = VK_NULL_HANDLE;
            r = vkCreateDevice(devs[i], &dci, NULL, &dev);
            printf("  vkCreateDevice => %s (0x%x)\n",
                   r == VK_SUCCESS ? "VK_SUCCESS" : "ERR", (unsigned)r);
            if (r == VK_SUCCESS) {
                printf("  logical device created OK\n");

                /* M1.2.0: 空提交探针 (0 个 command buffer, 合法用法) */
                VkQueue queue = VK_NULL_HANDLE;
                vkGetDeviceQueue(dev, 0, 0, &queue);
                printf("  queue = %p\n", (void*)(uintptr_t)queue);
                if (queue) {
                    VkFence fence = VK_NULL_HANDLE;
                    VkFenceCreateInfo fci = {
                        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                    };
                    r = vkCreateFence(dev, &fci, NULL, &fence);
                    printf("  vkCreateFence => %s\n", r == VK_SUCCESS ? "OK" : "ERR");
                    if (r == VK_SUCCESS) {
                        VkSubmitInfo si = {
                            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                            .commandBufferCount   = 0,
                            .pCommandBuffers      = NULL,
                        };
                        r = vkQueueSubmit(queue, 1, &si, fence);
                        printf("  vkQueueSubmit(empty) => %s (0x%x)\n",
                               r == VK_SUCCESS ? "VK_SUCCESS" : "ERR", (unsigned)r);
                        if (r == VK_SUCCESS) {
                            r = vkWaitForFences(dev, 1, &fence, VK_TRUE, 5ull * 1000 * 1000 * 1000);
                            printf("  vkWaitForFences => %s (0x%x)\n",
                                   r == VK_SUCCESS ? "OK" : "TIMEOUT/ERR", (unsigned)r);
                        }
                        vkDestroyFence(dev, fence, NULL);
                        r = vkQueueWaitIdle(queue);
                        printf("  vkQueueWaitIdle => %s (0x%x)\n",
                               r == VK_SUCCESS ? "OK" : "ERR", (unsigned)r);

                        /* M1.2.1: 非空命令缓冲录制/提交探针
                         * BeginCommandBuffer 触发 TU_CALLX(tu6_init_hw) A5XX 分支,
                         * 提交触发 IB1 非空解析。 */
                        VkCommandPool cpool = VK_NULL_HANDLE;
                        VkCommandPoolCreateInfo cpci = {
                            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                            .queueFamilyIndex = 0,
                        };
                        r = vkCreateCommandPool(dev, &cpci, NULL, &cpool);
                        printf("  vkCreateCommandPool => %s\n", r == VK_SUCCESS ? "OK" : "ERR");
                        if (r == VK_SUCCESS) {
                            VkCommandBuffer cb = VK_NULL_HANDLE;
                            VkCommandBufferAllocateInfo cbai = {
                                .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                .commandPool        = cpool,
                                .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                .commandBufferCount = 1,
                            };
                            r = vkAllocateCommandBuffers(dev, &cbai, &cb);
                            printf("  vkAllocateCommandBuffers => %s\n", r == VK_SUCCESS ? "OK" : "ERR");
                            if (r == VK_SUCCESS) {
                                VkCommandBufferBeginInfo binfo = {
                                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                };
                                r = vkBeginCommandBuffer(cb, &binfo);
                                printf("  vkBeginCommandBuffer => %s (0x%x)\n",
                                       r == VK_SUCCESS ? "VK_SUCCESS" : "ERR", (unsigned)r);
                                if (r == VK_SUCCESS) {
                                    r = vkEndCommandBuffer(cb);
                                    printf("  vkEndCommandBuffer => %s (0x%x)\n",
                                           r == VK_SUCCESS ? "VK_SUCCESS" : "ERR", (unsigned)r);
                                }
                                if (r == VK_SUCCESS) {
                                    VkFence fence2 = VK_NULL_HANDLE;
                                    r = vkCreateFence(dev, &fci, NULL, &fence2);
                                    if (r == VK_SUCCESS) {
                                        VkSubmitInfo si2 = {
                                            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                            .commandBufferCount = 1,
                                            .pCommandBuffers    = &cb,
                                        };
                                        r = vkQueueSubmit(queue, 1, &si2, fence2);
                                        printf("  vkQueueSubmit(1 cb) => %s (0x%x)\n",
                                               r == VK_SUCCESS ? "VK_SUCCESS" : "ERR", (unsigned)r);
                                        if (r == VK_SUCCESS) {
                                            r = vkWaitForFences(dev, 1, &fence2, VK_TRUE, 5ull * 1000 * 1000 * 1000);
                                            printf("  vkWaitForFences(1 cb) => %s (0x%x)\n",
                                                   r == VK_SUCCESS ? "OK" : "TIMEOUT/ERR", (unsigned)r);
                                        }
                                        vkDestroyFence(dev, fence2, NULL);
                                    }
                                }
                                vkFreeCommandBuffers(dev, cpool, 1, &cb);
                            }
                            vkDestroyCommandPool(dev, cpool, NULL);
                            printf("  command pool destroyed\n");
                        }
                    }
                }

                vkDestroyDevice(dev, NULL);
                printf("  vkDestroyDevice done\n");
            }
        }
    }

    free(devs);
    dlclose(h);
    printf("vkenum clean exit\n");
    return 0;
}