# 阶段 0 侦察报告（只读）

> 日期：2026-08-30
> 方式：plink SSH（umeko@192.168.10.50），全程只读查询，未修改设备任何状态
> 项目：mido（红米 Note 4/4X）tu5xx Vulkan 驱动自研立项前置侦察

---

## 1. 系统概况

| 项 | 实测值 |
|---|---|
| 操作系统 | Ubuntu 24.04 LTS (noble)，非 Mobian |
| 内核 | `6.7.5-rv2-umeko-msm8953+ #7 SMP PREEMPT`，2024-02-23 构建 |
| 内核构建方式 | **容器内交叉编译**（aarch64-linux-gnu-gcc 9.4.0，Ubuntu 20.04 工具链）→ 内核源码构建链有先例可循 |
| 存储 | eMMC 29.1G（mmcblk0），标准 A/B 布局（p21/p22 = 64M boot_a/b），root=p49 24G 余 10G，**无 SD 卡** |
| 内存 | 2813 MB，**无 swap**，当前仅余 ~85 MB（Chromium + netdata 常驻占用） |
| 内核文件 | /boot/ 下 vmlinuz/initrd.img/config/System.map 全套齐全 |
| 权限 | sudo 可用；umeko 已在 video/render 组（GPU 免 root）；设备装有 docker |
| 当前负载 | load ~4，设备正作为活桌面使用（Chromium 常驻，~373MB） |

**结论 1：设备端编译彻底不可行（内存余量 <100MB 且无 swap），PC 交叉编译是唯一正解——与既定决策一致。**

---

## 2. GPU 驱动栈现状（实测）

- `drm/msm` 模块加载，`adreno 1c00000.gpu` 正常绑定，MDP5 v1.16 显示管线 OK（panel: xiaomi otm1911），fb0 就绪
- GPU 微码：`a530_pm4.fw` / `a530_pfp.fw` 已加载；**zap shader 固件未见加载**（仅有 8KB reserved-mem 节点）——GL 可用证明无 zap 也能跑
- 运行时 GL 实测（glxinfo）：
  ```
  Vendor: freedreno  Device: FD506  Version: 24.0.5
  direct rendering: Yes  Accelerated: yes
  GL 3.1 / GLES 3.1 / GLES1 1.1
  ```
- Xorg：未装 xf86-video-freedreno，走 modesetting 默认路径；存在 00-rotate.conf
- MDP5 偶发 `errors: 04000000` 显示中断错误（非致命，记录在案）

---

## 3. 重大修正：`DRM_MSM_GEM_INFO -22` 的真相

- **实测复现**：24.0.5 下运行 glxinfo 仍打印：
  ```
  MESA: warning: Failed to set BO metadata with DRM_MSM_GEM_INFO: -22
  ```
- 即 `-22` 在 **24.0.5 下就存在**，只是被当作**非致命警告**（BO metadata 命名失败，容忍继续）；
- 26.1.7（kisak）下同样的调用成为**致命错误** → GPU 进程崩溃；
- **修正结论**：不是「内核缺 ioctl 功能」，而是「新旧 Mesa 对该失败的处理级别不同」。自研 tu5xx 代码中该调用完全可控；
- 细节归因（Mesa 源码 diff）可在 M1 前补充，不阻塞。

---

## 4. Mesa 混装现状（定时炸弹，择机清理）

| 包 | 当前版本 | 状态 |
|---|---|---|
| libgl1-mesa-dri / libegl-mesa0 / libglx-mesa0 / libglapi-mesa / libgbm1 / mesa-vulkan-drivers | 24.0.5-1ubuntu1 | 已降级，实际生效 |
| **mesa-libgallium** | **26.1.7~kisak1** | **残留孤儿**（libgallium-26.1.7.so 存在但 GL 实际加载 24.0.5 自带实现） |
| mesa-common-dev / mesa-vdpau-drivers | 26.1.7~kisak1 | 残留（dev 头文件曾用于构建尝试） |
| kisak PPA 源 | kisak-mesa.bak.sources | 已禁用 ✅ |

- apt 历史还原：kisak 链 24.1.1 → 25.2.8 → 26.1.7，随后降级命令**漏掉了 mesa-libgallium**；
- libdrm-dev / libdrm-freedreno1 已装（此前构建尝试痕迹，后续开发直接可用）。

---

## 5. 内核配置（对 tu5xx 研发极其友好）

```
CONFIG_DRM_MSM=m
CONFIG_DRM_MSM_GPU_STATE=y      ← GPU 状态/崩溃转储支持
CONFIG_DEBUG_FS=y
CONFIG_DEBUG_FS_ALLOW_ALL=y
CONFIG_IOMMU_IOVA=y
（无 CONFIG_KGSL —— kgsl 捷径彻底排除，与 tu5xx 路线一致）
（无 CONFIG_LOCK_DOWN —— debugfs 无限制）
```

**debugfs 节点宝库（/sys/kernel/debug/dri/0/，已挂载可用）**：
`rd`（**命令流捕获**）、`gpu`、`state`、`reset`、`hangrd`、`hangcheck_period_ms`、`gem`、`gem_names`、`perf`、`devfreq`、`me/meq/pfp/roq/smp/mm/shrink`

**结论 2：`rd` 节点 = freedreno 命令流转储通道。可以让 fd5 GL 驱动当「标准答案」抓包，对照调 tu5xx——M2 阶段的核心调试手段已就位，比预期乐观。**

---

## 6. 对立项计划的影响

1. **编译环境**：维持 PC 交叉编译决策（设备内存撑不住任何编译负载）；
2. **M2 调试手段升级**：debugfs `rd` 捕获 + `GPU_STATE` + `hangrd`，调试基建完整；
3. **内核实验路径**：/boot 文件齐全、内核本就是交叉编译产物；boot.img 打包/引导链（p21/p22 A/B）**进入内核阶段前必须摸清并备份**；
4. **设备使用时段**：设备是活桌面，内核/GPU 实验需与你协调时段。

---

## 7. 遗留待查（不阻塞下一步决策）

- [ ] boot.img 打包/引导链细节（mkbootimg 流程，进入内核阶段前必查）
- [ ] glmark2 当前实测帧率（为不干扰屏幕未跑；GL 健康度已由 glxinfo 证实）
- [ ] 26.1.7 致命机制的源码级归因（diff Mesa 源码，可选）
- [ ] zap shader 缺失对 Vulkan 的影响（M1 时评估）
- [ ] Mesa 混装清理（降级 mesa-libgallium → 24.0.5 或移除残留，择机做）

---

## 结论

阶段 0 只读侦察完成。系统对 tu5xx 研发的适配度**超预期**：内核自带 GPU_STATE + debugfs 全开 + `rd` 命令流捕获；内核本身就有容器交叉编译先例；drm 开发头文件已就位。
两项关键修正：① `-22` 是「警告级」问题而非内核功能缺失；② Mesa 存在混装残留（不致害，需择机清理）。

---

## 8. 阶段 0 收尾：PC 交叉编译环境（已打通并验证）

> 日期：2026-08-30。WSL 因宿主 Windows 组件存储损坏（DISM 14098）不可用，最终方案：**MSYS2（仅承载环境）+ ARM 官方 GNU Toolchain + 设备 sysroot**。

### 8.1 组成

| 组件 | 位置/版本 |
|---|---|
| 工具链 | Arm GNU Toolchain 15.2.Rel1（mingw-w64 宿主，aarch64-none-linux-gnu，gcc 15.2.1）→ `tools\bin\` |
| sysroot | 设备 Ubuntu 24.04 arm64 根文件系统导出（/usr/include + /usr/lib）→ `sysroot\` |
| MSYS2 | `tools\msys64\`（pkgconf 等辅助，pacman 挂起时手动解包 zst） |
| 构建封装 | `cross.ps1`（自动重建 junction + 统一 flags） |

### 8.2 sysroot 的四项关键修复（踩坑记录，分发/重建必读）

1. **多架构头文件路径**：ARM 工具链 glibc 是非 multiarch 布局，GCC 不会搜 `usr/include/<三元组>` 子目录 → 必须显式 `-isystem sysroot/usr/include/aarch64-linux-gnu`；
2. **crt 对象不可达**：gcc 驱动按 `%s` 规则在 `$sysroot/usr/lib/`（其 LIBRARY_PATH 中唯一的 sysroot 目录）找 `crt1.o/crti.o/crtn.o`，而 Debian 放在 `usr/lib/aarch64-linux-gnu/` → 已复制到 `sysroot\usr\lib\`（cross.ps1 会自动补）；
3. **ld 脚本绝对路径**：Debian 的 `libc.so/libm.so` 等是含 `/usr/lib/aarch64-linux-gnu/` 绝对路径的 ld 脚本，Windows 宿主下必然失效 → 已全部重写为相对文件名（39 个脚本）；
4. **动态链接器校验**：ld 要求 sysroot 内存在 `/lib/ld-linux-aarch64.so.1` → `sysroot\lib` junction → `usr\lib`，并把 ld-linux 复制到 `usr\lib\`；`libc_nonshared.a`（sysroot 导出时缺失）已从设备单独拉回。

另：`slib` junction（PC 侧 → sysroot 的 arm64 库目录）作为 `-L` 参数，避开 ld 对 sysroot 内部路径的怪异处理。

### 8.3 验证结果（端到端）

- `tests\hello.c`（C，动态链设备 glibc 2.39）→ 设备运行输出正常 ✅
- `tests\hello.cpp`（C++，`-static-libstdc++`）→ 设备运行输出正常 ✅
- 推送/执行通道：pscp 推送 + plink 执行（PuTTY，`-ssh -batch -pw`）

### 8.4 日常使用

```powershell
.\cross.ps1 tests\hello.c                    # C
.\cross.ps1 tests\hello.cpp -Cpp             # C++
.\cross.ps1 src\foo.c -o out\foo -O2 -lEGL   # 透传任意编译参数
```

C++ 注意：工具链 libstdc++（GLIBCXX_3.4.34）比设备（3.4.33）新，故默认 `-static-libstdc++`；动态链接 C++ 时需同步推送工具链的 `libstdc++.so.6.0.34`。

### 8.5 分发打包清单

`tools\`（工具链+MSYS2）+ `sysroot\`（含上述修复，junction 由 cross.ps1 重建）+ `cross.ps1` —— 三项打包即可在任意 x86_64 Windows 复现整套交叉编译环境。

### 8.6 M1 里程碑（tu5xx 起步）建议入口

1. Mesa 24.0.5 源码拉取 + meson 交叉文件（复用本节 flags：sysroot/-isystem/-L）；
2. 内核源码定位（容器交叉编译先例的那套源码）+ boot.img 打包链摸底（mkbootimg，见 §7 遗留项）；
3. debugfs `rd` 命令流抓包管线（fd5 GL 标准答案采集脚本）。
